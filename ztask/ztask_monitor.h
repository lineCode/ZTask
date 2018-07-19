#ifndef ZTASK_MONITOR_H
#define ZTASK_MONITOR_H

#include <stdint.h>

struct ztask_monitor;

struct ztask_monitor * ztask_monitor_new();
void ztask_monitor_delete(struct ztask_monitor *);
void ztask_monitor_trigger(struct ztask_monitor *, uint32_t source, uint32_t destination);
void ztask_monitor_check(struct ztask_monitor *);

#endif
