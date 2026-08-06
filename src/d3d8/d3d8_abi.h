/*
 * The PC Direct3D 8 COM interfaces, as method ORDER and argument COUNT.
 *
 * This is the file everything else in src/d3d8/ is built from, and it is the
 * one place a method's slot may be written down. Getting an entry wrong is not
 * a bug that shows up here: the guest would call slot N, land on a different
 * method, and pop the wrong number of arguments -- which shifts the guest stack
 * and faults somewhere with no connection to DirectX at all.
 *
 * ---- why these numbers are trustworthy ----
 *
 * Not recalled. Three independent sources agree, and tools/d3d8_abi_check.py
 * re-checks two of them mechanically:
 *
 *  1. THE GAME. Gap::Gfx::igDxVisualContext::userInstantiate (libIGGfx
 *     0x1002c210) calls Direct3DCreate8, stores the result at this+0x140, and
 *     immediately calls `[EDX+0x34]` with three arguments plus this -- which is
 *     IDirect3D8::GetDeviceCaps(Adapter, DeviceType, pCaps), entry 13 below.
 *     The same function allocates 0xd4 bytes for the caps block and two 0x34
 *     blocks it zeroes 13 dwords at a time: sizeof(D3DCAPS8) == 212 and
 *     sizeof(D3DPRESENT_PARAMETERS) == 52, both exactly as declared here.
 *  2. THE PROJECT'S OWN MEASUREMENT. C128 scanned every device-touching engine
 *     body and found offset 0xc8 taking over half the call sites; 0xc8 is
 *     entry 50 below, SetRenderState -- which is precisely the method a
 *     renderer calls more than any other.
 *  3. A REAL d3d8.h, when the machine has one. tools/d3d8_abi_check.py diffs
 *     this table against it name-for-name and count-for-count. No header is
 *     committed here -- the checker says loudly when it found none, so a run
 *     that verified NOTHING cannot be mistaken for one that passed.
 *
 * ---- the calling convention ----
 *
 * COM on 32-bit Windows is __stdcall with `this` passed as the FIRST STACK
 * ARGUMENT, not in ECX. The call site above proves it: `this` (EAX) is pushed
 * LAST, so it lands at the lowest address. The counts below EXCLUDE `this`, so
 * a method pops `1 + args` dwords on return.
 */
#ifndef D3D8_ABI_H
#define D3D8_ABI_H

/*
 * One X-macro per interface: X(slot, Name, args-excluding-this).
 *
 * The slot is written out rather than implied by position so that a
 * mis-ordered or dropped line is a compile-time contradiction (d3d8_com.c
 * checks each entry lands at its declared index) instead of a silent shift.
 */

#define D3D8_IFACE_IDirect3D8(X)                                              \
    X(  0, QueryInterface,              2)                                    \
    X(  1, AddRef,                      0)                                    \
    X(  2, Release,                     0)                                    \
    X(  3, RegisterSoftwareDevice,      1)                                    \
    X(  4, GetAdapterCount,             0)                                    \
    X(  5, GetAdapterIdentifier,        3)                                    \
    X(  6, GetAdapterModeCount,         1)                                    \
    X(  7, EnumAdapterModes,            3)                                    \
    X(  8, GetAdapterDisplayMode,       2)                                    \
    X(  9, CheckDeviceType,             5)                                    \
    X( 10, CheckDeviceFormat,           6)                                    \
    X( 11, CheckDeviceMultiSampleType,  5)                                    \
    X( 12, CheckDepthStencilMatch,      5)                                    \
    X( 13, GetDeviceCaps,               3)                                    \
    X( 14, GetAdapterMonitor,           1)                                    \
    X( 15, CreateDevice,                6)

#define D3D8_IFACE_IDirect3DDevice8(X)                                        \
    X(  0, QueryInterface,              2)                                    \
    X(  1, AddRef,                      0)                                    \
    X(  2, Release,                     0)                                    \
    X(  3, TestCooperativeLevel,        0)                                    \
    X(  4, GetAvailableTextureMem,      0)                                    \
    X(  5, ResourceManagerDiscardBytes, 1)                                    \
    X(  6, GetDirect3D,                 1)                                    \
    X(  7, GetDeviceCaps,               1)                                    \
    X(  8, GetDisplayMode,              1)                                    \
    X(  9, GetCreationParameters,       1)                                    \
    X( 10, SetCursorProperties,         3)                                    \
    X( 11, SetCursorPosition,           3)                                    \
    X( 12, ShowCursor,                  1)                                    \
    X( 13, CreateAdditionalSwapChain,   2)                                    \
    X( 14, Reset,                       1)                                    \
    X( 15, Present,                     4)                                    \
    X( 16, GetBackBuffer,               3)                                    \
    X( 17, GetRasterStatus,             1)                                    \
    X( 18, SetGammaRamp,                2)                                    \
    X( 19, GetGammaRamp,                1)                                    \
    X( 20, CreateTexture,               7)                                    \
    X( 21, CreateVolumeTexture,         8)                                    \
    X( 22, CreateCubeTexture,           6)                                    \
    X( 23, CreateVertexBuffer,          5)                                    \
    X( 24, CreateIndexBuffer,           5)                                    \
    X( 25, CreateRenderTarget,          6)                                    \
    X( 26, CreateDepthStencilSurface,   5)                                    \
    X( 27, CreateImageSurface,          4)                                    \
    X( 28, CopyRects,                   5)                                    \
    X( 29, UpdateTexture,               2)                                    \
    X( 30, GetFrontBuffer,              1)                                    \
    X( 31, SetRenderTarget,             2)                                    \
    X( 32, GetRenderTarget,             1)                                    \
    X( 33, GetDepthStencilSurface,      1)                                    \
    X( 34, BeginScene,                  0)                                    \
    X( 35, EndScene,                    0)                                    \
    X( 36, Clear,                       6)                                    \
    X( 37, SetTransform,                2)                                    \
    X( 38, GetTransform,                2)                                    \
    X( 39, MultiplyTransform,           2)                                    \
    X( 40, SetViewport,                 1)                                    \
    X( 41, GetViewport,                 1)                                    \
    X( 42, SetMaterial,                 1)                                    \
    X( 43, GetMaterial,                 1)                                    \
    X( 44, SetLight,                    2)                                    \
    X( 45, GetLight,                    2)                                    \
    X( 46, LightEnable,                 2)                                    \
    X( 47, GetLightEnable,              2)                                    \
    X( 48, SetClipPlane,                2)                                    \
    X( 49, GetClipPlane,                2)                                    \
    X( 50, SetRenderState,              2)                                    \
    X( 51, GetRenderState,              2)                                    \
    X( 52, BeginStateBlock,             0)                                    \
    X( 53, EndStateBlock,               1)                                    \
    X( 54, ApplyStateBlock,             1)                                    \
    X( 55, CaptureStateBlock,           1)                                    \
    X( 56, DeleteStateBlock,            1)                                    \
    X( 57, CreateStateBlock,            2)                                    \
    X( 58, SetClipStatus,               1)                                    \
    X( 59, GetClipStatus,               1)                                    \
    X( 60, GetTexture,                  2)                                    \
    X( 61, SetTexture,                  2)                                    \
    X( 62, GetTextureStageState,        3)                                    \
    X( 63, SetTextureStageState,        3)                                    \
    X( 64, ValidateDevice,              1)                                    \
    X( 65, GetInfo,                     3)                                    \
    X( 66, SetPaletteEntries,           2)                                    \
    X( 67, GetPaletteEntries,           2)                                    \
    X( 68, SetCurrentTexturePalette,    1)                                    \
    X( 69, GetCurrentTexturePalette,    1)                                    \
    X( 70, DrawPrimitive,               3)                                    \
    X( 71, DrawIndexedPrimitive,        5)                                    \
    X( 72, DrawPrimitiveUP,             4)                                    \
    X( 73, DrawIndexedPrimitiveUP,      8)                                    \
    X( 74, ProcessVertices,             5)                                    \
    X( 75, CreateVertexShader,          4)                                    \
    X( 76, SetVertexShader,             1)                                    \
    X( 77, GetVertexShader,             1)                                    \
    X( 78, DeleteVertexShader,          1)                                    \
    X( 79, SetVertexShaderConstant,     3)                                    \
    X( 80, GetVertexShaderConstant,     3)                                    \
    X( 81, GetVertexShaderDeclaration,  3)                                    \
    X( 82, GetVertexShaderFunction,     3)                                    \
    X( 83, SetStreamSource,             3)                                    \
    X( 84, GetStreamSource,             3)                                    \
    X( 85, SetIndices,                  2)                                    \
    X( 86, GetIndices,                  2)                                    \
    X( 87, CreatePixelShader,           2)                                    \
    X( 88, SetPixelShader,              1)                                    \
    X( 89, GetPixelShader,              1)                                    \
    X( 90, DeletePixelShader,           1)                                    \
    X( 91, SetPixelShaderConstant,      3)                                    \
    X( 92, GetPixelShaderConstant,      3)                                    \
    X( 93, GetPixelShaderFunction,      3)                                    \
    X( 94, DrawRectPatch,               3)                                    \
    X( 95, DrawTriPatch,                3)                                    \
    X( 96, DeletePatch,                 1)

/* IDirect3DResource8's eleven methods are the prefix of every resource
   interface below, which is why each repeats them rather than sharing a table:
   a guest vtable is a flat array, and the prefix must be spelled out where it
   is used or a slot is silently off by the length of the base. */
#define D3D8_RESOURCE_PREFIX(X)                                               \
    X(  0, QueryInterface,              2)                                    \
    X(  1, AddRef,                      0)                                    \
    X(  2, Release,                     0)                                    \
    X(  3, GetDevice,                   1)                                    \
    X(  4, SetPrivateData,              4)                                    \
    X(  5, GetPrivateData,              3)                                    \
    X(  6, FreePrivateData,             1)                                    \
    X(  7, SetPriority,                 1)                                    \
    X(  8, GetPriority,                 0)                                    \
    X(  9, PreLoad,                     0)                                    \
    X( 10, GetType,                     0)

/* IDirect3DBaseTexture8 adds three; the concrete textures add five more. */
#define D3D8_BASETEXTURE_PREFIX(X)                                            \
    D3D8_RESOURCE_PREFIX(X)                                                   \
    X( 11, SetLOD,                      1)                                    \
    X( 12, GetLOD,                      0)                                    \
    X( 13, GetLevelCount,               0)

#define D3D8_IFACE_IDirect3DBaseTexture8(X) D3D8_BASETEXTURE_PREFIX(X)

#define D3D8_IFACE_IDirect3DTexture8(X)                                       \
    D3D8_BASETEXTURE_PREFIX(X)                                                \
    X( 14, GetLevelDesc,                2)                                    \
    X( 15, GetSurfaceLevel,             2)                                    \
    X( 16, LockRect,                    4)                                    \
    X( 17, UnlockRect,                  1)                                    \
    X( 18, AddDirtyRect,                1)

#define D3D8_IFACE_IDirect3DCubeTexture8(X)                                   \
    D3D8_BASETEXTURE_PREFIX(X)                                                \
    X( 14, GetLevelDesc,                2)                                    \
    X( 15, GetCubeMapSurface,           3)                                    \
    X( 16, LockRect,                    5)                                    \
    X( 17, UnlockRect,                  2)                                    \
    X( 18, AddDirtyRect,                2)

#define D3D8_IFACE_IDirect3DVolumeTexture8(X)                                 \
    D3D8_BASETEXTURE_PREFIX(X)                                                \
    X( 14, GetLevelDesc,                2)                                    \
    X( 15, GetVolumeLevel,              2)                                    \
    X( 16, LockBox,                     4)                                    \
    X( 17, UnlockBox,                   1)                                    \
    X( 18, AddDirtyBox,                 1)

#define D3D8_IFACE_IDirect3DVertexBuffer8(X)                                  \
    D3D8_RESOURCE_PREFIX(X)                                                   \
    X( 11, Lock,                        4)                                    \
    X( 12, Unlock,                      0)                                    \
    X( 13, GetDesc,                     1)

#define D3D8_IFACE_IDirect3DIndexBuffer8(X)                                   \
    D3D8_RESOURCE_PREFIX(X)                                                   \
    X( 11, Lock,                        4)                                    \
    X( 12, Unlock,                      0)                                    \
    X( 13, GetDesc,                     1)

/* Surfaces and volumes are NOT resources -- they descend straight from
   IUnknown, so their slot 3 is GetDevice with no SetPriority/PreLoad/GetType
   after it. Treating a surface as a resource would put LockRect three slots
   too late. */
#define D3D8_IFACE_IDirect3DSurface8(X)                                       \
    X(  0, QueryInterface,              2)                                    \
    X(  1, AddRef,                      0)                                    \
    X(  2, Release,                     0)                                    \
    X(  3, GetDevice,                   1)                                    \
    X(  4, SetPrivateData,              4)                                    \
    X(  5, GetPrivateData,              3)                                    \
    X(  6, FreePrivateData,             1)                                    \
    X(  7, GetContainer,                2)                                    \
    X(  8, GetDesc,                     1)                                    \
    X(  9, LockRect,                    3)                                    \
    X( 10, UnlockRect,                  0)

#define D3D8_IFACE_IDirect3DVolume8(X)                                        \
    X(  0, QueryInterface,              2)                                    \
    X(  1, AddRef,                      0)                                    \
    X(  2, Release,                     0)                                    \
    X(  3, GetDevice,                   1)                                    \
    X(  4, SetPrivateData,              4)                                    \
    X(  5, GetPrivateData,              3)                                    \
    X(  6, FreePrivateData,             1)                                    \
    X(  7, GetContainer,                2)                                    \
    X(  8, GetDesc,                     1)                                    \
    X(  9, LockBox,                     3)                                    \
    X( 10, UnlockBox,                   0)

#define D3D8_IFACE_IDirect3DSwapChain8(X)                                     \
    X(  0, QueryInterface,              2)                                    \
    X(  1, AddRef,                      0)                                    \
    X(  2, Release,                     0)                                    \
    X(  3, Present,                     4)                                    \
    X(  4, GetBackBuffer,               3)

/* Every interface this host can present to the guest, in one list, so that
   adding one cannot mean forgetting to register it. */
#define D3D8_INTERFACES(I)                                                    \
    I(IDirect3D8)                                                             \
    I(IDirect3DDevice8)                                                       \
    I(IDirect3DBaseTexture8)                                                  \
    I(IDirect3DTexture8)                                                      \
    I(IDirect3DCubeTexture8)                                                  \
    I(IDirect3DVolumeTexture8)                                                \
    I(IDirect3DVertexBuffer8)                                                 \
    I(IDirect3DIndexBuffer8)                                                  \
    I(IDirect3DSurface8)                                                      \
    I(IDirect3DVolume8)                                                       \
    I(IDirect3DSwapChain8)

#endif /* D3D8_ABI_H */
