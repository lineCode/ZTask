#include "ztask.h"

#include "ztask_monitor.h"
#include "ztask_server.h"
#include "ztask.h"
#include "atomic.h"

#include <stdlib.h>
#include <string.h>

struct ztask_monitor {
    int version;
    int check_version;
    uint32_t source;
    uint32_t destination;
};

struct ztask_monitor *
    ztask_monitor_new() {
    struct ztask_monitor * ret = ztask_malloc(sizeof(*ret));
    memset(ret, 0, sizeof(*ret));
    return ret;
}

void
ztask_monitor_delete(struct ztask_monitor *sm) {
    ztask_free(sm);
}

void
ztask_monitor_trigger(struct ztask_monitor *sm, uint32_t source, uint32_t destination) {
    sm->source = source;
    sm->destination = destination;
    ATOM_INC(&sm->version);
}

void
ztask_monitor_check(struct ztask_monitor *sm) {
    if (sm->version == sm->check_version) {
        if (sm->destination) {
            ztask_context_endless(sm->destination);
            ztask_error(NULL, "A message from [ :%08x ] to [ :%08x ] maybe in an endless loop (version = %d)", sm->source, sm->destination, sm->version);
        }
    }
    else {
        sm->check_version = sm->version;
    }
}
