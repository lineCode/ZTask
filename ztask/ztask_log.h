#ifndef ztask_log_h
#define ztask_log_h

#include "ztask_env.h"
#include "ztask.h"

#include <stdio.h>
#include <stdint.h>

FILE * ztask_log_open(struct ztask_context * ctx, uint32_t handle);
void ztask_log_close(struct ztask_context * ctx, FILE *f, uint32_t handle);
void ztask_log_output(FILE *f, uint32_t source, int type, int session, void * buffer, size_t sz);

#endif