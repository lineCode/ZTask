#include "ztask.h"

#include "ztask_timer.h"
#include "ztask_mq.h"
#include "ztask_server.h"
#include "ztask_handle.h"
#include "spinlock.h"

#if defined(_WIN32) || defined(_WIN64)
#include <sys/timeb.h>
#else
#include <time.h>
#endif
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#if defined(__APPLE__)
#include <sys/time.h>
#include <mach/task.h>
#include <mach/mach.h>
#endif

typedef void(*timer_execute_func)(void *ud, void *arg);

#define TIME_NEAR_SHIFT 8
#define TIME_NEAR (1 << TIME_NEAR_SHIFT) // 2^8 = 256
#define TIME_LEVEL_SHIFT 6
#define TIME_LEVEL (1 << TIME_LEVEL_SHIFT) // 64
#define TIME_NEAR_MASK (TIME_NEAR-1)  // 255
#define TIME_LEVEL_MASK (TIME_LEVEL-1) // 63

struct timer_event {
    uint32_t handle;//句柄
    int session;//时间
};

struct timer_node {
    struct timer_node *next;
    uint32_t expire;    //计时器事件触发时间
};

struct link_list { //计时器node列表
    struct timer_node head;
    struct timer_node *tail;
};
#undef near
struct timer {
    struct link_list near[TIME_NEAR];//最近的时间
    struct link_list t[4][TIME_LEVEL];//根据时间久远分级
    spinlock lock;
    uint32_t time;          // 计时器，每百分之一秒更新一次 
    uint32_t starttime;     //起始时间 秒  
    uint64_t current;       // 当前时间与starttime的时间差 单位为百分之一秒 
    uint64_t current_point; //上一次update的时间， 百分之一秒
};

static struct timer * TI = NULL;
//清除链表,返回链表链
static inline struct timer_node *
link_clear(struct link_list *list) {
    struct timer_node * ret = list->head.next;// 获得链表头的下一个节点
    list->head.next = 0; // 链表头的下一个节点为0
    list->tail = &(list->head); // 链表尾 = 链表头

    return ret; // 返回链表头的下一个节点
}

static inline void
link(struct link_list *list, struct timer_node *node) {
    list->tail->next = node;
    list->tail = node;
    node->next = 0;
}
//添加节点
static void
add_node(struct timer *T, struct timer_node *node) {
    uint32_t time = node->expire;
    uint32_t current_time = T->time;

    if ((time | TIME_NEAR_MASK) == (current_time | TIME_NEAR_MASK)) {
        link(&T->near[time&TIME_NEAR_MASK], node);
    }
    else {
        int i;
        uint32_t mask = TIME_NEAR << TIME_LEVEL_SHIFT;
        for (i = 0; i < 3; i++) {
            if ((time | (mask - 1)) == (current_time | (mask - 1))) {
                break;
            }
            mask <<= TIME_LEVEL_SHIFT;
        }

        link(&T->t[i][((time >> (TIME_NEAR_SHIFT + i*TIME_LEVEL_SHIFT)) & TIME_LEVEL_MASK)], node);
    }
}
//添加定时器
static void
timer_add(struct timer *T, void *arg, size_t sz, int time) {
    struct timer_node *node = (struct timer_node *)ztask_malloc(sizeof(*node) + sz);
    memcpy(node + 1, arg, sz);

    SPIN_LOCK(T);

    node->expire = time + T->time;
    add_node(T, node);

    SPIN_UNLOCK(T);
}

static void
move_list(struct timer *T, int level, int idx) {
    struct timer_node *current = link_clear(&T->t[level][idx]);
    while (current) {
        struct timer_node *temp = current->next;
        add_node(T, current);
        current = temp;
    }
}

static void
timer_shift(struct timer *T) {
    int mask = TIME_NEAR;
    uint32_t ct = ++T->time;
    if (ct == 0) {
        move_list(T, 3, 0);
    }
    else {
        uint32_t time = ct >> TIME_NEAR_SHIFT;
        int i = 0;

        while ((ct & (mask - 1)) == 0) {
            int idx = time & TIME_LEVEL_MASK;
            if (idx != 0) {
                move_list(T, i, idx);
                break;
            }
            mask <<= TIME_LEVEL_SHIFT;
            time >>= TIME_LEVEL_SHIFT;
            ++i;
        }
    }
}

static inline void
dispatch_list(struct timer_node *current) {
    do {
        struct timer_event * event = (struct timer_event *)(current + 1);
        struct ztask_message message;
        message.source = 0;
        message.session = event->session;
        message.data = NULL;
        message.sz = (size_t)PTYPE_RESPONSE << MESSAGE_TYPE_SHIFT;

        ztask_context_push(event->handle, &message);

        struct timer_node * temp = current;
        current = current->next;
        ztask_free(temp);
    } while (current);
}

static inline void
timer_execute(struct timer *T) {
    int idx = T->time & TIME_NEAR_MASK;

    while (T->near[idx].head.next) {
        struct timer_node *current = link_clear(&T->near[idx]);
        SPIN_UNLOCK(T);
        // dispatch_list don't need lock T
        dispatch_list(current);
        SPIN_LOCK(T);
    }
}

static void
timer_update(struct timer *T) {
    SPIN_LOCK(T);

    // try to dispatch timeout 0 (rare condition)
    timer_execute(T);

    // shift time first, and then dispatch timer message
    timer_shift(T);

    timer_execute(T);

    SPIN_UNLOCK(T);
}

static struct timer *
timer_create_timer() {
    struct timer *r = (struct timer *)ztask_malloc(sizeof(struct timer));
    memset(r, 0, sizeof(*r));

    int i, j;

    for (i = 0; i < TIME_NEAR; i++) {
        link_clear(&r->near[i]);
    }

    for (i = 0; i < 4; i++) {
        for (j = 0; j < TIME_LEVEL; j++) {
            link_clear(&r->t[i][j]);
        }
    }

    SPIN_INIT(r);

    r->current = 0;

    return r;
}

int
ztask_timeout(uint32_t handle, int time, int session) {
    if (time <= 0) {
        struct ztask_message message;
        message.source = 0;
        message.session = session;
        message.data = NULL;
        message.sz = (size_t)PTYPE_RESPONSE << MESSAGE_TYPE_SHIFT;

        if (ztask_context_push(handle, &message)) {
            return -1;
        }
    }
    else {
        struct timer_event event;
        event.handle = handle;
        event.session = session;
        timer_add(TI, &event, sizeof(event), time / 10);
    }

    return session;
}

void ztask_timeout_del(uint32_t handle, int session) {
    size_t idx, i, j;
    for (idx = 0; idx < TIME_NEAR; idx++)
    {
        while (TI->near[idx].head.next) {
            SPIN_UNLOCK(TI);
            struct timer_node * node = TI->near[idx].head.next;// 获得链表头的下一个节点
            struct timer_node * old = &TI->near[idx].head;
            while (node)
            {
                struct timer_event * event = (struct timer_event *)(node + 1);
                if (event->session == session && event->handle == handle) {
                    //从链表移除
                    old->next = node->next;
                    //如果是尾节点
                    if (TI->near[idx].tail == node) {
                        TI->near[idx].tail = old;
                    }
                    ztask_free(node);
                    return;
                }
                old = node;
                node = node->next;
            }

            SPIN_LOCK(TI);
        }
    }
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < TIME_LEVEL; j++)
        {
            while (TI->t[i][j].head.next) {
                SPIN_UNLOCK(TI);
                struct timer_node * node = TI->t[i][j].head.next;// 获得链表头的下一个节点
                struct timer_node * old = &TI->t[i][j].head;
                while (node)
                {
                    struct timer_event * event = (struct timer_event *)(node + 1);
                    if (event->session == session && event->handle == handle) {
                        //从链表移除
                        old->next = node->next;
                        //如果是尾节点
                        if (TI->t[i][j].tail == node) {
                            TI->t[i][j].tail = old;
                        }
                        ztask_free(node);
                        return;
                    }
                    old = node;
                    node = node->next;
                }

                SPIN_LOCK(TI);
            }
        }
    }
}
// centisecond: 1/100 second
static void
systime(uint32_t *sec, uint32_t *cs) {
#if defined(_WIN32) || defined(_WIN64)
    struct _timeb timebuffer;
    _ftime_s(&timebuffer);
    *sec = (uint32_t)timebuffer.time;
    *cs = timebuffer.millitm / 10;
#elif !defined(__APPLE__)
    struct timespec ti;
    clock_gettime(CLOCK_REALTIME, &ti);
    *sec = (uint32_t)ti.tv_sec;
    *cs = (uint32_t)(ti.tv_nsec / 10000000);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *sec = tv.tv_sec;
    *cs = tv.tv_usec / 10000;
#endif
}

static uint64_t
gettime() {
    uint64_t t;
#if defined(_WIN32) || defined(_WIN64)
    struct _timeb timebuffer;
    _ftime_s(&timebuffer);
    t = timebuffer.time * 100;
    t += timebuffer.millitm / 10;
#elif !defined(__APPLE__)

#ifdef CLOCK_MONOTONIC_RAW
#define CLOCK_TIMER CLOCK_MONOTONIC_RAW
#else
#define CLOCK_TIMER CLOCK_MONOTONIC
#endif

    struct timespec ti;
    clock_gettime(CLOCK_TIMER, &ti);
    t = (uint64_t)ti.tv_sec * 100;
    t += ti.tv_nsec / 10000000;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    t = (uint64_t)tv.tv_sec * 100;
    t += tv.tv_usec / 10000;
#endif
    return t;
}

void
ztask_updatetime(void) {
    uint64_t cp = gettime();
    if (cp < TI->current_point) {
        ztask_error(NULL, "time diff error: change from %lld to %lld", cp, TI->current_point);
        TI->current_point = cp;
    }
    else if (cp != TI->current_point) {
        uint32_t diff = (uint32_t)(cp - TI->current_point);
        TI->current_point = cp;
        TI->current += diff;
        int i;
        for (i = 0; i < diff; i++) {
            timer_update(TI);
        }
    }
}

uint32_t
ztask_starttime(void) {
    return TI->starttime;
}

uint64_t
ztask_now(void) {
    return TI->current;
}

void
ztask_timer_init(void) {
    TI = timer_create_timer();
    uint32_t current = 0;
    systime(&TI->starttime, &current);
    TI->current = current;
    TI->current_point = gettime();
}

// for profile

#define NANOSEC 1000000000
#define MICROSEC 1000000

uint64_t
ztask_thread_time(void) {
#if  defined(__APPLE__)
    struct task_thread_times_info aTaskInfo;
    mach_msg_type_number_t aTaskInfoCount = TASK_THREAD_TIMES_INFO_COUNT;
    if (KERN_SUCCESS != task_info(mach_task_self(), TASK_THREAD_TIMES_INFO, (task_info_t)&aTaskInfo, &aTaskInfoCount)) {
        return 0;
    }

    return (uint64_t)(aTaskInfo.user_time.seconds) + (uint64_t)aTaskInfo.user_time.microseconds;
#elif defined(_WIN32) || defined(_WIN64)
    //return uv_hrtime();
    LARGE_INTEGER counter;

    if (!QueryPerformanceCounter(&counter)) {
        return 0;
    }
    return counter.QuadPart;
#else
    struct timespec ti;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ti);

    return (uint64_t)ti.tv_sec * MICROSEC + (uint64_t)ti.tv_nsec / (NANOSEC / MICROSEC);
#endif
}
#if defined(_WIN32) || defined(_WIN64)
void usleep(uint32_t us)
{
    LARGE_INTEGER litmp;
    LONGLONG QPart1, QPart2;
    double dfFreq;
    if (us > 0)
        us--;
    QueryPerformanceFrequency(&litmp);
    dfFreq = (double)litmp.QuadPart;// 获得计数器的时钟频率  

    QueryPerformanceCounter(&litmp);
    QPart1 = litmp.QuadPart;// 获得初始值

    do {
        QueryPerformanceCounter(&litmp);
        QPart2 = litmp.QuadPart;//获得中止值
    } while ((double)(QPart2 - QPart1) / dfFreq * 1000000 < us);
}
#endif
