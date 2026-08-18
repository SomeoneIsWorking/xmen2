/*
 * A census of every script XMen2.exe launches, by name.
 *
 * XMen2.exe FUN_004a1320 is the script manager's launch-by-name (its vtable
 * +0x3c): `bool launch(const char *name, int flag)`, RET 8, returning AL. Every
 * route into a BehavEd script arrives here -- the conversation manager's
 * launchScript, a level's entry script, and a script command that starts
 * another -- so one override sees them all.
 *
 * This is an instrument and it is written to be readable when the answer is
 * NOTHING: it reports its total with the distinct-name count beside it, names
 * the last script launched, and says so explicitly when no script ran at all.
 * "The level's opening act never ran" and "the trace was not compiled in" have
 * to look different, because the whole of the tutorial soft lock is one script
 * in a chain of five that never started.
 */
#include "script_trace.h"

#include "gpu_device.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NAME_MAX_LEN 96
#define NAMES_MAX    256

static struct {
    char name[NAME_MAX_LEN];
    unsigned long launches, failures;
    unsigned long first_frame;
} g_names[NAMES_MAX];

static int g_nnames;
static unsigned long g_launches, g_failures, g_overflow;
static char g_last[NAME_MAX_LEN];
static int g_live = -1;

static int live(void)
{
    if (g_live < 0) {
        const char *e = getenv("X2_SCRIPTS");
        g_live = e && *e && *e != '0';
    }
    return g_live;
}

static void copy_guest_string(char *dst, size_t cap, uint32_t s)
{
    size_t i = 0;
    if (!s) { snprintf(dst, cap, "(null)"); return; }
    for (i = 0; i + 1 < cap; i++) {
        uint8_t ch = RD8(s + (uint32_t)i);
        if (!ch) break;
        dst[i] = (char)ch;
    }
    dst[i] = '\0';
    if (!dst[0]) snprintf(dst, cap, "(empty)");
}

static void record(const char *name, int ok)
{
    int i;
    for (i = 0; i < g_nnames; i++)
        if (!strcmp(g_names[i].name, name)) break;
    if (i == g_nnames) {
        if (g_nnames == NAMES_MAX) { g_overflow++; return; }
        snprintf(g_names[i].name, sizeof g_names[i].name, "%s", name);
        g_names[i].first_frame = gpu_frames_presented();
        g_nnames++;
    }
    g_names[i].launches++;
    if (!ok) g_names[i].failures++;
}

void fn_XMen2_004a1320(CPU *C);

void x2_override_004a1320(CPU *C)
{
    char name[NAME_MAX_LEN];
    int ok;

    copy_guest_string(name, sizeof name, RD32(C->esp + 4u));
    fn_XMen2_004a1320(C);
    ok = (int)(C->eax & 0xFFu);

    g_launches++;
    if (!ok) g_failures++;
    snprintf(g_last, sizeof g_last, "%s", name);
    record(name, ok);
    if (live())
        fprintf(stderr, "SCRIPT: %-6s \"%s\" at frame %lu\n",
                ok ? "launch" : "FAILED", name, gpu_frames_presented());
}

/*
 * The startConversation script command.
 *
 * Its handler comes from the 289-entry script-command table, whose entries are
 * `{ handler, name, returnType, argSpec }` starting at 0x0068a908 -- the
 * address FUN_0049fe30 hands to the registrar. Splitting those four fields one
 * dword later gives plausible-looking names against the WRONG handlers, and
 * the tell is the argument spec: read correctly `lockControls` takes `f` and
 * `act` takes `aa`, which is what the scripts pass; read one field out they
 * come back empty. tools/script_commands.py extracts the table so this does
 * not have to be re-derived.
 *
 * It is traced separately because its body is guarded: at 0x004a5961 it tests
 * the participant it resolved and, when that is null, skips everything --
 * silently. A conversation that never starts and a command that was never
 * reached look identical from outside, and the tutorial soft lock is exactly
 * that ambiguity, so this records the call AND what the conversation manager's
 * flags did across it.
 */
#define CONV_SINGLETON_VA 0x00717aacu
#define CONV_FLAGS        0x21b24u

static unsigned long g_startconv, g_startconv_took, g_lockcontrols;

void fn_XMen2_004a5660(CPU *C);

static uint8_t conversation_flags(void)
{
    uint32_t self = 0;
    uint8_t f = 0;
    X86Module *m;
    for (m = x86_modules(); m; m = m->next)
        if (m->preferred == 0x00400000u && m->base && *m->base) {
            if (!x86_peek32(*m->base + (CONV_SINGLETON_VA - 0x00400000u),
                            &self) || !self)
                return 0xffu;              /* no singleton: distinct from 0 */
            x86_peek(self + CONV_FLAGS, &f, 1);
            return f;
        }
    return 0xffu;
}

static int conversation_field(uint32_t off, uint32_t *out)
{
    uint32_t self = 0;
    X86Module *m;
    *out = 0;
    for (m = x86_modules(); m; m = m->next)
        if (m->preferred == 0x00400000u && m->base && *m->base) {
            if (!x86_peek32(*m->base + (CONV_SINGLETON_VA - 0x00400000u),
                            &self) || !self)
                return 0;
            return x86_peek32(self + off, out);
        }
    return 0;
}

void x2_override_004a5660(CPU *C)
{
    uint8_t before = conversation_flags(), after;

    fn_XMen2_004a5660(C);
    after = conversation_flags();
    g_startconv++;
    if (after != before) g_startconv_took++;
    if (live())
        fprintf(stderr, "SCRIPT: startConversation at frame %lu -- "
                        "conversation flags 0x%02x -> 0x%02x%s\n",
                gpu_frames_presented(), before, after,
                after == before ? "  (NO CHANGE: the command ran and the "
                                  "conversation did not start)" : "");
}

/*
 * lockControls -- the CONTROL for the trace above.
 *
 * tutorial1.py calls lockControls(-1) at level entry and the game is
 * demonstrably locked afterwards, so this override MUST fire. If it does not,
 * the handler addresses read out of the command table are wrong and the
 * startConversation silence above says nothing about the game. An instrument
 * whose negative could equally mean "I am broken" is not evidence, and this is
 * what tells the two apart.
 */
void fn_XMen2_0049f8c0(CPU *C);

void x2_override_0049f8c0(CPU *C)
{
    g_lockcontrols++;
    if (live())
        fprintf(stderr, "SCRIPT: lockControls at frame %lu (call #%lu)\n",
                gpu_frames_presented(), g_lockcontrols);
    fn_XMen2_0049f8c0(C);
}

/*
 * igConversationManager::start(name, actorB, actorA) -- vtable +0x14,
 * FUN_0045c950, RET 0xc, returns success in AL.
 *
 * The script command above always forwards to this, so tracing the command
 * alone cannot say whether the conversation was refused or never asked for.
 * This function has four exits and three of them are silent refusals: a time
 * gate at +0x21b2c, "a conversation is already visible", and the name not
 * being in the level's table. Only the fourth returns true. Recording the name
 * with the result is what separates them.
 */
void fn_XMen2_0045c950(CPU *C);

static unsigned long g_convstart, g_convstart_ok, g_reset;

/*
 * The 160-bit "this line has already been said" bitmap at +0x21b48.
 * FUN_0045c460 tests one bit per line and sets it after playing. The index
 * starts at 0 WITHIN a conversation, so two conversations use the same bits
 * unless something clears the bitmap between them -- convmgr vt+0x50
 * (FUN_00458090) is the only code in the binary that does, and vt+0x04
 * (FUN_00455af0) is its only caller.
 */
#define CONV_SEEN 0x21b48u

static void seen_bitmap(uint32_t out[5])
{
    uint32_t self = 0, i;
    X86Module *m;
    for (i = 0; i < 5; i++) out[i] = 0;
    for (m = x86_modules(); m; m = m->next)
        if (m->preferred == 0x00400000u && m->base && *m->base) {
            if (!x86_peek32(*m->base + (CONV_SINGLETON_VA - 0x00400000u),
                            &self) || !self)
                return;
            for (i = 0; i < 5; i++)
                x86_peek32(self + CONV_SEEN + i * 4u, &out[i]);
            return;
        }
}

void fn_XMen2_00455af0(CPU *C);

/* convmgr vt+0x04 -- the only thing that clears the seen bitmap. Counted so
   "it was never called" and "it ran and the bitmap was dirty anyway" cannot
   look alike. */
void x2_override_00455af0(CPU *C)
{
    g_reset++;
    if (live())
        fprintf(stderr, "SCRIPT: conversation reset (vt+0x04) #%lu at "
                        "frame %lu\n",
                g_reset, gpu_frames_presented());
    fn_XMen2_00455af0(C);
}

void x2_override_0045c950(CPU *C)
{
    char name[NAME_MAX_LEN];
    int ok;
    uint8_t before, after;
    uint32_t line_before = 0, line_after = 0;
    uint32_t seen_before[5], seen_after[5];

    copy_guest_string(name, sizeof name, RD32(C->esp + 4u));
    before = conversation_flags();
    conversation_field(0x4bcu, &line_before);
    seen_bitmap(seen_before);
    fn_XMen2_0045c950(C);
    after = conversation_flags();
    conversation_field(0x4bcu, &line_after);
    seen_bitmap(seen_after);
    ok = (int)(C->eax & 0xFFu);
    g_convstart++;
    if (ok) g_convstart_ok++;
    /* Flags AND the current-line id across the call. A start that returns true
       while leaving the visible bit clear and no line selected has succeeded
       by its own return value and done nothing, and only both readings
       together say so. */
    if (live())
        fprintf(stderr, "SCRIPT: conversation start \"%s\" -> %s at frame %lu "
                        "(flags 0x%02x -> 0x%02x, line 0x%08x -> 0x%08x)\n",
                name, ok ? "STARTED" : "REFUSED", gpu_frames_presented(),
                before, after, line_before, line_after);
    if (live())
        fprintf(stderr, "        seen bitmap %08x %08x %08x %08x %08x -> "
                        "%08x %08x %08x %08x %08x\n",
                seen_before[0], seen_before[1], seen_before[2], seen_before[3],
                seen_before[4], seen_after[0], seen_after[1], seen_after[2],
                seen_after[3], seen_after[4]);
}

__attribute__((constructor))
static void x2_script_trace_register_overrides(void)
{
    x86_register_override("XMen2.exe", 0x004a1320, x2_override_004a1320);
    x86_register_override("XMen2.exe", 0x004a5660, x2_override_004a5660);
    x86_register_override("XMen2.exe", 0x0049f8c0, x2_override_0049f8c0);
    x86_register_override("XMen2.exe", 0x0045c950, x2_override_0045c950);
    x86_register_override("XMen2.exe", 0x00455af0, x2_override_00455af0);
}

void script_trace_report(void)
{
    int i;

    if (!g_launches) {
        printf("  scripts: NO script was launched in this run.\n"
               "           The override is compiled in and registered, so this "
               "is the game not asking, not the trace not looking.\n");
        return;
    }
    printf("  scripts: %lu launch(es) over %d distinct name(s), %lu of which "
           "failed to launch; last was \"%s\"\n",
           g_launches, g_nnames, g_failures, g_last);
    for (i = 0; i < g_nnames; i++)
        printf("           %-52s %lu launch(es)%s, first at frame %lu\n",
               g_names[i].name, g_names[i].launches,
               g_names[i].failures ? " WITH FAILURES" : "",
               g_names[i].first_frame);
    if (g_overflow)
        printf("           ... and %lu launch(es) of names past the %d-name "
               "table, NOT listed above\n", g_overflow, NAMES_MAX);
    printf("           startConversation: %lu call(s), %lu of which changed "
           "the conversation manager's flags\n",
           g_startconv, g_startconv_took);
    printf("           conversation start: %lu request(s), %lu started, "
           "%lu refused; the seen-line bitmap was reset %lu time(s)\n",
           g_convstart, g_convstart_ok, g_convstart - g_convstart_ok, g_reset);
    printf("           lockControls: %lu call(s) -- this is the CONTROL: the "
           "level locks controls at entry, so 0 here means the command "
           "addresses are wrong, not that the game never asked\n",
           g_lockcontrols);
    if (!live())
        printf("           set X2_SCRIPTS=1 to see each launch as it "
               "happens\n");
}
