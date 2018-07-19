#include "ztask.h"
#if (defined(_WIN32) || defined(_WIN64))
#include <Windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

struct logger {
    FILE * handle;
    char * filename;
    int close;
#if (defined(_WIN32) || defined(_WIN64))
    HANDLE console;
#endif
};

struct logger *
    logger_create(void) {
    struct logger * inst = ztask_malloc(sizeof(*inst));
    inst->handle = NULL;
    inst->close = 0;
    inst->filename = NULL;
#if (defined(_WIN32) || defined(_WIN64))
    inst->console = NULL;
#endif
    return inst;
}

void
logger_release(struct logger * inst) {
    if (inst->close) {
        fclose(inst->handle);
    }
    ztask_free(inst->filename);
    ztask_free(inst);
}

static int
logger_cb(struct ztask_context * context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
    struct logger * inst = ud;
    switch (type) {
    case PTYPE_SYSTEM:
        if (inst->filename) {
            inst->handle = freopen(inst->filename, "a", inst->handle);
        }
        break;
    case PTYPE_TEXT:
#if (defined(_WIN32) || defined(_WIN64))
        SetConsoleTextAttribute(inst->console, FOREGROUND_GREEN);
#endif
        fprintf(inst->handle, "[:%08x]", source);
#if (defined(_WIN32) || defined(_WIN64))
        SetConsoleTextAttribute(inst->console, FOREGROUND_BLUE | FOREGROUND_RED | FOREGROUND_INTENSITY);
#endif
        time_t t = time(0);
        char timestring[255];
        strftime(timestring, 255, "%Y-%m-%d %H:%M:%S", localtime(&t));
        fprintf(inst->handle, "[%s] ", timestring);
#if (defined(_WIN32) || defined(_WIN64))
        SetConsoleTextAttribute(inst->console, FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY);
#endif
        fwrite(msg, sz, 1, inst->handle);
        fprintf(inst->handle, "\n");
        fflush(inst->handle);
        break;
    }

    return 0;
}

int
logger_init(struct logger * inst, struct ztask_context *ctx, const char * parm, const size_t sz) {
    if (parm) {
        inst->handle = fopen(parm, "w");
        if (inst->handle == NULL) {
            return 1;
        }
        inst->filename = ztask_malloc(strlen(parm) + 1);
        strcpy(inst->filename, parm);
        inst->close = 1;
    }
    else {
#if (defined(_WIN32) || defined(_WIN64))
        inst->console = GetStdHandle(STD_OUTPUT_HANDLE);
#endif
        inst->handle = stdout;
    }
    if (inst->handle) {
        ztask_callback(ctx, inst, logger_cb);
        ztask_command(ctx, "REG", ".logger");
        ztask_alias(ctx, "LOG");
        return 0;
    }
    return 1;
}
