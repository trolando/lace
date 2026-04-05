/**
 * test_crash_handler.h
 *
 * Best-effort crash diagnostics for test binaries.
 *
 * Include this in test sources and call crash_handler_install() near the
 * start of main(). On crash, it prints basic info using only
 * async-signal-safe functions (write, _exit, raise, getpid), then
 * re-raises the signal to preserve normal crash behavior (core dump).
 *
 * On Windows, uses SetUnhandledExceptionFilter.
 * On POSIX, installs handlers for SIGSEGV, SIGBUS, SIGILL, and SIGFPE.
 *
 * The backtrace() call is NOT async-signal-safe per POSIX but is widely
 * used in practice. It is called only after restoring SIG_DFL, so if it
 * faults the kernel handles it cleanly.
 *
 * Build with debug info for useful backtrace output:
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
    case EXCEPTION_ACCESS_VIOLATION:      return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW:        return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_BREAKPOINT:            return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION";
    case EXCEPTION_PRIV_INSTRUCTION:      return "EXCEPTION_PRIV_INSTRUCTION";
    default:                              return "UNKNOWN_EXCEPTION";
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
    return EXCEPTION_CONTINUE_SEARCH;
}

static inline void crash_handler_install(void)
{
    SetUnhandledExceptionFilter(crash_exception_filter);
}

#else /* POSIX */

#include <signal.h>
#include <string.h>
#include <unistd.h>

 /* backtrace() availability */
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#include <execinfo.h>
#define HAVE_BACKTRACE 1
#else
#define HAVE_BACKTRACE 0
#endif

/* Alternate signal stack so we can handle stack overflow */
static unsigned char crash_altstack_mem[64 * 1024];

/* Prevent infinite recursion if the handler itself faults */
static volatile sig_atomic_t crash_handler_active = 0;

/* ---- async-signal-safe output helpers ---- */

static void crash_write(const char* s, size_t n)
{
    while (n > 0) {
        ssize_t r = write(STDERR_FILENO, s, n);
        if (r <= 0) break;
        s += (size_t)r;
        n -= (size_t)r;
    }
}

static void crash_puts(const char* s)
{
    size_t n = 0;
    while (s[n] != '\0') n++;
    crash_write(s, n);
}

static void crash_put_hex(unsigned long long val)
{
    char buf[18]; /* "0x" + up to 16 hex digits */
    buf[0] = '0';
    buf[1] = 'x';
    if (val == 0) {
        buf[2] = '0';
        crash_write(buf, 3);
        return;
    }
    int i = 17;
    while (i >= 2 && val > 0) {
        unsigned d = (unsigned)(val & 0xf);
        buf[i--] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        val >>= 4;
    }
    crash_write(buf + i + 1, (size_t)(17 - i));
}

static void crash_put_dec(long val)
{
    char buf[21];
    int neg = 0;
    unsigned long uval;

    if (val < 0) {
        neg = 1;
        uval = (unsigned long)(-(val + 1)) + 1UL;
    }
    else {
        uval = (unsigned long)val;
    }

    int i = 20;
    if (uval == 0) {
        buf[i--] = '0';
    }
    else {
        while (uval > 0 && i >= 0) {
            buf[i--] = (char)('0' + (uval % 10));
            uval /= 10;
        }
    }
    if (neg) buf[i--] = '-';
    crash_write(buf + i + 1, (size_t)(20 - i));
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
    default:      return "SIG???";
    }
}

static void crash_signal_handler(int sig, siginfo_t* info, void* uctx)
{
    (void)uctx;

    if (crash_handler_active) {
        crash_puts("\n*** recursive crash in handler ***\n");
        _exit(128 + sig);
    }
    crash_handler_active = 1;

    /* Restore default handler FIRST so any fault from here on
       goes straight to the kernel (core dump, no recursion). */
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigaction(sig, &sa, NULL);
    }

    /* All output uses only write() — async-signal-safe. */

    crash_puts("\n*** ");
    crash_puts(crash_signal_name(sig));
    crash_puts(" (signal ");
    crash_put_dec(sig);
    crash_puts(") ***\n");

    crash_puts("  PID:              ");
    crash_put_dec((long)getpid());
    crash_puts("\n");

    if (info) {
        crash_puts("  Faulting address: ");
        crash_put_hex((unsigned long long)(unsigned long)info->si_addr);
        crash_puts("\n");

        crash_puts("  si_code:          ");
        crash_put_dec(info->si_code);
        crash_puts("\n");
    }

    /*
     * backtrace() and backtrace_symbols_fd() are NOT async-signal-safe
     * per POSIX, but are widely used in signal handlers in practice.
     * Called after restoring SIG_DFL so if they fault, the kernel
     * handles it cleanly (core dump, no recursion).
     */
#if HAVE_BACKTRACE
    {
        void* frames[64];
        int n = backtrace(frames, (int)(sizeof(frames) / sizeof(frames[0])));
        crash_puts("  Backtrace (");
        crash_put_dec(n);
        crash_puts(" frames, best-effort):\n");
        backtrace_symbols_fd(frames, n, STDERR_FILENO);
    }
#else
    crash_puts("  (backtrace not available)\n");
#endif

    crash_puts("\nRe-raising signal...\n");

    raise(sig);
    _exit(128 + sig);
}

static inline void crash_handler_install(void)
{
#if defined(__FreeBSD__)
    /* FreeBSD's libthr generates transient SIGSEGV/SIGBUS during thread
       stack setup (guard page probing). With SIG_DFL these are handled
       internally by the kernel. Any custom handler, even an empty one,
       runs in a thread whose libthr state is not yet initialized,
       causing a secondary crash. */
    return;
#endif

    /* Install alternate signal stack */
    {
        stack_t ss;
        memset(&ss, 0, sizeof(ss));
        ss.ss_sp = crash_altstack_mem;
        ss.ss_size = sizeof(crash_altstack_mem);
        ss.ss_flags = 0;
        sigaltstack(&ss, NULL);
    }

    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = crash_signal_handler;
        sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
#ifdef SA_ONSTACK
        sa.sa_flags |= SA_ONSTACK;
#endif
        sigfillset(&sa.sa_mask);

        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
        sigaction(SIGILL, &sa, NULL);
#ifdef SIGFPE
        sigaction(SIGFPE, &sa, NULL);
#endif
    }
}

#endif /* _WIN32 */

#endif /* TEST_CRASH_HANDLER_H */