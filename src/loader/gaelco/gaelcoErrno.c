// The Gaelco games' errno (see gaelco.h).
//
// Built with a 2001 glibc, the games' executables hold errno themselves (a
// copy relocation of libc's errno@GLIBC_2.0, the variable it had before
// errno was per thread), which today's glibc no longer exports: they would
// not start ("undefined symbol: errno, version GLIBC_2.0"). Here is one to
// copy from. The games' own system call wrappers set their copy; libc's
// calls set the thread's errno, which the games do not read.
//
// gaelcoErrno.ver versions it GLIBC_2.0: unversioned, it would also answer
// libm's errno@GLIBC_PRIVATE (a thread-local), for every game.
//
// (No <errno.h> here: its errno is a macro.)

__attribute__((visibility("default"))) int errno;
