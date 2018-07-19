#include "ztask.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int snjs_init(struct snjs *l, struct ztask_context *ctx, const char * args, const size_t sz);
struct snjs *snjs_create(void);
void snjs_release(struct snjs *l);
void snjs_signal(struct snjs *l, int signal);



