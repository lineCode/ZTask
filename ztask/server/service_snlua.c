#include <ztask.h>
#include <queue.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <zlib.h>
#include <unzip.h>
#include <ioapi_mem.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "lua_zip.h"

//内存警戒线
#define MEMORY_WARNING_REPORT (1024 * 1024 * 32)
//lua服务结构
struct snlua {
    lua_State * L;              //虚拟机句柄
    struct ztask_context * ctx; //上下文句柄
    size_t mem;
    size_t mem_report;
    size_t mem_limit;
};
//lua模块
struct snlua_module {
    QUEUE wq;
    char *name;
    lua_CFunction func;
};
//lua是否初始化
static int lua_inited = 0;
//lua库资源压缩包
static unzFile _lua_zip = NULL;
static zlib_filefunc_def filefunc32 = { 0 };
static ourmemory_t unzmem = { 0 };
//c库链表
static QUEUE _clua;

// LUA_CACHELIB may defined in patched lua for shared proto
#ifdef LUA_CACHELIB
#define codecache luaopen_cache
#else
static int
cleardummy(lua_State *L) {
    return 0;
}
static int codecache(lua_State *L) {
    luaL_Reg l[] = {
        { "clear", cleardummy },
        { "mode", cleardummy },
        { NULL, NULL },
    };
    luaL_newlib(L, l);
    lua_getglobal(L, "loadfile");
    lua_setfield(L, -2, "loadfile");
    return 1;
}
#endif

//打印lua堆栈
static int traceback(lua_State *L) {
    const char *msg = lua_tostring(L, 1);
    if (msg)
        luaL_traceback(L, L, msg, 1);
    else {
        lua_pushliteral(L, "(no error message)");
    }
    return 1;
}

static void report_launcher_error(struct ztask_context *ctx) {
    // sizeof "ERROR" == 5
    ztask_sendname(ctx, 0, ".launcher", PTYPE_TEXT, 0, "ERROR", 5);
}

static const char * optstring(struct ztask_context *ctx, const char *key, const char * str) {
    const char * ret = ztask_command(ctx, "GETENV", key);
    if (ret == NULL) {
        return str;
    }
    return ret;
}
//检测文件是否存在
static int readable(const char *filename) {
    char *file = filename;
    char *pos = file;
    while (pos[0] != '\0')
    {
        if (pos[0] == '/')
            pos[0] = '\\';
        pos++;
    }
    if (file[0] == '.')
        file++;
    if (file[0] == '\\')
        file++;
    if (unzLocateFile(_lua_zip, file, 0) == 0)
        return 1;
    return 0;
}
//
static const char *pushnexttemplate(lua_State *L, const char *path) {
    const char *l;
    while (*path == *LUA_PATH_SEP) path++;  /* skip separators */
    if (*path == '\0') return NULL;  /* no more templates */
    l = strchr(path, *LUA_PATH_SEP);  /* find next separator */
    if (l == NULL) l = path + strlen(path);
    lua_pushlstring(L, path, l - path);  /* template */
    return l;
}
//搜索路径
static const char *searchpath(lua_State *L, const char *name, const char *path, const char *sep, const char *dirsep) {
    luaL_Buffer msg;  /* to build error message */
    luaL_buffinit(L, &msg);
    if (*sep != '\0')  /* non-empty separator? */
        name = luaL_gsub(L, name, sep, dirsep);  /* replace it by 'dirsep' */
    while ((path = pushnexttemplate(L, path)) != NULL) {
        //得到路径
        const char *filename = luaL_gsub(L, lua_tostring(L, -1), LUA_PATH_MARK, name);
        lua_remove(L, -2);  /* remove path template */
        if (readable(filename))  /* does file exist and is readable? */
            return filename;  /* return that file name */
        lua_pushfstring(L, "\n\tno file '%s'", filename);
        lua_remove(L, -2);  /* remove file name */
        luaL_addvalue(&msg);  /* concatenate error msg. entry */
    }
    luaL_pushresult(&msg);  /* create error message */
    return NULL;  /* not found */
}
//查找文件
static const char *findfile(lua_State *L, const char *name, const char *pname, const char *dirsep) {
    const char *path;
    lua_getglobal(L, "package");
    lua_getfield(L, -1, pname);
    path = lua_tostring(L, -1);
    if (path == NULL)
        luaL_error(L, "'package.%s' must be a string", pname);
    return searchpath(L, name, path, ".", dirsep);
}
//检测是否加载成功
static int checkload(lua_State *L, int stat, const char *filename) {
    if (stat) {  /* module loaded successfully? */
        lua_pushstring(L, filename);  /* will be 2nd argument to module */
        return 2;  /* return open function and file name */
    }
    else
        return luaL_error(L, "error loading module '%s' from file '%s':\n\t%s",
            lua_tostring(L, 1), filename, lua_tostring(L, -1));
}
//新的lua模块搜索器
static int searcher_Lua(lua_State* L)
{
    const char *filename;
    const char *name = luaL_checkstring(L, 1);
    //先查找内置C模块
    QUEUE *q = NULL;
    QUEUE_FOREACH(q, &_clua) {
        //从队列中取出一个
        struct snlua_module* w = QUEUE_DATA(q, struct snlua_module, wq);
        if (strcmp(name, w->name) == 0) {
            lua_pushcfunction(L, w->func);
            return 1;
        }
    }
    filename = findfile(L, name, "path", "\\");
    if (filename == NULL) return 1;  /* module not found in this path */
    return checkload(L, (luaL_loadfile(L, filename) == LUA_OK), filename);
}
//设置lua搜索器
static void SetLoader(lua_State *L)
{
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "searchers");   // push "package.loaders"
                                        // insert loader into index 2
    lua_pushcfunction(L, searcher_Lua);           /* L: package, loaders, func */
    for (int i = lua_rawlen(L, -2) + 1; i > 2; --i)
    {
        lua_rawgeti(L, -2, i - 1);        /* L: package, loaders, func, function */
                                                // we call lua_rawgeti, so the loader table now is at -3
        lua_rawseti(L, -3, i);            /* L: package, loaders, func */
    }
    lua_rawseti(L, -2, 2);                /* L: package, loaders */
                                                // set loaders into package
    lua_setfield(L, -2, "searchers");       /* L: package */
    lua_pop(L, 1);
}

//初始化回调
static int init_cb(struct snlua *l, struct ztask_context *ctx, const char * args, size_t sz) {
    lua_State *L = l->L;
    l->ctx = ctx;
    // 使用自动 gc 会有一个问题。它很可能使系统的峰值内存占用远超过实际需求量。原因就在于，收集行为往往发生在调用栈很深的地方。
    // 当你的应用程序呈现出某种周期性（大多数包驱动的服务都是这样）。在一个服务周期内，往往会引用众多临时对象，这个时候做 mark 工作，会导致许多临时对象也被 mark 住。
    // 一个经验方法是，调用LUA_GCSTOP停止自动 GC。在周期间定期调用 gcstep 且使用较大的 data 值，在有限个周期做完一整趟 gc 。
    lua_gc(L, LUA_GCSTOP, 0);//停止垃圾收集器

    lua_pushboolean(L, 1);  /* signal for libraries to ignore env. vars. */
    lua_setfield(L, LUA_REGISTRYINDEX, "LUA_NOENV");
    //加载全部标准库
    luaL_openlibs(L);
    //重定义lua加载器
    SetLoader(L);

    //设置ztask上下文
    lua_pushlightuserdata(L, ctx);
    lua_setfield(L, LUA_REGISTRYINDEX, "ztask_context");

    luaL_requiref(L, "ztask.codecache", codecache, 0);
    lua_pop(L, 1);

    const char *path = optstring(ctx, "lua_path", ".\\?.lua;.\\lualib\\?.lua");
    lua_pushstring(L, path);
    lua_setglobal(L, "LUA_PATH");
    const char *cpath = optstring(ctx, "lua_cpath", ".\\luaclib/?.so");
    lua_pushstring(L, cpath);
    lua_setglobal(L, "LUA_CPATH");
    const char *service = optstring(ctx, "luaservice", ".\\?.lua;.\\service\\?.lua;");
    lua_pushstring(L, service);
    lua_setglobal(L, "LUA_SERVICE");
    const char *preload = ztask_command(ctx, "GETENV", "preload");
    lua_pushstring(L, preload);
    lua_setglobal(L, "LUA_PRELOAD");

    lua_pushcfunction(L, traceback);
    assert(lua_gettop(L) == 1);

    const char * loader = optstring(ctx, "lualoader", ".\\lualib\\loader.lua");

    int r = luaL_loadfile(L, loader);
    if (r != LUA_OK) {
        ztask_error(ctx, "Can't load %s : %s", loader, lua_tostring(L, -1));
        report_launcher_error(ctx);
        return 1;
    }
    lua_pushlstring(L, args, sz);
    r = lua_pcall(L, 1, 0, 1);
    if (r != LUA_OK) {
        ztask_error(ctx, "lua loader error : %s", lua_tostring(L, -1));
        report_launcher_error(ctx);
        return 1;
    }
    lua_settop(L, 0);//清除栈上所有元素

    if (lua_getfield(L, LUA_REGISTRYINDEX, "memlimit") == LUA_TNUMBER) {
        size_t limit = lua_tointeger(L, -1);
        l->mem_limit = limit;
        ztask_error(ctx, "Set memory limit to %.2f M", (float)limit / (1024 * 1024));
        lua_pushnil(L);
        lua_setfield(L, LUA_REGISTRYINDEX, "memlimit");
    }
    lua_pop(L, 1);

    lua_gc(L, LUA_GCRESTART, 0);//重启垃圾收集器
    return 0;
}
//加载回调
static int launch_cb(struct ztask_context * context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
    assert(type == 0 && session == 0);
    struct snlua *l = ud;
    //将回调地址设置为空
    ztask_callback(context, NULL, NULL);
    int err = init_cb(l, context, msg, sz);
    if (err) {
        ztask_command(context, "EXIT", NULL);
    }
    return 0;
}
//服务初始化
int snlua_init(struct snlua *l, struct ztask_context *ctx, const char * args, const size_t sz) {
    char * tmp = ztask_malloc(sz);
    memcpy(tmp, args, sz);
    ztask_callback(ctx, l, launch_cb);
    const char * self = ztask_command(ctx, "REG", NULL);
    uint32_t handle_id = strtoul(self + 1, NULL, 16);
    //给自己发送一个消息,方便进入调度器
    ztask_send(ctx, 0, handle_id, PTYPE_TAG_DONTCOPY, 0, tmp, sz);
    return 0;
}
//lua内存分配器
static void *lalloc(void * ud, void *ptr, size_t osize, size_t nsize) {
    struct snlua *l = ud;
    size_t mem = l->mem;
    l->mem += nsize;
    if (ptr)
        l->mem -= osize;
    if (l->mem_limit != 0 && l->mem > l->mem_limit) {
        if (ptr == NULL || nsize > osize) {
            l->mem = mem;
            return NULL;
        }
    }
    if (l->mem > l->mem_report) {
        l->mem_report *= 2;
        ztask_error(l->ctx, "Memory warning %.2f M", (float)l->mem / (1024 * 1024));
    }
    return ztask_lalloc(ptr, osize, nsize);
}
//lua文件加载
static int snlua_load_cb(const char *filename, char **buf) {
    unz_file_info64 info;
    char *file = filename;
    char *pos = file;
    while (pos[0] != '\0')
    {
        if (pos[0] == '/')
            pos[0] = '\\';
        pos++;
    }
    if (file[0] == '.')
        file++;
    if (file[0] == '\\')
        file++;
    if (unzLocateFile(_lua_zip, file, 0) == 0) {
        if (unzGetCurrentFileInfo64(_lua_zip, &info, NULL, 0, 0, 0, 0, 0) == 0) {
            char pass[11];
            pass[0] = 143;
            pass[1] = 149;
            pass[2] = 131;
            pass[3] = 144;
            pass[4] = 79;
            pass[5] = 79;
            pass[6] = 80;
            pass[7] = 80;
            pass[8] = 81;
            pass[9] = 81;
            pass[10] = 0;
            for (size_t i = 0; i < 10; i++)
            {
                pass[i] -= 30;
            }
            if (unzOpenCurrentFilePassword(_lua_zip, pass) == 0) {
                int buflen = (int)info.uncompressed_size;
                *buf = ztask_malloc(buflen + 100);
                if (unzReadCurrentFile(_lua_zip, *buf, buflen) != buflen) {
                    ztask_free(*buf);
                    *buf = NULL;
                    return 0;
                }
                else {
                    return buflen;
                }
            }
        }
    }
    return 0;
}
//lua文件释放
static void snlua_close_cb(char *buf) {
    if (buf)
        ztask_free(buf);
}

LUAMOD_API int luaopen_ztask_core(lua_State *L);
LUAMOD_API int luaopen_profile(lua_State *L);
LUAMOD_API int luaopen_ztask_memory(lua_State *L);
LUAMOD_API int luaopen_lpeg(lua_State *L);
LUAMOD_API int luaopen_sproto_core(lua_State *L);
LUAMOD_API int luaopen_netpack(lua_State *L);
LUAMOD_API int luaopen_crypt(lua_State *L);
LUAMOD_API int luaopen_sharedata_core(lua_State *L);
LUAMOD_API int luaopen_multicast_core(lua_State *L);
LUAMOD_API int luaopen_ztask_debugchannel(lua_State *L);
LUAMOD_API int luaopen_cluster_core(lua_State *L);
LUAMOD_API int luaopen_bson(lua_State *L);
LUAMOD_API int luaopen_curl_core(lua_State *L);
LUAMOD_API int luaopen_ztask_socketdriver(lua_State *L);
LUAMOD_API int luaopen_ztask_mysqlaux_c(lua_State *L);
LUAMOD_API int luaopen_iconv(lua_State *L);
LUAMOD_API int luaopen_cjson(lua_State *l);
//检查初始化状态
static void snlua_check_init() {
    if (lua_inited)
        return;
    lua_inited = 1;

    QUEUE_INIT(&_clua);

    unzmem.size = sizeof(lua_zip);
    unzmem.base = (char *)malloc(unzmem.size);
    memcpy(unzmem.base, lua_zip, unzmem.size);

    fill_memory_filefunc(&filefunc32, &unzmem);

    //加载压缩文件
    _lua_zip = unzOpen2("__notused__", &filefunc32);
    if (_lua_zip) {
        luaL_loadinit(snlua_load_cb, snlua_close_cb);
    }
    //加载内置库
    ztask_snlua_addlib("ztask.core", luaopen_ztask_core);
    ztask_snlua_addlib("profile", luaopen_profile);
    ztask_snlua_addlib("ztask.memory", luaopen_ztask_memory);
    ztask_snlua_addlib("lpeg", luaopen_lpeg);
    ztask_snlua_addlib("sproto.core", luaopen_sproto_core);
    ztask_snlua_addlib("netpack", luaopen_netpack);
    ztask_snlua_addlib("crypt", luaopen_crypt);
    ztask_snlua_addlib("sharedata.core", luaopen_sharedata_core);
    ztask_snlua_addlib("multicast.core", luaopen_multicast_core);
    ztask_snlua_addlib("ztask.debugchannel", luaopen_ztask_debugchannel);
    ztask_snlua_addlib("cluster.core", luaopen_cluster_core);
    ztask_snlua_addlib("bson", luaopen_bson);
    ztask_snlua_addlib("curl.core", luaopen_curl_core);
    ztask_snlua_addlib("ztask.socketdriver", luaopen_ztask_socketdriver);
    ztask_snlua_addlib("mysqlaux.c", luaopen_ztask_mysqlaux_c);
    ztask_snlua_addlib("iconv", luaopen_iconv);
    ztask_snlua_addlib("cjson", luaopen_cjson);
}
//创建lua服务
struct snlua *snlua_create(void) {
    snlua_check_init();
    struct snlua * l = ztask_malloc(sizeof(*l));
    memset(l, 0, sizeof(*l));
    l->mem_report = MEMORY_WARNING_REPORT;
    l->mem_limit = 0;
    l->L = lua_newstate(lalloc, l);
    return l;
}
//释放lua服务
void snlua_release(struct snlua *l) {
    lua_close(l->L);
    ztask_free(l);
}

void snlua_signal(struct snlua *l, int signal) {
    ztask_error(l->ctx, "recv a signal %d", signal);
    if (signal == 0) {
#ifdef lua_checksig
        // If our lua support signal (modified lua version by ztask), trigger it.
        ztask_sig_L = l->L;
#endif
    }
    else if (signal == 1) {
        ztask_error(l->ctx, "Current Memory %.3fK", (float)l->mem / 1024);
    }
}
//添加lua库
void ztask_snlua_addlib(char *name, int(*func) (void *L)) {
    if (!name || !func)
        return;
    snlua_check_init();
    struct snlua_module *module = (struct snlua_module *)ztask_malloc(sizeof(*module));
    if (module) {
        memset(module, 0, sizeof(*module));
        module->name = ztask_strdup(name);
        module->func = func;
        QUEUE_INSERT_TAIL(&_clua, &module->wq);
    }
}
//创建lua服务
