#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "ztask.h"
#include "coroutine.h"
#include "ztask_ui.h"
struct snc {
    struct ui _ui;
    struct ztask_context * ctx;
    ztask_cb _cb;
    ztask_snc_exit_cb _exit_cb;
    void *ud;
    struct coroutine_tree_s coroutine;
};
int snc_init(struct snc *l, struct ztask_context *ctx, const char * args, const size_t sz);
struct snc *snc_create(void);
void snc_release(struct snc *l);
void snc_signal(struct snc *l, int signal);



