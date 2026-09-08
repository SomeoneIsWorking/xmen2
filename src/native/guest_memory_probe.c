/* Checked diagnostic reads return refusal instead of recursively faulting.
 * Browser probes share the sparse owner; native hosts retain the process VM
 * reader that can safely inspect an unmapped guest pointer in a fault report.
 */
#include "guest_memory.h"
#include "x86rt_native.h"

#if !defined(X2_GUEST_MEMORY_SPARSE)
#include "platform_posix.h"
#include <sys/uio.h>
#if defined(__ANDROID__)
#include <sys/syscall.h>
#endif
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif
#endif

static int process_read(uint32_t addr, void *dst, size_t n) {
#if defined(X2_GUEST_MEMORY_SPARSE)
  return guest_memory_try_read(addr, dst, n);
#else
  const void *source = guest_memory_const_pointer(addr);
#if defined(__APPLE__)
  mach_vm_size_t copied = 0;
  kern_return_t result = mach_vm_read_overwrite(
      mach_task_self(), (mach_vm_address_t)(uintptr_t)source, (mach_vm_size_t)n,
      (mach_vm_address_t)(uintptr_t)dst, &copied);
  return result == KERN_SUCCESS && copied == (mach_vm_size_t)n;
#else
  struct iovec loc, rem;
  loc.iov_base = dst;
  loc.iov_len = n;
  rem.iov_base = (void *)source;
  rem.iov_len = n;
#if defined(__ANDROID__)
  /* Bionic exposes the libc wrapper only from API 23, but the checked Linux
   * syscall exists at the API-21 64-bit floor. Keep the signal-handler-safe
   * read contract instead of replacing it with a faulting dereference. */
  return syscall(SYS_process_vm_readv, getpid(), &loc, 1, &rem, 1, 0) ==
         (ssize_t)n;
#else
  return process_vm_readv(getpid(), &loc, 1, &rem, 1, 0) == (ssize_t)n;
#endif
#endif
#endif
}

int x86_peek(uint32_t addr, void *dst, size_t n) {
  return process_read(addr, dst, n);
}

int x86_peek32(uint32_t addr, uint32_t *out) {
  return x86_peek(addr, out, sizeof *out);
}
