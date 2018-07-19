#include "atomic.h"
#if (defined(_WIN32) || defined(_WIN64))
LONG InterlockedAdd_r(LONG volatile *Addend, LONG Value) {
    return InterlockedExchangeAdd(Addend, Value) + Value;
}
#endif
