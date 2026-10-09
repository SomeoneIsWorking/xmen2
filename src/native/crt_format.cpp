#include "crt_format.h"

#include "crt_internal.h"
#include "guest_memory.h"
#include "x2_log.h"
#include "x86rt.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace x2::native {

/* ---- the format walker -------------------------------------------------
 *
 * A va_list on x86-32 cdecl IS a pointer into the guest stack: arguments are
 * pushed right-to-left, each padded to 4 bytes, doubles taking 8. So the
 * varargs family does not need a synthesised va_list at all -- it needs this,
 * a walk of the format string that pulls each argument from guest memory by
 * hand and formats it one directive at a time with the host's snprintf.
 *
 * Everything it cannot handle STOPS by name. That matters more here than
 * usual: the alternative to refusing an unknown conversion is emitting
 * something plausible into a string the game then uses as a path, a key or a
 * displayed line, and the damage would surface far away from the cause.
 */
static uint32_t g_vfmt_va; /* the walking guest va pointer */

static uint32_t va_dword(void) {
  uint32_t v = RD32(g_vfmt_va);
  g_vfmt_va += 4u;
  return v;
}
static uint64_t va_qword(void) {
  uint64_t lo = RD32(g_vfmt_va), hi = RD32(g_vfmt_va + 4u);
  g_vfmt_va += 8u;
  return lo | (hi << 32);
}
static double va_double(void) {
  union {
    uint64_t u;
    double d;
  } u;
  u.u = va_qword();
  return u.d;
}

/*
 * Whether MSVCR71's _output classifies ch as CH_OTHER. Its __lookuptable
 * (msvcr71.dll file offset 0x40958, ' '..'x') sends CH_OTHER from every
 * directive state back to ST_NORMAL, which writes the character literally;
 * characters outside the table are CH_OTHER too. Everything else after '%' is
 * a flag, width, precision, size or type the walker must handle by name.
 */
static int msvcr71_printf_other(char ch) {
  return ch != 0 && !strchr("%.*0123456789 #+-"    /* directive syntax */
                            "FILNhlw"              /* CH_SIZE */
                            "BCEGSXZcdefginopsux", /* CH_TYPE */
                            ch);
}

/*
 * Returns the length that WOULD have been written (snprintf semantics), and
 * writes at most cap bytes including the NUL. MSVC's _snprintf family differs
 * from C99 -- it returns -1 on truncation and does not always NUL-terminate --
 * so the callers below adjust rather than this doing it two ways.
 */
int guest_vformat(char *out, size_t cap, const char *fmt, uint32_t va) {
  size_t used = 0;
  char spec[64], tmp[512];
  const char *p = fmt;
  g_vfmt_va = va;
  if (!fmt) {
    crt_unimpl("_vsnprintf", "the format string pointer is NULL");
    return -1;
  }
  while (*p) {
    int n = 0, si = 0, star_w = 0, star_p = 0, lng = 0;
    if (*p != '%') {
      if (used + 1 < cap)
        out[used] = *p;
      used++;
      p++;
      continue;
    }
    spec[si++] = *p++; /* '%' */
    if (*p == '%') {
      if (used + 1 < cap)
        out[used] = '%';
      used++;
      p++;
      continue;
    }
    while (*p && strchr("-+ #0", *p) && si < 40)
      spec[si++] = *p++; /* flags */
    if (*p == '*') {
      star_w = 1;
      p++;
    } else
      while (*p >= '0' && *p <= '9' && si < 40)
        spec[si++] = *p++; /* width */
    if (*p == '.') {
      spec[si++] = *p++;
      if (*p == '*') {
        star_p = 1;
        p++;
      } else
        while (*p >= '0' && *p <= '9' && si < 50)
          spec[si++] = *p++;
    }
    /* length modifiers; MSVC also spells 64-bit as I64 */
    if (p[0] == 'I' && p[1] == '6' && p[2] == '4') {
      lng = 2;
      p += 3;
    } else if (p[0] == 'l' && p[1] == 'l') {
      lng = 2;
      p += 2;
    } else if (*p == 'l' || *p == 'L') {
      lng = (*p == 'L') ? 3 : 1;
      p++;
    } else if (*p == 'h') {
      lng = -1;
      p++;
    }
    if (star_w) {
      int w = (int)va_dword();
      si += snprintf(spec + si, sizeof spec - si, "%d", w);
    }
    if (star_p) {
      int pr = (int)va_dword();
      si += snprintf(spec + si, sizeof spec - si, ".%d", pr);
    }
    switch (*p) {
    case 'd':
    case 'i':
    case 'u':
    case 'x':
    case 'X':
    case 'o':
      if (lng == 2) {
        spec[si++] = 'l';
        spec[si++] = 'l';
        spec[si++] = *p;
        spec[si] = 0;
        n = snprintf(tmp, sizeof tmp, spec, (long long)va_qword());
      } else {
        spec[si++] = *p;
        spec[si] = 0;
        n = snprintf(tmp, sizeof tmp, spec, (int)va_dword());
      }
      break;
    case 'f':
    case 'F':
    case 'e':
    case 'E':
    case 'g':
    case 'G':
      spec[si++] = *p;
      spec[si] = 0;
      n = snprintf(tmp, sizeof tmp, spec, va_double());
      break;
    case 'c':
      spec[si++] = 'c';
      spec[si] = 0;
      n = snprintf(tmp, sizeof tmp, spec, (int)(va_dword() & 0xFF));
      break;
    case 'p':
      spec[si++] = 'p';
      spec[si] = 0;
      n = snprintf(tmp, sizeof tmp, spec, (void *)(uintptr_t)va_dword());
      break;
    case 's': {
      uint32_t sp = va_dword();
      spec[si++] = 's';
      spec[si] = 0;
      /* A NULL string is printed as MSVC prints it rather than crashing
         the host on a guest bug. */
      n = snprintf(tmp, sizeof tmp, spec,
                   sp ? guest_memory_const_pointer(sp) : "(null)");
      break;
    }
    default:
      if (msvcr71_printf_other(*p)) {
        /* MSVCR71 drops the pending directive and prints the character
           itself (XMen2.exe's "%s%[%s]%s" relies on this for a literal
           '['). Star arguments were already consumed, as MSVCR71 does. */
        if (used + 1 < cap)
          out[used] = *p;
        used++;
        p++;
        continue;
      }
      /* Refuse: see the header comment. The conversion is named so the
         next one can be added deliberately. */
      x2_log_error("crt: format walker met %%%c, which it does not "
                   "implement -- refusing rather than inventing "
                   "output for it\n",
                   *p ? *p : '?');
      crt_unimpl("_vsnprintf", "unimplemented printf conversion");
      return -1;
    }
    p++;
    if (n < 0)
      return -1;
    {
      int i;
      for (i = 0; i < n; i++) {
        if (used + 1 < cap)
          out[used] = tmp[i];
        used++;
      }
    }
  }
  if (cap)
    out[used < cap ? used : cap - 1] = 0;
  return (int)used;
}

/* ---- the scanf walker --------------------------------------------------
 *
 * The mirror of guest_vformat: one directive at a time, with the host's sscanf
 * doing the actual conversion and "%n" reporting how much input it consumed, so
 * the position advances by what really matched rather than by a guess.
 *
 * The results go to POINTERS pulled from the guest stack, and the width of each
 * store matters -- writing four bytes for a %hd would corrupt whatever follows
 * it in the guest's struct. So each conversion writes exactly its own size.
 *
 * As with the printf side, a conversion this does not implement STOPS by name.
 * A scanf that silently matches nothing returns a count the caller believes,
 * and the caller then uses uninitialised locals.
 */
int guest_vsscanf(const char *in, const char *fmt, uint32_t va) {
  int filled = 0, pos = 0, n;
  const char *p = fmt;
  /* Large enough for a real scanset: the engine parses identifiers with
     %[_a-zA-Z0-9./\-] spelled out in full, which is 67 characters. At 64
     this refused with "unterminated scanset" -- the right refusal for the
     wrong reason, and it would have been read as a malformed format in the
     game rather than a small buffer here. */
  char spec[320];
  if (!in || !fmt) {
    crt_unimpl("sscanf", "the input or format pointer is NULL");
    return -1;
  }
  g_vfmt_va = va;
  while (*p) {
    if (isspace((unsigned char)*p)) {
      while (isspace((unsigned char)in[pos]))
        pos++;
      p++;
      continue;
    }
    if (*p != '%') {
      /* A literal must match, and a mismatch ends the scan -- that is
         how the caller learns the input was not what it expected. */
      if (in[pos] != *p)
        return filled;
      pos++;
      p++;
      continue;
    }
    p++;
    if (*p == '%') {
      if (in[pos] != '%')
        return filled;
      pos++;
      p++;
      continue;
    }
    {
      int suppress = 0, width = 0, lng = 0, si = 0, consumed = 0;
      if (*p == '*') {
        suppress = 1;
        p++;
      }
      while (*p >= '0' && *p <= '9')
        width = width * 10 + (*p++ - '0');
      if (p[0] == 'l' && p[1] == 'l') {
        lng = 2;
        p += 2;
      } else if (*p == 'l' || *p == 'L') {
        lng = 1;
        p++;
      } else if (*p == 'h') {
        lng = -1;
        p++;
      }

      spec[si++] = '%';
      if (width)
        si += snprintf(spec + si, sizeof spec - si, "%d", width);

      switch (*p) {
      case 'd':
      case 'i':
      case 'u':
      case 'x':
      case 'X':
      case 'o': {
        long long v = 0;
        spec[si++] = 'l';
        spec[si++] = 'l';
        spec[si++] = *p;
        snprintf(spec + si, sizeof spec - si, "%%n");
        n = sscanf(in + pos, spec, &v, &consumed);
        if (n < 1)
          return filled;
        pos += consumed;
        if (!suppress) {
          uint32_t dst = va_dword();
          if (lng == 2) {
            WR32(dst, (uint32_t)v);
            WR32(dst + 4u, (uint32_t)((uint64_t)v >> 32));
          } else if (lng == -1)
            WR16(dst, (uint16_t)v);
          else
            WR32(dst, (uint32_t)v);
          filled++;
        }
        break;
      }
      case 'f':
      case 'e':
      case 'g':
      case 'E':
      case 'G': {
        double v = 0;
        spec[si++] = 'l';
        spec[si++] = 'f';
        snprintf(spec + si, sizeof spec - si, "%%n");
        n = sscanf(in + pos, spec, &v, &consumed);
        if (n < 1)
          return filled;
        pos += consumed;
        if (!suppress) {
          uint32_t dst = va_dword();
          /* `%f` stores a float and `%lf` a double -- a four-byte
             store for a double would leave half the value behind. */
          if (lng) {
            double dv = v;
            memcpy(guest_memory_pointer(dst), &dv, 8);
          } else {
            float fv = (float)v;
            memcpy(guest_memory_pointer(dst), &fv, 4);
          }
          filled++;
        }
        break;
      }
      case 's': {
        char buf[512];
        snprintf(spec + si, sizeof spec - si, "s%%n");
        n = sscanf(in + pos, spec, buf, &consumed);
        if (n < 1)
          return filled;
        pos += consumed;
        if (!suppress) {
          uint32_t dst = va_dword();
          memcpy(guest_memory_pointer(dst), buf, strlen(buf) + 1);
          filled++;
        }
        break;
      }
      case '[': {
        /* A scanset, `%[abc]` or `%[^\n]`. The host's sscanf
           implements these, so the bracket expression is copied
           through verbatim -- including the two places where a `]` is
           a literal member rather than the terminator: immediately
           after `[` and immediately after `[^`. */
        char buf[512];
        const char *q = p + 1;
        int depth_ok = 0;
        spec[si++] = '[';
        if (*q == '^')
          spec[si++] = *q++;
        if (*q == ']')
          spec[si++] = *q++;
        while (*q && *q != ']') {
          if (si >= (int)sizeof spec - 8)
            break;
          spec[si++] = *q++;
        }
        if (*q == ']') {
          spec[si++] = ']';
          q++;
          depth_ok = 1;
        }
        if (!depth_ok) {
          x2_log_error("crt: scanf walker met an unterminated "
                       "%%[ scanset in format \"%s\" -- "
                       "refusing\n",
                       fmt);
          crt_unimpl("sscanf", "unterminated scanset");
          return -1;
        }
        snprintf(spec + si, sizeof spec - si, "%%n");
        n = sscanf(in + pos, spec, buf, &consumed);
        if (n < 1)
          return filled;
        pos += consumed;
        if (!suppress) {
          uint32_t dst = va_dword();
          memcpy(guest_memory_pointer(dst), buf, strlen(buf) + 1);
          filled++;
        }
        p = q - 1; /* the switch advances past *p below */
        break;
      }
      case 'c': {
        int w = width ? width : 1, k;
        if (!in[pos])
          return filled;
        if (!suppress) {
          uint32_t dst = va_dword();
          for (k = 0; k < w && in[pos + k]; k++)
            WR8(dst + (uint32_t)k, (uint8_t)in[pos + k]);
          filled++;
        }
        for (k = 0; k < w && in[pos]; k++)
          pos++;
        break;
      }
      default:
        x2_log_error("crt: scanf walker met %%%c in format \"%s\", "
                     "which it does not implement -- refusing rather "
                     "than reporting a match it did not make\n",
                     *p ? *p : '?', fmt);
        crt_unimpl("sscanf", "unimplemented scanf conversion");
        return -1;
      }
      p++;
    }
  }
  return filled;
}

} // namespace x2::native
