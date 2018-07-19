#include "ztask.h"
#include "ztask_func.h"
#include "ztask_env.h"
#include "ztask_server.h"
#include "lualib/luashrtbl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <signal.h>
#include <assert.h>


static ztask_func func = { 0 };


ZTASK_EXTERN void ztask_init() {
    ztask_memory_init();
    luaS_initshr();
    ztask_globalinit();
    ztask_env_init();
    //初始化函数指针
    func.size = sizeof(ztask_func);

    func.malloc = ztask_malloc;
    func.realloc = ztask_realloc;
    func.calloc = ztask_calloc;
    func.free = ztask_free;
    func.strdup = ztask_strdup;
    func.strndup = ztask_strndup;

    func.command = ztask_command;
    func.context_new = ztask_context_new;
    func.context_handle = ztask_context_handle;
    func.handle_grab = ztask_context_grab;
    func.queryname = ztask_queryname;
    func.send = ztask_send;
    func.sendname = ztask_sendname;
    func.alias = ztask_alias;
    func.isremote = ztask_isremote;
    func.callback = ztask_callback;
    func.getud = ztask_getud;
    func.getcb = ztask_getcb;
    func.current_handle = ztask_current_handle;




}
ZTASK_EXTERN void ztask_uninit() {
    ztask_globalexit();
    luaS_exitshr();
}

//获取函数指针
ZTASK_EXTERN void ztask_getfunc(ztask_func *_func) {

    memcpy(_func + sizeof(_func->size), &func + sizeof(_func->size), _func->size - sizeof(_func->size));

}

