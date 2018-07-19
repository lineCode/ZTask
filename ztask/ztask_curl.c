#include "ztask.h"
#include "queue.h"
#include "thread.h"
#include "rwlock.h"
#include "ztask_http.h"
#include "ztask_handle.h"
#include "ztask_server.h"
#include "ztask_mq.h"
#include <curl/curl.h>
#include <errno.h>
typedef struct curl_data_s
{
    size_t m_pos;//当前读写位置
    size_t m_len;//缓冲总长
    char *m_data;//数据

    int session;
    uint32_t source;
}curl_data_t;
typedef struct curl_handle_wq {
    QUEUE wq;
    void* esay;
}curl_handle_wq;
typedef struct curl_thread_data {
    CURLM* curl_handle;
    int num;        //当前线程任务数量
}curl_thread_data;
static CURLSH* curl_share_handle;       //共享句柄
static struct rwlock curl_share_lock;   
static ztask_mutex curl_mutex;          //互斥体
static ztask_cond curl_cond;            //
static QUEUE curl_wq;



//数据回调
static unsigned int curl_write_cb(char* in, unsigned int size, unsigned int nmemb, curl_data_t* data) {
    if ((data->m_len - 1) < (data->m_pos + size*nmemb)) {
        data->m_len += (size*nmemb + 1024);
        data->m_data = ztask_realloc(data->m_data, data->m_len);
    }
    memcpy(data->m_data + data->m_pos, in, size*nmemb);
    data->m_pos += (size*nmemb);
    data->m_data[data->m_pos] = 0;
    return size * nmemb;
}
//头回调
static size_t curl_header_cb(char *buffer, size_t size, size_t nmemb, curl_data_t* data)
{
    if (memcmp(buffer, "Set-Cookie: ", 12) == 0 && nmemb * size > 13) {
        char* end = strchr(buffer + 12, ';');
        if (end) {
            int len = end - buffer - 12;
            char *cookies = NULL;

            if ((data->m_len - 1) < (data->m_pos + size * nmemb)) {
                data->m_len += (size*nmemb + 1024);
                data->m_data = ztask_realloc(data->m_data, data->m_len);
            }

            cookies = data->m_data + data->m_pos;
            data->m_pos += len + 2;

            memcpy(cookies, buffer + 12, len);
            cookies[len] = ';';
            cookies[len + 1] = ' ';
            cookies[len + 2] = '\0';
        }
    }
    else if ((buffer[0] == '\r' && buffer[1] == '\n') || buffer[0] == '\n') {
        //头部响应完成
        if ((data->m_len - 1) < (data->m_pos + 1)) {
            data->m_len += (size*nmemb + 1024);
            data->m_data = ztask_realloc(data->m_data, data->m_len);
        }
        ((struct ztask_curl_message *)data->m_data)->cookies_len = data->m_pos - sizeof(struct ztask_curl_message);
        data->m_data[data->m_pos] = 0;
        data->m_pos++;
    }
    return nmemb * size;
}
//添加一个管理的句柄
void ztask_curl(uint32_t source, int session, void* esay) {
    curl_data_t* data = (curl_data_t*)ztask_malloc(sizeof(curl_data_t));
    data->m_data = ztask_malloc(sizeof(struct ztask_curl_message) + 1024);
    data->m_len = sizeof(struct ztask_curl_message) + 1024;
    data->m_pos = sizeof(struct ztask_curl_message);
    memset(data->m_data, 0, sizeof(struct ztask_curl_message));
    data->session = session;
    data->source = source;
    curl_easy_setud(esay, data);
    //设置读数据回调,将数据写到缓冲区
    curl_easy_setopt(esay, CURLOPT_WRITEDATA, data);
    curl_easy_setopt(esay, CURLOPT_WRITEFUNCTION, curl_write_cb);
    //设置头回调
    curl_easy_setopt(esay, CURLOPT_HEADERDATA, data);
    curl_easy_setopt(esay, CURLOPT_HEADERFUNCTION, curl_header_cb);

    curl_easy_setopt(esay, CURLOPT_NOSIGNAL, 1L);//禁用掉alarm信号，防止多线程中使用超时崩溃  
    curl_easy_setopt(esay, CURLOPT_FORBID_REUSE, 1L); //禁掉alarm后会有大量CLOSE_WAIT  

    //设置dns共享
    curl_easy_setopt(esay, CURLOPT_SHARE, curl_share_handle);
    curl_easy_setopt(esay, CURLOPT_DNS_CACHE_TIMEOUT, 60 * 5);

    //添加到异步事件
    curl_handle_wq *wq = ztask_malloc(sizeof(*wq));
    wq->esay = esay;
    ztask_mutex_lock(&curl_mutex);
    QUEUE_INSERT_TAIL(&curl_wq, &wq->wq);
    //curl_multi_add_handle(curl_handle, esay);
    ztask_mutex_unlock(&curl_mutex);
    //唤醒线程来处理
    ztask_cond_signal(&curl_cond);
}
//curl事件循环
int ztask_http_poll(void *p) {
    int still_running = 0;
    CURLMsg* msg;
    int msgs_left; /* how many messages are left */
    curl_thread_data *hp = p;
    CURLM* curl_handle = hp->curl_handle;

    fd_set fdread;
    fd_set fdwrite;
    fd_set fdexcep;
    int maxfd = -1;

    do {
        struct timeval timeout;
        int rc; /* select() return code */
        CURLMcode mc; /* curl_multi_fdset() return code */

        long curl_timeo = -1;

        FD_ZERO(&fdread);
        FD_ZERO(&fdwrite);
        FD_ZERO(&fdexcep);

        /* set a suitable timeout to play around with */
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        curl_multi_timeout(curl_handle, &curl_timeo);
        if (curl_timeo >= 0) {
            timeout.tv_sec = curl_timeo / 1000;
            if (timeout.tv_sec > 1) {
                timeout.tv_sec = 1;
            } else {
                timeout.tv_usec = (curl_timeo % 1000) * 1000;
            }
        }

        /* get file descriptors from the transfers */
        mc = curl_multi_fdset(curl_handle, &fdread, &fdwrite, &fdexcep, &maxfd);

        if (mc != CURLM_OK) {
            fprintf(stderr, "curl_multi_fdset() failed, code %d.\n", mc);
            break;
        }

        /* On success the value of maxfd is guaranteed to be >= -1. We call
        select(maxfd + 1, ...); specially in case of (maxfd == -1) there are
        no fds ready yet so we call select(0, ...) --or Sleep() on Windows--
        to sleep 100ms, which is the minimum suggested value in the
        curl_multi_fdset() doc. */

        if (maxfd == -1) {
#ifdef _WIN32
            //Sleep(100);
            if (ztask_cond_wait_timeout(&curl_cond, &curl_mutex, 20) == 1) {
                goto wakeup;
            }
            else {
                rc = 0;
            }
#else
            /* Portable sleep for platforms other than Windows. */
            struct timeval wait = { 0, 100 * 1000 }; /* 100ms */
            rc = select(0, NULL, NULL, NULL, &wait);
#endif
        }
        else {
            /* Note that on some platforms 'timeout' may be modified by select().
            If you need access to the original value save a copy beforehand. */
            rc = select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);
        }

        switch (rc) {
        case -1:
            /* select error */
            break;
        case 0: /* timeout */
        default: /* action */
            curl_multi_perform(curl_handle, &still_running);
            break;
        }
    } while (still_running);

    /* See how the transfers went */
    while ((msg = curl_multi_info_read(curl_handle, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            //处理完毕,移除句柄
            curl_multi_remove_handle(curl_handle, msg->easy_handle);
            hp->num--;
            curl_data_t* data = (curl_data_t *)curl_easy_getud(msg->easy_handle);
            if (data)
            {
                //处理数据
                ((struct ztask_curl_message *)data->m_data)->data_len = data->m_pos == sizeof(struct ztask_curl_message) ? 0 : data->m_pos - sizeof(struct ztask_curl_message) - ((struct ztask_curl_message *)data->m_data)->cookies_len - 1;

                struct ztask_message message;
                message.source = 0;
                message.session = data->session;
                message.data = data->m_data;
                message.sz = data->m_pos | ((size_t)(PTYPE_RESPONSE | PTYPE_TAG_DONTCOPY) << MESSAGE_TYPE_SHIFT);

                if (ztask_context_push(data->source, &message)) {
                    ztask_free(data->m_data);
                }
                ztask_free(data);
            }
        }
    }


    if (ztask_cond_wait_timeout(&curl_cond, &curl_mutex, 20) == 0)//没有业务就挂起
        return 1;
                                             //被唤醒,处理新到的业务
wakeup:
    ztask_mutex_lock(&curl_mutex);
    if (!QUEUE_EMPTY(&curl_wq) && hp->num < FD_SETSIZE) {
        QUEUE *wq = QUEUE_HEAD(&curl_wq);
        QUEUE_REMOVE(wq);
        ztask_mutex_unlock(&curl_mutex);

        curl_handle_wq *q = QUEUE_DATA(wq, curl_handle_wq, wq);
        curl_multi_add_handle(curl_handle, q->esay);
        hp->num++;
        ztask_free(wq);
        goto wakeup;
    }
    else {
        ztask_mutex_unlock(&curl_mutex);
    }
    return 1;
}

//curl内存
static void* _curl_malloc_callback(size_t size) {
    return ztask_malloc(size);
}
static void _curl_free_callback(void* ptr) {
    ztask_free(ptr);
}
static void* _curl_realloc_callback(void* ptr, size_t size) {
    return ztask_realloc(ptr, size);
}
static char* _curl_strdup_callback(const char* str) {
    return ztask_strdup(str);
}
static void* _curl_calloc_callback(size_t nmemb, size_t size) {
    return ztask_calloc(nmemb, size);
}
//curl互斥锁
static void _curl_Lock(CURL *h, curl_lock_data data, curl_lock_access access, void*userptr) {
    if (data == CURL_LOCK_DATA_DNS) {
        if (access == CURL_LOCK_ACCESS_SHARED)
            rwlock_rlock(&curl_share_lock);
        else if (access == CURL_LOCK_ACCESS_SINGLE)
            rwlock_wlock(&curl_share_lock);
    }
}
static void _curl_Unlock(CURL *handle, curl_lock_data data, void*userptr) {
    if (data == CURL_LOCK_DATA_DNS) {
        rwlock_wunlock(&curl_share_lock);
        rwlock_runlock(&curl_share_lock);
    }
}

void ztask_http_init() {
    //初始化curl
    curl_global_init_mem(CURL_GLOBAL_ALL, _curl_malloc_callback, _curl_free_callback, _curl_realloc_callback, _curl_strdup_callback, _curl_calloc_callback);

    //初始化互斥体
    ztask_mutex_init(&curl_mutex);
    rwlock_init(&curl_share_lock);
    //初始化信号
    ztask_cond_init(&curl_cond);
    //初始化队列
    QUEUE_INIT(&curl_wq);

    //创建共享句柄
    curl_share_handle = curl_share_init();
    curl_share_setopt(curl_share_handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    //curl_share_setopt(curl_share_handle, CURLSHOPT_LOCKFUNC, _curl_Lock);
    //curl_share_setopt(curl_share_handle, CURLSHOPT_UNLOCKFUNC, _curl_Unlock);
}
void *ztask_http_create() {
    curl_thread_data *hp = ztask_malloc(sizeof(curl_thread_data));
    //创建异步句柄
    hp->curl_handle = curl_multi_init();
    hp->num = 0;
    return hp;
}








