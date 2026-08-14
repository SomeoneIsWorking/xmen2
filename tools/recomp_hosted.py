"""Hosted-runtime import adapters used by recomp.py's runtime emitter."""

CRT_X87_ADAPTERS = {
    "imp_MSVCR71__CIpow": "x87_crt_cipow",
    "imp_MSVCR71__CIfmod": "x87_crt_cifmod",
    "imp_MSVCR71__CIacos": "x87_crt_ciacos",
    "imp_MSVCR71__CIasin": "x87_crt_ciasin",
    "imp_MSVCR71_ftol": "x87_crt_ftol",
    "imp_MSVCR71__ftol": "x87_crt_ftol",
    "imp_MSVCR71_atof": "x87_crt_atof",
    "imp_MSVCR71_ceil": "x87_crt_ceil",
    "imp_MSVCR71_floor": "x87_crt_floor",
    "imp_MSVCR71__finite": "x87_crt_finite",
}

CALLBACK_ADAPTERS = {
    "imp_MSVCR71__initterm": "x86_host_initterm",
}

TERMINATING_IMPORTS = {
    "imp_KERNEL32_ExitProcess", "imp_MSVCR71_exit", "imp_MSVCR71__exit",
    "imp_MSVCR71__amsg_exit", "imp_MSVCR71_terminate",
}

DISPATCH_BODY = r'''
static void x86_dispatch_one(CPU *C, uint32_t target)
{
    int i;
    for (i = 0; i < g_fn_count; i++)
        if (g_fns[i].ep == target) { g_fns[i].fn(C); return; }
    if (g_image_lo && (target < g_image_lo || target >= g_image_hi)) {
        x86_call_host(C, (void *)(uintptr_t)target, "indirect host call");
        return;
    }
    if (g_image_lo && x86_allow_fallback) {
        x86_note_fallback(target);
        x86_call_host(C, (void *)(uintptr_t)target, "original code (not recompiled)");
        return;
    }
    x86_dispatch_miss(target);
}

void x86_dispatch(CPU *C, uint32_t target)
{
    uint32_t outer_depth = C->dispatch_depth;
    uint32_t outer_target = C->tail_target;
    C->dispatch_depth = C->call_depth + 1;
    do {
        C->tail_target = 0;
        x86_dispatch_one(C, target);
        target = C->tail_target;
    } while (target);
    C->dispatch_depth = outer_depth;
    C->tail_target = outer_target;
}

void x86_tail_dispatch(CPU *C, uint32_t target)
{
    /* A deeper body was reached by an ordinary direct C call. Its tail target
       has to finish before that direct caller resumes. */
    if (C->dispatch_depth && C->call_depth == C->dispatch_depth) {
        C->tail_target = target;
        return;
    }
    x86_dispatch(C, target);
}
'''


def import_adapter(ident, module, symbol, index):
    """Return an emitted wrapper, or None for the ordinary host bridge."""
    math_adapter = CRT_X87_ADAPTERS.get(ident)
    if math_adapter:
        return "void %s(CPU *C) { %s(C); }" % (ident, math_adapter)
    callback_adapter = CALLBACK_ADAPTERS.get(ident)
    if callback_adapter:
        return "void %s(CPU *C) { %s(C); }" % (ident, callback_adapter)
    if ident in TERMINATING_IMPORTS:
        has_code = 0 if ident == "imp_MSVCR71_terminate" else 1
        safe_symbol = symbol.replace('"', "'")
        return ('void %s(CPU *C) { x86_termination_note(C, "%s!%s", %d); '
                'x86_call_host(C, g_imp[%d], "%s!%s"); }'
                % (ident, module, safe_symbol, has_code, index,
                   module, safe_symbol))
    return None
