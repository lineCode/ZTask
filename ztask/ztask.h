#ifndef ZTASK_H
#define ZTASK_H

#include <stddef.h>
#include <stdint.h>

#ifdef ZTASK_STATICLIB
#  define ZTASK_EXTERN
#elif defined(WIN32) || defined(_WIN32) || defined(__SYMBIAN32__)
#  if defined(BUILDING_LIBZTASK)
#    define ZTASK_EXTERN  __declspec(dllexport)
#    define ZTASK_MODULE  __declspec(dllexport)
#  else
#    define ZTASK_EXTERN  __declspec(dllimport)
#    define ZTASK_MODULE  __declspec(dllimport)
#  endif
#elif defined(BUILDING_LIBZTASK) && defined(ZTASK_HIDDEN_SYMBOLS)
#  define ZTASK_EXTERN ZTASK_EXTERN_SYMBOL
#else
#  define ZTASK_EXTERN
#endif

#define PTYPE_TEXT 0        //文本消息
#define PTYPE_RESPONSE 1    //返回消息
#define PTYPE_MULTICAST 2   //组播消息
#define PTYPE_CLIENT 3
#define PTYPE_SYSTEM 4      //系统消息
#define PTYPE_HARBOR 5
#define PTYPE_SOCKET 6      //io消息
#define PTYPE_ERROR 7       //错误消息
#define PTYPE_RESERVED_QUEUE 8
#define PTYPE_RESERVED_DEBUG 9
#define PTYPE_RESERVED_LUA 10
#define PTYPE_RESERVED_SNAX 11

#define PTYPE_USER  15      //用户定义

#define PTYPE_TAG_DONTCOPY 0x10000      //不用拷贝消息,在消息处理完成后会调用free回收
#define PTYPE_TAG_ALLOCSESSION 0x20000  //分配一个session
/*
调度器启动流程,日志服务,bootstrap服务
服务寄生于模块,具体服务在调度器层面不可见,不同实现的服务由对应的模块加载,模块实现应该为中间件,也允许存在模块即服务的形态
服务创建成功后会分配全局唯一的id号,高8位为节点id,低24位为本地唯一id
每个服务都有自己的context
*/
struct ztask_context;
typedef int(*ztask_cb)(struct ztask_context * context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz);

struct ztask_config {
    int work_thread;            //业务线程数
    int io_thread;              //IO线程数
    int http_thread;            //HTTP线程数
    int harbor;                 //节点id
    int profile;                //开启性能分析
    const char * standalone;    //中心节点监听地址
    const char * address;       //节点监听地址
    const char * master;        //中心节点连接地址
    const char * debug;         //调试监听地址
    const char * daemon;        //守护模式,仅unix
    const char * module_path;   //模块查找路径
    const char * start;         //boot后启动的服务
    const char * bootstrap;     //bootstrap服务
    const char * bootstrap_parm;//bootstrap服务参数
    size_t bootstrap_parm_sz;   //参数长度
    const char * logservice;    //日志服务
    const char * logger;        //日志文件,不定义则打印到控制台

};
//初始化调度器
ZTASK_EXTERN void ztask_init();
//反初始化调度器
ZTASK_EXTERN void ztask_uninit();
//启动调度器
ZTASK_EXTERN void ztask_start(struct ztask_config * config);
//ui循环
struct ztask_ui_parm {
    void *param1;
    void *param2;
};
ZTASK_EXTERN void ztask_ui_loop(struct ztask_ui_parm *parm);

typedef void * (*ztask_dl_create)(void);
typedef int(*ztask_dl_init)(void * inst, struct ztask_context *, const char * parm, const size_t sz);
typedef void(*ztask_dl_release)(void * inst);
typedef void(*ztask_dl_signal)(void * inst, int signal);

struct ztask_module {
    const char * name;
    void * module;
    ztask_dl_create create;
    ztask_dl_init init;
    ztask_dl_release release;
    ztask_dl_signal signal;
};
//插入模块
void ztask_module_insert(struct ztask_module *mod);

ZTASK_EXTERN const char * ztask_getenv(const char *key);
ZTASK_EXTERN void ztask_setenv(const char *key, const char *value);

//输出调试信息
ZTASK_EXTERN void ztask_error(struct ztask_context * context, const char *msg, ...);
//调用控制函数,一般由没有底层调用能力的脚本类模块使用
ZTASK_EXTERN const char * ztask_command(struct ztask_context * context, const char * cmd, const char * parm);
//创建一个上下文,等同服务
ZTASK_EXTERN struct ztask_context * ztask_context_new(const char * name, const char * parm, const size_t sz);
//通过上下文查询handle
ZTASK_EXTERN uint32_t ztask_context_handle(struct ztask_context *);
//通过handle获取上下文
ZTASK_EXTERN struct ztask_context * ztask_handle_grab(uint32_t handle);
//名称查询handle
ZTASK_EXTERN uint32_t ztask_queryname(struct ztask_context * context, const char * name);
//通过handle发送消息
ZTASK_EXTERN int ztask_send(struct ztask_context * context, uint32_t source, uint32_t destination, int type, int session, void * msg, size_t sz);
//通过名称发送消息
ZTASK_EXTERN int ztask_sendname(struct ztask_context * context, uint32_t source, const char * destination, int type, int session, void * msg, size_t sz);
//设置和获取服务别名
ZTASK_EXTERN char *ztask_alias(struct ztask_context * context, char *alias);
//是否是远程服务
ZTASK_EXTERN int ztask_isremote(struct ztask_context *, uint32_t handle, int * harbor);

//设置回调地址
ZTASK_EXTERN void ztask_callback(struct ztask_context * context, void *ud, ztask_cb cb);
ZTASK_EXTERN void *ztask_getud(struct ztask_context * context);
ZTASK_EXTERN ztask_cb ztask_getcb(struct ztask_context * context);
//发送一个事件到ui服务,不通过调度器,请确保在UI线程被调用
ZTASK_EXTERN void ztask_senduitask(uint32_t handle, int type, const void * msg, size_t sz);
//获得线程当前的handle
ZTASK_EXTERN uint32_t ztask_current_handle(void);

ZTASK_EXTERN uint64_t ztask_now(void);

//调试接口

//获取服务列表,返回数量
ZTASK_EXTERN uint32_t ztask_debug_getservers(struct ztask_context ***ctxs);
ZTASK_EXTERN void ztask_debug_freeservers(struct ztask_context **ctxs);
//获取服务信息
ZTASK_EXTERN void ztask_debug_info(struct ztask_context *ctx, uint32_t *handle, char **alias, size_t *mem, double *cpu, int *mqlen, size_t *message, uint32_t *task);
//获取服务内存大小
ZTASK_EXTERN size_t ztask_handle_memory(uint32_t handle);
//
ZTASK_EXTERN uint64_t ztask_thread_time(void);	// for profile, in micro second
#if defined(WIN32) || defined(WIN64)
ZTASK_EXTERN void usleep(uint32_t us);
ZTASK_EXTERN char *strsep(char **s, const char *ct);
#endif

/*io相关*/
#define ZTASK_SOCKET_TYPE_DATA 1
#define ZTASK_SOCKET_TYPE_CONNECT 2
#define ZTASK_SOCKET_TYPE_START 3
#define ZTASK_SOCKET_TYPE_CLOSE 4
#define ZTASK_SOCKET_TYPE_ACCEPT 5
#define ZTASK_SOCKET_TYPE_ERROR 6
#define ZTASK_SOCKET_TYPE_UDP 7
#define ZTASK_SOCKET_TYPE_WARNING 8
#define ZTASK_SOCKET_TYPE_BIND 9
#define ZTASK_SOCKET_TYPE_GETADDRINFO 10

struct ztask_socket_message {
    int type;
    int id;
    int ud;
    char * buffer;
    char addr[32];
};
struct ztask_curl_message {
    size_t data_len;//数据长度
    size_t cookies_len;//cookies长度
};
ZTASK_EXTERN void ztask_getaddrinfo(struct ztask_context *ctx, int session, char* host);
ZTASK_EXTERN void ztask_curl(uint32_t source, int session, void* esay);

//发送数据
ZTASK_EXTERN int ztask_socket_send(struct ztask_context *ctx, int id, void *buffer, int sz);
//监听端口
ZTASK_EXTERN int ztask_socket_listen(struct ztask_context *ctx, int session, const char *host, int port, int backlog);
//连接服务器
ZTASK_EXTERN int ztask_socket_connect(struct ztask_context *ctx, int session, const char *host, int port);
ZTASK_EXTERN int ztask_socket_bind(struct ztask_context *ctx, int fd);
ZTASK_EXTERN void ztask_socket_close(struct ztask_context *ctx, int id);
ZTASK_EXTERN void ztask_socket_shutdown(struct ztask_context *ctx, int id);
ZTASK_EXTERN void ztask_socket_start(struct ztask_context *ctx, int session, int id);
ZTASK_EXTERN void ztask_socket_accept(struct ztask_context *ctx, int session, int id);
ZTASK_EXTERN void ztask_socket_nodelay(struct ztask_context *ctx, int id);

ZTASK_EXTERN int ztask_socket_udp(struct ztask_context *ctx, const char * addr, int port);
ZTASK_EXTERN int ztask_socket_udp_connect(struct ztask_context *ctx, int id, const char * addr, int port);
ZTASK_EXTERN int ztask_socket_udp_send(struct ztask_context *ctx, int id, const char * address, short port, const void *buffer, int sz);

/*模块相关*/
/*lua模块*/
//添加lua库
ZTASK_EXTERN void ztask_snlua_addlib(char *name, int(*lua_CFunction) (void *L));

/*C模块*/
//c模块启动参数
typedef int(*ztask_snc_init_cb)(struct ztask_context * context, void *ud, const void * msg, size_t sz);
typedef int(*ztask_snc_exit_cb)(struct ztask_context * context, void *ud);
struct ztask_snc {
    ztask_cb _cb;               //回调
    ztask_snc_init_cb _init_cb; //初始化回调
    ztask_snc_exit_cb _exit_cb; //释放回调
    uint32_t is_ui;             //是否ui模块
    char *name;                 //模块名字
    void *ud;                   
    void * msg;
    size_t sz;
};
ZTASK_EXTERN void *ztask_snc_userdata(struct ztask_context * ctx, void *ud);
ZTASK_EXTERN ztask_cb ztask_snc_callback(struct ztask_context * ctx, ztask_cb cb);
//c模块封装函数
ZTASK_EXTERN int ztask_snc_getaddrinfo(struct ztask_context *ctx, const char *host, uint32_t *addr);
ZTASK_EXTERN int ztask_snc_curl(void *esay, void **data, size_t *size, void **cookies, size_t *c_size);

ZTASK_EXTERN int ztask_snc_socket_listen(struct ztask_context *ctx, const char *host, int port, int backlog);
ZTASK_EXTERN void ztask_snc_socket_start(struct ztask_context *ctx, int id);
ZTASK_EXTERN void ztask_snc_socket_close(struct ztask_context *ctx, int fd);
ZTASK_EXTERN int ztask_snc_socket_connect(struct ztask_context *ctx, const char *host, int port);
ZTASK_EXTERN void ztask_snc_sleep(int time);//延迟,单位ms,精度0.01s
typedef void(*ztask_snc_timerout_cb)(struct ztask_context * context, void *ud, int id, const void * msg, size_t sz);
ZTASK_EXTERN int ztask_snc_timerout(int time, ztask_snc_timerout_cb cb, int id, const void * msg, size_t sz);//
ZTASK_EXTERN void ztask_snc_yield(int *type, void ** msg, size_t *sz);//挂起会话,等待唤醒,可以接受返回值
ZTASK_EXTERN void ztask_snc_yield_s(int *type, void ** msg, size_t *sz, int time);
ZTASK_EXTERN void ztask_snc_resume(int session, int type, void * msg, size_t sz);//唤醒一个挂起的会话
ZTASK_EXTERN int ztask_snc_session();//得到当前所在的会话
ZTASK_EXTERN int ztask_snc_ret(void * msg, size_t sz);//返回给调用方,内部有内存拷贝
ZTASK_EXTERN int ztask_snc_retsession();

/*协程相关*/
typedef struct coroutine_s coroutine_t;
typedef void(*fcontext_cb)(struct coroutine_s*);
//创建一个协程
ZTASK_EXTERN coroutine_t *coroutine_new(fcontext_cb cb);
ZTASK_EXTERN void coroutine_delete(coroutine_t *c);
//返回当前协程是否执行完毕
ZTASK_EXTERN int coroutine_resume(coroutine_t *c);
ZTASK_EXTERN void coroutine_yield();
ZTASK_EXTERN coroutine_t *coroutine_running();
ZTASK_EXTERN void coroutine_msg(int *type, void ** msg, size_t *sz);
//
ZTASK_EXTERN int ztask_snc_call(struct ztask_context * context, int handle, int type, void * data, size_t sz, int *o_type, void ** o_msg, size_t *o_sz);
ZTASK_EXTERN int ztask_snc_callname(char* name, int type, void * data, size_t sz, void ** o_msg, size_t *o_sz);
ZTASK_EXTERN int ztask_snc_callname_s(char* name, int type, void * data, size_t sz, void ** o_msg, size_t *o_sz, int time);
/*内存相关*/
#include <stddef.h>
#include <malloc.h>
#define USE_MALLOC_DEBUG 0
#ifndef USE_JEMALLOC
#define ztask_malloc malloc
#define ztask_calloc calloc
#define ztask_realloc realloc
#define ztask_free free
ZTASK_EXTERN char * ztask_strdup(const char *str);
ZTASK_EXTERN char * ztask_strndup(const char *str, size_t len);
#else
#if USE_MALLOC_DEBUG
typedef struct _MEM
{
    int del;//是否被管理
    const char *_Func;
    const char *_File;
    unsigned int _Line;
    size_t _Size;
    uint32_t timer;
    void *ptr;
}MEM;

ZTASK_EXTERN void * ztask_malloc_debug(size_t sz, const char *_Func, const char *_File, unsigned int _Line);
ZTASK_EXTERN void * ztask_calloc_debug(size_t nmemb, size_t size, const char *_Func, const char *_File, unsigned int _Line);
ZTASK_EXTERN void * ztask_realloc_debug(void *ptr, size_t size, const char *_Func, const char *_File, unsigned int _Line);
ZTASK_EXTERN void ztask_free_debug(void *ptr);
ZTASK_EXTERN char * ztask_strdup_debug(const char *str, const char *_Func, const char *_File, unsigned int _Line);
ZTASK_EXTERN char * ztask_strndup_debug(const char *str, size_t len, const char *_Func, const char *_File, unsigned int _Line);

ZTASK_EXTERN void ztask_memory_info(MEM ***mem, size_t *num, size_t *msize);
ZTASK_EXTERN void ztask_memory_lock();
ZTASK_EXTERN void ztask_memory_unlock();
ZTASK_EXTERN void ztask_memory_clean();

#define ztask_malloc(size)   ztask_malloc_debug(size, __FUNCTION__, __FILE__, __LINE__)
#define ztask_free(ptr)    ztask_free_debug(ptr);
#define ztask_calloc(ptr,size)  ztask_calloc_debug(ptr, size, __FUNCTION__, __FILE__, __LINE__)
#define ztask_realloc(ptr, size)   ztask_realloc_debug(ptr, size, __FUNCTION__, __FILE__, __LINE__)
#define ztask_strdup(ptr) ztask_strdup_debug(ptr, __FUNCTION__, __FILE__, __LINE__)
#define ztask_strndup(ptr, len) ztask_strndup_debug(ptr, len, __FUNCTION__, __FILE__, __LINE__)
#else
ZTASK_EXTERN void * ztask_malloc(size_t sz);
ZTASK_EXTERN void * ztask_calloc(size_t nmemb, size_t size);
ZTASK_EXTERN void * ztask_realloc(void *ptr, size_t size);
ZTASK_EXTERN void ztask_free(void *ptr);
ZTASK_EXTERN char * ztask_strdup(const char *str);
ZTASK_EXTERN char * ztask_strndup(const char *str, size_t len);
#endif
#endif
ZTASK_EXTERN void * ztask_lalloc(void *ptr, size_t osize, size_t nsize);	// use for lua
ZTASK_EXTERN void ztask_debug_memory(const char *info);	// for debug use, output current service memory to stderr

#endif
