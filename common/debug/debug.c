#include <ztask.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <io.h>
#include <fcntl.h>  
#include <stdio.h>
#include <time.h>
#include <commctrl.h>
#include <stdlib.h>


static HINSTANCE hInstance;
static HWND hWnd = NULL;      //主窗口句柄
static HWND hTab;      //选择夹句柄
static HWND Page[4];   //
static HWND MemList;
static HWND MemStatic;
static HWND SevList = NULL;
static UINT_PTR sre_id = 0;
static int oldhash = 0;

#if USE_MALLOC_DEBUG
//刷新数据时钟 1s
static VOID CALLBACK UPDATETIME(HWND H, UINT U, UINT_PTR Pt, DWORD D) {

    LVITEM lvItem = { 0 };
    WCHAR buf[1024];
    int hash = 0;
    static int oldhash = 0;
    //锁住分配器
    ztask_memory_lock();
    //获取内存信息
    size_t count = 0;
    size_t msize = 0;
    MEM **mem = NULL;
    ztask_memory_info(&mem, &count, &msize);
    for (size_t i = 0; i < count; i++)
    {
        if ((i & 1) == 0)
        {
            hash ^= ((hash << 7) ^ (int)mem[i] ^ (hash >> 3));
        }
        else
        {
            hash ^= (~((hash << 11) ^ ((int)(mem[i])) ^ (hash >> 5)));
        }
    }
    if (oldhash == hash) {
        //未变化就只刷新时间
        for (size_t i = 0; i < ListView_GetItemCount(MemList); i++)
        {
            lvItem.mask = LVIF_PARAM;    //说明pszText是有效的
            lvItem.iItem = i;        //行号
            lvItem.iSubItem = 0;        //列号
            ListView_GetItem(MemList, (LPARAM)(LV_ITEM *)&lvItem);

            lvItem.iSubItem = 2;
            sprintf(buf, "%d", (GetTickCount() - lvItem.lParam) / 1000);
            lvItem.pszText = buf;
            SendMessageA(MemList, LVM_SETITEMTEXT, i, (LPARAM)(LV_ITEM *)&lvItem);
        }
        //释放分配器
        ztask_memory_unlock();
        return;
    }
    oldhash = hash;
    SendMessage(MemList, LVM_DELETEALLITEMS, 0, 0);

    //for (size_t i = count-1; i > count-200; i--)
    for (size_t i = 0; i < count; i++)
    {
        MEM* p = ((MEM *)((char *)mem[i] - sizeof(MEM)));
        //先需要插入一行，才能对这行输入内容
        lvItem.mask = LVIF_PARAM;
        lvItem.iSubItem = 0;
        lvItem.lParam = p->timer;
        ListView_InsertItem(MemList, (LPARAM)&lvItem);

        lvItem.iSubItem = 0;
        sprintf(buf, "0x%p", p->ptr);
        lvItem.pszText = buf;
        SendMessageA(MemList, LVM_SETITEMTEXT, 0, (LPARAM)(LV_ITEM *)&lvItem);
        lvItem.iSubItem = 1;
        sprintf(buf, "%d", p->_Size);
        lvItem.pszText = buf;
        SendMessageA(MemList, LVM_SETITEMTEXT, 0, (LPARAM)(LV_ITEM *)&lvItem);
        lvItem.iSubItem = 2;
        sprintf(buf, "%d", (GetTickCount() - p->timer) / 1000);
        lvItem.pszText = buf;
        SendMessageA(MemList, LVM_SETITEMTEXT, 0, (LPARAM)(LV_ITEM *)&lvItem);
        lvItem.iSubItem = 3;
        lvItem.pszText = p->_Func;
        SendMessageA(MemList, LVM_SETITEMTEXT, 0, (LPARAM)(LV_ITEM *)&lvItem);
        lvItem.iSubItem = 4;
        lvItem.pszText = p->_File;
        SendMessageA(MemList, LVM_SETITEMTEXT, 0, (LPARAM)(LV_ITEM *)&lvItem);
        lvItem.iSubItem = 5;
        sprintf(buf, "%d", p->_Line);
        lvItem.pszText = buf;
        SendMessageA(MemList, LVM_SETITEMTEXT, 0, (LPARAM)(LV_ITEM *)&lvItem);
    }
    sprintf(buf, "%d/%d", count, msize);
    SetWindowTextA(MemStatic, buf);

    //释放分配器
    ztask_memory_unlock();
}
#endif
static convertFileSize(char *buf, long size) {
    long kb = 1024;
    long mb = kb * 1024;
    long gb = mb * 1024;

    if (size >= gb) {
        return sprintf(buf, "%.1f GB", (float)size / gb);
    }
    else if (size >= mb) {
        float f = (float)size / mb;
        return sprintf(buf, f > 100 ? "%.0f MB" : "%.1f MB", f);
    }
    else if (size >= kb) {
        float f = (float)size / kb;
        return sprintf(buf, f > 100 ? "%.0f KB" : "%.1f KB", f);
    }
    else
        return sprintf(buf, "%d B", size);
}
//刷新数据时钟 1s
static VOID CALLBACK UPDATETIME_SERVER(HWND H, UINT U, UINT_PTR Pt, DWORD D) {
    LVITEM lvItem = { 0 };
    char buf[1024];
    struct ztask_context **ctxs = NULL;
    uint32_t count = ztask_debug_getservers(&ctxs);
    int hash = 0;
    for (size_t i = 0; i < count; i++)
    {
        if ((i & 1) == 0)
        {
            hash ^= ((hash << 7) ^ (int)ctxs[i] ^ (hash >> 3));
        }
        else
        {
            hash ^= (~((hash << 11) ^ ((int)(ctxs[i])) ^ (hash >> 5)));
        }
    }
    if (oldhash == hash) {
        //未变化就只刷新时间
        for (size_t i = 0; i < ListView_GetItemCount(SevList); i++)
        {
            lvItem.mask = LVIF_PARAM;    //说明pszText是有效的
            lvItem.iItem = i;        //行号
            lvItem.iSubItem = 0;        //列号
            ListView_GetItem(SevList, (LPARAM)(LV_ITEM *)&lvItem);

            uint32_t handle = NULL;
            char *alias = NULL;
            size_t mem = 0;
            double cpu = 0;
            int mqlen = 0;
            size_t message = 0;
            uint32_t task = 0;
            ztask_debug_info(lvItem.lParam, &handle, &alias, &mem, &cpu, &mqlen, &message, &task);

            if (alias) {
                lvItem.iSubItem = 1;
                lvItem.pszText = alias;
                SendMessageA(SevList, LVM_SETITEMTEXT, lvItem.iItem, (LPARAM)(LV_ITEM *)&lvItem);
            }

            lvItem.iSubItem = 2;
            convertFileSize(buf, mem);
            lvItem.pszText = buf;
            SendMessageA(SevList, LVM_SETITEMTEXT, i, (LPARAM)(LV_ITEM *)&lvItem);

            lvItem.iSubItem = 3;
            sprintf(buf, "%lf", cpu);
            lvItem.pszText = buf;
            SendMessageA(SevList, LVM_SETITEMTEXT, i, (LPARAM)(LV_ITEM *)&lvItem);

            lvItem.iSubItem = 4;
            sprintf(buf, "%zu", message);
            lvItem.pszText = buf;
            SendMessageA(SevList, LVM_SETITEMTEXT, lvItem.iItem, (LPARAM)(LV_ITEM *)&lvItem);

            lvItem.iSubItem = 5;
            sprintf(buf, "%d", mqlen);
            lvItem.pszText = buf;
            SendMessageA(SevList, LVM_SETITEMTEXT, lvItem.iItem, (LPARAM)(LV_ITEM *)&lvItem);

            lvItem.iSubItem = 6;
            sprintf(buf, "%u", task);
            lvItem.pszText = buf;
            SendMessageA(SevList, LVM_SETITEMTEXT, lvItem.iItem, (LPARAM)(LV_ITEM *)&lvItem);
        }
        ztask_debug_freeservers(ctxs);
        return;
    }
    oldhash = hash;

    SendMessage(SevList, LVM_DELETEALLITEMS, 0, 0);
    for (size_t i = 0; i < count; i++)
    {
        uint32_t handle = NULL;
        char *alias = NULL;
        size_t mem = 0;
        double cpu = 0;
        int mqlen = 0;
        size_t message = 0;
        uint32_t task = 0;
        ztask_debug_info(ctxs[i], &handle, &alias, &mem, &cpu, &mqlen, &message, &task);

        //先需要插入一行，才能对这行输入内容
        lvItem.mask = LVIF_PARAM;
        lvItem.iSubItem = 0;
        lvItem.lParam = ctxs[i];
        lvItem.iItem = ListView_GetItemCount(SevList);
        lvItem.iItem = ListView_InsertItem(SevList, (LPARAM)&lvItem);
        ListView_SetItem(SevList, &lvItem);

        lvItem.iSubItem = 0;
        sprintf(buf, "%08x", handle);
        lvItem.pszText = buf;
        SendMessageA(SevList, LVM_SETITEMTEXT, lvItem.iItem, (LPARAM)(LV_ITEM *)&lvItem);

        if (alias) {
            lvItem.iSubItem = 1;
            lvItem.pszText = alias;
            SendMessageA(SevList, LVM_SETITEMTEXT, lvItem.iItem, (LPARAM)(LV_ITEM *)&lvItem);
        }

        lvItem.iSubItem = 2;
        convertFileSize(buf, mem);
        lvItem.pszText = buf;
        SendMessageA(SevList, LVM_SETITEMTEXT, i, (LPARAM)(LV_ITEM *)&lvItem);

        lvItem.iSubItem = 3;
        sprintf(buf, "%lf", cpu);
        lvItem.pszText = buf;
        SendMessageA(SevList, LVM_SETITEMTEXT, lvItem.iItem, (LPARAM)(LV_ITEM *)&lvItem);

        lvItem.iSubItem = 4;
        sprintf(buf, "%zu", message);
        lvItem.pszText = buf;
        SendMessageA(SevList, LVM_SETITEMTEXT, lvItem.iItem, (LPARAM)(LV_ITEM *)&lvItem);

        lvItem.iSubItem = 5;
        sprintf(buf, "%d", mqlen);
        lvItem.pszText = buf;
        SendMessageA(SevList, LVM_SETITEMTEXT, lvItem.iItem, (LPARAM)(LV_ITEM *)&lvItem);

        lvItem.iSubItem = 6;
        sprintf(buf, "%u", task);
        lvItem.pszText = buf;
        SendMessageA(SevList, LVM_SETITEMTEXT, lvItem.iItem, (LPARAM)(LV_ITEM *)&lvItem);
    }
    ztask_debug_freeservers(ctxs);
}
//主窗口回调
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    //判断消息ID
    switch (uMsg)
    {
    case WM_NOTIFY: //TAB切换
    {
        switch (((LPNMHDR)lParam)->code)
        {
        case TCN_SELCHANGING://Tab改变前  
        {
            int iPage = TabCtrl_GetCurSel(hTab);
            ShowWindow(Page[iPage], SW_HIDE);
            return FALSE;
        }
        case TCN_SELCHANGE://Tab改变后  
        {
            int iPage = TabCtrl_GetCurSel(hTab);
            ShowWindow(Page[iPage], SW_SHOW);
            return TRUE;
        }
        }
        break;
    }
    case WM_CREATE: //居中
    {
        int scrWidth, scrHeight;
        RECT rect;
        scrWidth = GetSystemMetrics(SM_CXSCREEN);
        scrHeight = GetSystemMetrics(SM_CYSCREEN);
        GetWindowRect(hwnd, &rect);
        rect.left = (scrWidth - rect.right) / 2;
        rect.top = (scrHeight - rect.bottom) / 2;
        SetWindowPos(hwnd, HWND_TOP, rect.left, rect.top, rect.right, rect.bottom, SWP_SHOWWINDOW);
        break;
    }
    case WM_DESTROY:
    {
        oldhash = 0;
        hWnd = NULL;
        KillTimer(0, sre_id);
        break;
    }
    default:
        return DefWindowProcA(hwnd, uMsg, wParam, lParam);
        break;
    }
    return 0;
}
//服务窗口回调
static LRESULT CALLBACK PageProc1(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
    {
        break;
    }
    default:
        return DefWindowProcA(hWnd, message, wParam, lParam);
    }
    return 0;
}
static LRESULT CALLBACK PageProc2(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
#if USE_MALLOC_DEBUG
    switch (message)
    {
    case WM_COMMAND:
        if (LOWORD(wParam) == 1)
        {
            //登陆按钮
            UPDATETIME(0, 0, 0, 0);
        }
        else if (LOWORD(wParam) == 2)
        {
            ztask_memory_clean();
        }
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    int ret = DefWindowProcA(hWnd, message, wParam, lParam);
    return ret;
#else
    return DefWindowProc(hWnd, message, wParam, lParam);
#endif
}
static LRESULT CALLBACK PageProc3(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    int ret = DefWindowProcA(hWnd, message, wParam, lParam);
    return ret;
}

//打开调试窗口
void debug_main() {
    //生成随机窗口类名
    char wclass[20];
    char pclass[20];
    srand((unsigned)time(NULL) - 2);
    itoa(rand(), wclass, 10);
    itoa(rand(), pclass, 10);
    hInstance = GetModuleHandleA(NULL);
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    WNDCLASSA wndClass = { 0 };
    wndClass.style = CS_HREDRAW | CS_VREDRAW;
    wndClass.cbClsExtra = 0;
    wndClass.cbWndExtra = 0;
    wndClass.hInstance = hInstance;
    wndClass.hIcon = LoadIcon(NULL, MAKEINTRESOURCE(IDI_SHIELD));
    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndClass.hbrBackground = (HBRUSH)(COLOR_BTNSHADOW);
    wndClass.lpszMenuName = NULL;
    wndClass.lpszClassName = wclass;
    wndClass.lpfnWndProc = WindowProc;
    if (!RegisterClassA(&wndClass))
        return;
    wndClass.hbrBackground = (HBRUSH)(COLOR_BTNSHADOW);
    wndClass.lpszClassName = pclass;
    wndClass.lpfnWndProc = DefWindowProcA;
    if (!RegisterClassA(&wndClass))
        return;


    // 创建窗口
    hWnd = CreateWindowA(wclass, "引擎调试器",
        WS_OVERLAPPEDWINDOW ^ WS_THICKFRAME ^ WS_MAXIMIZEBOX,
        0, 0, 600, 420,
        NULL, NULL, hInstance, NULL);
    SendMessageA(hWnd, WM_SETFONT, (WPARAM)hFont, 1);  //设置控件字体  

    RECT rect;
    GetClientRect(hWnd, &rect);
    //创建选择夹
    hTab = CreateWindowExA(0, WC_TABCONTROLA, 0,
        TCS_FIXEDWIDTH | WS_CHILD | WS_VISIBLE,
        rect.left + 1, rect.top, rect.right - 1, rect.bottom,
        hWnd, NULL, hInstance, 0);
    SendMessageA(hTab, WM_SETFONT, (WPARAM)hFont, 1);

    GetClientRect(hTab, &rect);//获取Tab窗口的大小
    HWND tmp = 0;
    TCITEMA ti = { 0 };
    ti.mask = TCIF_TEXT;
    ti.pszText = "  服务管理  ";
    ti.cchTextMax = strlen(ti.pszText);
    SendMessageA(hTab, TCM_INSERTITEM, 0, (LPARAM)&ti);
    {
        Page[0] = CreateWindowExA(
            NULL, pclass, NULL, WS_CHILD,
            1, 21, rect.right - rect.left - 4, rect.bottom - rect.top - 23,
            hTab, NULL, hInstance, NULL);
        SetWindowLongPtrA(Page[0], GWLP_WNDPROC, PageProc1);

        //创建列表框
        SevList = CreateWindowExA(LVS_EX_SUBITEMIMAGES | LVS_EX_INFOTIP, "SysListView32", "", WS_BORDER | WS_VISIBLE | WS_CHILDWINDOW | LBS_NOTIFY | WS_CLIPSIBLINGS,
            1, 1, rect.right - rect.left - 6, rect.bottom - rect.top - 26 - 2, Page[0], 0, GetModuleHandleA(NULL), NULL);
        SendMessageA(SevList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_UNDERLINEHOT | LVS_EX_CHECKBOXES | LVS_EX_HEADERDRAGDROP | LVS_EX_DOUBLEBUFFER);
        SendMessageA(SevList, WM_SETFONT, (WPARAM)hFont, 1);
        LV_COLUMN lvc;
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;

        lvc.pszText = "handle";
        lvc.cx = 75;
        SendMessage(SevList, LVM_INSERTCOLUMN, 0, &lvc);

        lvc.pszText = "服务别名";
        lvc.cx = 140;
        SendMessage(SevList, LVM_INSERTCOLUMN, 1, &lvc);

        lvc.pszText = "内存";
        lvc.cx = 60;
        SendMessage(SevList, LVM_INSERTCOLUMN, 2, &lvc);

        lvc.pszText = "CPU时间";
        lvc.cx = 60;
        SendMessage(SevList, LVM_INSERTCOLUMN, 3, &lvc);

        lvc.pszText = "消息总量";
        lvc.cx = 60;
        SendMessage(SevList, LVM_INSERTCOLUMN, 4, &lvc);

        lvc.pszText = "队列长度";
        lvc.cx = 60;
        SendMessage(SevList, LVM_INSERTCOLUMN, 5, &lvc);

        lvc.pszText = "待处理任务";
        lvc.cx = 72;
        SendMessage(SevList, LVM_INSERTCOLUMN, 6, &lvc);

        lvc.pszText = "死循环";
        lvc.cx = 54;
        SendMessage(SevList, LVM_INSERTCOLUMN, 7, &lvc);

        sre_id = SetTimer(0, 0, 1000 * 1, UPDATETIME_SERVER);
    }
    ti.pszText = "  内存管理  ";
    ti.cchTextMax = strlen(ti.pszText);
    SendMessageA(hTab, TCM_INSERTITEM, 1, (LPARAM)&ti);
    {
        Page[1] = CreateWindowExA(
            NULL, pclass, NULL, WS_CHILD,
            1, 21, rect.right - rect.left - 4, rect.bottom - rect.top - 23,
            hTab, NULL, hInstance, NULL);
        SetWindowLongPtrA(Page[1], GWLP_WNDPROC, PageProc2);

        //创建列表框
        MemList = CreateWindowExA(LVS_EX_SUBITEMIMAGES | LVS_EX_INFOTIP, "SysListView32", "", WS_BORDER | WS_VISIBLE | WS_CHILDWINDOW | LBS_NOTIFY | WS_CLIPSIBLINGS,
            1, 1, rect.right - rect.left - 6 - 100, rect.bottom - rect.top - 26 - 2, Page[1], 0, GetModuleHandleA(NULL), NULL);
        SendMessageA(MemList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_UNDERLINEHOT | LVS_EX_CHECKBOXES | LVS_EX_HEADERDRAGDROP | LVS_EX_DOUBLEBUFFER);
        SendMessageA(MemList, WM_SETFONT, (WPARAM)hFont, 1);
        LV_COLUMN lvc;
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;

        lvc.pszText = "地址";
        lvc.cx = 120;
        SendMessage(MemList, LVM_INSERTCOLUMN, 0, &lvc);

        lvc.pszText = "大小";
        lvc.cx = 40;
        SendMessage(MemList, LVM_INSERTCOLUMN, 1, &lvc);

        lvc.pszText = "时间";
        lvc.cx = 40;
        SendMessage(MemList, LVM_INSERTCOLUMN, 2, &lvc);

        lvc.pszText = "函数";
        lvc.cx = 120;
        SendMessage(MemList, LVM_INSERTCOLUMN, 3, &lvc);

        lvc.pszText = "文件";
        lvc.cx = 120;
        SendMessage(MemList, LVM_INSERTCOLUMN, 4, &lvc);

        lvc.pszText = "行";
        lvc.cx = 40;
        SendMessage(MemList, LVM_INSERTCOLUMN, 5, &lvc);

        //SetTimer(0, 0, 1000*10, UPDATETIME);

        tmp = CreateWindowA("Button", "刷新", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            rect.right - rect.left - 6 - 100 + 20, 10, 60, 28, Page[1], 1, hInstance, NULL);
        SendMessageA(tmp, WM_SETFONT, (WPARAM)hFont, 1);

        tmp = CreateWindowA("Button", "清空", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            rect.right - rect.left - 6 - 100 + 20, 40, 60, 28, Page[1], 2, hInstance, NULL);
        SendMessageA(tmp, WM_SETFONT, (WPARAM)hFont, 1);


        tmp = CreateWindowA("STATIC", "", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE,
            rect.right - rect.left - 6 - 100 + 20, 80, 65, 28, Page[1], 2, hInstance, NULL);
        SendMessageA(tmp, WM_SETFONT, (WPARAM)hFont, 1);
        MemStatic = tmp;
    }
    ti.pszText = "  控制台  ";
    ti.cchTextMax = strlen(ti.pszText);
    SendMessageA(hTab, TCM_INSERTITEM, 2, (LPARAM)&ti);
    {
        Page[2] = CreateWindowExA(
            NULL, pclass, NULL, WS_CHILD,
            1, 21, rect.right - rect.left - 4, rect.bottom - rect.top - 23,
            hTab, NULL, hInstance, NULL);
        SetWindowLongPtrA(Page[2], GWLP_WNDPROC, PageProc3);//子类化窗口



    }
    ti.pszText = "  日  志  ";
    ti.cchTextMax = strlen(ti.pszText);
    SendMessageA(hTab, TCM_INSERTITEM, 3, (LPARAM)&ti);
    {
        Page[3] = CreateWindowExA(
            NULL, pclass, NULL, WS_CHILD,
            1, 21, rect.right - rect.left - 4, rect.bottom - rect.top - 23,
            hTab, NULL, hInstance, NULL);
        SetWindowLongPtrA(Page[3], GWLP_WNDPROC, PageProc3);//子类化窗口



    }
    ShowWindow(Page[0], SW_SHOW);//显示第一个页面 
    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);
}
void open_debug() {
    if (!hWnd)
    {
        //载入窗口
        debug_main();
    }
}
