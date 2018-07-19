#ifndef ZTASK_HARBOR_H
#define ZTASK_HARBOR_H

#include <stdint.h>
#include <stdlib.h>

#define GLOBALNAME_LENGTH 16
#define REMOTE_MAX 256

struct remote_name {
    char name[GLOBALNAME_LENGTH];
    uint32_t handle;
};

struct remote_message {
    struct remote_name destination;
    const void * message;
    size_t sz;
    int type;
};

void ztask_harbor_send(struct remote_message *rmsg, uint32_t source, int session);
int ztask_harbor_message_isremote(uint32_t handle);
void ztask_harbor_init(int harbor);
void ztask_harbor_start(void * ctx);
void ztask_harbor_exit();

#endif
