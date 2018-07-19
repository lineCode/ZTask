#include "ztask.h"
#include "thread.h"
#include "ztask_dns.h"
#include "ztask_mq.h"
#include <ares.h>

typedef struct dns_s
{
    int session;
    uint32_t source;
}dns_t;

static ares_channel dns_channel;
static ztask_mutex dns_mutex;
static ztask_cond dns_cond;

void dns_callback(void* arg, int status, int timeouts, struct hostent* host)  //ares  处理完成，返回DNS解析的信息  
{
    dns_t *d = arg;
    struct ztask_message message;
    message.source = 0;
    message.session = d->session;
    if (status == ARES_SUCCESS) {
        message.data = ztask_malloc(4);
        *((uint32_t*)message.data) = *((uint32_t *)host->h_addr_list[0]);
        message.sz = 4 | ((size_t)(PTYPE_RESPONSE | PTYPE_TAG_DONTCOPY) << MESSAGE_TYPE_SHIFT);
    }
    else {
        message.data = NULL;
        message.sz = 0 | ((size_t)(PTYPE_RESPONSE | PTYPE_TAG_DONTCOPY) << MESSAGE_TYPE_SHIFT);
    }
    if (ztask_context_push(d->source, &message)) {
        if (message.data)
            ztask_free(message.data);
    }
    ztask_free(d);
}

void ztask_getaddrinfo(struct ztask_context *ctx, int session, char* host) {
    dns_t *d = ztask_malloc(sizeof(*d));
    d->session = session;
    d->source = ztask_context_handle(ctx);
    ztask_mutex_lock(&dns_mutex);
    ares_gethostbyname(dns_channel, host, AF_INET, dns_callback, d);
    ztask_mutex_unlock(&dns_mutex);
    ztask_cond_signal(&dns_cond);
}

int ztask_dns_poll()
{
    int nfds, count;
    fd_set readers, writers;
    struct timeval tv, *tvp;
    FD_ZERO(&readers);
    FD_ZERO(&writers);
    ztask_mutex_lock(&dns_mutex);
    nfds = ares_fds(dns_channel, &readers, &writers);   //获取ares channel使用的FD
    if (nfds == 0)
    {
        ztask_mutex_unlock(&dns_mutex);
        //没有可操作的
        ztask_cond_wait(&dns_cond, &dns_mutex);//没有业务就挂起
        return 0;
    }
    tvp = ares_timeout(dns_channel, NULL, &tv);
    ztask_mutex_unlock(&dns_mutex);
    count = select(nfds, &readers, &writers, NULL, tvp);   //将ares的SOCKET FD 加入事件循环  
    ztask_mutex_lock(&dns_mutex);
    ares_process(dns_channel, &readers, &writers);  // 有事件发生 交由ares 处理  
    ztask_mutex_unlock(&dns_mutex);
    return 1;
}

static void* _ares_malloc_callback(size_t size) {
    return ztask_malloc(size);
}
static void _ares_free_callback(void* ptr) {
    ztask_free(ptr);
}
static void* _ares_realloc_callback(void* ptr, size_t size) {
    return ztask_realloc(ptr, size);
}
void ztask_dns_init() {
    ares_library_init_mem(ARES_LIB_INIT_ALL, _ares_malloc_callback, _ares_free_callback, _ares_realloc_callback);
    ares_init(&dns_channel);
    //初始化互斥体
    ztask_mutex_init(&dns_mutex);
    //初始化信号
    ztask_cond_init(&dns_cond);
}