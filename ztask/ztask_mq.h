#ifndef ZTASK_MESSAGE_QUEUE_H
#define ZTASK_MESSAGE_QUEUE_H

#include <stdlib.h>
#include <stdint.h>

struct ztask_message {
    uint32_t source;
    int session;
    void * data;
    size_t sz;
};

// type is encoding in ztask_message.sz high 8bit
#define MESSAGE_TYPE_MASK (SIZE_MAX >> 8)
#define MESSAGE_TYPE_SHIFT ((sizeof(size_t)-1) * 8)

struct message_queue;

void ztask_globalmq_push(struct message_queue * queue);
struct message_queue * ztask_globalmq_pop(void);

struct message_queue * ztask_mq_create(uint32_t handle);
void ztask_mq_mark_release(struct message_queue *q);

typedef void(*message_drop)(struct ztask_message *, void *);

void ztask_mq_release(struct message_queue *q, message_drop drop_func, void *ud);
uint32_t ztask_mq_handle(struct message_queue *);

// 0 for success
int ztask_mq_pop(struct message_queue *q, struct ztask_message *message);
void ztask_mq_push(struct message_queue *q, struct ztask_message *message);

// return the length of message queue, for debug
int ztask_mq_length(struct message_queue *q);
int ztask_mq_overload(struct message_queue *q);

void ztask_mq_init();

#endif
