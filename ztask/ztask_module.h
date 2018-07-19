#ifndef ZTASK_MODULE_H
#define ZTASK_MODULE_H

struct ztask_context;

struct ztask_module * ztask_module_query(const char * name);
void * ztask_module_instance_create(struct ztask_module *);
int ztask_module_instance_init(struct ztask_module *, void * inst, struct ztask_context *ctx, const char * parm, const size_t sz);
void ztask_module_instance_release(struct ztask_module *, void *inst);
void ztask_module_instance_signal(struct ztask_module *, void *inst, int signal);

void ztask_module_init(const char *path);

#endif
