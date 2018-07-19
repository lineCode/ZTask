#ifndef ZTASK_UI_H
#define ZTASK_UI_H
//重载这个结构
struct ui {
    ztask_cb snc_cb;
};
int ztask_ui_init();
int ztask_ui_cb(struct ztask_context * context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz);
#endif