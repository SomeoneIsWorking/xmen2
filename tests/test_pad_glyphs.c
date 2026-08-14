#include "pad_glyphs.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define SIZE 0x00700000u
#define BUFFER_RVA 0x0066aec8u

static uint32_t mapped_base;
static X86Module module = { .name = "XMen2", .base = &mapped_base,
                            .preferred = 0x00400000u, .size = SIZE };
static int real_calls;

X86Module *x86_modules(void) { return &module; }
int dinput_pad_uses_xbox_glyphs(int pad) { return pad == 0; }
void __real_fn_XMen2_006281f0(CPU *c)
{
    real_calls++;
    c->eax = 0x12345678u;
    c->esp += 12u;
}
void x86_seg_unset(const char *seg) { (void)seg; abort(); }
__thread uint32_t g_fsbase, g_gsbase;

void __wrap_fn_XMen2_006281f0(CPU *c);

static int check_call(uint32_t kind, uint32_t code, uint32_t want,
                      int want_real)
{
    CPU c = {0};
    uint32_t *stack = (uint32_t *)(uintptr_t)(mapped_base + 0x1000u);
    int before = real_calls;
    stack[0] = 0xfeedfaceu;
    stack[1] = kind;
    stack[2] = code;
    c.esp = mapped_base + 0x1000u;
    __wrap_fn_XMen2_006281f0(&c);
    if (c.esp != mapped_base + 0x100cu || real_calls - before != want_real)
        return 0;
    if (want_real) return c.eax == 0x12345678u;
    return c.eax == mapped_base + BUFFER_RVA && RD8(c.eax) == want &&
           RD8(c.eax + 1u) == 0;
}

int main(int argc, char **argv)
{
    int ok;
    (void)argc;
    void *region = mmap(NULL, SIZE, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (region == MAP_FAILED || (uintptr_t)region > UINT32_MAX) {
        perror("test_pad_glyphs mmap");
        return 77;
    }
    mapped_base = (uint32_t)(uintptr_t)region;
    if (argc == 2 && strcmp(argv[1], "--disabled") == 0) {
        unsetenv("X2_PAD_GLYPHS");
        ok = check_call(3, 0x15, 0, 1);
        printf("pad glyph disabled gate: %s\n", ok ? "ok" : "FAIL");
        return ok ? 0 : 1;
    }

    setenv("X2_PAD_GLYPHS", "1", 1);
    ok = check_call(3, 0x15, 0x80, 0) && /* A */
         check_call(3, 5,    0x86, 0) && /* Z+ = LT */
         check_call(3, 6,    0x87, 0) && /* Z- = RT */
         check_call(3, 0x11, 0x8a, 0) && /* POV */
         check_call(4, 0x15, 0, 1) &&    /* non-Xbox slot */
         check_call(3, 0x1d, 0, 1);      /* LS has no authored glyph */
    if (!ok) {
        fprintf(stderr, "pad glyph shipping-wrapper checks FAILED\n");
        return 1;
    }
    printf("pad glyph wrapper: 6 enabled cases passed\n");
    return 0;
}
