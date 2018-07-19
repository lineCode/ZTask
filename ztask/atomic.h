#ifndef _ZNET_ATOMIC_H
#define _ZNET_ATOMIC_H
#if (defined(_WIN32) || defined(_WIN64))
#include <Windows.h>
extern  long WINAPI _InterlockedAnd(long volatile * _Value, long _Mask);
LONG InterlockedAdd_r(LONG volatile *Addend, LONG Value);
#define __sync_fetch_and_add(ptr,value)		InterlockedExchangeAdd(ptr,value)
#define __sync_fetch_and_sub(ptr, value)	InterlockedExchangeAdd(ptr,-value)
#define __sync_add_and_fetch(ptr, value)	InterlockedAdd_r(ptr,value)
#define __sync_sub_and_fetch(ptr, value)	InterlockedAdd_r(ptr,-value)
#define __sync_or_and_fetch(ptr, value)		InterlockedOr(ptr ,value)
#define __sync_and_and_fetch(ptr, value)	_InterlockedAnd(ptr ,value)
#define __sync_xor_and_fetch(ptr, value)	InterlockedXor(ptr ,value)

#define __sync_bool_compare_and_swap(ptr, oldval,newval) (InterlockedCompareExchange(ptr,newval ,oldval)==oldval)

#define __sync_lock_test_and_set(ptr ,value) InterlockedExchange(ptr ,value)
#define __sync_lock_release(ptr) InterlockedExchange(ptr ,0)

#define __sync_synchronize _mm_mfence
#endif

#define ATOM_CAS(ptr, oval, nval) __sync_bool_compare_and_swap(ptr, oval, nval)
#define ATOM_CAS_POINTER(ptr, oval, nval) __sync_bool_compare_and_swap(ptr, oval, nval)
#define ATOM_CAS16(ptr, oval, nval) (InterlockedCompareExchange16(ptr, nval, oval)!=nval)
#define ATOM_INC(ptr) __sync_add_and_fetch(ptr, 1)
#define ATOM_FINC(ptr) __sync_fetch_and_add(ptr, 1)
#define ATOM_DEC(ptr) __sync_sub_and_fetch(ptr, 1)
#define ATOM_FDEC(ptr) __sync_fetch_and_sub(ptr, 1)
#define ATOM_ADD(ptr,n) __sync_add_and_fetch(ptr, n)
#define ATOM_SUB(ptr,n) __sync_sub_and_fetch(ptr, n)
#define ATOM_AND(ptr,n) __sync_and_and_fetch(ptr, n)

#endif