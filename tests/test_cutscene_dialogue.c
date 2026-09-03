#include "conversation_player.h"
#include "cutscene_dialogue.h"
#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

enum {
    ARENA_BASE = 0x35000000u,
    ARENA_SIZE = 0x00400000u,
    MANAGER = ARENA_BASE + 0x00010000u,
    MANAGER_VTABLE = ARENA_BASE + 0x00020000u,
    AUDIO = ARENA_BASE + 0x00030000u,
    AUDIO_VTABLE = ARENA_BASE + 0x00040000u,
    STACK = ARENA_BASE + ARENA_SIZE - 0x100u,
    FN_AUDIO = ARENA_BASE + (0x00592480u - 0x00400000u),
    FN_CHOOSE_RESPONSE = ARENA_BASE + 0x00050000u,
    FN_STOP_SOUND = ARENA_BASE + 0x00050010u,
    FN_BEGIN_RESPONSE = 0x00458700u,
    FN_LINE_AUDIO = 0x0045a170u,
    CONV_SINGLETON = ARENA_BASE + (0x00717aacu - 0x00400000u),
    NULL_SOUND_HANDLE = ARENA_BASE + (0x0069d05cu - 0x00400000u),
    CV_SOUND_HANDLE = 0x21b80u,
    CV_CHOSEN_RESPONSE = 0x239a0u,
    CV_ACCEPT_STATE = 0x239a4u,
};

static uint32_t mapped_exe = ARENA_BASE;
static X86Module module = {
    .name = "XMen2.exe",
    .base = &mapped_exe,
    .preferred = 0x00400000u,
};
static x86_override_fn begin_response_override;
static x86_override_fn line_audio_override;
static unsigned failures;
static unsigned original_presentations;
static unsigned response_applications;
static unsigned voice_stops;
static uint32_t stopped_handle;

volatile uint32_t x2_write_watch_addr;

void x2_write_watch_fire(uint32_t address, uint32_t value)
{
    (void)address;
    (void)value;
    abort();
}

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

#define CHECK(condition, message) \
    do { if (!(condition)) fail(message); } while (0)

X86Module *x86_modules(void)
{
    return &module;
}

int x86_peek(uint32_t address, void *out, size_t size)
{
    uint64_t end = (uint64_t)address + size;

    if (address < ARENA_BASE || end > (uint64_t)ARENA_BASE + ARENA_SIZE)
        return 0;
    memcpy(out, guest_memory_const_pointer(address), size);
    return 1;
}

int x86_peek32(uint32_t address, uint32_t *out)
{
    return x86_peek(address, out, sizeof *out);
}

int conversation_player_selection(CPU *cpu,
                                  ConversationPlayerSelection *out)
{
    (void)cpu;
    out->manager = MANAGER;
    out->choose_response = FN_CHOOSE_RESPONSE;
    out->selected = 0u;
    out->line_presenter = 0x00459e00u;
    return 1;
}

void x86_register_override(const char *name, uint32_t ep,
                           x86_override_fn function)
{
    CHECK(!strcmp(name, "XMen2.exe"),
          "dialogue override registered for the wrong module");
    if (ep == FN_BEGIN_RESPONSE)
        begin_response_override = function;
    else if (ep == FN_LINE_AUDIO)
        line_audio_override = function;
    else
        fail("dialogue override registered for the wrong entry point");
}

static void guest_body_00458700(CPU *cpu)
{
    ++original_presentations;
    cpu->eax = (cpu->eax & ~0xffu) | 1u;
    cpu->esp += 8u;
}

static void guest_body_0045a170(CPU *cpu)
{
    ++original_presentations;
    WR32(MANAGER + CV_SOUND_HANDLE, 0x00000099u);
    cpu->esp += 8u;
}

static void invoke_line_audio(CPU *parent)
{
    CPU call = *parent;

    call.esp = STACK - 0x20u;
    WR32(call.esp, 0xc001c0deu);
    WR32(call.esp + 4u, STACK - 0x40u);
    call.ecx = 0x0042u;
    line_audio_override(&call);
    CHECK(call.esp == STACK - 0x18u,
          "line-audio override did not reproduce RET 4");
}

static void invoke_begin_response(CPU *parent)
{
    CPU call = *parent;

    call.esp = STACK - 0x20u;
    WR32(call.esp, 0xc001c0deu);
    WR32(call.esp + 4u, 0x0020u);
    call.ecx = MANAGER;
    begin_response_override(&call);
    CHECK(call.esp == STACK - 0x18u,
          "beginResponse override did not reproduce RET 4");
    if (!(uint8_t)call.eax) ++response_applications;
}

void x86_guest_call_args(CPU *cpu, uint32_t target,
                         uint32_t callee_pop_bytes)
{
    if (target == FN_AUDIO) {
        CHECK(callee_pop_bytes == 0u,
              "audio getter received a callee-pop argument");
        cpu->eax = AUDIO;
    } else if (target == FN_STOP_SOUND) {
        CHECK(cpu->ecx == AUDIO && callee_pop_bytes == 4u,
              "voice stop used the wrong receiver or ABI");
        stopped_handle = RD32(cpu->esp);
        ++voice_stops;
        cpu->esp += callee_pop_bytes;
    } else if (target == FN_CHOOSE_RESPONSE) {
        CHECK(cpu->ecx == MANAGER && RD32(cpu->esp) == 0u &&
                  callee_pop_bytes == 4u,
              "cutscene dialogue chose the wrong response or ABI");
        invoke_begin_response(cpu);
        invoke_line_audio(cpu);
        cpu->esp += callee_pop_bytes;
    } else {
        fail("cutscene dialogue called an unexpected guest function");
    }
}

void x86_guest_call(CPU *cpu, uint32_t target)
{
    x86_guest_call_args(cpu, target, 0u);
}

static CPU fresh_cpu(void)
{
    CPU cpu;

    memset(&cpu, 0, sizeof cpu);
    cpu.esp = STACK;
    return cpu;
}

static void reset_guest(void)
{
    memset(guest_memory_pointer(ARENA_BASE), 0, ARENA_SIZE);
    WR32(NULL_SOUND_HANDLE, 0xffffffffu);
    WR32(CONV_SINGLETON, MANAGER);
    WR32(MANAGER, MANAGER_VTABLE);
    WR32(AUDIO, AUDIO_VTABLE);
    WR32(AUDIO_VTABLE + 0x74u, FN_STOP_SOUND);
    original_presentations = 0u;
    response_applications = 0u;
    voice_stops = 0u;
    stopped_handle = 0u;
}

static void test_ordinary_presentation_positive_control(void)
{
    CutsceneDialogueSnapshot before, after;
    CPU cpu = fresh_cpu();

    cutscene_dialogue_snapshot(&before);
    cpu.esp -= 8u;
    WR32(cpu.esp, 0xc001c0deu);
    WR32(cpu.esp + 4u, 0x0020u);
    cpu.ecx = MANAGER;
    begin_response_override(&cpu);
    cpu = fresh_cpu();
    cpu.esp -= 8u;
    WR32(cpu.esp, 0xc001c0deu);
    WR32(cpu.esp + 4u, STACK - 0x40u);
    cpu.ecx = 0x0042u;
    line_audio_override(&cpu);
    cutscene_dialogue_snapshot(&after);

    CHECK(original_presentations == 2u,
          "positive controls did not reach both retail presenters");
    CHECK(after.ordinary_response_starts ==
              before.ordinary_response_starts + 1u &&
              after.ordinary_line_starts == before.ordinary_line_starts + 1u,
          "presentation instrument did not observe both positive controls");
}

static void test_skip_stops_and_suppresses_dialogue(void)
{
    CutsceneDialogueSnapshot before, after;
    CPU cpu = fresh_cpu();

    WR32(MANAGER + CV_SOUND_HANDLE, 0x12345678u);
    WR32(MANAGER + CV_CHOSEN_RESPONSE, 0x00abcdefu);
    WR8(MANAGER + CV_ACCEPT_STATE, 0xffu);
    cutscene_dialogue_snapshot(&before);

    CHECK(cutscene_dialogue_advance(&cpu),
          "cutscene dialogue refused a deterministic selection");
    cutscene_dialogue_snapshot(&after);

    CHECK(voice_stops == 1u && stopped_handle == 0x12345678u,
          "skip did not stop the currently playing dialogue voice");
    CHECK(RD32(MANAGER + CV_SOUND_HANDLE) == 0xffffffffu,
          "skip did not clear the stopped dialogue handle");
    CHECK((RD8(MANAGER + CV_ACCEPT_STATE) & 1u) == 0u,
          "skip did not clear the retail voice-accept state");
    CHECK(RD32(MANAGER + CV_CHOSEN_RESPONSE) == 0u,
          "suppressed beginResponse lost its unconditional state write");
    CHECK(response_applications == 1u,
          "suppressed presentation did not apply the retail response");
    CHECK(original_presentations == 2u,
          "skip leaked into the ordinary presentation implementation");
    CHECK(after.active_voice_stops == before.active_voice_stops + 1u &&
              after.suppressed_response_starts ==
                  before.suppressed_response_starts + 1u &&
              after.suppressed_line_starts ==
                  before.suppressed_line_starts + 1u &&
              after.skip_presentation_starts ==
                  before.skip_presentation_starts,
          "skip presentation counters did not record stop/suppression/zero-start");
}

static void test_skip_scope_suppresses_adjacent_line(void)
{
    CutsceneDialogueSnapshot before, after;
    CPU cpu = fresh_cpu();

    WR32(MANAGER + CV_SOUND_HANDLE, 0xffffffffu);
    cutscene_dialogue_snapshot(&before);
    cutscene_dialogue_skip_begin();
    invoke_line_audio(&cpu);
    cutscene_dialogue_skip_end(&cpu);
    cutscene_dialogue_snapshot(&after);

    CHECK(original_presentations == 2u,
          "outer cutscene scope leaked into the line presenter");
    CHECK(after.suppressed_line_starts ==
              before.suppressed_line_starts + 1u &&
              after.skip_presentation_starts ==
                  before.skip_presentation_starts,
          "outer cutscene scope did not suppress an adjacent line start");
}

int main(void)
{
    if (guest_memory_init() != 0 ||
        guest_memory_map_fixed(ARENA_BASE, ARENA_SIZE,
                               PROT_READ | PROT_WRITE) != 0) {
        fprintf(stderr, "FAIL: could not map isolated guest arena\n");
        return 1;
    }
    CHECK(begin_response_override != NULL && line_audio_override != NULL,
          "dialogue presenter overrides did not self-register");
    reset_guest();
    test_ordinary_presentation_positive_control();
    test_skip_stops_and_suppresses_dialogue();
    test_skip_scope_suppresses_adjacent_line();
    return failures != 0u;
}

/*
 * The retail bodies these tests super-call into. Production reaches them
 * through x86_guest_body, so the test models the same seam rather than a
 * symbol per function -- and an entry point this test does not model is a
 * FAILURE that names itself, never a silent return.
 */
void x86_guest_body(CPU *C, const char *module, uint32_t linked_ep)
{
    if (linked_ep == 0x00458700u && !strcmp(module, "XMen2.exe"))
        { guest_body_00458700(C); return; }
    if (linked_ep == 0x0045a170u && !strcmp(module, "XMen2.exe"))
        { guest_body_0045a170(C); return; }
    fprintf(stderr, "%s: x86_guest_body(%s, 0x%08x) is not modelled by this test.\n",
            "test_cutscene_dialogue.c", module, linked_ep);
    abort();
}
