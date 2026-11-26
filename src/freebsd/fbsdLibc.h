#ifndef FBSD_LIBC_H
#define FBSD_LIBC_H

// FreeBSD 9.1's libc functions whose ABI is not glibc's (flags, structures,
// errno, pthread objects, the cabinet's devices): the shim standing in for
// one, or NULL (the host's own does).
void *fbsdShim(const char *name);

#endif // FBSD_LIBC_H
