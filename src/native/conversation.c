/*
 * The conversation state machine, ported.
 *
 * WHY THIS FILE EXISTS. Issue #63 -- the tutorial party is eliminated moments
 * after the opening conversation -- narrowed to one fact: the response that
 * carries `chosenscriptfile="act0/tutorial/tutorial1/conv_0020b_end"` never
 * launches its script, so the level's opening act never runs. Narrowing it
 * further by breakpointing recompiled bodies was answering "what did the
 * translated code do", which is a question about the translation, not about
 * the game. These functions are small, their fields are all named in the
 * conversation XMLB, and the port has to own them eventually anyway -- so they
 * are WRITTEN HERE, in C, and the recompiled bodies stay linked as `__real_`
 * beside them for diffing.
 *
 * THE RECORD LAYOUT, read from the parser (XMen2.exe FUN_00458820), which
 * fills a response/line record from the XMLB attributes. Every string
 * attribute is stored as a PAIR -- pointer then length -- and the pointer is
 * the FIRST dword, which is the part that cost a session: reading +0x1c as
 * "the second half of the chosenScriptFile pair" made it look like a length.
 * The parser is handed `record + 4`, so its own +0x18 is the record's +0x1c.
 *
 *      +0x08/+0x0c   text        (pointer, length)
 *      +0x14/+0x18   scriptFile
 *      +0x1c/+0x20   chosenScriptFile
 *      +0x24/+0x28   scriptCommand
 *      +0x2c/+0x30   conditionScript
 *      +0x7c         flags, bit 0x2 = conversationEnd
 *      +0x84         tag index
 *      +0x8c[]       child record ids
 *      +0xac         how many children
 *
 * and on the conversation singleton (0x00717aac, one object, ~0x23a000 bytes):
 *
 *      +0x4bc        the current line's id
 *      +0x4c0[]      the offered responses' ids, 0xFFFFFFFF for an empty slot
 *      +0x4e0        how many slots are offered
 *      +0x21b24      flags: 0x1 speaking, 0x2 VISIBLE, 0x8 ending, 0x10 enabled
 *      +0x21b26/28   tag index / tag count
 *      +0x21b30/34   the two actors a launched script runs against
 *      +0x239a0      the response id chosen this frame, 0 for none
 *      +0x239c0      a 32-bit eligibility bitmap over a record's children
 *
 * An override must reproduce the original's RETURN VALUE, not just its stack
 * effect (issue #54), so each function below states what it returns in what
 * register and pops exactly what the original's RET pops.
 */
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- where the exe actually landed ------------------------------------- */

#define EXE_PREFERRED   0x00400000u

static uint32_t exe_base(void)
{
    static uint32_t cached;
    X86Module *m;
    if (cached) return cached;
    for (m = x86_modules(); m; m = m->next)
        if (m->preferred == EXE_PREFERRED && *m->base) {
            cached = *m->base;
            return cached;
        }
    /* Never silently 0: every address below would then be an offset from the
       null page and the first read would fault with no explanation. */
    fprintf(stderr, "conversation: XMen2.exe is not mapped, so no conversation "
                    "address can be resolved. Refusing to guess.\n");
    abort();
}

#define EXE(rva)        (exe_base() + (uint32_t)(rva))

/* Guest functions this port does not own yet. Named, so a reader can see
   exactly how much of the subsystem is still the translated original. */
#define FN_ENSURE_SINGLETON     EXE(0x00058380u)  /* FUN_00458380 */
#define FN_SCRIPT_MANAGER       EXE(0x000a1670u)  /* FUN_004a1670 */
#define FN_ACTOR_IS_LIVE        EXE(0x000654d0u)  /* FUN_004654d0 */
#define FN_BEGIN_RESPONSE       EXE(0x00058700u)  /* FUN_00458700 */
#define FN_LINE_BY_ID           EXE(0x000573f0u)  /* FUN_004573f0 */
#define FN_APPLY_RESPONSE       EXE(0x0005cde0u)  /* FUN_0045cde0 */
#define FN_AFTER_CHOICE         EXE(0x00058410u)  /* FUN_00458410 */

#define G_SINGLETON             EXE(0x00317aacu)  /* 0x00717aac */
#define G_EMPTY_STRING          EXE(0x00281968u)  /* 0x00681968, "" */

/* Conversation-object fields. */
#define CV_CUR_LINE     0x004bcu
#define CV_RESP_IDS     0x004c0u
#define CV_RESP_COUNT   0x004e0u
#define CV_FLAGS        0x21b24u
#define CV_TAG_INDEX    0x21b26u
#define CV_TAG_COUNT    0x21b28u
#define CV_ACTOR_A      0x21b30u
#define CV_ACTOR_B      0x21b34u
#define CV_ELIGIBLE     0x239c0u

#define CVF_VISIBLE     0x2u
#define CVF_ENDING      0x8u

/* Record fields. */
#define RC_SCRIPT_FILE      0x14u
#define RC_CHOSEN_SCRIPT    0x1cu
#define RC_SCRIPT_COMMAND   0x24u
#define RC_CONDITION_SCRIPT 0x2cu
#define RC_FLAGS            0x7cu
#define RC_TAG              0x84u
#define RC_CHILDREN         0x8cu
#define RC_CHILD_COUNT      0xacu

/* Script-manager vtable slots, as the original calls them. */
#define SM_SET_ACTOR_A  0x14u
#define SM_SET_ACTOR_B  0x18u
#define SM_LAUNCH       0x3cu
#define SM_CLEAR_A      0x1cu
#define SM_CLEAR_B      0x20u

/* ---- counters ----------------------------------------------------------
 *
 * Printed unconditionally and with their denominators. "0 of 0" and "this
 * never ran" have to look different, because the whole of issue #63 was a
 * script launch that did not happen and left no trace saying so.
 */
static unsigned long c_launch_asked, c_launch_empty, c_launch_done;
static unsigned long c_next_called, c_next_nochild, c_next_noneligible,
                     c_next_found;
static unsigned long c_choose_called, c_choose_outofrange, c_choose_applied,
                     c_choose_refused;
static unsigned long c_respscripts;
static char          last_launched[128];

/*
 * The flag byte, every time it changes.
 *
 * isVisible() is asked once per frame by the per-frame update and is the
 * predicate that decides whether the conversation exists at all -- so it is
 * the one place the flag byte can be watched without a debugger, in the
 * shipping build, for the cost of a byte compare. EVERY change is printed
 * (the boring case is the unchanged one, and that is what gets dropped), so
 * "the conversation stopped being visible" arrives with the frame it happened
 * on instead of being inferred from its consequences.
 */
static unsigned long c_flag_polls, c_flag_changes, c_flag_unprinted;
static int           have_flags;
static uint8_t       last_flags;

/*
 * WHO asks, and when it last asked.
 *
 * "The conversation is visible" and "the conversation is being updated" are
 * different facts, and confusing them cost a session: the flag byte reaches
 * visible+speaking and never changes again, which reads as a live
 * conversation, while the per-frame update may have stopped calling at all.
 * The word at ESP on entry is the return address the emitted call site pushed
 * -- reading it is passive, the same read X2_EPCOUNT makes -- so each caller
 * can be counted separately and its LAST poll recorded. A caller that stops is
 * then visible as a gap, not inferred from a consequence.
 */
#define FLAG_CALLERS_MAX 8
static struct { uint32_t ret; unsigned long n, last; } g_askers[FLAG_CALLERS_MAX];
static int g_naskers, g_askers_lost;

static void note_asker(uint32_t ret)
{
    int i;
    for (i = 0; i < g_naskers; i++)
        if (g_askers[i].ret == ret) {
            g_askers[i].n++;
            g_askers[i].last = c_flag_polls;
            return;
        }
    if (g_naskers < FLAG_CALLERS_MAX) {
        g_askers[g_naskers].ret = ret;
        g_askers[g_naskers].n = 1;
        g_askers[g_naskers].last = c_flag_polls;
        g_naskers++;
    } else {
        g_askers_lost++;
    }
}

#define FLAG_LINES_MAX 64

static void note_flags(uint8_t f)
{
    c_flag_polls++;
    if (have_flags && f == last_flags) return;
    c_flag_changes++;
    if (c_flag_changes <= FLAG_LINES_MAX)
        fprintf(stderr, "conversation: flags 0x%02x -> 0x%02x  "
                        "[speaking=%d visible=%d ending=%d enabled=%d]  "
                        "(poll %lu)\n",
                have_flags ? last_flags : 0u, f,
                !!(f & 0x1), !!(f & 0x2), !!(f & 0x8), !!(f & 0x10),
                c_flag_polls);
    else
        c_flag_unprinted++;
    have_flags = 1;
    last_flags = f;
}

void conversation_report(void)
{
    printf("  conversation (ported: launchScript, nextLine, chooseResponse, "
           "responseScripts, the two flag predicates):\n");
    printf("        script launches: %lu asked, %lu had an empty name, "
           "%lu reached the script manager%s%s\n",
           c_launch_asked, c_launch_empty, c_launch_done,
           last_launched[0] ? "; last was " : " -- none, so no conversation "
                                              "ever ran a script",
           last_launched[0] ? last_launched : "");
    printf("        nextLine: %lu call(s) -- %lu found a line, %lu found the "
           "record childless, %lu found no ELIGIBLE child\n",
           c_next_called, c_next_found, c_next_nochild, c_next_noneligible);
    printf("        chooseResponse: %lu call(s) -- %lu applied, %lu refused by "
           "the engine, %lu with the index past the offered count\n",
           c_choose_called, c_choose_applied, c_choose_refused,
           c_choose_outofrange);
    printf("        responseScripts: %lu record(s) walked for scriptCommand "
           "and scriptFile\n", c_respscripts);
    printf("        flags: %lu poll(s) of the visible predicate, %lu change(s)"
           "%s; last value 0x%02x%s\n",
           c_flag_polls, c_flag_changes,
           c_flag_unprinted ? " (some past the print cap, counted here)" : "",
           have_flags ? last_flags : 0u,
           have_flags ? "" : " -- the predicate was NEVER asked, so this port "
                             "did not run at all");
    {
        int i;
        if (!g_naskers)
            printf("        nobody asked whether the conversation is visible "
                   "in this run.\n");
        for (i = 0; i < g_naskers; i++) {
            const char *nm = x86_native_name_at(g_askers[i].ret);
            /* "Stopped" needs a THRESHOLD, not an exact match. The first
               version said IT STOPPED ASKING of a caller whose last poll was
               two before the end, which is a caller that never stopped -- the
               gap is the measurement and it is printed either way. */
            unsigned long gap = c_flag_polls - g_askers[i].last;
            int stopped = gap > c_flag_polls / 20u;
            printf("        asked from 0x%08x%s%s: %lu time(s), last at poll "
                   "%lu of %lu (%lu poll(s) before the end)%s\n",
                   g_askers[i].ret, nm ? " -- " : "", nm ? nm : "",
                   g_askers[i].n, g_askers[i].last, c_flag_polls, gap,
                   stopped ? "  <- STOPPED, and the run went on without it"
                           : "");
        }
        if (g_askers_lost)
            printf("        ... and %d poll(s) from call sites past the "
                   "table.\n", g_askers_lost);
    }
}

/* ---- guest calling ------------------------------------------------------
 *
 * These callees are __thiscall with callee cleanup, so they leave ESP HIGHER
 * than x86_guest_call pushed -- which that helper reports once, by design, as
 * "a `ret N` whose N arguments the host never pushed". Here the host DID push
 * them; the note is expected and the reset it does is correct either way,
 * because the CPU handed down is a copy and the caller's ESP never moves.
 */
static uint32_t thiscall(CPU *C, uint32_t fn, uint32_t ecx,
                         int argc, const uint32_t *argv)
{
    CPU K = *C;
    int i;
    K.esp -= (uint32_t)argc * 4u;
    for (i = 0; i < argc; i++) WR32(K.esp + (uint32_t)i * 4u, argv[i]);
    K.ecx = ecx;
    x86_guest_call(&K, fn);
    return K.eax;
}

static uint32_t call0(CPU *C, uint32_t fn, uint32_t ecx)
{
    return thiscall(C, fn, ecx, 0, NULL);
}

static uint32_t call1(CPU *C, uint32_t fn, uint32_t ecx, uint32_t a)
{
    return thiscall(C, fn, ecx, 1, &a);
}

/* The singleton, created on demand exactly as every call site in the original
   does: test the static, and if it is null call the constructor. */
static uint32_t conv_singleton(CPU *C)
{
    uint32_t v = RD32(G_SINGLETON);
    if (!v) {
        CPU K = *C;
        x86_guest_call(&K, FN_ENSURE_SINGLETON);
        v = RD32(G_SINGLETON);
    }
    return v;
}

static uint32_t script_manager(CPU *C)
{
    CPU K = *C;
    x86_guest_call(&K, FN_SCRIPT_MANAGER);
    return K.eax;
}

static uint32_t vslot(uint32_t obj, uint32_t off)
{
    return RD32(RD32(obj) + off);
}

/* The original's emptiness test is `REPE CMPSB` with ECX=1 against the empty
   string -- one byte, so it asks whether the FIRST byte is NUL and nothing
   more. A pointer that is null is a different thing and the original would
   fault on it; this reports instead, because a null here means the parser did
   not run, which is worth knowing by name. */
static int name_is_empty(uint32_t s, const char *what)
{
    if (!s) {
        static int said;
        if (!said++)
            fprintf(stderr, "conversation: a record's %s pointer is NULL, not "
                            "the empty string -- the parser never filled this "
                            "field. Treated as empty. Reported once.\n", what);
        return 1;
    }
    return RD8(s) == 0;
}

static void note_name(char *dst, size_t cap, uint32_t s)
{
    size_t i = 0;
    if (!s) { dst[0] = 0; return; }
    for (i = 0; i + 1 < cap; i++) {
        uint8_t ch = RD8(s + (uint32_t)i);
        if (!ch) break;
        dst[i] = (char)ch;
    }
    dst[i] = 0;
}

/* ---------------------------------------------------------------------
 * XMen2.exe 0x00455600 -- launch a script with the conversation's actors bound.
 *
 *     bool igConversationManager::launchScript(const char *name, int flag)
 *
 * Binds the two actor slots into the script manager, calls its by-name launch
 * (vtable +0x3c, which is FUN_004a1320), then clears both slots again. Returns
 * the launch's own bool in AL; the original keeps it in BL across the two
 * clearing calls, so those cannot be allowed to overwrite it.
 */
void __real_fn_XMen2_00455600(CPU *C);

void __wrap_fn_XMen2_00455600(CPU *C)
{
    uint32_t self = C->ecx;
    uint32_t name = RD32(C->esp + 4u);
    uint32_t flag = RD32(C->esp + 8u);
    uint32_t sm, actor, args[2];
    uint32_t ok;

    c_launch_asked++;
    if (name_is_empty(name, "script name")) c_launch_empty++;

    /* Actor A: the object at +0x21b30, or nothing. */
    actor = RD32(self + CV_ACTOR_A);
    actor = actor ? RD32(actor + 0x1cu) : 0u;
    sm = script_manager(C);
    call1(C, vslot(sm, SM_SET_ACTOR_A), sm, actor);

    /* Actor B is passed only while it is still live -- the original asks
       FUN_004654d0 and substitutes nothing when the answer is false. */
    actor = RD32(self + CV_ACTOR_B);
    if (actor && (uint8_t)call0(C, FN_ACTOR_IS_LIVE, self + CV_ACTOR_B))
        actor = RD32(self + CV_ACTOR_B);
    else
        actor = 0;
    sm = script_manager(C);
    call1(C, vslot(sm, SM_SET_ACTOR_B), sm, actor);

    sm = script_manager(C);
    args[0] = name;
    args[1] = flag;
    ok = thiscall(C, vslot(sm, SM_LAUNCH), sm, 2, args);

    if (ok & 0xFFu) {
        c_launch_done++;
        note_name(last_launched, sizeof last_launched, name);
    }

    sm = script_manager(C);
    call0(C, vslot(sm, SM_CLEAR_A), sm);
    sm = script_manager(C);
    call0(C, vslot(sm, SM_CLEAR_B), sm);

    C->eax = (C->eax & ~0xFFu) | (ok & 0xFFu);   /* AL, as `MOV AL,BL` */
    C->esp += 4u + 8u;                            /* RET 0x8 */
}

/* ---------------------------------------------------------------------
 * XMen2.exe 0x0045a100 -- run a record's own two scripts.
 *
 *     void record::runScripts()      // scriptCommand, then scriptFile
 */
void __real_fn_XMen2_0045a100(CPU *C);

void __wrap_fn_XMen2_0045a100(CPU *C)
{
    uint32_t rec = C->ecx;
    uint32_t s;

    c_respscripts++;

    s = RD32(rec + RC_SCRIPT_COMMAND);
    if (!name_is_empty(s, "scriptCommand")) {
        uint32_t self = conv_singleton(C);
        uint32_t args[2];
        args[0] = s; args[1] = 1u;
        thiscall(C, EXE(0x00055600u), self, 2, args);
    }
    s = RD32(rec + RC_SCRIPT_FILE);
    if (!name_is_empty(s, "scriptFile")) {
        uint32_t self = conv_singleton(C);
        uint32_t args[2];
        args[0] = s; args[1] = 1u;
        thiscall(C, EXE(0x00055600u), self, 2, args);
    }
    C->esp += 4u;                                 /* RET */
}

/* ---------------------------------------------------------------------
 * XMen2.exe 0x004559e0 -- make the low six bits of a bitmap eligible.
 *
 *     void bitmap::resetEligible()   // *p = ~*p, then clear bits 6..31
 *
 * The callers zero the word first, so the effect is 0x3F -- six candidate
 * children. It is ported literally rather than as the constant, because the
 * constant is a consequence of the caller and would stop being true silently.
 */
void __real_fn_XMen2_004559e0(CPU *C);

void __wrap_fn_XMen2_004559e0(CPU *C)
{
    uint32_t p = C->ecx;
    int i;
    WR32(p, ~RD32(p));
    for (i = 6; i < 32; i++) {
        uint32_t w = p + (uint32_t)(i >> 5) * 4u;
        WR32(w, RD32(w) & ~(1u << (i & 31)));
    }
    C->esp += 4u;                                 /* RET */
}

/* ---------------------------------------------------------------------
 * XMen2.exe 0x0045b6d0 -- which line follows this record.
 *
 *     uint32 record::nextLine()      // 0 when there is none
 *
 * Runs the record's conditionScript first (that is what gets to change the
 * eligibility bitmap), then returns the first ELIGIBLE child's id, running the
 * record's own scripts on the way out. Returning 0 is what ends a conversation
 * -- FUN_0045cde0 sets the ending flag on it -- so the two ways of returning 0
 * are counted apart.
 */
void __real_fn_XMen2_0045b6d0(CPU *C);

void __wrap_fn_XMen2_0045b6d0(CPU *C)
{
    uint32_t rec = C->ecx;
    uint32_t self, cond, kids, count;
    uint32_t i;

    c_next_called++;
    count = RD32(rec + RC_CHILD_COUNT);
    if (!count) {
        c_next_nochild++;
        C->eax = 0;
        C->esp += 4u;
        return;
    }

    self = conv_singleton(C);
    WR32(self + CV_ELIGIBLE, 0);
    call0(C, EXE(0x000559e0u), self + CV_ELIGIBLE);

    cond = RD32(rec + RC_CONDITION_SCRIPT);
    if (!name_is_empty(cond, "conditionScript")) {
        uint32_t args[2];
        self = conv_singleton(C);
        args[0] = cond; args[1] = 1u;
        thiscall(C, EXE(0x00055600u), self, 2, args);
    }

    self = conv_singleton(C);
    kids = rec + RC_CHILDREN;
    for (i = 0; i < count; i++) {
        uint32_t word = RD32(self + CV_ELIGIBLE + (i >> 5) * 4u);
        if (word & (1u << (i & 31))) break;
    }
    if (i == count) {
        c_next_noneligible++;
        C->eax = 0;
        C->esp += 4u;
        return;
    }

    call0(C, EXE(0x0005a100u), rec);              /* the record's own scripts */
    c_next_found++;
    C->eax = RD32(kids + i * 4u);
    C->esp += 4u;                                 /* RET */
}

/* ---------------------------------------------------------------------
 * XMen2.exe 0x0045d5d0 -- the player picked the Nth offered response.
 *
 *     void igConversationManager::chooseResponse(int sel)
 *
 * The offered ids are a fixed array with holes (0xFFFFFFFF); `sel` counts only
 * the filled ones, which is why the loop keeps two indices. FUN_00458700 is
 * the engine's own "begin this response" and a TRUE return from it means it
 * declined -- the original skips applying the response in that case, so a
 * refusal is recorded here by name rather than looking like a missing call.
 */
void __real_fn_XMen2_0045d5d0(CPU *C);

void __wrap_fn_XMen2_0045d5d0(CPU *C)
{
    uint32_t self = C->ecx;
    int sel = (int)RD32(C->esp + 4u);
    int count = (int)RD32(self + CV_RESP_COUNT);
    int slot, filled = 0;

    c_choose_called++;
    if (sel >= count || count <= 0) {
        /* The original does NOT return here -- both early jumps land on the
           tail, which still calls FUN_00458410. Skipping it was the first
           mistake this port made, and it would have left the UI a frame
           behind on every out-of-range choice. */
        c_choose_outofrange++;
        call0(C, FN_AFTER_CHOICE, self);
        C->esp += 4u + 4u;                        /* RET 0x4 */
        return;
    }

    for (slot = 0; slot < (int)RD32(self + CV_RESP_COUNT); slot++) {
        uint32_t id = RD32(self + CV_RESP_IDS + (uint32_t)slot * 4u);
        if (id == 0xFFFFFFFFu) continue;
        if (filled == sel) {
            if (!((uint8_t)call1(C, FN_BEGIN_RESPONSE, self, id))) {
                uint32_t line = call1(C, FN_LINE_BY_ID, self,
                                      RD32(self + CV_CUR_LINE));
                if (line) {
                    int tag = (int)(int16_t)RD16(self + CV_TAG_INDEX) + 1;
                    WR32(line + RC_TAG, (uint32_t)tag);
                    if (tag >= (int)(int16_t)RD16(self + CV_TAG_COUNT))
                        WR32(line + RC_TAG, 0);
                }
                c_choose_applied++;
                call1(C, FN_APPLY_RESPONSE, self, id);
            } else {
                /* Counted only. Naming the refused response would mean
                   resolving its id, and the id->record call that would do it
                   (FUN_00457460) is not the one this function uses -- a
                   diagnostic that guesses which lookup applies is how an
                   instrument starts lying. */
                c_choose_refused++;
            }
        }
        filled++;
    }
    call0(C, FN_AFTER_CHOICE, self);
    C->esp += 4u + 4u;                            /* RET 0x4 */
}

/* ---------------------------------------------------------------------
 * XMen2.exe 0x00458010 / 0x00458020 -- the two flag predicates.
 *
 *     bool isVisible()   { return (flags >> 1) & 1; }   // drives the update
 *     bool isSpeaking()  { return (flags >> 2) & 1; }
 *
 * Both return in AL only, leaving the rest of EAX as the original's `SHR AL`
 * does.
 */
void __real_fn_XMen2_00458010(CPU *C);

void __wrap_fn_XMen2_00458010(CPU *C)
{
    uint8_t f = RD8(C->ecx + CV_FLAGS);
    note_flags(f);
    note_asker(RD32(C->esp));
    C->eax = (C->eax & ~0xFFu) | (uint32_t)((f >> 1) & 1u);
    C->esp += 4u;
}

void __real_fn_XMen2_00458020(CPU *C);

void __wrap_fn_XMen2_00458020(CPU *C)
{
    uint8_t f = RD8(C->ecx + CV_FLAGS);
    C->eax = (C->eax & ~0xFFu) | (uint32_t)((f >> 2) & 1u);
    C->esp += 4u;
}
