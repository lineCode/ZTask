#include "ztask.h"
#include "ztask_harbor.h"
#include "ztask_server.h"
#include "ztask_mq.h"
#include "ztask_handle.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>

static struct ztask_context * REMOTE = 0;
static unsigned int HARBOR = ~0;

static inline int
invalid_type(int type) {
    return type != PTYPE_SYSTEM && type != PTYPE_HARBOR;
}

void
ztask_harbor_send(struct remote_message *rmsg, uint32_t source, int session) {
    assert(invalid_type(rmsg->type) && REMOTE);
    ztask_context_send(REMOTE, rmsg, sizeof(*rmsg), source, PTYPE_SYSTEM, session);
}

int
ztask_harbor_message_isremote(uint32_t handle) {
    assert(HARBOR != ~0);
    int h = (handle & ~HANDLE_MASK);
    return h != HARBOR && h != 0;
}

void
ztask_harbor_init(int harbor) {
    HARBOR = (unsigned int)harbor << HANDLE_REMOTE_SHIFT;
}

void
ztask_harbor_start(void *ctx) {
    // the HARBOR must be reserved to ensure the pointer is valid.
    // It will be released at last by calling ztask_harbor_exit
    ztask_context_reserve(ctx);
    REMOTE = ctx;
}

void
ztask_harbor_exit() {
    struct ztask_context * ctx = REMOTE;
    REMOTE = NULL;
    if (ctx) {
        ztask_context_release(ctx);
    }
}
