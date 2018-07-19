#include "ztask.h"
#include "ztask_server.h"
#include "ztask_socket.h"
#include "ztask_timer.h"
#include "tree.h"
#include "coroutine.h"
#include "service_snc.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>


struct snc_timeout_data {
    int id;
    void * msg;
    size_t sz;
    ztask_snc_timerout_cb cb;
};

//封装异步操作
int ztask_snc_curl(void *esay, void **data, size_t *size, void **cookies, size_t *c_size) {
    coroutine_t *cur = coroutine_running();
    ztask_curl(ztask_context_handle(cur->context), cur->session, esay);
    coroutine_yield();//切出
    struct ztask_curl_message *msg = cur->msg;
    if (msg) {
        if (data)
            *data = (char *)msg + sizeof(struct ztask_curl_message) + msg->cookies_len + 1;
        if (size)
            *size = msg->data_len;
        if (cookies)
            *cookies = (char *)msg + sizeof(struct ztask_curl_message);
        if (c_size)
            *c_size = msg->cookies_len;
        return 0;
    }
    return -1;
}
int ztask_snc_getaddrinfo(struct ztask_context *ctx, const char *host, uint32_t *addr) {
    coroutine_t *cur = coroutine_running();
    ztask_getaddrinfo(ctx, cur->session, host);
    coroutine_yield();//切出
    if (cur->sz == sizeof(uint32_t)) {
        *addr = *(uint32_t *)cur->msg;
        return 0;
    }
    return -1;
}

int ztask_snc_socket_listen(struct ztask_context *ctx, const char *host, int port, int backlog) {
    coroutine_t *cur = coroutine_running();
    int ret = ztask_socket_listen(ctx, cur->session, host, port, backlog);
    //连接是个异步调用,所以切出当前协程
    coroutine_yield();
    return ret;
}
void ztask_snc_socket_start(struct ztask_context *ctx, int fd) {
    coroutine_t *cur = coroutine_running();
    ztask_socket_start(ctx, cur->session, fd);
    //连接是个异步调用,所以切出当前协程
    coroutine_yield();
}
int ztask_snc_socket_connect(struct ztask_context *ctx, const char *host, int port) {
    coroutine_t *cur = coroutine_running();
    ztask_socket_connect(ctx, cur->session, host, port);
    //连接是个异步调用,所以切出当前协程
    coroutine_yield();
    struct ztask_socket_message *sm = cur->msg;
    if (sm->type == ZTASK_SOCKET_TYPE_CONNECT)
        return sm->id;
    return -1;
}
void ztask_snc_socket_close(struct ztask_context *ctx, int fd) {
    ztask_socket_close(ctx, fd);
}
//定时器协程回调
static void _timer_func(coroutine_t *c) {
    struct snc_timeout_data *d = c->data;
    struct snc *l = c->ud;
    d->cb(c->context, l->ud, d->id, d->msg, d->sz);
    ztask_free(d);
}
int ztask_snc_timerout(int time, ztask_snc_timerout_cb cb, int id, const void * msg, size_t sz) {
    if (!cb)
        return -1;
    coroutine_t *cur = coroutine_running();
    struct snc *l = ztask_getud(cur->context);
    //创建一个协程
    coroutine_t * c = coroutine_new(_timer_func);
    ztask_context_task_inc(cur->context);
    c->session = ztask_context_newsession(cur->context);
    struct snc_timeout_data *d = (struct snc_timeout_data *)ztask_malloc(sizeof(struct snc_timeout_data));
    d->msg = msg;
    d->sz = sz;
    d->cb = cb;
    d->id = id;
    c->data = d;
    c->ud = l;
    c->context = cur->context;
    //把协程保存起来
    RB_INSERT(coroutine_tree_s, &l->coroutine, c);
    return ztask_timeout(ztask_context_handle(cur->context), time, c->session);
}
void ztask_snc_sleep(int time) {
    coroutine_t *cur = coroutine_running();
    ztask_timeout(ztask_context_handle(cur->context), time, cur->session);
    coroutine_yield();//切出
}
//挂起
void ztask_snc_yield(int *type, void ** msg, size_t *sz) {
    coroutine_yield();
    coroutine_t *cur = coroutine_running();
    if (type)
        *type = cur->type;
    if (msg)
        *msg = cur->msg;
    if (sz)
        *sz = cur->sz;
}
//带超时的挂起
void ztask_snc_yield_s(int *type, void ** msg, size_t *sz, int time) {
    //先注册一个timer
    coroutine_t *cur = coroutine_running();
    ztask_timeout(ztask_context_handle(cur->context), time, cur->session);
    coroutine_yield();
    cur = coroutine_running();
    if (cur->msg || cur->sz)
        ztask_timeout_del(ztask_context_handle(cur->context), ztask_snc_session());//移除定时器
    if (type)
        *type = cur->type;
    if (msg)
        *msg = cur->msg;
    if (sz)
        *sz = cur->sz;
}
//唤醒一个会话
void ztask_snc_resume(int session, int type, void * msg, size_t sz) {
    coroutine_t *cur = coroutine_running();
    struct snc *l = ztask_getud(cur->context);
    struct coroutine_s theNode = { 0 };
    struct coroutine_s *n;
    theNode.session = session;
    n = RB_FIND(coroutine_tree_s, &l->coroutine, &theNode);
    if (n) {
        n->msg = msg;
        n->sz = sz;
        n->type = type;
        if (coroutine_resume(n)) {
            //协程执行完毕,删除这个协程
            RB_REMOVE(coroutine_tree_s, &l->coroutine, n);
            coroutine_delete(n);
            ztask_context_task_dec(cur->context);
        }
    }
}
//获取当前session
int ztask_snc_session() {
    coroutine_t *cur = coroutine_running();
    if (cur)
        return cur->session;
    return 0;
}
//返回给调用方
int ztask_snc_ret(void * msg, size_t sz) {
    coroutine_t *cur = coroutine_running();
    return ztask_send(cur->context, 0, cur->source_ret, PTYPE_RESPONSE, cur->session_ret, msg, sz);
}
//获取返回session
int ztask_snc_retsession() {
    coroutine_t *cur = coroutine_running();
    return cur->session_ret;
}
//发起一个异步调用
int ztask_snc_call(struct ztask_context * context, int handle, int type, void * data, size_t sz, int *o_type, void ** o_msg, size_t *o_sz) {
    coroutine_t *cur = coroutine_running();
    ztask_send(context, ztask_context_handle(context), handle, type, cur->session, data, sz);
    coroutine_yield();//切出
    if (o_type)
        *o_type = cur->type;
    if (o_msg)
        *o_msg = cur->msg;
    if (o_sz)
        *o_sz = cur->sz;
}
//以名称方式发起一个异步调用
int ztask_snc_callname(char* name, int type, void * data, size_t sz, void ** o_msg, size_t *o_sz) {
    coroutine_t *cur = coroutine_running();
    ztask_sendname(cur->context, 0, name, type, cur->session, data, sz);
    coroutine_yield();//切出
    if (o_msg)
        *o_msg = cur->msg;
    if (o_sz)
        *o_sz = cur->sz;
}
//带超时的调用
int ztask_snc_callname_s(char* name, int type, void * data, size_t sz, void ** o_msg, size_t *o_sz, int time) {
    coroutine_t *cur = coroutine_running();
    ztask_sendname(cur->context, ztask_context_handle(cur->context), name, type, cur->session, data, sz);
    ztask_timeout(ztask_context_handle(cur->context), time, cur->session);//超时
    coroutine_yield();//切出
    if (cur->msg || cur->sz)
        ztask_timeout_del(ztask_context_handle(cur->context), ztask_snc_session());//移除定时器
    if (o_msg)
        *o_msg = cur->msg;
    if (o_sz)
        *o_sz = cur->sz;
}
//
void *ztask_snc_userdata(struct ztask_context * ctx, void *ud) {
    if (ud)
        ((struct snc *)ztask_getud(ctx))->ud = ud;
    return ((struct snc *)ztask_getud(ctx))->ud;
}
ztask_cb ztask_snc_callback(struct ztask_context * ctx, ztask_cb cb) {
    if (cb)
        ((struct snc *)ztask_getud(ctx))->_cb = cb;
    return ((struct snc *)ztask_getud(ctx))->_cb;
}

//协程环境回调
static void _async_func(coroutine_t *c) {
    struct snc *l = c->ud;
    l->_cb(c->context, l->ud, c->type, c->session, c->source, c->msg, c->sz);
}
//转发callback
static int _cb(struct ztask_context * context, struct snc *l, int type, int session, uint32_t source, const void * msg, size_t sz) {
    if (type == PTYPE_RESPONSE)
    {
        //切回返回的协程
        struct coroutine_s theNode = { 0 };
        struct coroutine_s *n;
        theNode.session = session;
        n = RB_FIND(coroutine_tree_s, &l->coroutine, &theNode);
        if (n) {
            n->ud = l;
            n->type = type;
            n->msg = msg;
            n->sz = sz;
            n->source = source;
            if (coroutine_resume(n)) {
                //协程执行完毕,删除这个协程
                RB_REMOVE(coroutine_tree_s, &l->coroutine, n);
                coroutine_delete(n);
                ztask_context_task_dec(context);
            }
        }
        else {
            ztask_error(context, "[snc] Unknown session %x", session);
        }
    }
    else {
        //没找到对应的协程
        //创建新的协程执行
        coroutine_t * c = coroutine_new(_async_func);
        if (!c || !l->_cb)
            return 0;
        ztask_context_task_inc(context);
        c->ud = l;
        c->type = type;
        c->context = context;
        c->session = ztask_context_newsession(context);
        c->msg = msg;
        c->sz = sz;
        c->source = source;
        c->session_ret = session;
        c->source_ret = source;
        //把协程保存起来
        RB_INSERT(coroutine_tree_s, &l->coroutine, c);
        if (coroutine_resume(c)) {
            //协程执行完毕,删除这个协程
            RB_REMOVE(coroutine_tree_s, &l->coroutine, c);
            coroutine_delete(c);
            ztask_context_task_dec(context);
        }
    }
    return 0;
}


//初始化回调
static void _init_func(coroutine_t *c) {
    struct ztask_snc * args = c->msg;
    int err = args->_init_cb(c->context, args->ud, args->msg, args->sz);
    //初始化流程正式完毕
    if (err) {
        ztask_command(c->context, "EXIT", NULL);
    }
}
//此回调保证在业务线程被调用
static int launch_cb(struct ztask_context * context, struct snc *l, int type, int session, uint32_t source, struct ztask_snc * args, size_t sz) {
    assert(type == 0 && session == 0);
    if (sizeof(struct ztask_snc) != sz)
        return -1;
    l->_cb = args->_cb;
    if (!args->_init_cb)
        return -1;
    //设置回调函数
    if (args->is_ui) {
        l->_ui.snc_cb = _cb;
    } else
        ztask_callback(context, l, _cb);
    l->ud = args->ud;
    l->_exit_cb = args->_exit_cb;
    //创建协程执行init
    coroutine_t * c = coroutine_new(_init_func);
    ztask_context_task_inc(context);
    c->context = context;
    c->session = ztask_context_newsession(context);
    c->msg = args;
    c->sz = sz;
    //把协程保存起来
    RB_INSERT(coroutine_tree_s, &l->coroutine, c);
    if (coroutine_resume(c)) {
        //协程执行完毕,删除这个协程
        RB_REMOVE(coroutine_tree_s, &l->coroutine, c);
        coroutine_delete(c);
        ztask_context_task_dec(context);
    }
    //返回不一定初始化成功可能是协程挂起
    return 0;
}

int snc_init(struct snc *l, struct ztask_context *ctx, const char * args, const size_t sz) {
    if (!args || !sz)
        return -1;
    char * tmp = ztask_malloc(sz);
    memcpy(tmp, args, sz);
    struct ztask_snc *_args = args;
    uint32_t handle_id = ztask_context_handle(ctx);
    if (_args->is_ui) {
        //转发初始化调用到ui
        l->_ui.snc_cb = launch_cb;
        ztask_callback(ctx, l, ztask_ui_cb);
        ztask_ui_cb(ctx, l, PTYPE_TEXT, 0, handle_id, tmp, sz);
        ztask_free(tmp);
    }
    else {
        // it must be first message
        ztask_callback(ctx, l, launch_cb);
        ztask_send(ctx, 0, handle_id, PTYPE_TAG_DONTCOPY, 0, tmp, sz);
    }
    return 0;
}

struct snc *snc_create(void) {
    struct snc * l = ztask_malloc(sizeof(*l));
    memset(l, 0, sizeof(*l));
    return l;
}

void snc_release(struct snc *l) {
    if (l->_exit_cb) {
        l->_exit_cb(l->ctx, l->ud);
    }
    ztask_free(l);
}

void snc_signal(struct snc *l, int signal) {

}

