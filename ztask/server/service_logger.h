#include <ztask.h>
#include <stdio.h>

struct logger *logger_create(void);
void logger_release(struct logger *inst);
int logger_init(struct logger * inst, struct ztask_context *ctx, const char * parm, const size_t sz);
