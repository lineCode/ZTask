#ifndef ZTASK_SERVER_H
#define ZTASK_SERVER_H

#include <stdint.h>
#include <stdlib.h>
struct ztask_context;
struct ztask_message;
struct ztask_monitor;

void ztask_context_grab(struct ztask_context *ctx);
void ztask_context_reserve(struct ztask_context *ctx);
struct ztask_context * ztask_context_release(struct ztask_context *);
int ztask_context_push(uint32_t handle, struct ztask_message *message);
void ztask_context_send(struct ztask_context * context, void * msg, size_t sz, uint32_t source, int type, int session);
int ztask_context_newsession(struct ztask_context *ctx);
struct message_queue * ztask_context_message_dispatch(struct ztask_monitor *, struct message_queue *, int weight);	// return next queue
int ztask_context_total();
void ztask_context_dispatchall(struct ztask_context * context);	// for ztask_error output before exit

void ztask_context_task_inc(struct ztask_context *ctx);
void ztask_context_task_dec(struct ztask_context *ctx);

void ztask_context_endless(uint32_t handle);	// for monitor

void ztask_globalinit(void);
void ztask_globalexit(void);
void ztask_initthread(int m);

void ztask_profile_enable(int enable);

#endif
