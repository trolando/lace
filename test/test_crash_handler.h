/**
 * test_crash_handler.h
 *
 * Best-effort crash diagnostics for test binaries.
 *
 * Include this in test sources and call crash_handler_install() near the
 * start of main(). On crash, it prints basic crash information and
 * attempts a backtrace of the crashing thread, then re-raises the signal
 * to preserve normal crash behavior (and possibly produce a core dump).
 *
 * On Windows, uses SetUnhandledExceptionFilter to report unhandled
 * exceptions. On POSIX, installs handlers for SIGSEGV, SIGBUS, SIGILL,
 * and optionally SIGFPE.
 *
 * WARNING: The POSIX signal handler intentionally uses facilities such as
 * fprintf(), backtrace(), and backtrace_symbols_fd() that are NOT
 * async-signal-safe. This is acceptable here because the goal is better
 * diagnostics in CI/local testing, not production-grade crash reporting.
 * In rare cases the handler may deadlock, recurse, or produce garbled
 * output if the crash happens inside libc or while a relevant lock is held.
 *
 * Build with debug info for useful output:
 *   - Linux:   -g -rdynamic -fno-omit-frame-pointer
 *   - macOS:   -g -fno-omit-frame-pointer
 *   - FreeBSD: -g -lexecinfo
 *   - Windows: generate PDBs
 */

#ifndef TEST_CRASH_HANDLER_H
#define TEST_CRASH_HANDLER_H

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI
#include <windows.h>
#include <stdio.h>

static const char* crash_exception_name(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:         return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ILLEGAL_INSTRUCTION:      return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW:           return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_BREAKPOINT:               return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:    return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INVALID_OPERATION:    return "EXCEPTION_FLT_INVALID_OPERATION";
    case EXCEPTION_PRIV_INSTRUCTION:         return "EXCEPTION_PRIV_INSTRUCTION";
    default:                                 return "UNKNOWN_EXCEPTION";
    }
}

static LONG WINAPI crash_exception_filter(EXCEPTION_POINTERS* ep)
{
    EXCEPTION_RECORD* er = ep->ExceptionRecord;

    fprintf(stderr,
        "\n*** Unhandled exception %s (0x%08lX) ***\n",
        crash_exception_name(er->ExceptionCode),
        (unsigned long)er->ExceptionCode);
    fprintf(stderr, "  Process ID:        %lu\n", (unsigned long)GetCurrentProcessId());
    fprintf(stderr, "  Thread ID:         %lu\n", (unsigned long)GetCurrentThreadId());
    fprintf(stderr, "  Exception address: %p\n", er->ExceptionAddress);

    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
        const char* kind = "unknown access to";
        if (er->ExceptionInformation[0] == 0) kind = "read from";
        else if (er->ExceptionInformation[0] == 1) kind = "write to";
        else if (er->ExceptionInformation[0] == 8) kind = "execute at";
        fprintf(stderr, "  Access violation:  %s %p\n",
            kind, (void*)er->ExceptionInformation[1]);
    }

    fflush(stderr);

    /* Let normal unhandled-exception processing continue. */
    return EXCEPTION_CONTINUE_SEARCH;
}

static inline void crash_handler_install(void)
{
    SetUnhandledExceptionFilter(crash_exception_filter);
}

#else /* POSIX */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/syscall.h>
#endif

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#include <execinfo.h>
#define HAVE_BACKTRACE 1
#else
#define HAVE_BACKTRACE 0
#endif

 /* Separate stack for signal handlers, so we still have a chance to run
    if the normal thread stack is corrupted or exhausted. */
static unsigned char crash_altstack_mem[64 * 1024];

/* Prevent infinite recursion if the handler itself crashes. */
static volatile sig_atomic_t crash_handler_active = 0;

/* Tiny async-signal-safe helper for fixed strings. */
static void crash_write_literal(const char* s)
{
    size_t n = 0;
    while (s[n] != '\0') n++;

    while (n > 0) {
        ssize_t r = write(STDERR_FILENO, s, n);
        if (r <= 0) break;
        s += (size_t)r;
        n -= (size_t)r;
    }
}

static const char* crash_signal_name(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS:  return "SIGBUS";
    case SIGILL:  return "SIGILL";
#ifdef SIGFPE
    case SIGFPE:  return "SIGFPE";
#endif
    default:      return "UNKNOWN";
    }
}

static long crash_get_tid_best_effort(void)
{
#if defined(__linux__) && defined(SYS_gettid)
    return (long)syscall(SYS_gettid);
#else
    return -1;
#endif
}

static void crash_signal_handler(int sig, siginfo_t* info, void* uctx)
{
    (void)uctx;

    if (crash_handler_active) {
        crash_write_literal("\n*** recursive crash in crash handler; exiting immediately ***\n");
        _exit(128 + sig);
    }
    crash_handler_active = 1;

    /* Minimal reliable breadcrumb first. */
    crash_write_literal("\n*** crash handler entered ***\n");

    /* Best-effort diagnostics below. These are intentionally not
       async-signal-safe, but are useful in CI and local testing. */
    fprintf(stderr, "*** %s (signal %d) ***\n", crash_signal_name(sig), sig);
    fprintf(stderr, "  Process ID:        %ld\n", (long)getpid());

    {
        long tid = crash_get_tid_best_effort();
        if (tid >= 0) {
            fprintf(stderr, "  Thread ID:         %ld\n", tid);
        }
    }

    if (info) {
        fprintf(stderr, "  Faulting address:  %p\n", info->si_addr);
        fprintf(stderr, "  si_code:           %d\n", info->si_code);
    }

#if HAVE_BACKTRACE
    {
        void* frames[64];
        int n = backtrace(frames, (int)(sizeof(frames) / sizeof(frames[0])));
        fprintf(stderr, "  Backtrace (%d frames, best-effort):\n", n);
        backtrace_symbols_fd(frames, n, STDERR_FILENO);
    }
#else
    fprintf(stderr, "  (backtrace not available on this platform)\n");
#endif

    fprintf(stderr, "\nRe-raising signal to preserve normal crash handling...\n");
    fflush(stderr);

    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sigaction(sig, &sa, NULL);
    }

    raise(sig);

    /* If re-raise somehow fails, exit hard. */
    _exit(128 + sig);
}

static inline void crash_handler_install(void)
{
    /* Best effort: install alternate signal stack. */
#if defined(SIGSTKSZ)
    {
        stack_t ss;
        memset(&ss, 0, sizeof(ss));
        ss.ss_sp = crash_altstack_mem;
        ss.ss_size = sizeof(crash_altstack_mem);
        ss.ss_flags = 0;
        (void)sigaltstack(&ss, NULL);
    }
#endif

    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = crash_signal_handler;
        sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
#ifdef SA_ONSTACK
        sa.sa_flags |= SA_ONSTACK;
#endif
        sigfillset(&sa.sa_mask);

        (void)sigaction(SIGSEGV, &sa, NULL);
        (void)sigaction(SIGBUS, &sa, NULL);
        (void)sigaction(SIGILL, &sa, NULL);
#ifdef SIGFPE
        (void)sigaction(SIGFPE, &sa, NULL);
#endif
    }
}

#endif /* _WIN32 */

#endif /* TEST_CRASH_HANDLER_H */