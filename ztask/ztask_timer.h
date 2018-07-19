#ifndef ZTASK_TIMER_H
#define ZTASK_TIMER_H

#include <stdint.h>

int ztask_timeout(uint32_t handle, int time, int session);
void ztask_timeout_del(uint32_t handle, int session);
void ztask_updatetime(void);
uint32_t ztask_starttime(void);


void ztask_timer_init(void);

#endif
