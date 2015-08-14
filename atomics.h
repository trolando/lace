#ifndef __ATOMICS_H
#define __ATOMICS_H

#ifdef _MSC_VER
#include <intrin.h>
#endif

#if defined(__cplusplus) && __cplusplus >= 201103L

#define ATOMIC_READ_size_t(x) (*(volatile decltype(x) *)&(x))
#define ATOMIC_WRITE_size_t(v,a) (*(volatile decltype(v) *)(&(v)) = (a))
#define ATOMIC_READ_Task(x) (*(volatile decltype(x) *)&(x))
#define ATOMIC_WRITE_Task(v,a) (*(volatile decltype(v) *)(&(v)) = (a))

#elif defined(_MSC_VER)

#define ATOMIC_READ_size_t(x) (*(volatile size_t *)&(x))
#define ATOMIC_WRITE_size_t(v,a) (*(volatile  size_t *)(&(v)) = (a))
#define ATOMIC_READ_Task(x) (*(volatile Task* *)&(x))
#define ATOMIC_WRITE_Task(v,a) (*(volatile Task* *)(&(v)) = (a))

#else

#define ATOMIC_READ_size_t(x) (*(volatile typeof(x) *)&(x))
#define ATOMIC_WRITE_size_t(v,a) (*(volatile typeof(v) *)(&(v)) = (a))
#define ATOMIC_READ_Task(x) (*(volatile typeof(x) *)&(x))
#define ATOMIC_WRITE_Task(v,a) (*(volatile typeof(v) *)(&(v)) = (a))

#endif

/* Size of processor cache line */
#ifndef LINE_SIZE
#define LINE_SIZE 64
#endif

/* Some fences */
#ifndef compiler_barrier
#ifndef _MSC_VER
#define compiler_barrier() { asm volatile("" ::: "memory"); }
#else
#define compiler_barrier() { _ReadWriteBarrier(); }
#endif
#endif

#ifndef mfence
#ifndef _MSC_VER
#define mfence() { asm volatile("mfence" ::: "memory"); }
#else
#ifdef _M_AMD64
#define MemoryBarrier __faststorefence
#elif defined(_IA64_)
#define MemoryBarrier __mf
#else
// x86

__forceinline
void
MemoryBarrier(
	VOID
	) {
	long Barrier;
	__asm {
		xchg Barrier, eax
	}
}

#endif
#define mfence() { MemoryBarrier(); }
#endif
#endif

/* CAS operation */
#if !defined(cas) || !defined(cas_int)
#undef cas
#undef cas_int
#ifndef _MSC_VER
#define cas(ptr, old, new) (__sync_bool_compare_and_swap((ptr),(old),(new)))
#define cas_int(ptr, old, newval) (__sync_bool_compare_and_swap((ptr),(old),(newval)))
#else
#define cas(ptr, old, newval) (_InterlockedCompareExchange64(((LONG64 volatile*)(ptr)),((LONG64)(newval)),((LONG64)(old))) == ((LONG64)(old)))
#define cas_int(ptr, old, newval) (_InterlockedCompareExchange(((LONG volatile*)(ptr)),((LONG)(newval)),((LONG)(old))) == ((LONG)(old)))
#endif
#endif

/* Atomic add and fetch operation */
#ifndef add_fetch
#ifndef _MSC_VER
#define add_fetch(a, b) __sync_add_and_fetch(&a,b)
#else
#define add_fetch(a, b) _InterlockedAdd64(&(a), (b))
#endif
#endif

/* Compiler specific branch prediction optimization */
#ifndef likely
#ifndef _MSC_VER
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
#else
#define likely(x)       (x)
#define unlikely(x)     (x)
#endif
#endif

#ifndef xinc
#ifndef _MSC_VER
#define xinc(ptr) (__sync_fetch_and_add((ptr), 1))
#else
#define xinc(ptr) (_InterlockedExchangeAdd64((ptr), 1))
#endif
#endif

#endif
