#include"ztask.h"

#include<lua.h>
#include<lualib.h>
#include<lauxlib.h>

#include<assert.h>
#include<string.h>
#include<stdlib.h>
#include<stdio.h>

#define MEMORY_WARNING_REPORT 1024*1024*32
int snlua_init(struct snlua *l, struct ztask_context *ctx, const char * args, const size_t sz);
struct snlua *snlua_create(void);
void snlua_release(struct snlua *l);
void snlua_signal(struct snlua *l, int signal);

