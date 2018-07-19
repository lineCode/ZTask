#include "ztask.h"
#include "ztask_env.h"
#include "spinlock.h"

#include <lua.h>
#include <lauxlib.h>

#include <stdlib.h>
#include <assert.h>

struct ztask_env {
    spinlock lock;
    lua_State *L;
};

static struct ztask_env *E = NULL;

const char *
ztask_getenv(const char *key) {
    SPIN_LOCK(E);

    lua_State *L = E->L;

    lua_getglobal(L, key);
    const char * result = lua_tostring(L, -1);
    lua_pop(L, 1);

    SPIN_UNLOCK(E);

    return result;
}

void
ztask_setenv(const char *key, const char *value) {
    SPIN_LOCK(E);

    lua_State *L = E->L;
    lua_getglobal(L, key);
    //assert(lua_isnil(L, -1));
    lua_pop(L, 1);
    lua_pushstring(L, value);
    lua_setglobal(L, key);

    SPIN_UNLOCK(E);
}

void
ztask_env_init() {
    E = ztask_malloc(sizeof(*E));
    SPIN_INIT(E);
    E->L = luaL_newstate();
}
