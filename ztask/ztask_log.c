#include "ztask_log.h"
#include "ztask_timer.h"
#include "ztask.h"
#include "ztask_socket.h"
#include <string.h>
#include <time.h>
#include <malloc.h>

FILE *
ztask_log_open(struct ztask_context * ctx, uint32_t handle) {
    const char * logpath = ztask_getenv("logpath");
    if (logpath == NULL)
        return NULL;
    size_t sz = strlen(logpath);
#if defined(_WIN32) || defined(_WIN64)
    char *tmp = alloca(sz + 16);
#else
    char tmp[sz + 16];
#endif
    sprintf(tmp, "%s/%08x.log", logpath, handle);
    FILE *f = fopen(tmp, "ab");
    if (f) {
        uint32_t starttime = ztask_starttime();
        uint64_t currenttime = ztask_now();
        time_t ti = starttime + currenttime / 100;
        ztask_error(ctx, "Open log file %s", tmp);
        fprintf(f, "open time: %u %s", (uint32_t)currenttime, ctime(&ti));
        fflush(f);
    }
    else {
        ztask_error(ctx, "Open log file %s fail", tmp);
    }
    return f;
}

void
ztask_log_close(struct ztask_context * ctx, FILE *f, uint32_t handle) {
    ztask_error(ctx, "Close log file :%08x", handle);
    fprintf(f, "close time: %u\n", (uint32_t)ztask_now());
    fclose(f);
}

static void
log_blob(FILE *f, void * buffer, size_t sz) {
    size_t i;
    uint8_t * buf = buffer;
    for (i = 0; i != sz; i++) {
        fprintf(f, "%02x", buf[i]);
    }
}

static void
log_socket(FILE * f, struct ztask_socket_message * message, size_t sz) {
    fprintf(f, "[socket] %d %d %d ", message->type, message->id, message->ud);

    if (message->buffer == NULL) {
        const char *buffer = (const char *)(message + 1);
        sz -= sizeof(*message);
        const char * eol = memchr(buffer, '\0', sz);
        if (eol) {
            sz = eol - buffer;
        }
        fprintf(f, "[%*s]", (int)sz, (const char *)buffer);
    }
    else {
        sz = message->ud;
        log_blob(f, message->buffer, sz);
    }
    fprintf(f, "\n");
    fflush(f);
}

void
ztask_log_output(FILE *f, uint32_t source, int type, int session, void * buffer, size_t sz) {
    if (type == PTYPE_SOCKET) {
        log_socket(f, buffer, sz);
    }
    else {
        uint32_t ti = (uint32_t)ztask_now();
        fprintf(f, ":%08x %d %d %u ", source, type, session, ti);
        log_blob(f, buffer, sz);
        fprintf(f, "\n");
        fflush(f);
    }
}
