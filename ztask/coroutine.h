#ifndef BOTCOT_HEADER
#define BOTCOT_HEADER

#include <stddef.h>
#include <assert.h>
#include <math.h>
#include "tree.h"
#include "ztask.h"
#define COROUTINE_DEAD     0
#define COROUTINE_READY    1
#define COROUTINE_RUNNING  2
#define COROUTINE_SUSPEND  3
#define COROUTINE_END      4

typedef void* fcontext_t;//协程上下文
typedef void(*fcontext_cb)(struct coroutine_s*);
//协程结构
typedef struct coroutine_s {
    RB_ENTRY(coroutine_s) entry;    //红黑树结构,方便管理海量协程
    fcontext_t nfc; //新协程
    fcontext_t ofc; //旧协程
    struct coroutine_s *c_old;
    int status;     //执行状态
    void* sptr;     //栈数据指针
    size_t ssize;   //栈大小
    fcontext_cb cb; //用户回调
    void *data;     //存储协程相关数据
    //用户封装参数
    struct ztask_context * context;
    void *ud;
    int type;
    int session;
    int session_ret;//创建时确定
    uint32_t source;
    uint32_t source_ret;//创建时确定
    const void * msg;
    size_t sz;
    uint32_t magic;//魔数
}coroutine_t;

RB_HEAD(coroutine_tree_s, coroutine_s);
RB_PROTOTYPE(coroutine_tree_s, coroutine_s, ,);

void coroutine_init();
#endif // #ifndef BOTCOT_HEADER
