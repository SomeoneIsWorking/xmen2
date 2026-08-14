#include "x86watch_trace.h"

#define NOTE_RING 64

typedef struct WatchNote {
    int kind;
    uint32_t address;
    uint32_t esp;
    unsigned long thread_id;
} WatchNote;

static WatchNote notes[NOTE_RING];
static unsigned note_count;

void x86_watch_trace_reset(void)
{
    note_count = 0;
}

void x86_watch_trace_note(int kind, uint32_t address, uint32_t esp,
                          unsigned long thread_id)
{
    unsigned slot = note_count++ % NOTE_RING;
    notes[slot].kind = kind;
    notes[slot].address = address;
    notes[slot].esp = esp;
    notes[slot].thread_id = thread_id;
}

void x86_watch_trace_dump(FILE *out)
{
    static const char *labels[] = {
        "ENTER guest", "CALL host  ", "host RET   ", "guest RET  "
    };
    unsigned count = note_count < NOTE_RING ? note_count : NOTE_RING;
    unsigned i;

    if (!note_count) {
        fprintf(out, "[TRACE] the boundary ring is EMPTY: no recompiled body was "
                "entered and no host call was made before this point, so this "
                "fault is not downstream of one.\n");
        return;
    }
    fprintf(out, "[TRACE] last %u of %u boundary crossings, oldest first:\n",
            count, note_count);
    for (i = note_count - count; i < note_count; i++) {
        const WatchNote *note = &notes[i % NOTE_RING];
        fprintf(out, "[TRACE]   tid %-5lu %s  addr=0x%08x esp=0x%08x\n",
                note->thread_id, labels[note->kind & 3],
                note->address, note->esp);
    }
}
