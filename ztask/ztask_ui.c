#include "ztask.h"
#include "ztask_ui.h"
#include "ztask_server.h"
#include "thread.h"
#include "queue.h"

#include "server/service_snc.h"

//一条转发的消息
struct ztask_ui_msg {
    QUEUE wq;
    struct ztask_context * ctx;
    struct ui *ui;
    int type;
    int session;
    uint32_t source;
    size_t sz;
};
#if defined(_WIN32) || defined(_WIN64)
static DWORD idThread = 0;
#endif
static QUEUE ui_queue = { 0 };          //ui队列
static ztask_mutex ui_mutex = { 0 };    //ui互斥

//处理队列
static inline void dispatch_message() {
    while (1)
    {
        QUEUE* q;
        // 同步
        ztask_mutex_lock(&ui_mutex);

        if (QUEUE_EMPTY(&ui_queue))
        {
            //空队列
            ztask_mutex_unlock(&ui_mutex);
            break;
        }
        // 取出队列的头部节点（第一个task）
        q = QUEUE_HEAD(&ui_queue);

        // 从队列中移除这个task
        QUEUE_REMOVE(q);
        ztask_mutex_unlock(&ui_mutex);

        // 取出task_client首地址
        struct ztask_ui_msg *w = QUEUE_DATA(q, struct ztask_ui_msg, wq);

        if (w->ui)
        {
            w->ui->snc_cb(w->ctx, w->ui, w->type, w->session, w->source, w->sz ? (w + 1) : NULL, w->sz);
        }

        ztask_free(w);
    }
}
#if defined(_WIN32) || defined(_WIN64)
//定时处理一下
static VOID CALLBACK __TimerProc(HWND hWnd, UINT a, UINT_PTR b, DWORD c) {
    dispatch_message();
}
#endif
//主循环
void ztask_ui_loop(struct ztask_ui_parm *parm) {
#if defined(_WIN32) || defined(_WIN64)
    MSG Msg;
    while (GetMessageA(&Msg, NULL, 0, 0)) {
        if (!Msg.hwnd && WM_APP + 1000 == Msg.message && Msg.wParam == 0x01020304 && Msg.lParam == 0x04030201) {
            dispatch_message();
        }
        else if (WM_QUIT == Msg.message) {
            break;
        }
        if (parm) {
            //加速键
            if (!TranslateAccelerator(*(HWND *)parm->param2, parm->param1, &Msg))
            {
                TranslateMessage(&Msg);
                DispatchMessageA(&Msg);
            }
        }
        else {
            TranslateMessage(&Msg);
            DispatchMessageA(&Msg);
        }
    }
#endif
}

//转发消息到ui线程
int ztask_ui_cb(struct ztask_context * context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
    struct ui * inst = ud;
    struct ztask_ui_msg *d = (struct ztask_ui_msg *)ztask_malloc(sizeof(struct ztask_ui_msg) + sz);
    d->ctx = context;
    d->ui = inst;
    d->type = type;
    d->session = session;
    d->source = source;
    d->sz = sz;
    memcpy(d + 1, msg, sz);

    //插入到队列里面
    ztask_mutex_lock(&ui_mutex);
    QUEUE_INSERT_TAIL(&ui_queue, &d->wq);
    ztask_mutex_unlock(&ui_mutex);
#if defined(_WIN32) || defined(_WIN64)
    //通知ui线程处理
    PostThreadMessage(idThread, WM_APP + 1000, 0x01020304, 0x04030201);
#endif
    return 0;
}

//发送一个事件到ui服务,不通过调度器,请确保在UI线程被调用
void ztask_senduitask(uint32_t handle, int type, const void * msg, size_t sz) {
    struct ztask_context * ctx = ztask_handle_grab(handle);
    if (!ctx)
        return;
    struct ui *ui = (struct ui*)ztask_getud(ctx);
    if (ui)
        ui->snc_cb(ctx, ui, type, ztask_context_newsession(ctx), 0, msg, sz);
}

int ztask_ui_init() {
    QUEUE_INIT(&ui_queue);
    ztask_mutex_init(&ui_mutex);
#if defined(_WIN32) || defined(_WIN64)
    //创建一个定时器,防止漏掉消息
    SetTimer(0, 0, 5, __TimerProc);
    //获取UI线程ID
    idThread = GetCurrentThreadId();
#else

#endif
    return 0;
}