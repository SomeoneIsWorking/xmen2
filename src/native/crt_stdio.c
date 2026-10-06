#include "x2_log.h"

/*
 * The guest's stdio: the FILE* handle table, `_iob`, and the import shims
 * that use them.
 *
 * Split out of crt.c, which had grown to hold the allocator, the string and
 * math families, the format and scanf walkers, RTTI and startup as well. The
 * handle table has its own invariant -- a guest FILE* is either a small
 * handle this file owns or a pointer into the `_iob` array -- and nothing
 * else in the CRT shim touches it.
 */
#include "crt_console.h"
#include "crt_internal.h"
#include "crt_stdio.h"
#include "guest_file_io.h"
#include "guest_heap.h"
#include "guest_memory.h"
#include "host_dir_cache.h"
#include "win_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define AS(i) ((char *)guest_memory_pointer(A(i)))
#define ACS(i) ((const char *)guest_memory_const_pointer(A(i)))

/* ---- stdio -------------------------------------------------------------
 *
 * A FILE * does not fit in a guest pointer on x86-64, so the guest gets a
 * small handle and this side keeps the table. Handles start at 1 so that 0
 * stays "failed", which is what the caller tests.
 */
/* Defined further down (the format walker) and in win_path.c (path
   translation); declared here so stdio can use both. */
int guest_vformat(char *out, size_t cap, const char *fmt, uint32_t va);

#define MAX_FILES 64
static FILE *g_files[MAX_FILES];

/*
 * _iob -- the guest's stdin/stdout/stderr.
 *
 * MSVCRT exports it as DATA, and the guest reaches a standard stream by taking
 * the address of an element: stderr is &_iob[2]. So a FILE* here is EITHER one
 * of our small handles from fopen, or a pointer into that array, and crt_file()
 * has to accept both. They cannot be confused: a handle is 1..64 and the array
 * lives on the guest heap.
 *
 * MSVC's FILE (struct _iobuf) is 32 bytes. Nothing here reads its fields -- the
 * array exists so the ADDRESS arithmetic works and the pointer can be
 * recognised -- so it is zeroed rather than filled with a fake buffer.
 */
#define IOB_N 3
#define IOB_SIZEOF_FILE 32u
static uint32_t g_iob; /* guest address of _iob[0] */

uint32_t crt_iob_base(void) {
  if (!g_iob) {
    g_iob = guest_malloc(IOB_N * IOB_SIZEOF_FILE);
    if (!g_iob) {
      x2_log_error("crt: could not allocate _iob on the guest heap\n");
      abort();
    }
    memset(guest_memory_pointer(g_iob), 0, IOB_N * IOB_SIZEOF_FILE);
  }
  return g_iob;
}

FILE *crt_file(uint32_t h) {
  if (g_iob && h >= g_iob && h < g_iob + IOB_N * IOB_SIZEOF_FILE) {
    uint32_t i = (h - g_iob) / IOB_SIZEOF_FILE;
    /* stdin is not readable in this host -- it is not connected to
       anything -- so a read from it must not silently return EOF as if the
       stream were merely empty. */
    return i == 0 ? stdin : i == 1 ? stdout : stderr;
  }
  if (h == 0 || h > MAX_FILES || !g_files[h - 1]) {
    x2_log_error("crt: file handle %u is not open, and it is not a "
                 "pointer into _iob either\n",
                 h);
    abort();
  }
  return g_files[h - 1];
}

void imp_MSVCR71_fopen(CPU *C) {
  int i;
  for (i = 0; i < MAX_FILES; i++) {
    if (g_files[i])
      continue;
    /*
     * Through the SAME resolver CreateFileA uses (kernel32.c), not just
     * win_path: it is where case-insensitive resolution, the X2_ASSETS
     * replacement pack and the what-did-this-run-open instrument all live.
     * This path was outside all three, and it is the one the ENGINE loads
     * its assets through -- so a run reported 31 files while it was
     * reading fonts, models and packages that never appeared.
     */
    const char *guest = ACS(0), *mode = ACS(1);
    int wr =
        mode && (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+'));
    int repl = k32_open_replaced(guest, wr);
    const char *host = k32_open_path(guest, wr);
    g_files[i] = fopen(host, mode);
    if (wr)
      host_dir_forget_for(host);
    k32_open_note(guest, g_files[i] != NULL, repl, host);
    ret_c(C, g_files[i] ? (uint32_t)(i + 1) : 0u);
    return;
  }
  x2_log_error("crt: more than %d files open at once\n", MAX_FILES);
  abort();
}

void imp_MSVCR71_fclose(CPU *C) {
  uint32_t h = A(0);
  int rc = fclose(crt_file(h));
  g_files[h - 1] = NULL;
  ret_c(C, (uint32_t)rc);
}

void imp_MSVCR71_fread(CPU *C) {
  ret_c(C, (uint32_t)x2_guest_fread(A(0), A(1), A(2), crt_file(A(3))));
}

void imp_MSVCR71_fseek(CPU *C) {
  ret_c(C, (uint32_t)fseek(crt_file(A(0)), (long)(int32_t)A(1), (int)A(2)));
}

void imp_MSVCR71_ftell(CPU *C) { ret_c(C, (uint32_t)ftell(crt_file(A(0)))); }

void imp_MSVCR71__mkdir(CPU *C) {
  const char *path = win_path(ACS(0));
  int rc = mkdir(path, 0777);
  host_dir_forget_for(path);
  ret_c(C, (uint32_t)rc);
}

/* ---- the rest of stdio -------------------------------------------------- */

void imp_MSVCR71_fflush(CPU *C) {
  FILE *f = A(0) ? crt_file(A(0)) : NULL;
  if (!f || crt_console_is(f)) {
    crt_console_flush();
  }
  ret_c(C, (uint32_t)(f && !crt_console_is(f) ? fflush(f) : 0));
}
/* The two formatted writers share this; the console is a log, not a stream. */
static void console_or_stream(FILE *f, const char *text) {
  if (crt_console_is(f)) {
    crt_console_write(text, strlen(text));
    return;
  }
  fputs(text, f);
}

void imp_MSVCR71_fputc(CPU *C) {
  FILE *f = crt_file(A(1));
  char c = (char)A(0);
  if (crt_console_is(f)) {
    crt_console_write(&c, 1u);
    ret_c(C, A(0));
    return;
  }
  ret_c(C, (uint32_t)fputc((int)A(0), f));
}
void imp_MSVCR71_fputs(CPU *C) {
  const char *text = ACS(0);
  FILE *f = crt_file(A(1));
  if (crt_console_is(f)) {
    crt_console_write(text, strlen(text));
    ret_c(C, 0);
    return;
  }
  ret_c(C, (uint32_t)fputs(text, f));
}
void imp_MSVCR71_fgetc(CPU *C) { ret_c(C, (uint32_t)fgetc(crt_file(A(0)))); }
void imp_MSVCR71_ungetc(CPU *C) {
  ret_c(C, (uint32_t)ungetc((int)A(0), crt_file(A(1))));
}
void imp_MSVCR71_fwrite(CPU *C) {
  FILE *f = crt_file(A(3));
  if (crt_console_is(f)) {
    const char *bytes = (const char *)guest_memory_const_pointer(A(0));
    size_t total = A(1) * A(2);
    if (bytes) {
      crt_console_write(bytes, total);
    }
    ret_c(C, A(2));
    return;
  }
  ret_c(C, (uint32_t)x2_guest_fwrite(A(0), A(1), A(2), f));
}
void imp_MSVCR71_fgets(CPU *C) {
  char *r = fgets(AS(0), (int)A(1), crt_file(A(2)));
  ret_c(C, r ? A(0) : 0u);
}
void imp_MSVCR71_fprintf(CPU *C) {
  char buf[4096];
  int n =
      guest_vformat(buf, sizeof buf, ACS(1), C->reg[kX86pEsp] + 4u + 2u * 4u);
  if (n >= 0) {
    console_or_stream(crt_file(A(0)), buf);
  }
  ret_c(C, (uint32_t)n);
}
void imp_MSVCR71_vfprintf(CPU *C) {
  char buf[4096];
  int n = guest_vformat(buf, sizeof buf, ACS(1), A(2));
  if (n >= 0) {
    console_or_stream(crt_file(A(0)), buf);
  }
  ret_c(C, (uint32_t)n);
}
