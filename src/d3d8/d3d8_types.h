/*
 * The D3D8 structures the guest hands across the boundary, and the constants
 * this host has to recognise.
 *
 * These are LAYOUTS, not a copy of anyone's header: the guest already has the
 * real d3d8.h compiled into it, and what matters here is that a pointer it
 * passes is read at the same offsets. Two of the three are confirmed by the
 * game itself rather than by a header -- igDxVisualContext::userInstantiate
 * allocates 0xd4 bytes for the caps block and zeroes its two present-parameter
 * blocks 13 dwords at a time, so sizeof(D3DCAPS8) is 212 and
 * sizeof(D3DPRESENT_PARAMETERS) is 52, exactly as declared below.
 * tools/d3d8_abi_check.py re-checks all of them against a real d3d8.h when the
 * machine has one, and says loudly when it did not.
 *
 * Every field is four bytes, so the host struct needs no packing attribute to
 * match the guest's; the static assertions below are what makes that a checked
 * fact rather than an assumption.
 */
#ifndef D3D8_TYPES_H
#define D3D8_TYPES_H

#include <stdint.h>

typedef struct {
  uint32_t DeviceType;
  uint32_t AdapterOrdinal;
  uint32_t Caps;
  uint32_t Caps2;
  uint32_t Caps3;
  uint32_t PresentationIntervals;
  uint32_t CursorCaps;
  uint32_t DevCaps;
  uint32_t PrimitiveMiscCaps;
  uint32_t RasterCaps;
  uint32_t ZCmpCaps;
  uint32_t SrcBlendCaps;
  uint32_t DestBlendCaps;
  uint32_t AlphaCmpCaps;
  uint32_t ShadeCaps;
  uint32_t TextureCaps;
  uint32_t TextureFilterCaps;
  uint32_t CubeTextureFilterCaps;
  uint32_t VolumeTextureFilterCaps;
  uint32_t TextureAddressCaps;
  uint32_t VolumeTextureAddressCaps;
  uint32_t LineCaps;
  uint32_t MaxTextureWidth;
  uint32_t MaxTextureHeight;
  uint32_t MaxVolumeExtent;
  uint32_t MaxTextureRepeat;
  uint32_t MaxTextureAspectRatio;
  uint32_t MaxAnisotropy;
  float MaxVertexW;
  float GuardBandLeft;
  float GuardBandTop;
  float GuardBandRight;
  float GuardBandBottom;
  float ExtentsAdjust;
  uint32_t StencilCaps;
  uint32_t FVFCaps;
  uint32_t TextureOpCaps;
  uint32_t MaxTextureBlendStages;
  uint32_t MaxSimultaneousTextures;
  uint32_t VertexProcessingCaps;
  uint32_t MaxActiveLights;
  uint32_t MaxUserClipPlanes;
  uint32_t MaxVertexBlendMatrices;
  uint32_t MaxVertexBlendMatrixIndex;
  float MaxPointSize;
  uint32_t MaxPrimitiveCount;
  uint32_t MaxVertexIndex;
  uint32_t MaxStreams;
  uint32_t MaxStreamStride;
  uint32_t VertexShaderVersion;
  uint32_t MaxVertexShaderConst;
  uint32_t PixelShaderVersion;
  float MaxPixelShaderValue;
} D3DCAPS8;

typedef struct {
  uint32_t BackBufferWidth;
  uint32_t BackBufferHeight;
  uint32_t BackBufferFormat;
  uint32_t BackBufferCount;
  uint32_t MultiSampleType;
  uint32_t SwapEffect;
  uint32_t hDeviceWindow;
  uint32_t Windowed;
  uint32_t EnableAutoDepthStencil;
  uint32_t AutoDepthStencilFormat;
  uint32_t Flags;
  uint32_t FullScreen_RefreshRateInHz;
  uint32_t FullScreen_PresentationInterval;
} D3DPRESENT_PARAMETERS;

typedef struct {
  uint32_t Width;
  uint32_t Height;
  uint32_t RefreshRate;
  uint32_t Format;
} D3DDISPLAYMODE;

typedef struct {
  char Driver[512];
  char Description[512];
  uint32_t DriverVersionLow;
  uint32_t DriverVersionHigh;
  uint32_t VendorId;
  uint32_t DeviceId;
  uint32_t SubSysId;
  uint32_t Revision;
  uint8_t DeviceIdentifier[16];
  uint32_t WHQLLevel;
} D3DADAPTER_IDENTIFIER8;

typedef struct {
  uint32_t AdapterOrdinal;
  uint32_t DeviceType;
  uint32_t hFocusWindow;
  uint32_t BehaviorFlags;
} D3DDEVICE_CREATION_PARAMETERS;

typedef struct {
  uint32_t x1, y1, x2, y2;
} D3DRECT;

typedef struct {
  uint32_t X, Y, Width, Height;
  float MinZ, MaxZ;
} D3DVIEWPORT8;

/* A C89-compatible static assertion: a negative array size is a diagnostic at
   compile time, and the name of the array is what the compiler prints. */
#define D3D8_ASSERT_SIZE(type, bytes)                                          \
  typedef char d3d8_##type##_must_be_##bytes##_bytes[(sizeof(type) == (bytes)) \
                                                         ? 1                   \
                                                         : -1]

D3D8_ASSERT_SIZE(D3DCAPS8, 212);             /* the game allocates 0xd4 */
D3D8_ASSERT_SIZE(D3DPRESENT_PARAMETERS, 52); /* the game zeroes 13 dwords */
D3D8_ASSERT_SIZE(D3DDISPLAYMODE, 16);
D3D8_ASSERT_SIZE(D3DVIEWPORT8, 24);
D3D8_ASSERT_SIZE(D3DDEVICE_CREATION_PARAMETERS, 16);

/* ---- the constants this host recognises -------------------------------- */

#define D3D_SDK_VERSION_D3D8 220 /* the 0xdc the game pushes */

#define D3DDEVTYPE_HAL 1
#define D3DDEVTYPE_REF 2
#define D3DDEVTYPE_SW 3

#define D3DFMT_UNKNOWN 0
#define D3DFMT_R8G8B8 20
#define D3DFMT_A8R8G8B8 21
#define D3DFMT_X8R8G8B8 22
#define D3DFMT_R5G6B5 23
#define D3DFMT_X1R5G5B5 24
#define D3DFMT_A1R5G5B5 25
#define D3DFMT_A4R4G4B4 26
#define D3DFMT_A8 28
#define D3DFMT_DXT1 0x31545844u
#define D3DFMT_DXT2 0x32545844u
#define D3DFMT_DXT3 0x33545844u
#define D3DFMT_DXT4 0x34545844u
#define D3DFMT_DXT5 0x35545844u
#define D3DFMT_D32 71
#define D3DFMT_D15S1 73
#define D3DFMT_D24S8 75
#define D3DFMT_D16 80
#define D3DFMT_D24X8 77

#define D3DADAPTER_DEFAULT 0

#define D3D_OK 0u
#define D3DERR_NOTAVAILABLE 0x8876086Au
#define D3DERR_INVALIDCALL 0x8876086Cu
#define D3DERR_OUTOFVIDEOMEMORY 0x8876017Cu
#define E_NOINTERFACE 0x80004002u
#define E_OUTOFMEMORY 0x8007000Eu

#endif /* D3D8_TYPES_H */
