#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <lua.h>
#include <stdio.h>

#include "malloc_hook.h"
#include "ztask.h"
#include "atomic.h"

static size_t _used_memory = 0;
static size_t _memory_block = 0;
typedef struct _mem_data {
    uint32_t handle;
    size_t allocated;
} mem_data;

#define SLOT_SIZE 0x10000
#define PREFIX_SIZE sizeof(uint32_t)

static mem_data mem_stats[SLOT_SIZE];

//使用jemalloc
#ifdef USE_JEMALLOC

#include "jemalloc.h"

//开启内存调试
#if USE_MALLOC_DEBUG

typedef struct _MArray
{
    int size;
    int count;
    size_t msize;
    ztask_mutex lock;
    void **data;
}MArray;

MArray *marray_create()
{
    int i = 0;
    MArray *darray = (MArray *)malloc(sizeof(MArray));
    if (darray != NULL)
    {
        darray->count = 0;
        darray->size = 0;
        darray->msize = 0;
        ztask_mutex_init(&darray->lock);
        darray->data = (void **)malloc(sizeof(void *) * 10);

        if (darray->data != NULL)
        {
            darray->size = 10;
            for (i = 0; i < darray->size; i++)
            {
                darray->data[i] = NULL;
            }
        }
        return darray;
    }

    return NULL;
}
static BOOL marray_expand(MArray *darray, int needone)
{
    int newallocsize = 0;

    if (needone == 2)
    {
        newallocsize = darray->count + (darray->count >> 1) + 10;
    }
    else
    {
        newallocsize = darray->count + 1;
    }
    void **data = (void **)realloc(darray->data, sizeof(void *) * newallocsize);
    if (data != NULL)
    {
        darray->data = data;
        darray->size = newallocsize;

    }
    return TRUE;

}
BOOL marray_append(MArray *darray, void *_Ptr, size_t _Size, const char *_Func, const char *_File, unsigned int _Line)
{
    MEM* p = ((MEM *)((char *)_Ptr - sizeof(MEM)));
    if ((darray->count + 1) >= darray->size)
    {
        marray_expand(darray, 2);

    }

    darray->data[darray->count] = _Ptr;
    p->del = 0;
    p->_File = _File;
    p->_Func = _Func;
    p->_Line = _Line;
    p->_Size = _Size;
    p->ptr = _Ptr;
    p->timer = GetTickCount();
    darray->count++;
    darray->msize += _Size;
    return TRUE;

}
BOOL marray_shrink(MArray *darray)
{
    if ((darray->count >> 1) < darray->size && (darray->size > 10))
    {
        int newallocsize = darray->count + darray->count >> 1;
        void **data = (void **)realloc(darray->data, sizeof(void *) * newallocsize);
        if (data != NULL)
        {
            darray->data = data;
            darray->size = newallocsize;
        }
        return TRUE;
    }
    return FALSE;
}
BOOL marray_delete(MArray *darray, int index, int del)
{
    int i = 0;
    ((MEM *)((char *)darray->data[index] - sizeof(MEM)))->del = del;
    darray->msize -= ((MEM *)((char *)darray->data[index] - sizeof(MEM)))->_Size;
    for (i = index; (i + 1) < darray->count; i++)
    {
        darray->data[i] = darray->data[i + 1];
    }

    darray->count--;

    marray_shrink(darray);

    return TRUE;
}
int marray_find(MArray * darray, void * data) {
    for (size_t i = 0; i < darray->count; i++)
    {
        if (darray->data[i] == data)
            return i;
    }
    return -1;
}

MArray *mem = NULL;

void memory_info_dump(void) {
    ztask_error(NULL, "No jemalloc");
}

void ztask_free_debug(void *ptr) {
    if (!ptr)
        return;
    ztask_mutex_lock(&mem->lock);       // 进入临界区
                                        //检查效验数据
    if (memcmp((char*)ptr + ((MEM*)((char *)ptr - sizeof(MEM)))->_Size, "checkmem", 8) == 0) {
        if (((MEM*)((char *)ptr - sizeof(MEM)))->del == 0)
            marray_delete(mem, marray_find(mem, ptr), 0);
        je_free((char *)ptr - sizeof(MEM));
    }
    else {
        MessageBoxA(NULL, ((MEM*)((char *)ptr - sizeof(MEM)))->_Func, "memory write-overflow", MB_ICONWARNING);
    }
    ztask_mutex_unlock(&mem->lock);       // 离开临界区
}

void *ztask_malloc_debug(size_t size, const char *_Func, const char *_File, unsigned int _Line) {
    if (!mem) {
        mem = marray_create();
    }
    ztask_mutex_lock(&mem->lock);       // 进入临界区
    void *p = (char *)je_malloc(size + sizeof(MEM) + 8) + sizeof(MEM);
    marray_append(mem, p, size, _Func, _File, _Line);
    //写入效验数据
    memcpy((char *)p + size, "checkmem", 8);
    ztask_mutex_unlock(&mem->lock);       // 离开临界区
    return p;
}

void *ztask_realloc_debug(void *ptr, size_t size, const char *_Func, const char *_File, unsigned int _Line) {
    if (ptr) {
        ztask_mutex_lock(&mem->lock);       // 进入临界区
        MEM *old = (char*)ptr - sizeof(MEM);
        void *new = (char *)ztask_malloc_debug(size, _Func, _File, _Line);
        memcpy(new, ptr, old->_Size);
        ztask_free_debug(ptr);
        ztask_mutex_unlock(&mem->lock);       // 离开临界区
        return new;
    }
    else
        return ztask_malloc_debug(size, _Func, _File, _Line);
}

void *ztask_calloc_debug(size_t nmemb, size_t size, const char *_Func, const char *_File, unsigned int _Line) {
    void *ptr = ztask_malloc_debug(nmemb * size, _Func, _File, _Line);
    if (ptr)
    {
        return memset(ptr, 0, nmemb*size);
    }
    return NULL;
}

char *ztask_strdup_debug(const char *str, const char *_Func, const char *_File, unsigned int _Line) {
    if (!str)
        return NULL;
    size_t sz = strlen(str);
    char * ret = ztask_malloc_debug(sz + 1, _Func, _File, _Line);
    memcpy(ret, str, sz + 1);
    return ret;
}
char * ztask_strndup_debug(const char *str, size_t len, const char *_Func, const char *_File, unsigned int _Line) {
    if (!str)
        return NULL;
    char * ret = ztask_malloc_debug(len + 1, _Func, _File, _Line);
    memcpy(ret, str, len);
    ret[len] = 0;
    return ret;
}

void ztask_memory_info(MEM ***_mem, size_t *num, size_t *msize) {
    ztask_mutex_lock(&mem->lock);
    *_mem = mem->data;
    *num = mem->count;
    *msize=mem->msize;
    ztask_mutex_unlock(&mem->lock);
}
void ztask_memory_lock() {
    ztask_mutex_lock(&mem->lock);
}
void ztask_memory_unlock() {
    ztask_mutex_unlock(&mem->lock);
}
void ztask_memory_clean() {
    ztask_mutex_lock(&mem->lock);
    while (mem->count > 0)
    {
        void *ptr = mem->data[0];
        //检查效验数据
        if (memcmp((char*)ptr + ((MEM*)((char *)ptr - sizeof(MEM)))->_Size, "checkmem", 8) == 0) {
            //从管理数组中移出
            marray_delete(mem, marray_find(mem, ptr), 1);
        }
        else {
            char buf[1024];
            snprintf(buf, sizeof(buf), "file:[%s],func:[%s],line:[%u]", ((MEM*)((char *)ptr - sizeof(MEM)))->_File, ((MEM*)((char *)ptr - sizeof(MEM)))->_Func, ((MEM*)((char *)ptr - sizeof(MEM)))->_Line);
            MessageBoxA(NULL, buf, "memory write-overflow", MB_ICONWARNING);
        }
    }
    ztask_mutex_unlock(&mem->lock);
}

#else

#ifdef _MSC_VER
typedef intptr_t ssize_t;
#endif
// for ztask_lalloc use
#define raw_realloc je_realloc
#define raw_free je_free

static ssize_t*
get_allocated_field(uint32_t handle) {
    int h = (int)(handle & (SLOT_SIZE - 1));
    mem_data *data = &mem_stats[h];
    uint32_t old_handle = data->handle;
    ssize_t old_alloc = data->allocated;
    if (old_handle == 0 || old_alloc <= 0) {
        // data->allocated may less than zero, because it may not count at start.
        if (!ATOM_CAS(&data->handle, old_handle, handle)) {
            return 0;
        }
        if (old_alloc < 0) {
            ATOM_CAS(&data->allocated, old_alloc, 0);
        }
    }
    if (data->handle != handle) {
        return 0;
    }
    return &data->allocated;
}

inline static void
update_xmalloc_stat_alloc(uint32_t handle, size_t __n) {
    ATOM_ADD(&_used_memory, __n);
    ATOM_INC(&_memory_block);
    ssize_t* allocated = get_allocated_field(handle);
    if (allocated) {
        ATOM_ADD(allocated, __n);
    }
}

inline static void
update_xmalloc_stat_free(uint32_t handle, size_t __n) {
    ATOM_SUB(&_used_memory, __n);
    ATOM_DEC(&_memory_block);
    ssize_t* allocated = get_allocated_field(handle);
    if (allocated) {
        ATOM_SUB(allocated, __n);
    }
}

inline static void*
fill_prefix(char* ptr) {
    uint32_t handle = ztask_current_handle();
    size_t size = je_malloc_usable_size(ptr);
    uint32_t *p = (uint32_t *)(ptr + size - sizeof(uint32_t));
    memcpy(p, &handle, sizeof(handle));

    update_xmalloc_stat_alloc(handle, size);
    return ptr;
}

inline static void*
clean_prefix(char* ptr) {
    size_t size = je_malloc_usable_size(ptr);
    uint32_t *p = (uint32_t *)(ptr + size - sizeof(uint32_t));
    uint32_t handle;
    memcpy(&handle, p, sizeof(handle));
    update_xmalloc_stat_free(handle, size);
    return ptr;
}

static void malloc_oom(size_t size) {
    fprintf(stderr, "xmalloc: Out of memory trying to allocate %zu bytes\n",
        size);
    fflush(stderr);
    abort();
}

void
memory_info_dump(void) {
    je_malloc_stats_print(0, 0, 0);
}

size_t
mallctl_int64(const char* name, size_t* newval) {
    size_t v = 0;
    size_t len = sizeof(v);
    if (newval) {
        je_mallctl(name, &v, &len, newval, sizeof(size_t));
    }
    else {
        je_mallctl(name, &v, &len, NULL, 0);
    }
    ztask_error(NULL, "name: %s, value: %zd\n", name, v);
    return v;
}

int
mallctl_opt(const char* name, int* newval) {
    int v = 0;
    size_t len = sizeof(v);
    if (newval) {
        int ret = je_mallctl(name, &v, &len, newval, sizeof(int));
        if (ret == 0) {
            //ztask_error(NULL, "set new value(%d) for (%s) succeed\n", *newval, name);
        }
        else {
            //ztask_error(NULL, "set new value(%d) for (%s) failed: error -> %d\n", *newval, name, ret);
        }
    }
    else {
        je_mallctl(name, &v, &len, NULL, 0);
    }

    return v;
}

// hook : malloc, realloc, free, calloc

void *
ztask_malloc(size_t size) {
    //void *ptr = je_malloc(size);
    //if (!ptr) {
    //    char tmp[20];
    //    snprintf(tmp, sizeof(tmp), "分配%d内存失败", size);
    //    MessageBoxA(NULL, "", "严重异常", 0);
    //}
    //return ptr;
    void* ptr = je_malloc(size + PREFIX_SIZE);
    if (!ptr) malloc_oom(size);
    return fill_prefix(ptr);
}

void *
ztask_realloc(void *ptr, size_t size) {
    //if (!size)
    //    size = 4;
    //void *newptr = je_realloc(ptr, size);
    //if (!newptr) {
    //    char tmp[20];
    //    snprintf(tmp, sizeof(tmp), "分配%d内存失败", size);
    //    MessageBoxA(NULL, "", "严重异常", 0);
    //}
    //return newptr;

    if (ptr == NULL) return ztask_malloc(size);

    void* rawptr = clean_prefix(ptr);
    void *newptr = je_realloc(rawptr, size + PREFIX_SIZE);
    if (!newptr) malloc_oom(size);
    return fill_prefix(newptr);
}

void
ztask_free(void *ptr) {
    //je_free(ptr);
    //return;
    if (ptr == NULL) return;
    void* rawptr = clean_prefix(ptr);
    je_free(rawptr);
}

void *
ztask_calloc(size_t nmemb, size_t size) {
    //void *ptr = je_calloc(nmemb, size);
    //if (!ptr)
    //    MessageBoxA(NULL, "内存分配失败", "严重异常", 0);
    //return ptr;
    void* ptr = je_calloc(nmemb + ((PREFIX_SIZE + size - 1) / size), size);
    if (!ptr) malloc_oom(size);
    return fill_prefix(ptr);
}

char *ztask_strdup(const char *str) {
    if (!str)
        return NULL;
    size_t sz = strlen(str);
    char * ret = ztask_malloc(sz + 1);
    memcpy(ret, str, sz + 1);
    return ret;
}
char * ztask_strndup(const char *str, size_t len) {
    if (!str)
        return NULL;
    char * ret = ztask_malloc(len + 1);
    memcpy(ret, str, len);
    ret[len] = 0;
    return ret;
}


#endif
#else

// for ztask_lalloc use
#define raw_realloc realloc
#define raw_free free

void
memory_info_dump(void) {
    ztask_error(NULL, "No jemalloc");
}

size_t
mallctl_int64(const char* name, size_t* newval) {
    ztask_error(NULL, "No jemalloc : mallctl_int64 %s.", name);
    return 0;
}

int
mallctl_opt(const char* name, int* newval) {
    ztask_error(NULL, "No jemalloc : mallctl_opt %s.", name);
    return 0;
}

char *ztask_strdup(const char *str) {
    if (!str)
        return NULL;
    size_t sz = strlen(str);
    char * ret = ztask_malloc(sz + 1);
    memcpy(ret, str, sz + 1);
    return ret;
}
char * ztask_strndup(const char *str, size_t len) {
    if (!str)
        return NULL;
    char * ret = ztask_malloc(len + 1);
    memcpy(ret, str, len);
    ret[len] = 0;
    return ret;
}


#endif





size_t malloc_used_memory(void) {
    return _used_memory;
}

size_t malloc_memory_block(void) {
    return _memory_block;
}

void dump_c_mem() {
    int i;
    size_t total = 0;
    ztask_error(NULL, "dump all service mem:");
    for (i = 0; i < SLOT_SIZE; i++) {
        mem_data* data = &mem_stats[i];
        if (data->handle != 0 && data->allocated != 0) {
            total += data->allocated;
            ztask_error(NULL, ":%08x -> %zdkb %db", data->handle, data->allocated >> 10, (int)(data->allocated % 1024));
        }
    }
    ztask_error(NULL, "+total: %zdkb", total >> 10);
}

void *ztask_lalloc(void *ptr, size_t osize, size_t nsize) {
    if (nsize == 0) {
        ztask_free(ptr);
        return NULL;
    }
    else {
        return ztask_realloc(ptr, nsize);
    }
}

int dump_mem_lua(lua_State *L) {
    int i;
    lua_newtable(L);
    for (i = 0; i < SLOT_SIZE; i++) {
        mem_data* data = &mem_stats[i];
        if (data->handle != 0 && data->allocated != 0) {
            lua_pushinteger(L, data->allocated);
            lua_rawseti(L, -2, (lua_Integer)data->handle);
        }
    }
    return 1;
}

size_t malloc_current_memory(void) {
    uint32_t handle = ztask_current_handle();
    int i;
    for (i = 0; i < SLOT_SIZE; i++) {
        mem_data* data = &mem_stats[i];
        if (data->handle == (uint32_t)handle && data->allocated != 0) {
            return (size_t)data->allocated;
        }
    }
    return 0;
}

void ztask_debug_memory(const char *info) {
    // for debug use
    uint32_t handle = ztask_current_handle();
    size_t mem = malloc_current_memory();
    fprintf(stderr, "[:%08x] %s %p\n", handle, info, (void *)mem);
}
size_t ztask_handle_memory(uint32_t handle) {
    int i;
    for (i = 0; i < SLOT_SIZE; i++) {
        mem_data* data = &mem_stats[i];
        if (data->handle == (uint32_t)handle && data->allocated != 0) {
            return (size_t)data->allocated;
        }
    }
    return 0;
}
void ztask_memory_init() {
#ifdef USE_JEMALLOC
    ssize_t set = 0;
    je_mallctl("arenas.dirty_decay_ms", NULL, NULL, &set, sizeof(ssize_t));
    je_mallctl("arenas.muzzy_decay_ms", NULL, NULL, &set, sizeof(ssize_t));
    //mallctl_opt("arenas.dirty_decay_ms", &ctl);
    //mallctl_opt("arenas.muzzy_decay_ms", &ctl);
#endif
}
