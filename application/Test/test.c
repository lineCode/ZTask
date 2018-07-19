#include <ztask.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <Windows.h>

int main() {
    //初始化任务调度器
    ztask_init();

    struct ztask_config config = { 0 };
    config.work_thread = 0;
    config.http_thread = 0;
    config.module_path = "./?.dll";
    config.daemon = NULL;
    config.logger = NULL;
    config.logservice = "logger";
    config.profile = 1;
    config.bootstrap = "snlua";
    config.bootstrap_parm = "bootstrap";
    config.bootstrap_parm_sz = 9;
    config.start = "main";
    config.debug = "127.0.0.1:8888";

    config.harbor = 0;

    open_debug();

    //启动任务调度
    ztask_start(&config);
    //开始消息循环
    ztask_ui_loop(NULL);
    //退出系统,善后
    ztask_uninit();
}
