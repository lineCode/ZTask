#include <stddef.h>
#include <assert.h>
#include <math.h>
#include <malloc.h>
#include "thread.h"
#include "coroutine.h"
#include <tree.h>
#include <ztask.h>
static ztask_key cur_key = { 0 };

//汇编实现
int* __cdecl jump_fcontext(fcontext_t* ofc, fcontext_t nfc, void* vp, char preserve_fpu);
fcontext_t __cdecl make_fcontext(void* sp, size_t size, void(*fn)(int*));

// Detect posix
#if !defined(_WIN32) && (defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__)))
/* UNIX-style OS. ------------------------------------------- */
#   include <unistd.h>
#   define _HAVE_POSIX 1
#endif

#ifdef _WIN32
#   define WIN32_LEAN_AND_LEAN
#   include <Windows.h>
#if defined(__x86_64__) || defined(__x86_64) \
    || defined(__amd64__) || defined(__amd64) \
    || defined(_M_X64) || defined(_M_AMD64)
/* Windows seams not to provide a constant or function
* telling the minimal stacksize */
#   define MIN_STACKSIZE  8 * 1024
#else
#   define MIN_STACKSIZE  4 * 1024
#endif

static size_t getPageSize()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (size_t)si.dwPageSize;
}

static size_t getMinSize()
{
    return MIN_STACKSIZE;
}

static size_t getMaxSize()
{
    return  1 * 1024 * 1024 * 1024; /* 1GB */
}

static size_t getDefaultSize()
{
#ifdef WIN64
    return 512 * 1024;   /* 128Kb */
#else
    return 256 * 1024;   /* 128Kb */
#endif
}

#elif defined(_HAVE_POSIX)
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#if !defined (SIGSTKSZ)
# define SIGSTKSZ (8 * 1024)
# define UDEF_SIGSTKSZ
#endif

static size_t getPageSize()
{
    /* conform to POSIX.1-2001 */
    return (size_t)sysconf(_SC_PAGESIZE);
}

static size_t getMinSize()
{
    return SIGSTKSZ;
}

static size_t getMaxSize()
{
    rlimit limit;
    getrlimit(RLIMIT_STACK, &limit);

    return (size_t)limit.rlim_max;
}

static size_t getDefaultSize()
{
    size_t size;
    size_t maxSize;
    rlimit limit;

    getrlimit(RLIMIT_STACK, &limit);

    size = 8 * getMinSize();
    if (RLIM_INFINITY == limit.rlim_max)
        return size;
    maxSize = getMaxSize();
    return maxSize < size ? maxSize : size;
}
#endif

//创建栈空间
void create_fcontext_stack(void ** sptr, size_t *size)
{
    size_t pages;
    size_t size_;
    void* vp;
    *sptr = NULL;

    if (*size == 0)
        *size = getDefaultSize();
    if (*size <= getMinSize())
        *size = getMinSize();
    assert(*size <= getMaxSize());

    pages = (size_t)floorf((float)(*size) / (float)(getPageSize()));
    assert(pages >= 2);     /* at least two pages must fit into stack (one page is guard-page) */

    size_ = pages * getPageSize();
    assert(size_ != 0 && *size != 0);
    assert(size_ <= *size);

#ifdef _WIN32
    vp = VirtualAlloc(0, size_, MEM_COMMIT, PAGE_READWRITE);
    if (!vp)
        return;

    DWORD old_options;
    VirtualProtect(vp, getPageSize(), PAGE_READWRITE | PAGE_GUARD, &old_options);
#elif defined(_HAVE_POSIX)
# if defined(MAP_ANON)
    vp = mmap(0, size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
# else
    vp = mmap(0, size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
# endif
    if (vp == MAP_FAILED)
        return s;
    mprotect(vp, getPageSize(), PROT_NONE);
#else
    vp = ztask_malloc(size_);
    if (!vp)
        return;
#endif
    //栈由低向上发展
    *sptr = (char*)vp + size_;
    *size = size_;
}
//释放栈空间
void destroy_fcontext_stack(void * sptr, size_t size)
{
    void* vp;

    assert(size >= getMinSize());
    assert(size <= getMaxSize());

    vp = (char*)sptr - size;

#ifdef _WIN32
    VirtualFree(vp, 0, MEM_RELEASE);
#elif defined(_HAVE_POSIX)
    munmap(vp, size);
#else
    ztask_free(vp);
#endif
}
//协程入口地址
static void mainfunc(coroutine_t* c) {
    //设置协程状态为运行
    c->status = COROUTINE_RUNNING;
    //保存当前协程到线程
    ztask_key_set(&cur_key, c);
    c->cb(c);
    //协程执行完毕,切回原协程
    c->status = COROUTINE_END;
    jump_fcontext(&c->nfc, c->ofc, c, TRUE);
}
//创建新协程
coroutine_t *coroutine_new(fcontext_cb cb) {
    coroutine_t *p = (coroutine_t *)ztask_malloc(sizeof(*p));
    if (!p)
        return NULL;
    memset(p, 0, sizeof(*p));
    //分配栈空间
    create_fcontext_stack(&p->sptr, &p->ssize);
    if(!p->sptr){
        ztask_free(p);
        return NULL;
    }
    //生成上下文
    p->nfc = make_fcontext(p->sptr, p->ssize, mainfunc);
    //保存用户回调
    p->cb = cb;
    //设置初始状态
    p->status = COROUTINE_READY;
    //设置魔数
    p->magic = 0x12345678;
    return p;
}
//回收协程
void coroutine_delete(coroutine_t *c) {
    if (c->status != COROUTINE_END || c->magic != 0x12345678)
        return;
    destroy_fcontext_stack(c->sptr, c->ssize);
    ztask_free(c);
}
//恢复
int coroutine_resume(coroutine_t *c) {
    if (!c)
        return 0;
    if (c->status != COROUTINE_SUSPEND && c->status != COROUTINE_READY)
        return 0;
    //记录切换前协程
    coroutine_t *old = c->c_old = ztask_key_get(&cur_key);
    //保存当前协程到线程
    ztask_key_set(&cur_key, c);
    jump_fcontext(&c->ofc, c->nfc, c, TRUE);
    //恢复切换前的协程
    ztask_key_set(&cur_key, old);
    if (c->status == COROUTINE_END) {
        return 1;
    }
    return 0;
}
//挂起
void coroutine_yield() {
    coroutine_t *cur = ztask_key_get(&cur_key);
    //保存当前协程到线程
    ztask_key_set(&cur_key, cur->c_old);
    //设置协程状态为挂起
    cur->status = COROUTINE_SUSPEND;
    //切回原协程
    jump_fcontext(&cur->nfc, cur->ofc, cur, TRUE);
}
//返回当前执行的协程
coroutine_t *coroutine_running() {
    return ztask_key_get(&cur_key);
}
void coroutine_msg(int *type, void ** msg, size_t *sz) {
    coroutine_t *c = ztask_key_get(&cur_key);
    if (c && type && msg && sz)
    {
        *type = c->type;
        *msg = c->msg;
        *sz = c->sz;
    }
}

void coroutine_init() {
    ztask_key_create(&cur_key);
    ztask_key_set(&cur_key, NULL);
}

int coroutine_compare(struct coroutine_s *e1, struct coroutine_s *e2)
{
    return (e1->session < e2->session ? -1 : e1->session > e2->session);
}

RB_GENERATE(coroutine_tree_s, coroutine_s, entry, coroutine_compare);

