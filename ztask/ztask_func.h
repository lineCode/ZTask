#ifndef ZTASK_FUNC_H
#define ZTASK_FUNC_H
#include <ztask.h>
typedef void *(*pztask_malloc)();
typedef void *(*pztask_calloc)();
typedef void *(*pztask_realloc)();
typedef void *(*pztask_free)();
typedef char *(*pztask_strdup)(const char *str);
typedef char *(*pztask_strndup)(const char *str, size_t len);

//调用控制函数,一般由没有底层调用能力的脚本类模块使用
typedef const char *(*pztask_command)(struct ztask_context * context, const char * cmd, const char * parm);
//创建一个上下文,等同服务
typedef struct ztask_context * (*pztask_context_new)(const char * name, const char * parm, const size_t sz);
//通过上下文查询handle
typedef uint32_t(*pztask_context_handle)(struct ztask_context *);
//通过handle获取上下文
typedef struct ztask_context * (*pztask_handle_grab)(uint32_t handle);
//名称查询handle
typedef uint32_t(*pztask_queryname)(struct ztask_context * context, const char * name);
//通过handle发送消息
typedef int(*pztask_send)(struct ztask_context * context, uint32_t source, uint32_t destination, int type, int session, void * msg, size_t sz);
//通过名称发送消息
typedef int(*pztask_sendname)(struct ztask_context * context, uint32_t source, const char * destination, int type, int session, void * msg, size_t sz);
//设置和获取服务别名
typedef char *(*pztask_alias)(struct ztask_context * context, char *alias);
//是否是远程服务
typedef int(*pztask_isremote)(struct ztask_context *, uint32_t handle, int * harbor);
//设置回调地址
typedef void(*pztask_callback)(struct ztask_context * context, void *ud, ztask_cb cb);
typedef void *(*pztask_getud)(struct ztask_context * context);
typedef ztask_cb(*pztask_getcb)(struct ztask_context * context);
//获得线程当前的handle
typedef uint32_t(*pztask_current_handle)(void);






typedef struct _ztask_func
{
    uint32_t size;          //结构大小
    //内存相关
    pztask_malloc malloc;
    pztask_calloc calloc;
    pztask_realloc realloc;
    pztask_free free;
    pztask_strdup strdup;
    pztask_strndup strndup;
    //调度器相关
    pztask_command command;
    pztask_context_new context_new;
    pztask_context_handle context_handle;
    pztask_handle_grab handle_grab;
    pztask_queryname queryname;
    pztask_send send;
    pztask_sendname sendname;
    pztask_alias alias;
    pztask_isremote isremote;
    pztask_callback callback;
    pztask_getud getud;
    pztask_getcb getcb;
    pztask_current_handle current_handle;

}ztask_func;










////插入模块
//void ztask_module_insert(struct ztask_module *mod);
////输出调试信息
//void ztask_error(struct ztask_context * context, const char *msg, ...);



//


////获取ui上下文
//struct ztask_context * ztask_ui_ctx();
////发送一个事件到ui服务,不通过调度器,请确保在UI线程被调用
//void ztask_senduitask(int type, const void * msg, size_t sz);
////设置ui回调
//ztask_cb ztask_ui_callback(ztask_cb cb);

//
//uint64_t ztask_now(void);
//
////调试接口
//
////获取服务列表,返回数量
//uint32_t ztask_debug_getservers(struct ztask_context ***ctxs);
//void ztask_debug_freeservers(struct ztask_context **ctxs);
////获取服务信息
//void ztask_debug_info(struct ztask_context *ctx, uint32_t *handle, char **alias, double *cpu, int *mqlen, size_t *message, uint32_t *task);
////
//uint64_t ztask_thread_time(void);	// for profile, in micro second
//#if defined(WIN32) || defined(WIN64)
//void usleep(uint32_t us);
//char *strsep(char **s, const char *ct);
//#endif
//
///*io相关*/
//#define ZTASK_SOCKET_TYPE_DATA 1
//#define ZTASK_SOCKET_TYPE_CONNECT 2
//#define ZTASK_SOCKET_TYPE_START 3
//#define ZTASK_SOCKET_TYPE_CLOSE 4
//#define ZTASK_SOCKET_TYPE_ACCEPT 5
//#define ZTASK_SOCKET_TYPE_ERROR 6
//#define ZTASK_SOCKET_TYPE_UDP 7
//#define ZTASK_SOCKET_TYPE_WARNING 8
//#define ZTASK_SOCKET_TYPE_BIND 9
//#define ZTASK_SOCKET_TYPE_GETADDRINFO 10
//
//struct ztask_socket_message {
//    int type;
//    int id;
//    int ud;
//    char * buffer;
//};
//struct ztask_curl_message {
//    size_t data_len;//数据长度
//    size_t cookies_len;//cookies长度
//};
//void ztask_getaddrinfo(struct ztask_context *ctx, int session, char* host);
//void ztask_curl(struct ztask_context *ctx, int session, void* esay);
//
////发送数据
//int ztask_socket_send(struct ztask_context *ctx, int id, void *buffer, int sz);
////监听端口
//int ztask_socket_listen(struct ztask_context *ctx, int session, const char *host, int port, int backlog);
////连接服务器
//int ztask_socket_connect(struct ztask_context *ctx, int session, const char *host, int port);
//int ztask_socket_bind(struct ztask_context *ctx, int fd);
//void ztask_socket_close(struct ztask_context *ctx, int id);
//void ztask_socket_shutdown(struct ztask_context *ctx, int id);
//void ztask_socket_start(struct ztask_context *ctx, int session, int id);
//void ztask_socket_nodelay(struct ztask_context *ctx, int id);
//
//int ztask_socket_udp(struct ztask_context *ctx, const char * addr, int port);
//int ztask_socket_udp_connect(struct ztask_context *ctx, int id, const char * addr, int port);
//int ztask_socket_udp_send(struct ztask_context *ctx, int id, const char * address, short port, const void *buffer, int sz);
//
///*模块相关*/
///*lua模块*/
////添加lua库
//void ztask_snlua_addlib(char *name, int(*lua_CFunction) (void *L));
//
///*C模块*/
////c模块启动参数
//typedef int(*ztask_snc_init_cb)(struct ztask_context * context, void *ud, const void * msg, size_t sz);
//typedef int(*ztask_snc_exit_cb)(struct ztask_context * context, void *ud);
//struct ztask_snc {
//    ztask_cb _cb;
//    ztask_snc_init_cb _init_cb;
//    ztask_snc_exit_cb _exit_cb;
//    char *name; //模块名字
//    void *ud;
//    void * msg;
//    size_t sz;
//};
//void *ztask_snc_userdata(struct ztask_context * ctx, void *ud);
//ztask_cb ztask_snc_callback(struct ztask_context * ctx, ztask_cb cb);
////c模块封装函数
//int ztask_snc_getaddrinfo(struct ztask_context *ctx, const char *host, uint32_t *addr);
//int ztask_snc_curl(void *esay, void **data, size_t *size, void **cookies, size_t *c_size);
//
//int ztask_snc_socket_listen(struct ztask_context *ctx, const char *host, int port, int backlog);
//void ztask_snc_socket_start(struct ztask_context *ctx, int id);
//void ztask_snc_socket_close(struct ztask_context *ctx, int fd);
//int ztask_snc_socket_connect(struct ztask_context *ctx, const char *host, int port);
//void ztask_snc_sleep(int time);//延迟,单位ms,精度0.01s
//typedef void(*ztask_snc_timerout_cb)(struct ztask_context * context, void *ud, int id, const void * msg, size_t sz);
//int ztask_snc_timerout(int time, ztask_snc_timerout_cb cb, int id, const void * msg, size_t sz);//
//void ztask_snc_yield(int *type, void ** msg, size_t *sz);//挂起会话,等待唤醒,可以接受返回值
//void ztask_snc_yield_s(int *type, void ** msg, size_t *sz, int time);
//void ztask_snc_resume(int session, int type, void * msg, size_t sz);//唤醒一个挂起的会话
//int ztask_snc_session();//得到当前所在的会话
//int ztask_snc_ret(void * msg, size_t sz);//返回给调用方,内部有内存拷贝
//int ztask_snc_retsession();
//
///*协程相关*/
//typedef struct coroutine_s coroutine_t;
////创建一个协程
//coroutine_t *coroutine_new(fcontext_cb);
//void coroutine_delete(coroutine_t *c);
////返回当前协程是否执行完毕
//int coroutine_resume(coroutine_t *c);
//void coroutine_yield();
//coroutine_t *coroutine_running();
//void coroutine_msg(int *type, void ** msg, size_t *sz);
////
//int ztask_snc_call(struct ztask_context * context, int handle, int type, void * data, size_t sz, int *o_type, void ** o_msg, size_t *o_sz);
//int ztask_snc_callname(char* name, int type, void * data, size_t sz, void ** o_msg, size_t *o_sz);
//int ztask_snc_callname_s(char* name, int type, void * data, size_t sz, void ** o_msg, size_t *o_sz, int time);

#endif
