/*
 * kernel32_virtual.c -- the guest's virtual memory: VirtualAlloc, VirtualFree,
 * VirtualQuery and GlobalMemoryStatus, and the reservation and decommit
 * records they share.
 */
#include "../config/environment.h"
#include "guest_heap.h"
#include "guest_layout.h"
#include "guest_memory.h"
#include "kernel32_handles.h"
#include "platform_mman.h"
#include "stdcall_import.h"
#include "x2_log.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <errno.h>
#include <string.h>

#include <lucent/cvar_c.h>

/* Reservations are real mappings with no access; a commit inside one grants
   it. g_reserved_bytes is what the guest holds against phys_bytes(). */
static uint64_t g_reserved_bytes;

#define MEM_COMMIT 0x1000u
#define MEM_RESERVE 0x2000u

/* How much memory this machine has, as far as the guest is concerned. Used by
   BOTH GlobalMemoryStatus and VirtualAlloc, because a budget the allocator
   does not enforce is not a budget -- the game allocates until allocation
   fails, and with the two disagreeing it took 937 MB after being told 512. */
static uint64_t phys_bytes(void) {
  /* X2_PHYS_MB overrides it. How much memory to claim is an empirical
     question -- the game allocates until allocation fails, so the number
     decides how much it takes and possibly whether it gets far enough to
     finish initialising. A constant would have made that untestable. */
  static uint64_t v;
  if (!v) {
    unsigned mb = (unsigned)lucent_cvar_number("phys_mb", 512);
    if (mb < 64u)
      mb = 64u;
    v = (uint64_t)mb * 1048576ULL;
  }
  return v;
}
#define X2_PHYS_BYTES phys_bytes()

/* Memory layout is worth seeing: which arena the game placed where, and how
   big. X2_VERBOSE=1 prints it. Silent by default, because a shipping run
   should not narrate. */
static int verbose(void) {
  static int v = -1;
  if (v < 0) {
    const char *e = x2_config_override_get(kX2ConfigVerbose);
    v = e && *e == '1';
  }
  return v;
}

/*
 * What the guest has reserved, so a later commit over its OWN reservation can
 * be told from a request that collides with the runtime's memory.
 *
 * The first version returned success on any EEXIST, reasoning that a commit
 * over an existing reservation looks exactly like that. It does -- but so does
 * a request landing on the guest heap or a mapped module, and answering
 * "granted" there hands the game memory that belongs to us. That is the
 * plausible-value failure this layer refuses everywhere else, and it took
 * writing the runaway-allocation diagnostic to notice it.
 */
#define MAX_RESERVED 256
static struct {
  uint32_t base, size;
} g_reserved[MAX_RESERVED];
static int g_nreserved;

static int guest_reserved(uint32_t base, uint32_t len) {
  int i;
  for (i = 0; i < g_nreserved; i++)
    if (base >= g_reserved[i].base &&
        base + len <= g_reserved[i].base + g_reserved[i].size)
      return 1;
  return 0;
}

/*
 * The span containing an address, for VirtualQuery.
 *
 * This table is the ONLY record that a guest reservation exists -- the address
 * is in no module and in no guest heap -- so a VirtualQuery that does not
 * consult it calls the guest's own memory FREE. That is not a cosmetic
 * inaccuracy: libIGCore's CRT grows its heap by scanning for a free region and
 * reserving it, so being told its own reservations are still free makes the
 * scan never finish. It reserved ~527 MB in 28 grows and only stopped when the
 * budget refused (C088). The allocator and the query have to describe the same
 * address space, which is the same defect as C070/C071 on the Xbox side.
 */
/*
 * Pages the guest has DECOMMITTED inside its own reservations.
 *
 * VirtualFree(MEM_DECOMMIT) mprotects them PROT_NONE so a use-after-decommit
 * faults instead of reading stale data. That was right, and it made
 * VirtualQuery a liar: it reported the whole reservation as MEM_COMMIT on the
 * grounds that "the reservation is mapped PROT_READ|PROT_WRITE", which stopped
 * being true the moment the first decommit ran. The guest asked, was told the
 * range was committed, used it, and faulted -- issue #41.
 *
 * On Windows a decommitted page reads back as MEM_RESERVE, and the region size
 * is the run of pages in that state. So the two calls have to share one record
 * of what is committed, which is this.
 */
#define MAX_DECOMMIT 256
static struct {
  uint32_t base, size;
} g_decommit[MAX_DECOMMIT];
static int g_ndecommit, g_decommit_lost;

/* The decommitted range containing `addr`, if any. */
static int guest_decommitted(uint32_t addr, uint32_t *base, uint32_t *size) {
  int i;
  for (i = 0; i < g_ndecommit; i++)
    if (addr >= g_decommit[i].base &&
        addr - g_decommit[i].base < g_decommit[i].size) {
      if (base)
        *base = g_decommit[i].base;
      if (size)
        *size = g_decommit[i].size;
      return 1;
    }
  return 0;
}

static void decommit_note(uint32_t base, uint32_t size) {
  int i;
  for (i = 0; i < g_ndecommit; i++) /* already recorded */
    if (g_decommit[i].base == base && g_decommit[i].size == size)
      return;
  if (g_ndecommit == MAX_DECOMMIT) {
    /* Counted and reported rather than dropped silently: a decommit this
       host forgets is one VirtualQuery will call committed again. */
    g_decommit_lost++;
    return;
  }
  g_decommit[g_ndecommit].base = base;
  g_decommit[g_ndecommit].size = size;
  g_ndecommit++;
}

/* A commit over decommitted pages takes them off the list. Only whole entries
   are dropped: a partial re-commit leaves the entry, which errs toward
   reporting RESERVE for something committed -- the safe direction, because the
   guest then asks again rather than using memory it was wrongly promised. */
static void decommit_clear(uint32_t base, uint32_t len) {
  int i;
  for (i = 0; i < g_ndecommit;) {
    if (g_decommit[i].base >= base &&
        g_decommit[i].base + g_decommit[i].size <= base + len)
      g_decommit[i] = g_decommit[--g_ndecommit];
    else
      i++;
  }
}

static int guest_reserved_span(uint32_t addr, uint32_t *base, uint32_t *size) {
  int i;
  for (i = 0; i < g_nreserved; i++)
    if (addr >= g_reserved[i].base &&
        addr - g_reserved[i].base < g_reserved[i].size) {
      *base = g_reserved[i].base;
      *size = g_reserved[i].size;
      return 1;
    }
  return 0;
}

void imp_KERNEL32_VirtualAlloc(CPU *C) {
  uint32_t addr = A(0), size = A(1), type = A(2), p;
  if (verbose())
    x2_log_error("[mem] VirtualAlloc(0x%08x, %u = %.1f MB, type 0x%x)\n", addr,
                 size, size / 1048576.0, type);
  if (addr) {
    /* A fixed address is directly implementable here in a way it is not in
       an emulator: guest addresses ARE host addresses, so the request can
       be passed to mmap as-is. The game asks for one to place its own
       heap. Rounded out to pages, because mmap requires it and Win32 does
       it silently. */
    uint32_t base = addr & ~0xFFFu;
    uint32_t len = ((addr - base) + size + 0xFFFu) & ~0xFFFu;
    if (guest_memory_map_fixed(base, len, PROT_READ | PROT_WRITE) != 0) {
      /* Already mapped: fine ONLY if the guest reserved it. Anything
         else is our own memory and must be refused. */
      if (errno == EEXIST && guest_reserved(base, len)) {
        /*
         * A COMMIT over a reservation this host already mapped.
         *
         * The pages may have been DECOMMITTED since, which mprotects
         * them PROT_NONE -- so returning success without restoring
         * access makes a decommit permanent, and the guest faults on
         * the memory Win32 just told it it had. Issue #41.
         */
        if (guest_memory_protect(base, len, PROT_READ | PROT_WRITE) != 0)
          x2_log_error("kernel32: VirtualAlloc could not restore "
                       "access to 0x%08x+%u: %s\n",
                       base, len, strerror(errno));
        decommit_clear(base, len);
        ret_std(C, addr, 4);
        return;
      }
      if (errno == EEXIST)
        x2_log_error("kernel32: VirtualAlloc(0x%08x, %u) collides "
                     "with memory the guest never reserved -- that "
                     "is the runtime's, and granting it would hand "
                     "the game our own heap or a mapped module\n",
                     base, size);
      else
        x2_log_error("kernel32: VirtualAlloc could not place %u "
                     "bytes at 0x%08x: %s\n",
                     size, base, strerror(errno));
      k32_set_last_error(8u); /* ERROR_NOT_ENOUGH_MEMORY */
      ret_std(C, 0, 4);
      return;
    }
    if (g_nreserved < MAX_RESERVED) {
      g_reserved[g_nreserved].base = base;
      g_reserved[g_nreserved].size = len;
      g_nreserved++;
    }
    g_reserved_bytes += len;
    /* The game is told 512 MB exists (GlobalMemoryStatus). Reserving far
       past that is not a memory-pressure problem, it is a runaway loop,
       and saying so beats silently consuming the address space until
       something unrelated collides. */
    if (g_reserved_bytes > X2_PHYS_BYTES) {
      /* Refuse, do not abort. GlobalMemoryStatus told the guest how much
         memory exists; a game that allocates until allocation fails is
         doing the normal thing, and the two APIs have to agree or the
         budget it was given means nothing. Failing here is the honest
         answer to "is there more?" -- and it is what makes such a loop
         terminate. */
      if (verbose())
        x2_log_error("[mem] refusing: %.0f MB reserved already, and "
                     "GlobalMemoryStatus reports %.0f MB of "
                     "physical memory\n",
                     g_reserved_bytes / 1048576.0, X2_PHYS_BYTES / 1048576.0);
      guest_memory_release(base, len);
      k32_set_last_error(8u); /* ERROR_NOT_ENOUGH_MEMORY */
      ret_std(C, 0, 4);
      return;
    }
    ret_std(C, addr, 4);
    return;
  }
  if (!(type & MEM_COMMIT)) {
    /*
     * MEM_RESERVE with no address: reserve address space, commit nothing.
     *
     * This used to abort as "needs real page semantics". It does, and the
     * pieces now exist: the reservation table below, the decommit table,
     * and the MEM_COMMIT-over-a-reservation path above that mprotects
     * access back. What was missing was only somewhere to PUT a
     * reservation the caller did not place itself.
     *
     * It has to land in the low 4 GB, because the guest stores the
     * pointer, and it must not collide with the mapped modules
     * (GUEST_IMAGE_BASE and GUEST_MODULE_LO+) or the runtime's own
     * arena (GUEST_RUNTIME_BASE and up). The window between them is for
     * this, walked with MAP_FIXED_NOREPLACE so a collision is refused by
     * the kernel rather than found later by the guest.
     *
     * PROT_NONE is the point: reserved-but-not-committed memory must FAULT
     * on access. Mapping it readable would make the difference between
     * reserve and commit invisible, which is exactly the bug the old abort
     * was there to avoid.
     */
    const uint32_t RES_LO = GUEST_RESERVE_LO, RES_HI = GUEST_RESERVE_HI;
    static uint32_t next = GUEST_RESERVE_LO;
    uint32_t len = (size + 0xFFFu) & ~0xFFFu;
    int tries;
    if (!len) {
      k32_set_last_error(87u);
      ret_std(C, 0, 4);
      return;
    }
    for (tries = 0; tries < 64; tries++) {
      if (next + len > RES_HI || next + len < next)
        next = RES_LO;
      if (guest_memory_map_fixed(next, len, PROT_NONE) == 0) {
        uint32_t base = next;
        next += len;
        if (g_nreserved < MAX_RESERVED) {
          g_reserved[g_nreserved].base = base;
          g_reserved[g_nreserved].size = len;
          g_nreserved++;
        } else {
          /* Untracked: a later MEM_COMMIT over it would be refused
             as "memory the guest never reserved". Said rather than
             left to surface as that unrelated-looking message. */
          x2_log_error("kernel32: the reservation table is full "
                       "(%d); 0x%08x+%u is mapped but NOT tracked, "
                       "so committing it later will be refused.\n",
                       MAX_RESERVED, base, len);
        }
        if (verbose())
          x2_log_error("[mem] reserved 0x%08x+%u (PROT_NONE; it "
                       "faults until committed)\n",
                       base, len);
        ret_std(C, base, 4);
        return;
      }
      next += 0x100000u; /* step past whatever is there */
    }
    x2_log_error("kernel32: VirtualAlloc could not RESERVE %u bytes "
                 "anywhere in 0x%08x-0x%08x after 64 attempts. That "
                 "window is the only 32-bit space not already holding a "
                 "module or the runtime arena.\n",
                 len, RES_LO, RES_HI);
    k32_set_last_error(8u); /* ERROR_NOT_ENOUGH_MEMORY */
    ret_std(C, 0, 4);
    return;
  }
  p = guest_malloc(size);
  if (p)
    memset(guest_memory_pointer(p), 0, size); /* VirtualAlloc zeroes */
  ret_std(C, p, 4);
}

/*
 * VirtualFree -- the counterpart of the mmap VirtualAlloc does, NOT a guest
 * heap free.
 *
 * It used to call guest_free(), which is wrong in a way that only showed up
 * once the game ran far enough to release memory: VirtualAlloc never allocates
 * from the guest heap, it mmaps, so every VirtualFree handed guest_free a
 * pointer it had never issued. guest_free caught it -- "free of a pointer
 * outside the guest heap" -- and that refusal is the only reason this was
 * found rather than corrupting the heap's free list.
 */
void imp_KERNEL32_VirtualFree(CPU *C) {
  /* (lpAddress, dwSize, dwFreeType) */
  uint32_t addr = A(0), size = A(1), type = A(2);
  const uint32_t MEM_DECOMMIT = 0x4000u, MEM_RELEASE = 0x8000u;
  int i;
  if (verbose())
    x2_log_error("[mem] VirtualFree(0x%08x, %u, type 0x%x)\n", addr, size,
                 type);
  if (!addr) {
    /* Win32 fails this too, but quietly: freeing NULL is an ordinary no-op
       in cleanup code and does not deserve a report that reads like a
       defect. */
    k32_set_last_error(487u);
    ret_std(C, 0, 3);
    return;
  }
  if (type & MEM_RELEASE) {
    /* Win32 requires dwSize == 0 and releases the WHOLE reservation, so
       the size comes from the table rather than from the caller. */
    if (size != 0) {
      x2_log_error("kernel32: VirtualFree(MEM_RELEASE) with size %u; "
                   "Win32 requires 0 and releases the whole "
                   "reservation\n",
                   size);
      k32_set_last_error(87u);
      ret_std(C, 0, 3);
      return;
    }
    for (i = 0; i < g_nreserved; i++)
      if (g_reserved[i].base == addr) {
        guest_memory_release(addr, g_reserved[i].size);
        g_reserved_bytes -= g_reserved[i].size;
        g_reserved[i] = g_reserved[--g_nreserved];
        ret_std(C, 1, 3);
        return;
      }
    x2_log_error("kernel32: VirtualFree(MEM_RELEASE) of 0x%08x, which "
                 "this host never reserved -- refusing rather than "
                 "unmapping something it does not own\n",
                 addr);
    k32_set_last_error(487u); /* ERROR_INVALID_ADDRESS */
    ret_std(C, 0, 3);
    return;
  }
  if (type & MEM_DECOMMIT) {
    /* Decommitted pages must fault on access; the reservation stays, so
       VirtualQuery still reports the range as the guest's. Leaving them
       readable would let a use-after-decommit read stale data silently. */
    uint32_t base = addr & ~0xFFFu;
    uint32_t len = ((addr - base) + size + 0xFFFu) & ~0xFFFu;
    if (len && guest_memory_protect(base, len, PROT_NONE) != 0)
      x2_log_error("kernel32: VirtualFree(MEM_DECOMMIT) could not "
                   "protect 0x%08x+%u: %s\n",
                   base, len, strerror(errno));
    else if (len)
      decommit_note(base, len);
    ret_std(C, 1, 3);
    return;
  }
  x2_log_error("kernel32: VirtualFree(0x%08x) with type 0x%x, which is "
               "neither MEM_DECOMMIT nor MEM_RELEASE\n",
               addr, type);
  k32_set_last_error(87u);
  ret_std(C, 0, 3);
}

void imp_KERNEL32_GlobalMemoryStatus(CPU *C) {
  /* MEMORYSTATUS. Reported consistently: the game sizes caches from it, so
     the numbers have to agree with each other even though they are not the
     host's real figures. 512 MB total, half free. */
  const uint32_t p = A(0);
  const uint32_t avail = (uint32_t)(g_reserved_bytes < X2_PHYS_BYTES
                                        ? X2_PHYS_BYTES - g_reserved_bytes
                                        : 0u);
  WR32(p + 0u, 32);                       /* dwLength */
  WR32(p + 4u, 50);                       /* dwMemoryLoad, percent */
  WR32(p + 8u, (uint32_t)X2_PHYS_BYTES);  /* dwTotalPhys */
  WR32(p + 12u, avail);                   /* dwAvailPhys */
  WR32(p + 16u, (uint32_t)X2_PHYS_BYTES); /* dwTotalPageFile */
  WR32(p + 20u, avail);                   /* dwAvailPageFile */
  WR32(p + 24u, 0x7FFF0000u);             /* dwTotalVirtual */
  WR32(p + 28u, 0x40000000u);             /* dwAvailVirtual */
  ret_std(C, 0, 1);
}

void imp_KERNEL32_VirtualQuery(CPU *C) {
  /* MEMORY_BASIC_INFORMATION, for a guest address. The game uses it to ask
     "is this pointer valid and how big is the region", so the answer has to
     be about the GUEST address space, not the host's -- a host VirtualQuery
     equivalent would describe mappings the guest cannot see and would call
     our own runtime's memory "committed" to the guest.
     Only the fields the game reads are filled, and the rest are zeroed
     rather than left as whatever was in the buffer. */
  uint32_t addr = A(0), buf = A(1), len = A(2);
  X86Module *m;
  uint32_t base = 0, size = 0, state, protect;
  /* 0 past the layout, as Windows answers past lpMaximumApplicationAddress:
     nothing is ever placed there, so no span of it can be offered as free. */
  if (len < 28u || addr >= GUEST_LAYOUT_LIMIT) {
    ret_std(C, 0, 3);
    return;
  }
  m = x86_module_for(addr);
  if (m) {
    base = *m->base;
    size = m->size;
    state = 0x1000u;
    protect = 0x02u;
  } else if (guest_decommitted(addr, &base, &size)) {
    /* DECOMMITTED: reserved, not committed, and not accessible. Windows
       answers MEM_RESERVE here with the run of pages in that state, and so
       does this -- the alternative is what issue #41 was, a guest told its
       memory was committed reading a page this host had mprotected away. */
    state = 0x2000u;
    protect = 0x01u; /* RESERVE, NOACCESS */
  } else if (guest_reserved_span(addr, &base, &size)) {
    /* Memory the guest itself reserved through VirtualAlloc, and not
       decommitted since (checked above). It is mapped PROT_READ|PROT_WRITE,
       so COMMIT is what this host actually did. */
    state = 0x1000u;
    protect = 0x04u;
  } else {
    /* Not in a module. The guest heap and stacks are committed and
       writable; anything else is genuinely unmapped as far as the guest is
       concerned, and saying so is the useful answer. */
    extern int guest_heap_contains(uint32_t a, uint32_t *b, uint32_t *n);
    if (guest_heap_contains(addr, &base, &size)) {
      state = 0x1000u;
      protect = 0x04u;
    } else {
      /* Neither: the page table decides. The span must be the whole run,
         not one page -- a caller walking the address space advances by
         RegionSize, and libIGCore's heap scan sat in a million-iteration
         loop when this said 4 KB. A mapped page here is the runtime's own
         (a stack, the TIB, a file view): occupied, and not the guest's to
         reserve over, which is what running its arena walk into our stack
         at 0x30000000 was. */
      int mapped;
      base = addr & ~0xFFFu;
      size = guest_memory_run(base, &mapped);
      state = mapped ? 0x2000u : 0x10000u; /* MEM_RESERVE : MEM_FREE */
      protect = 0x01u;                     /* PAGE_NOACCESS */
    }
  }
  if (verbose())
    x2_log_error("[mem] VirtualQuery(0x%08x) -> base 0x%08x size %u "
                 "(%.1f MB) state %s\n",
                 addr, base, size, size / 1048576.0,
                 state == 0x10000u  ? "FREE"
                 : state == 0x2000u ? "RESERVE"
                                    : "COMMIT");
  memset(guest_memory_pointer(buf), 0, 28);
  WR32(buf + 0u, base);        /* BaseAddress */
  WR32(buf + 4u, base);        /* AllocationBase */
  WR32(buf + 8u, protect);     /* AllocationProtect */
  WR32(buf + 12u, size);       /* RegionSize */
  WR32(buf + 16u, state);      /* State */
  WR32(buf + 20u, protect);    /* Protect */
  WR32(buf + 24u, 0x1000000u); /* Type: MEM_IMAGE/PRIVATE */
  ret_std(C, 28, 3);
}
