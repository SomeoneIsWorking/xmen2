/*
 * fwd.S -- the generic pass-through slots of the logging d3d8 proxy.
 *
 * proxy.c wraps IDirect3D8 and IDirect3DDevice8, plus vertex buffers for the
 * geometry probe. The unintercepted slots must still work, and writing a C
 * forwarder for each
 * would mean knowing every signature -- many chances to get an argument count
 * wrong in a diagnostic whose whole point is not to disturb the run it
 * measures.
 *
 * stdcall is CALLEE-cleanup, so a forwarder that JMPs (rather than calls and
 * returns) never has to know how many arguments there are: the real method
 * pops them and returns straight to the game. All a slot has to do is swap our
 * `this` for the real object's:
 *
 *     struct wrap { void **vtbl; void *real; void **real_vtbl; };
 *                   +0            +4         +8
 *
 *     mov eax, [esp+4]        ; our wrapper (the `this` the game pushed)
 *     mov edx, [eax+4]        ; w->real
 *     mov ecx, [eax+8]        ; w->real_vtbl
 *     mov [esp+4], edx        ; the real object becomes `this`
 *     jmp [ecx + slot*4]      ; and the real method returns to the game
 *
 * eax/ecx/edx are the caller-saved registers under stdcall, so clobbering them
 * before entering the callee is exactly what a compiler would have done.
 *
 * The trampolines are emitted by .rept at a FIXED 32-byte stride so the two
 * exported tables can be built by arithmetic; the bodies are at most 20 bytes.
 * `.org base + (n+1)*STRIDE` is what sets the stride, and it is also the check:
 * should a body ever outgrow STRIDE, .org cannot move backwards and the file
 * REFUSES TO ASSEMBLE. Padding with .balign instead would silently push the
 * next trampoline out and leave every later table entry pointing into the
 * middle of an instruction -- a proxy that crashes the game in a way that
 * looks like the game. The tables are what proxy.c copies into each wrapper's
 * vtable before overwriting the observed slots.
 *
 * mingw32 prefixes C symbols with an underscore, hence _fwd8_table.
 */
    .intel_syntax noprefix

#define D3D8_SLOTS      16      /* IDirect3D8:       slots 0..15  */
#define D3DDEV_SLOTS    97      /* IDirect3DDevice8: slots 0..96  */
#define D3DVB_SLOTS     14      /* IDirect3DVertexBuffer8: 0..13  */
#define STRIDE          32

    .text

    .balign STRIDE
fwd8_base:
    .set n, 0
    .rept D3D8_SLOTS
    mov     eax, [esp+4]
    mov     edx, [eax+4]
    mov     ecx, [eax+8]
    mov     [esp+4], edx
    jmp     dword ptr [ecx + n*4]
    .org    fwd8_base + (n + 1) * STRIDE
    .set n, n + 1
    .endr

    .balign STRIDE
fwddev_base:
    .set n, 0
    .rept D3DDEV_SLOTS
    mov     eax, [esp+4]
    mov     edx, [eax+4]
    mov     ecx, [eax+8]
    mov     [esp+4], edx
    jmp     dword ptr [ecx + n*4]
    .org    fwddev_base + (n + 1) * STRIDE
    .set n, n + 1
    .endr

    .balign STRIDE
fwdvb_base:
    .set n, 0
    .rept D3DVB_SLOTS
    mov     eax, [esp+4]
    mov     edx, [eax+4]
    mov     ecx, [eax+8]
    mov     [esp+4], edx
    jmp     dword ptr [ecx + n*4]
    .org    fwdvb_base + (n + 1) * STRIDE
    .set n, n + 1
    .endr

    .section .rdata,"dr"
    .globl _fwd8_table
_fwd8_table:
    .set n, 0
    .rept D3D8_SLOTS
    .long fwd8_base + n * STRIDE
    .set n, n + 1
    .endr

    .globl _fwddev_table
_fwddev_table:
    .set n, 0
    .rept D3DDEV_SLOTS
    .long fwddev_base + n * STRIDE
    .set n, n + 1
    .endr

    .globl _fwdvb_table
_fwdvb_table:
    .set n, 0
    .rept D3DVB_SLOTS
    .long fwdvb_base + n * STRIDE
    .set n, n + 1
    .endr
