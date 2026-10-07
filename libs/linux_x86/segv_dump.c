// 32-bit SIGSEGV reporter: dumps registers on crash so we can map the fault.
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>

// We interpose sigaction/signal: when the engine installs ITS SIGSEGV handler,
// we chain ours to run first (dump EIP), then call their handler. This lets us
// see the fault even though the engine overrides a plain constructor-installed
// handler.

static int g_fd = -1;
static struct sigaction g_prev_segv_sa;
static void on_segv(int sig, siginfo_t *si, void *cv);

static void raise_default(int sig)
{
    static int (*real_sigaction)(int, const struct sigaction*, struct sigaction*) = 0;
    if (!real_sigaction) real_sigaction = (int(*)(int,const struct sigaction*,struct sigaction*))dlsym(RTLD_NEXT, "sigaction");
    struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_handler = SIG_DFL;
    real_sigaction(sig, &sa, NULL);
    raise(sig);
}

static void on_segv(int sig, siginfo_t *si, void *cv)
{
    int lfd = (g_fd >= 0) ? g_fd : open("/tmp/csneo_segv.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (lfd < 0) { signal(sig, SIG_DFL); raise(sig); return; }
    write(lfd, "H\n", 2);
    ucontext_t *u = (ucontext_t *)cv;
    unsigned long long eip = (unsigned long long)(u ? (unsigned long long)u->uc_mcontext.gregs[REG_EIP] : 0);
    unsigned long long esp = (unsigned long long)(u ? (unsigned long long)u->uc_mcontext.gregs[REG_ESP] : 0);
    unsigned long long ebp = (unsigned long long)(u ? (unsigned long long)u->uc_mcontext.gregs[REG_EBP] : 0);
    char buf[128]; int o = 0;
    o += sprintf(buf + o, "si=%p eip=%llx esp=%llx ebp=%llx\n",
        si ? (void*)si->si_addr : 0, eip, esp, ebp);
    write(lfd, buf, o);
    // backtrace to a second fd (best-effort; may not work in-handler)
    int bfd = open("/tmp/csneo_segv_bt.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (bfd >= 0) {
        void *bt[64]; int n = backtrace(bt, 64);
        backtrace_symbols_fd(bt, n, bfd);
    }
    // chain to the engine's handler if any
    if (g_prev_segv_sa.sa_sigaction) g_prev_segv_sa.sa_sigaction(sig, si, cv);
    else if (g_prev_segv_sa.sa_handler) g_prev_segv_sa.sa_handler(sig);
    else raise_default(sig);
    // fall through to die
    for(;;);
}

// Interpose sigaction: capture the engine's SIGSEGV handler so we can chain.
int sigaction(int signo, const struct sigaction *act, struct sigaction *oldact)
{
    static int (*real_sigaction)(int, const struct sigaction*, struct sigaction*) = 0;
    if (!real_sigaction) real_sigaction = (int(*)(int,const struct sigaction*,struct sigaction*))dlsym(RTLD_NEXT, "sigaction");
    int r = real_sigaction(signo, act, oldact);
    if (signo == SIGSEGV && act) {
        g_prev_segv_sa = *act;
        // now re-install ours on top
        struct sigaction mine = *act;          // start from theirs
        mine.sa_sigaction = on_segv;           // but ours runs first
        mine.sa_flags |= SA_SIGINFO;
        real_sigaction(SIGSEGV, &mine, NULL);
    }
    return r;
}

// Interpose signal() too (some code uses it).
__sighandler_t signal(int signo, __sighandler_t handler)
{
    static __sighandler_t (*real_signal)(int, __sighandler_t) = 0;
    if (!real_signal) real_signal = (__sighandler_t(*)(int,__sighandler_t))dlsym(RTLD_NEXT, "signal");
    __sighandler_t r = real_signal(signo, handler);
    if (signo == SIGSEGV) {
        memset(&g_prev_segv_sa, 0, sizeof g_prev_segv_sa);
        g_prev_segv_sa.sa_handler = handler;
        g_prev_segv_sa.sa_flags = 0;
        real_signal(SIGSEGV, (__sighandler_t)on_segv);
    }
    return r;
}

__attribute__((constructor))
static void install(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_segv;
    sa.sa_flags = SA_SIGINFO;
    // use the real sigaction to avoid our own interpose recursion on first set
    static int (*real_sigaction)(int, const struct sigaction*, struct sigaction*) = 0;
    if (!real_sigaction) real_sigaction = (int(*)(int,const struct sigaction*,struct sigaction*))dlsym(RTLD_NEXT, "sigaction");
    real_sigaction(SIGSEGV, &sa, NULL);
    real_sigaction(SIGBUS, &sa, NULL);
    g_fd = open("/tmp/csneo_segv.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (g_fd >= 0) write(g_fd, "(watch)\n", 8);
}
