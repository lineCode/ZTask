#ifndef ZTASK_THREAD_H
#define ZTASK_THREAD_H

#if (defined(_WIN32) || defined(_WIN64))
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <process.h>
#else
#include <pthread.h>
#include <dlfcn.h>
#endif

#if (defined(_WIN32) || defined(_WIN64))
typedef HANDLE ztask_thread;
typedef CRITICAL_SECTION ztask_mutex;
typedef struct {
    DWORD tls_index;
} ztask_key;
typedef union {
    HANDLE CompletionPort;
} ztask_cond;
typedef HINSTANCE ztask_lib;
#else
typedef pthread_t ztask_thread;
typedef pthread_mutex_t ztask_mutex;
typedef pthread_key_t ztask_key;
typedef pthread_cond_t ztask_cond;
typedef void * ztask_lib;
#endif

typedef void(*ztask_thread_cb)(void* arg);
int ztask_thread_create(ztask_thread* tid, ztask_thread_cb entry, void* arg);


#define ztask_mutex_init InitializeCriticalSection
#define ztask_mutex_destroy DeleteCriticalSection
#define ztask_mutex_lock EnterCriticalSection
#define ztask_mutex_trylock(mutex) !TryEnterCriticalSection((mutex))
#define ztask_mutex_unlock LeaveCriticalSection



int ztask_key_create(ztask_key* key);
void ztask_key_delete(ztask_key* key);
void* ztask_key_get(ztask_key* key);
void ztask_key_set(ztask_key* key, void* value);


int ztask_cond_init(ztask_cond* cond);
void ztask_cond_destroy(ztask_cond* cond);
void ztask_cond_signal(ztask_cond* cond);
void ztask_cond_wait(ztask_cond* cond, ztask_mutex* mutex);
int ztask_cond_wait_timeout(ztask_cond* cond, ztask_mutex* mutex, size_t time);

int ztask_get_cpu_num();

void ztask_dlopen(char* path, ztask_lib*lib);
void ztask_dlsym(ztask_lib *lib, char *ptr, void **fp);
#endif //ZTASK_THREAD_H