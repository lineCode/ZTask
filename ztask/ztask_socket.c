#include "ztask.h"
#include "atomic.h"
#include "thread.h"
#include "spinlock.h"
#include "ztask_socket.h"
#include "ztask_server.h"
#include "ztask_mq.h"
#include "ztask_harbor.h"
#include "ztask_handle.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>
#include <MSWSock.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>



#define SOCKET_TYPE_INVALID 0
#define SOCKET_TYPE_RESERVE 1
#define SOCKET_TYPE_PLISTEN 2
#define SOCKET_TYPE_LISTEN 3
#define SOCKET_TYPE_CONNECTING 4
#define SOCKET_TYPE_CONNECTED 5
#define SOCKET_TYPE_HALFCLOSE 6
#define SOCKET_TYPE_PACCEPT 7
#define SOCKET_TYPE_BIND 8
//协议
#define PROTOCOL_TCP 0
#define PROTOCOL_UDP 1
#define PROTOCOL_UDPv6 2
#define PROTOCOL_UNKNOWN 255

#define MAX_SOCKET_P 16
#define MAX_SOCKET (1<<MAX_SOCKET_P)

#define HASH_ID(id) (((unsigned)id) % MAX_SOCKET)
#define ID_TAG16(id) ((id>>MAX_SOCKET_P) & 0xffff)

#define UDP_ADDRESS_SIZE 19	// ipv6 128bit + port 16bit + 1 byte type

LPFN_CONNECTEX lpfnConnectEx = NULL;
LPFN_ACCEPTEX  lpfnAcceptEx = NULL;

//套接字定义
struct socket {
    uintptr_t opaque;           //关联的服务handle
    SOCKET fd;                  //套接字
    int id;                     //
    uint8_t protocol;           //协议类型
    uint16_t type;              //状态
    uint8_t start;              //
};
//IO服务定义
struct socket_server {
    //完成端口数据
    HANDLE CompletionPort;
    //
    int alloc_id;
    //socket插槽
    struct socket slot[MAX_SOCKET];
};
//连接请求
struct request_connect {
    uintptr_t opaque;
    struct socket *ns;
    int session;
};
//接收客户请求
struct request_accept {
    struct socket *ns;
    SOCKET cfd;//新客户的句柄
    WSABUF buf;
    size_t RecvBytes;   //实际接收长度
};
//监听请求
struct request_listen {
    uintptr_t opaque;
    struct socket *ns;
    int session;
};
//开始接收数据请求
struct request_start {
    uintptr_t opaque;
    int id;
    int session;
};
//接收数据请求
struct request_recv {
    struct socket *ns;
    WSABUF buf;
    size_t RecvBytes;   //实际接收长度
};
//接收数据报请求
struct request_recvfrom {
    int fd;
    uintptr_t opaque;
    WSABUF buf;         //
    size_t RecvBytes;   //实际接收长度
    struct sockaddr_in remote_addr;
    int remote_addr_len;      //存储数据来源IP地址长度
};
//发送数据请求
struct request_close {
    int id;
};
//关闭连接请求
struct request_send {
    int id;
    WSABUF buf;
};
//发送数据报请求
struct request_sendfrom {
    int fd;
    struct sockaddr_in remote_addr;
    WSABUF buf;
};
//完成结构
typedef struct
{
    OVERLAPPED overlapped;      //系统对象
    uint32_t Type;              //请求类型
    union {
        char buffer[256];
        struct request_connect connect;
        struct request_accept accept;
        struct request_listen listen;
        struct request_start start;
        struct request_recv recv;
        struct request_send send;
        struct request_close close;
        struct request_recvfrom recvfrom;
        struct request_sendfrom sendfrom;
    } u;
}*LIO_DATA, IO_DATA;

static struct socket_server * ss = NULL;

//创建IO服务
void ztask_socket_init() {
    ss = (struct socket_server *)ztask_malloc(sizeof(*ss));
    memset(ss, 0, sizeof(*ss));
    //初始化套接字
    WORD wVersionRequested;
    WSADATA wsaData;
    wVersionRequested = MAKEWORD(2, 2);
    WSAStartup(wVersionRequested, &wsaData);

    //创建完成端口
    ss->CompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    //获取函数地址
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);

    DWORD dwBytes = 0;
    GUID GuidConnectEx = WSAID_CONNECTEX;
    if (SOCKET_ERROR == WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidConnectEx, sizeof(GuidConnectEx), &lpfnConnectEx, sizeof(lpfnConnectEx), &dwBytes, 0, 0))
    {
        return;
    }
    dwBytes = 0;
    GUID GuidAcceptEx = WSAID_ACCEPTEX;
    if (SOCKET_ERROR == WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAcceptEx, sizeof(GuidAcceptEx), &lpfnAcceptEx, sizeof(lpfnAcceptEx), &dwBytes, 0, 0))
    {
        return;
    }
    closesocket(s);
}
//退出IO服务
void ztask_socket_exit() {

}
//回收IO服务
void ztask_socket_free() {
    CloseHandle(ss->CompletionPort);
    ztask_free(ss);
    ss = NULL;
}

//新建套接字
static struct socket * new_fd(struct socket_server *ss, int id, int fd, int protocol, uintptr_t opaque) {
    struct socket * s = &ss->slot[HASH_ID(id)];
    assert(s->type == SOCKET_TYPE_RESERVE);


    s->id = id;
    s->fd = fd;
    s->protocol = protocol;
    s->opaque = opaque;
    return s;
}
//分配ID
static int reserve_id(struct socket_server *ss) {
    int i;
    for (i = 0; i<MAX_SOCKET; i++) {
        int id = ATOM_INC(&(ss->alloc_id));
        if (id < 0) {
            id = ATOM_AND(&(ss->alloc_id), 0x7fffffff);
        }
        struct socket *s = &ss->slot[HASH_ID(id)];
        if (s->type == SOCKET_TYPE_INVALID) {
            if (ATOM_CAS16(&s->type, SOCKET_TYPE_INVALID, SOCKET_TYPE_RESERVE)) {
                s->id = id;
                s->protocol = PROTOCOL_UNKNOWN;
                s->fd = -1;
                return id;
            }
            else {
                // retry
                --i;
            }
        }
    }
    return -1;
}
//开启心跳
static void socket_keepalive(int fd) {
    int keepalive = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&keepalive, sizeof(keepalive));
}
//投递Accept请求
static void post_acceptex(struct socket *s) {
    //投递一个请求
    IO_DATA *msg = ztask_malloc(sizeof(*msg));
    memset(msg, 0, sizeof(*msg));
    msg->Type = 'A';
    msg->u.accept.ns = s;
    msg->u.accept.buf.len = 8192;
    msg->u.accept.buf.buf = ztask_malloc(8192);
    //创建一个新的套接字
    msg->u.accept.cfd = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
    
    //投递一个接收请求
    if (lpfnAcceptEx(s->fd, msg->u.accept.cfd, msg->u.accept.buf.buf, 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &msg->u.accept.RecvBytes, (LPWSAOVERLAPPED)msg) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING)
        {
            //套接字错误
            ztask_free(msg->u.recv.buf.buf);
            ztask_free(msg);
        }
    }
}
//主循环
int ztask_socket_poll() {
    uint32_t handle = 0;
    size_t type = -1;
    int session = 0;

    struct ztask_socket_message *sm;
    size_t sz = sizeof(*sm);
    sm = (struct ztask_socket_message *)ztask_malloc(sz);
    memset(sm, 0, sizeof(*sm));

    void *lpContext = NULL;
    IO_DATA        *pOverlapped = NULL;
    DWORD            dwBytesTransfered = 0;
    BOOL bReturn = GetQueuedCompletionStatus(ss->CompletionPort, &dwBytesTransfered, (LPDWORD)&lpContext, (LPOVERLAPPED *)&pOverlapped, 1000 * 5);
    if (!pOverlapped)
        return 1;
    if (bReturn == 0) {
        //请求失败
        switch (pOverlapped->Type)
        {
        case 'C': //连接服务器
        {
            type = PTYPE_RESPONSE;
            handle = pOverlapped->u.connect.ns->opaque;
            session = pOverlapped->u.connect.session;

            sm->type = ZTASK_SOCKET_TYPE_ERROR;
            sm->id = pOverlapped->u.connect.ns->id;
            closesocket(pOverlapped->u.connect.ns->fd);
            break;
        }
        case 'R'://收到数据
        {
            type = PTYPE_SOCKET;
            handle = pOverlapped->u.recv.ns->opaque;

            sm->type = ZTASK_SOCKET_TYPE_ERROR;
            sm->id = pOverlapped->u.recv.ns->id;
            closesocket(pOverlapped->u.recv.ns->fd);

            ztask_free(pOverlapped->u.recv.buf.buf);


            break;
        }
        case 'S'://发送数据
        {
            int id = pOverlapped->u.send.id;
            struct socket *s = &ss->slot[HASH_ID(id)];
            if (s->type == SOCKET_TYPE_INVALID || s->id != id) {
                break;
            }

            type = PTYPE_SOCKET;
            handle = s->opaque;

            sm->type = ZTASK_SOCKET_TYPE_ERROR;
            sm->id = id;
            closesocket(s->fd);

            ztask_free(pOverlapped->u.send.buf.buf);
        }
        case 't': //发送数据报
        {
            type = PTYPE_SOCKET;
            //handle = pOverlapped->u.sendfrom.opaque;

            sm->type = ZTASK_SOCKET_TYPE_ERROR;
            sm->id = pOverlapped->u.sendfrom.fd;
            closesocket(pOverlapped->u.sendfrom.fd);

            ztask_free(pOverlapped->u.sendfrom.buf.buf);
            break;
        }
        case 'f'://收到数据报
        {
            type = PTYPE_SOCKET;
            handle = pOverlapped->u.recvfrom.opaque;

            sm->type = ZTASK_SOCKET_TYPE_ERROR;
            sm->id = pOverlapped->u.recvfrom.fd;
            closesocket(pOverlapped->u.recvfrom.fd);

            ztask_free(pOverlapped->u.recvfrom.buf.buf);
            break;
        }
        case 'A':
        {
            //回收资源
            ztask_free(pOverlapped->u.accept.buf.buf);
            //回收提前分配的句柄
            closesocket(pOverlapped->u.accept.cfd);
            break;
        }
        case 'a'://开始接收客户
        {
            type = PTYPE_RESPONSE;
            handle = pOverlapped->u.start.opaque;
            session = pOverlapped->u.start.session;

            sm->type = ZTASK_SOCKET_TYPE_ERROR;
            int id = pOverlapped->u.start.id;
            sm->id = id;

            struct socket *s = &ss->slot[HASH_ID(id)];
            if (s->type == SOCKET_TYPE_INVALID || s->id != id) {
                break;
            }
            closesocket(s->fd);
            break;
        }
        case 'L'://监听端口
        {
            type = PTYPE_RESPONSE;
            handle = pOverlapped->u.listen.opaque;
            session = pOverlapped->u.listen.session;

            sm->type = ZTASK_SOCKET_TYPE_ERROR;
            sm->id = pOverlapped->u.listen.ns->id;
            closesocket(pOverlapped->u.listen.ns->fd);
            break;
        }
        default:
            return 0;
            break;
        }
        goto _ret;
    }

    switch (pOverlapped->Type)
    {
    case 'C': //连接服务器
    {
        type = PTYPE_RESPONSE;
        handle = pOverlapped->u.connect.ns->opaque;
        session = pOverlapped->u.connect.session;

        sm->type = ZTASK_SOCKET_TYPE_CONNECT;
        sm->id = pOverlapped->u.connect.ns->id;

        break;
    }
    case 's'://开始接收数据
    {
        int id = pOverlapped->u.start.id;
        sm->id = id;

        struct socket *s = &ss->slot[HASH_ID(id)];
        if (s->type == SOCKET_TYPE_INVALID || s->id != id) {
            sm->type = ZTASK_SOCKET_TYPE_ERROR;
            break;
        }
        else {
            sm->type = ZTASK_SOCKET_TYPE_START;
        }
        s->opaque = pOverlapped->u.start.opaque;//记录新的地址

        type = PTYPE_RESPONSE;
        handle = pOverlapped->u.start.opaque;//原路返回
        session = pOverlapped->u.start.session;
        if (s->start) {
            //已经开始则不处理
            break;
        }
        s->start = 1;
        //投递一个请求
        IO_DATA *msg = ztask_malloc(sizeof(*msg));
        memset(msg, 0, sizeof(*msg));
        msg->Type = 'R';
        msg->u.recv.ns = s;
        msg->u.recv.buf.len = 8192;
        msg->u.recv.buf.buf = ztask_malloc(8192);

        //投递一个接收请求
        DWORD dwBufferCount = 1, dwRecvBytes = 0, Flags = 0;
        if (WSARecv(s->fd, &msg->u.recv.buf, 1, &msg->u.recv.RecvBytes, &Flags, (LPWSAOVERLAPPED)msg, NULL) == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING)
            {
                //套接字错误
                ztask_free(msg->u.recv.buf.buf);
                ztask_free(msg);
            }
        }
        break;
    }
    case 'R'://收到数据
    {
        type = PTYPE_SOCKET;
        handle = pOverlapped->u.recv.ns->opaque;

        if (dwBytesTransfered == 0) {
            //被主动断开?
            sm->type = ZTASK_SOCKET_TYPE_ERROR;
            sm->id = pOverlapped->u.recv.ns->id;

            ztask_free(pOverlapped->u.recv.buf.buf);
            closesocket(pOverlapped->u.recv.ns->fd);
            break;
        }

        sm->type = ZTASK_SOCKET_TYPE_DATA;
        sm->id = pOverlapped->u.recv.ns->id;
        sm->ud = dwBytesTransfered;
        sm->buffer = pOverlapped->u.recv.buf.buf;

        //投递一个请求
        IO_DATA *msg = ztask_malloc(sizeof(*msg));
        memset(msg, 0, sizeof(*msg));
        msg->Type = 'R';
        msg->u.recv.ns = pOverlapped->u.recv.ns;
        msg->u.recv.buf.len = 8192;
        msg->u.recv.buf.buf = ztask_malloc(8192);

        //投递一个接收请求
        DWORD dwBufferCount = 1, dwRecvBytes = 0, Flags = 0;
        if (WSARecv(pOverlapped->u.recv.ns->fd, &msg->u.recv.buf, 1, &msg->u.recv.RecvBytes, &Flags, (LPWSAOVERLAPPED)msg, NULL) == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING)
            {
                //套接字错误
                ztask_free(msg->u.recv.buf.buf);
                ztask_free(msg);
                //通知套接字错误
                sm->type = ZTASK_SOCKET_TYPE_ERROR;
                sm->id = pOverlapped->u.recv.ns->fd;
                sm->ud = 0;
                sm->buffer = NULL;
                ztask_free(sm->buffer);
            }
        }
        break;
    }
    case 'S'://发送数据
    {
        ztask_free(pOverlapped->u.send.buf.buf);
        break;
    }
    case 't': //发送数据报
    {
        ztask_free(pOverlapped->u.sendfrom.buf.buf);
        break;
    }
    case 'f'://收到数据报
    {
        type = PTYPE_SOCKET;
        handle = pOverlapped->u.recvfrom.opaque;

        sm->type = ZTASK_SOCKET_TYPE_UDP;
        sm->id = pOverlapped->u.recvfrom.fd;
        sm->ud = dwBytesTransfered;
        sm->buffer = pOverlapped->u.recvfrom.buf.buf;

        //继续投递请求
        IO_DATA *msg = ztask_malloc(sizeof(*msg));
        memset(msg, 0, sizeof(*msg));
        msg->Type = 'f';
        msg->u.recvfrom.opaque = handle;
        msg->u.recvfrom.fd = pOverlapped->u.recvfrom.fd;
        msg->u.recvfrom.buf.len = 8192;
        msg->u.recvfrom.buf.buf = ztask_malloc(8192);
        msg->u.recvfrom.remote_addr_len = sizeof(msg->u.recvfrom.remote_addr);
        DWORD Flags = 0;
        if (WSARecvFrom(pOverlapped->u.recvfrom.fd, &msg->u.recvfrom.buf, 1, &msg->u.recvfrom.RecvBytes, &Flags, (SOCKADDR*)&(msg->u.recvfrom.remote_addr), &(msg->u.recvfrom.remote_addr_len), msg, NULL) == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING)
            {
                //套接字错误
                ztask_free(msg->u.recvfrom.buf.buf);
                ztask_free(msg);
                //通知套接字错误
                sm->type = ZTASK_SOCKET_TYPE_ERROR;
                sm->id = pOverlapped->u.recvfrom.fd;
                sm->ud = 0;
                sm->buffer = NULL;
                ztask_free(sm->buffer);
            }
        }
        break;
    }
    case 'A'://收到客户
    {
        //回收资源
        ztask_free(pOverlapped->u.accept.buf.buf);

        //新分配一个id
        int nid = reserve_id(ss);
        if (nid < 0) {
            break;
        }
        //分配一个新socket
        struct socket *ns = new_fd(ss, nid, pOverlapped->u.accept.cfd, PROTOCOL_TCP, pOverlapped->u.accept.ns->opaque, false);
        if (ns == NULL) {
            break;
        }
        socket_keepalive(pOverlapped->u.accept.cfd);


        type = PTYPE_SOCKET;
        handle = pOverlapped->u.accept.ns->opaque;

        sm->type = ZTASK_SOCKET_TYPE_ACCEPT;
        sm->id = pOverlapped->u.accept.ns->id;
        sm->ud = nid;
        
        
        strcpy(sm->addr,"127.0.0.1");

        //关联到完成端口
        CreateIoCompletionPort((HANDLE)pOverlapped->u.accept.cfd, ss->CompletionPort, (ULONG_PTR)pOverlapped->u.accept.cfd, 0);

        //继续投递请求
        post_acceptex(pOverlapped->u.accept.ns);
        break;
    }
    case 'a'://开始接收客户
    {
        int id = pOverlapped->u.start.id;
        struct socket *s = &ss->slot[HASH_ID(id)];
        if (s->type == SOCKET_TYPE_INVALID || s->id != id) {
            sm->type = ZTASK_SOCKET_TYPE_ERROR;
            break;
        }
        else {
            sm->type = ZTASK_SOCKET_TYPE_START;
        }
        sm->id = id;

        type = PTYPE_RESPONSE;
        handle = pOverlapped->u.start.opaque;//原路返回
        session = pOverlapped->u.start.session;

        for (size_t i = 0; i < 10; i++)
        {
            post_acceptex(s);
        }
        break;
    }
    case 'L'://监听端口
    {
        type = PTYPE_RESPONSE;
        handle = pOverlapped->u.listen.ns->opaque;
        session = pOverlapped->u.listen.session;

        sm->type = ZTASK_SOCKET_TYPE_CONNECT;
        sm->id = pOverlapped->u.listen.ns->id;
        break;
    }
    case 'k'://关闭连接
    {
        int id = pOverlapped->u.close.id;
        struct socket * s = &ss->slot[HASH_ID(id)];
        if (s->id != id) {
            break;
        }
        s->type = SOCKET_TYPE_INVALID;
        closesocket(s->fd);
        break;
    }
    default:
        break;
    }
_ret:
    if (type >= 0 && handle) {
        struct ztask_message message;
        message.source = 0;
        message.session = session;
        message.data = sm;
        message.sz = sz | ((size_t)(type | PTYPE_TAG_DONTCOPY) << MESSAGE_TYPE_SHIFT);

        if (ztask_context_push(handle, &message)) {
            //失败
            ztask_free(sm->buffer);
            ztask_free(sm);
        }
    }
    else {
        ztask_free(sm);
    }
    ztask_free(pOverlapped);
    return 1;
}

//发送数据
int ztask_socket_send(struct ztask_context *ctx, int id, void *buffer, int sz) {
    struct socket * s = &ss->slot[HASH_ID(id)];
    if (s->id != id || s->type == SOCKET_TYPE_INVALID) {
        ztask_free(buffer);
        return -1;
    }
    
    
    IO_DATA *msg = ztask_malloc(sizeof(*msg));
    memset(msg, 0, sizeof(*msg));
    msg->Type = 'S';
    msg->u.send.id = id;
    msg->u.send.buf.buf = buffer;
    msg->u.send.buf.len = sz;
    //投递一个发送请求
    DWORD dwSendBytes = 0, Flags = 0;
    if (WSASend(s->fd, &msg->u.send.buf, 1, &dwSendBytes, Flags, msg, NULL) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING)
        {
            //套接字错误

        }
    }
    return 0;
}
//发送数据(低优先级)
int ztask_socket_send_lowpriority(struct socket_server *ss, int id, const void * buffer, int sz) {
}
//开始监听
int ztask_socket_listen(struct ztask_context *ctx, int session, const char *host, int port, int backlog) {
    uint32_t source = ztask_context_handle(ctx);
    IO_DATA *msg = ztask_malloc(sizeof(*msg));
    memset(msg, 0, sizeof(*msg));
    msg->Type = 'L';
    msg->u.listen.opaque = ztask_context_handle(ctx);
    msg->u.listen.session = session;

    //分配id
    int id = reserve_id(ss);
    if (id < 0) {
        return id;
    }

    //创建套接字
    SOCKET fd = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);
    socket_keepalive(fd);

    //分配套接字
    struct socket *ns = new_fd(ss, id, fd, PROTOCOL_TCP, msg->u.listen.opaque);
    if (ns == NULL) {

    }
    msg->u.listen.ns = ns;
    //绑定
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(struct sockaddr_in));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = inet_addr(host);
    local_addr.sin_port = htons(port);
    int irt = bind(fd, (struct sockaddr *)(&local_addr), sizeof(struct sockaddr_in));
    //监听
    irt = listen(fd, backlog);
    if (SOCKET_ERROR == irt) {
        
    }
    //关联到完成端口
    CreateIoCompletionPort((HANDLE)fd, ss->CompletionPort, (ULONG_PTR)fd, 0);

    //投递到完成端口
    PostQueuedCompletionStatus(ss->CompletionPort, 0, 0, msg);
    return id;
}
//连接服务
int ztask_socket_connect(struct ztask_context *ctx, int session, const char *host, int port) {
    IO_DATA *msg = ztask_malloc(sizeof(*msg));
    memset(msg, 0, sizeof(*msg));
    msg->Type = 'C';
    msg->u.connect.opaque = ztask_context_handle(ctx);
    msg->u.connect.session = session;

    //分配id
    int id = reserve_id(ss);
    if (id < 0) {
        return id;
    }

    //创建套接字
    SOCKET fd = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);
    socket_keepalive(fd);

    //分配套接字
    struct socket *ns = new_fd(ss, id, fd, PROTOCOL_TCP, msg->u.connect.opaque);
    if (ns == NULL) {

    }
    msg->u.connect.ns = ns;
    //绑定
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(struct sockaddr_in));
    local_addr.sin_family = AF_INET;
    int irt = bind(fd, (struct sockaddr *)(&local_addr), sizeof(struct sockaddr_in));
    //关联到完成端口
    CreateIoCompletionPort((HANDLE)fd, ss->CompletionPort, (ULONG_PTR)fd, 0);
    //异步连接
    struct sockaddr_in addrPeer;
    memset(&addrPeer, 0, sizeof(struct sockaddr_in));
    addrPeer.sin_family = AF_INET;
    addrPeer.sin_addr.s_addr = inet_addr(host);
    addrPeer.sin_port = htons(port);
    PVOID lpSendBuffer = NULL;
    lpfnConnectEx(fd, (struct sockaddr *)&addrPeer, sizeof(addrPeer), 0, 0, &lpSendBuffer, msg);
    return id;
}

int ztask_socket_bind(struct ztask_context *ctx, int fd) {
    uint32_t source = ztask_context_handle(ctx);

}

void ztask_socket_close(struct ztask_context *ctx, int id) {
    IO_DATA *msg = ztask_malloc(sizeof(*msg));
    memset(msg, 0, sizeof(*msg));
    msg->Type = 'k';
    msg->u.close.id = id;
    PostQueuedCompletionStatus(ss->CompletionPort, 0, 0, msg);
}

void ztask_socket_shutdown(struct ztask_context *ctx, int id) {
    IO_DATA *msg = ztask_malloc(sizeof(*msg));
    memset(msg, 0, sizeof(*msg));
    msg->Type = 'k';
    msg->u.close.id = id;
    PostQueuedCompletionStatus(ss->CompletionPort, 0, 0, msg);
}
//开始接收数据
void ztask_socket_start(struct ztask_context *ctx, int session, int id) {
    IO_DATA *msg = ztask_malloc(sizeof(*msg));
    memset(msg, 0, sizeof(*msg));
    msg->Type = 's';
    msg->u.start.opaque = ztask_context_handle(ctx);
    msg->u.start.session = session;
    msg->u.start.id = id;
    PostQueuedCompletionStatus(ss->CompletionPort, 0, 0, msg);
}
//开始接收客户连接
void ztask_socket_accept(struct ztask_context *ctx, int session, int id) {
    IO_DATA *msg = ztask_malloc(sizeof(*msg));
    memset(msg, 0, sizeof(*msg));
    msg->Type = 'a';
    msg->u.start.opaque = ztask_context_handle(ctx);
    msg->u.start.session = session;
    msg->u.start.id = id;
    PostQueuedCompletionStatus(ss->CompletionPort, 0, 0, msg);
}

void ztask_socket_nodelay(struct ztask_context *ctx, int id) {
    
}

//---udp

//创建一个UDP句柄,如未提供地址和端口,则由系统分配
int ztask_socket_udp(struct ztask_context *ctx, const char * addr, int port) {
    //创建套接字
    int fd = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (fd) {
        //绑定本机地址
        struct sockaddr_in local_addr;
        memset(&local_addr, 0, sizeof(struct sockaddr_in));
        local_addr.sin_family = AF_INET;
        local_addr.sin_port = (uint16_t)((((uint16_t)(port) & 0xff00) >> 8) | (((uint16_t)(port) & 0x00ff) << 8));
        if (addr != NULL)
            local_addr.sin_addr.S_un.S_addr = inet_addr(addr);
        bind(fd, (struct sockaddr *)(&local_addr), sizeof(struct sockaddr_in));
        //关联到完成端口
        CreateIoCompletionPort((HANDLE)fd, ss->CompletionPort, fd, 0);
        //投递一个接收请求
        IO_DATA *msg = ztask_malloc(sizeof(*msg));
        memset(msg, 0, sizeof(*msg));
        msg->Type = 'f';
        msg->u.recvfrom.opaque = ztask_context_handle(ctx);
        msg->u.recvfrom.fd = fd;
        msg->u.recvfrom.buf.len = 8192;
        msg->u.recvfrom.buf.buf = ztask_malloc(8192);
        msg->u.recvfrom.remote_addr_len = sizeof(msg->u.recvfrom.remote_addr);
        DWORD Flags = 0;
        if (WSARecvFrom(fd, &msg->u.recvfrom.buf, 1, &msg->u.recvfrom.RecvBytes, &Flags, (SOCKADDR*)&(msg->u.recvfrom.remote_addr), &(msg->u.recvfrom.remote_addr_len), msg, NULL) == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING)
            {
                //套接字错误

            }
        }
    }
    return fd;
}

int ztask_socket_udp_connect(struct ztask_context *ctx, int id, const char * addr, int port) {

}

int ztask_socket_udp_send(struct ztask_context *ctx, int id, const char * address, short port, const void *buffer, int sz) {
    IO_DATA *msg = ztask_malloc(sizeof(*msg));
    memset(msg, 0, sizeof(*msg));
    msg->Type = 't';
    //msg->u.sendfrom.opaque = ztask_context_handle(ctx);
    msg->u.sendfrom.fd = id;
    msg->u.sendfrom.buf.buf = buffer;
    msg->u.sendfrom.buf.len = sz;
    //投递一个发送请求
    DWORD dwSendBytes = 0, Flags = 0;
    msg->u.sendfrom.remote_addr.sin_family = AF_INET;
    msg->u.sendfrom.remote_addr.sin_addr.S_un.S_addr = inet_addr(address);
    msg->u.sendfrom.remote_addr.sin_port = (uint16_t)((((uint16_t)(port) & 0xff00) >> 8) | (((uint16_t)(port) & 0x00ff) << 8));
    if (WSASendTo(id, &msg->u.sendfrom.buf, 1, &dwSendBytes, Flags, &msg->u.sendfrom.remote_addr, sizeof(msg->u.sendfrom.remote_addr), msg, NULL) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING)
        {
            //套接字错误

        }
    }
    return 0;
}

