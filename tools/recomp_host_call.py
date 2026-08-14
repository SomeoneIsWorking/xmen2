"""Emit the x86 hosted-call bridge shared by runner and replacement DLLs."""


def host_call_bridge(capture_x87):
    begin = "    x87_host_begin(C);\n" if capture_x87 else ""
    end = "    x87_host_end(C);\n" if capture_x87 else ""
    return r'''/* Run a real x86 function on the guest stack and preserve both
   halves of its integer return. MSVC returns __int64 in EDX:EAX. */
void x86_call_host(CPU *C, void *fn, const char *what)
{
    uint32_t eax, edx, after, gsp = C->esp + 4;
    if (!fn) {
        fprintf(stderr, "x86_call_host: %%s unresolved\n", what);
        abort();
    }
#ifdef X86_WATCH
    x86_watch_note(1, (uint32_t)(uintptr_t)fn, C->esp);
#endif
%s    __asm__ __volatile__(
        "movl %%%%esp, %%%%edi\n\t"
        "movl %%[g], %%%%esp\n\t"
        "movl %%[c], %%%%ecx\n\t"
        "call *%%[f]\n\t"
        "movl %%%%esp, %%[aft]\n\t"
        "movl %%%%edi, %%%%esp\n\t"
        : "=a"(eax), "=d"(edx), [aft] "=r"(after)
        : [f] "r"(fn), [g] "r"(gsp), [c] "r"(C->ecx)
        : "ecx", "edi", "memory");
    C->eax = eax;
    C->edx = edx;
    C->esp = after;
%s#ifdef X86_WATCH
    x86_watch_note(2, (uint32_t)(uintptr_t)fn, after);
#endif
}
''' % (begin, end)
