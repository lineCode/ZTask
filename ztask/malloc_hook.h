#ifndef ZTASK_MALLOC_HOOK_H
#define ZTASK_MALLOC_HOOK_H

#include <stdlib.h>
#include <lua.h>
#include <stdint.h>
#include "thread.h"


extern size_t malloc_used_memory(void);
extern size_t malloc_memory_block(void);
extern void   memory_info_dump(void);
extern size_t mallctl_int64(const char* name, size_t* newval);
extern int    mallctl_opt(const char* name, int* newval);
extern void   dump_c_mem(void);
extern int    dump_mem_lua(lua_State *L);
extern size_t malloc_current_memory(void);
void ztask_memory_init();
#endif /* ZTASK_MALLOC_HOOK_H */

