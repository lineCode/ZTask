#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include "ztask.h"
#include "thread.h"

typedef struct {
    ztask_thread_cb call;
    void *arg;
} thread_arg;

static unsigned thread_call(thread_arg *p) {
    p->call(p->arg);
    ztask_free(p);
    return 0;
}

int ztask_thread_create(ztask_thread* tid, ztask_thread_cb entry, void* arg) {
    thread_arg *p = ztask_malloc(sizeof(thread_arg));
    p->call = entry;
    p->arg = arg;
#if (defined(_WIN32) || defined(_WIN64))
    HANDLE h = (HANDLE)_beginthread(thread_call, 0, p);;
    if (tid)
        *tid = h;
#else
    pthread_create((pthread_t *)tid, NULL, (void *(*)(void *))thread_call, p);
#endif
    return 0;
}

int ztask_key_create(ztask_key* key) {
#if (defined(_WIN32) || defined(_WIN64))
    key->tls_index = TlsAlloc();
    if (key->tls_index == TLS_OUT_OF_INDEXES)
        return 1;
#else
    pthread_key_create((pthread_key_t *)key, NULL);
#endif
    return 0;
}

void ztask_key_delete(ztask_key* key) {
#if (defined(_WIN32) || defined(_WIN64))
    if (TlsFree(key->tls_index) == FALSE)
        abort();
    key->tls_index = TLS_OUT_OF_INDEXES;
#else
    pthread_key_delete(*(pthread_key_t *)key);
#endif
}

void* ztask_key_get(ztask_key* key) {
    void* value;
#if (defined(_WIN32) || defined(_WIN64))
    value = TlsGetValue(key->tls_index);
    if (value == NULL)
        if (GetLastError() != ERROR_SUCCESS)
            abort();
#else
    value = pthread_getspecific(*(pthread_key_t *)key);
#endif
    return value;
}

void ztask_key_set(ztask_key* key, void* value) {
#if (defined(_WIN32) || defined(_WIN64))
    if (TlsSetValue(key->tls_index, value) == FALSE)
        abort();
#else
    pthread_setspecific(*(pthread_key_t *)key, value);
#endif
}


int ztask_cond_init(ztask_cond* cond) {
#if (defined(_WIN32) || defined(_WIN64))
    cond->CompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
#else
    pthread_cond_init((pthread_cond_t *)cond, NULL);
#endif
    return 0;
}
void ztask_cond_destroy(ztask_cond* cond) {
#if (defined(_WIN32) || defined(_WIN64))
    CloseHandle(cond->CompletionPort);
#else
    pthread_cond_destroy((pthread_cond_t *)cond);
#endif
}
void ztask_cond_signal(ztask_cond* cond) {
#if (defined(_WIN32) || defined(_WIN64))
    PostQueuedCompletionStatus(cond->CompletionPort, 0, 0, 0);
#else
    pthread_cond_signal((pthread_cond_t *)cond);
#endif
}
void ztask_cond_wait(ztask_cond* cond, ztask_mutex* mutex) {
#if (defined(_WIN32) || defined(_WIN64))
    void *lpContext = NULL;
    OVERLAPPED        *pOverlapped = NULL;
    DWORD            dwBytesTransfered = 0;
    GetQueuedCompletionStatus(cond->CompletionPort, &dwBytesTransfered, (LPDWORD)&lpContext, (LPOVERLAPPED *)&pOverlapped, INFINITE);
#else
    pthread_cond_wait((pthread_cond_t *)cond, (pthread_mutex_t *)mutex);
#endif
}
int ztask_cond_wait_timeout(ztask_cond* cond, ztask_mutex* mutex, size_t time) {
#if (defined(_WIN32) || defined(_WIN64))
    void *lpContext = NULL;
    OVERLAPPED        *pOverlapped = NULL;
    DWORD            dwBytesTransfered = 0;
    if (GetQueuedCompletionStatus(cond->CompletionPort, &dwBytesTransfered, (LPDWORD)&lpContext, (LPOVERLAPPED *)&pOverlapped, time) == 0)
        if (GetLastError() == WAIT_TIMEOUT)
            return 0;
    return 1;
#else
    pthread_cond_wait((pthread_cond_t *)cond, (pthread_mutex_t *)mutex);
#endif
}
int ztask_get_cpu_num() {
#if (defined(_WIN32) || defined(_WIN64))
    SYSTEM_INFO info = { 0 };
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors;
#else
    return 0;
#endif
}

void ztask_dlopen(char* path, ztask_lib*lib) {
#if (defined(_WIN32) || defined(_WIN64))
    *lib = LoadLibraryA(path);
#else

#endif
}
void ztask_dlsym(ztask_lib *lib, char *ptr, void **fp) {
#if (defined(_WIN32) || defined(_WIN64))
    *fp = GetProcAddress(*lib, ptr);
#else

#endif
}
