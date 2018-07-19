#ifndef ZTASK_CONTEXT_HANDLE_H
#define ZTASK_CONTEXT_HANDLE_H

#include <stdint.h>

// reserve high 8 bits for remote id
#define HANDLE_MASK 0xffffff
#define HANDLE_REMOTE_SHIFT 24

struct ztask_context;

uint32_t ztask_handle_register(struct ztask_context *);
int ztask_handle_retire(uint32_t handle);

void ztask_handle_retireall();

uint32_t ztask_handle_findname(const char * name);
const char * ztask_handle_namehandle(uint32_t handle, const char *name);

void ztask_handle_init(int harbor);

#endif
