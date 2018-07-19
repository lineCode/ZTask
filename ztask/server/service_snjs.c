#include "ztask.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <duktape.h>

struct snjs {
    struct ztask_context *ctx;
    duk_context *duk;
};


static int
init_cb(struct snjs *l, struct ztask_context *ctx, const char * args, size_t sz) {
    l->ctx = ctx;
    return 0;
}
//加载回调
static int launch_cb(struct ztask_context * context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
    assert(type == 0 && session == 0);
    struct snjs *l = ud;
    //将回调地址设置为空
    ztask_callback(context, NULL, NULL);
    int err = init_cb(l, context, msg, sz);
    if (err) {
        ztask_command(context, "EXIT", NULL);
    }
    return 0;
}
//服务初始化
int snjs_init(struct snjs *l, struct ztask_context *ctx, const char * args, const size_t sz) {
    char * tmp = ztask_malloc(sz);
    memcpy(tmp, args, sz);
    ztask_callback(ctx, l, launch_cb);
    const char * self = ztask_command(ctx, "REG", NULL);
    uint32_t handle_id = strtoul(self + 1, NULL, 16);
    //给自己发送一个消息,方便进入调度器
    ztask_send(ctx, 0, handle_id, PTYPE_TAG_DONTCOPY, 0, tmp, sz);
    return 0;
}
//js内存管理
static void *snjs_alloc(void *udata, size_t size) {
    return ztask_malloc(size);
}
static void *snjs_realloc(void *udata, void *ptr, size_t size) {
    return ztask_realloc(ptr, size);
}
static void snjs_free(void *udata, void *ptr) {
    ztask_free(ptr);
}

static void snjs_fatal(void *udata, const char *msg) {
    struct snjs * l = (struct snjs *)udata;  /* ignored in this case, silence warning */
    msg = (msg ? msg : "no message");
    ztask_error(l->ctx, "FATAL ERROR: %s\n", msg);
}
//创建js服务
struct snjs *snjs_create(void) {
    struct snjs * l = ztask_malloc(sizeof(*l));
    memset(l, 0, sizeof(*l));
    l->duk = duk_create_heap(snjs_alloc, snjs_realloc, snjs_free, l, snjs_fatal);
    return l;
}
//释放js服务
void snjs_release(struct snjs *l) {
    duk_destroy_heap(l->duk);
    ztask_free(l);
}

void
snjs_signal(struct snjs *l, int signal) {

}
