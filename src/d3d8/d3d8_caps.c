/*
 * The capability block this host reports for its adapter.
 *
 * ---- read this before changing a bit ----
 *
 * This is a DECLARED PROFILE, not a measurement. Every bit set here is a
 * promise to the engine that it may use that feature, and the engine believes
 * it: igCapabilityManager reads these once and branches on them for the rest
 * of the run, so a bit set for something the backend cannot do produces wrong
 * rendering far away from here, and a bit cleared silently costs a feature.
 *
 * The profile is the one the game was written against -- a 2005 DirectX 8
 * fixed-function-plus-shaders card -- with two deliberate departures:
 *
 *   * MaxTextureWidth/Height and MaxAnisotropy come from the host GPU where
 *     the backend can report them, because those are the ones the engine uses
 *     to SIZE things rather than to choose a path.
 *   * VertexShaderVersion and PixelShaderVersion are declared 1.1, not 0.
 *     libIGGfx shades through NVIDIA Cg (C112), and cg.dll does not load here,
 *     so the shader path is not exercised yet -- but reporting 0 makes the
 *     engine take a fixed-function fallback path that the real game never
 *     runs, which would mean debugging rendering the shipped game never did.
 *
 * Anything drawn while this profile is unverified is drawn on a promise. The
 * claim that records it carries the falsifier: any engine behaviour that
 * depends on a bit here and does not match the Wine oracle running the stock
 * game means this block, not the renderer, is what is wrong.
 */
#include "d3d8_caps.h"
#include "d3d8_caps_fields.h"
#include "d3d8_state.h"

#include <stdio.h>
#include <string.h>

/* Only the bits actually set below are named, with the values from a real
   d3d8caps.h. tools/d3d8_abi_check.py re-checks them against one when the
   machine has it. */
#define D3DCAPS_READ_SCANLINE 0x00020000u

#define D3DCAPS2_FULLSCREENGAMMA 0x00020000u
#define D3DCAPS2_CANRENDERWINDOWED 0x00080000u
#define D3DCAPS2_CANCALIBRATEGAMMA 0x00100000u
#define D3DCAPS2_CANMANAGERESOURCE 0x10000000u
#define D3DCAPS2_DYNAMICTEXTURES 0x20000000u

#define D3DCAPS3_ALPHA_FULLSCREEN_FLIP_OR_DISCARD 0x00000020u

#define D3DPRESENT_INTERVAL_DEFAULT 0x00000000u
#define D3DPRESENT_INTERVAL_ONE 0x00000001u
#define D3DPRESENT_INTERVAL_IMMEDIATE 0x80000000u

#define D3DCURSORCAPS_COLOR 0x00000001u

#define D3DDEVCAPS_EXECUTESYSTEMMEMORY 0x00000010u
#define D3DDEVCAPS_EXECUTEVIDEOMEMORY 0x00000020u
#define D3DDEVCAPS_TLVERTEXSYSTEMMEMORY 0x00000040u
#define D3DDEVCAPS_TLVERTEXVIDEOMEMORY 0x00000080u
#define D3DDEVCAPS_TEXTURESYSTEMMEMORY 0x00000100u
#define D3DDEVCAPS_TEXTUREVIDEOMEMORY 0x00000200u
#define D3DDEVCAPS_DRAWPRIMTLVERTEX 0x00000400u
#define D3DDEVCAPS_CANRENDERAFTERFLIP 0x00000800u
#define D3DDEVCAPS_DRAWPRIMITIVES2 0x00002000u
#define D3DDEVCAPS_DRAWPRIMITIVES2EX 0x00008000u
#define D3DDEVCAPS_HWTRANSFORMANDLIGHT 0x00010000u
#define D3DDEVCAPS_HWRASTERIZATION 0x00080000u
#define D3DDEVCAPS_PUREDEVICE 0x00100000u

#define D3DPMISCCAPS_MASKZ 0x00000002u
#define D3DPMISCCAPS_CULLNONE 0x00000010u
#define D3DPMISCCAPS_CULLCW 0x00000020u
#define D3DPMISCCAPS_CULLCCW 0x00000040u
#define D3DPMISCCAPS_COLORWRITEENABLE 0x00000080u
#define D3DPMISCCAPS_CLIPPLANESCALEDPOINTS 0x00000100u
#define D3DPMISCCAPS_CLIPTLVERTS 0x00000200u
#define D3DPMISCCAPS_TSSARGTEMP 0x00000400u
#define D3DPMISCCAPS_BLENDOP 0x00000800u

#define D3DPRASTERCAPS_DITHER 0x00000001u
#define D3DPRASTERCAPS_ZTEST 0x00000010u
#define D3DPRASTERCAPS_FOGVERTEX 0x00000080u
#define D3DPRASTERCAPS_FOGTABLE 0x00000100u
#define D3DPRASTERCAPS_MIPMAPLODBIAS 0x00002000u
#define D3DPRASTERCAPS_ZBIAS 0x00004000u
#define D3DPRASTERCAPS_FOGRANGE 0x00010000u
#define D3DPRASTERCAPS_ANISOTROPY 0x00020000u
#define D3DPRASTERCAPS_WBUFFER 0x00040000u
#define D3DPRASTERCAPS_WFOG 0x00100000u
#define D3DPRASTERCAPS_ZFOG 0x00200000u
#define D3DPRASTERCAPS_COLORPERSPECTIVE 0x00400000u

/* The eight comparison functions; the engine expects every one. */
#define D3DPCMPCAPS_ALL 0x000000FFu

#define D3DPBLENDCAPS_ZERO 0x00000001u
#define D3DPBLENDCAPS_ONE 0x00000002u
#define D3DPBLENDCAPS_SRCCOLOR 0x00000004u
#define D3DPBLENDCAPS_INVSRCCOLOR 0x00000008u
#define D3DPBLENDCAPS_SRCALPHA 0x00000010u
#define D3DPBLENDCAPS_INVSRCALPHA 0x00000020u
#define D3DPBLENDCAPS_DESTALPHA 0x00000040u
#define D3DPBLENDCAPS_INVDESTALPHA 0x00000080u
#define D3DPBLENDCAPS_DESTCOLOR 0x00000100u
#define D3DPBLENDCAPS_INVDESTCOLOR 0x00000200u
#define D3DPBLENDCAPS_SRCALPHASAT 0x00000400u
#define D3DPBLENDCAPS_BOTHSRCALPHA 0x00000800u
#define D3DPBLENDCAPS_BOTHINVSRCALPHA 0x00001000u
#define D3DPBLENDCAPS_ALL 0x00001FFFu

#define D3DPSHADECAPS_COLORGOURAUDRGB 0x00000008u
#define D3DPSHADECAPS_SPECULARGOURAUDRGB 0x00000200u
#define D3DPSHADECAPS_ALPHAGOURAUDBLEND 0x00004000u
#define D3DPSHADECAPS_FOGGOURAUD 0x00080000u

#define D3DPTEXTURECAPS_PERSPECTIVE 0x00000001u
#define D3DPTEXTURECAPS_ALPHA 0x00000004u
#define D3DPTEXTURECAPS_ALPHAPALETTE 0x00000080u
#define D3DPTEXTURECAPS_NONPOW2CONDITIONAL 0x00000100u
#define D3DPTEXTURECAPS_CUBEMAP 0x00000800u
#define D3DPTEXTURECAPS_VOLUMEMAP 0x00002000u
#define D3DPTEXTURECAPS_MIPMAP 0x00004000u
#define D3DPTEXTURECAPS_MIPVOLUMEMAP 0x00008000u
#define D3DPTEXTURECAPS_MIPCUBEMAP 0x00010000u

#define D3DPTFILTERCAPS_MINFPOINT 0x00000100u
#define D3DPTFILTERCAPS_MINFLINEAR 0x00000200u
#define D3DPTFILTERCAPS_MINFANISOTROPIC 0x00000400u
#define D3DPTFILTERCAPS_MIPFPOINT 0x00010000u
#define D3DPTFILTERCAPS_MIPFLINEAR 0x00020000u
#define D3DPTFILTERCAPS_MAGFPOINT 0x01000000u
#define D3DPTFILTERCAPS_MAGFLINEAR 0x02000000u
#define D3DPTFILTERCAPS_MAGFANISOTROPIC 0x04000000u

#define D3DPTADDRESSCAPS_WRAP 0x00000001u
#define D3DPTADDRESSCAPS_MIRROR 0x00000002u
#define D3DPTADDRESSCAPS_CLAMP 0x00000004u
#define D3DPTADDRESSCAPS_BORDER 0x00000008u
#define D3DPTADDRESSCAPS_INDEPENDENTUV 0x00000010u
#define D3DPTADDRESSCAPS_MIRRORONCE 0x00000020u

#define D3DLINECAPS_TEXTURE 0x00000001u
#define D3DLINECAPS_ZTEST 0x00000002u
#define D3DLINECAPS_BLEND 0x00000004u
#define D3DLINECAPS_ALPHACMP 0x00000008u
#define D3DLINECAPS_FOG 0x00000010u

#define D3DSTENCILCAPS_ALL 0x000000FFu

#define D3DFVFCAPS_PSIZE 0x00100000u

#define D3DTEXOPCAPS_COMMON 0x03FFFFFFu

#define D3DVTXPCAPS_TEXGEN 0x00000001u
#define D3DVTXPCAPS_MATERIALSOURCE7 0x00000002u
#define D3DVTXPCAPS_DIRECTIONALLIGHTS 0x00000008u
#define D3DVTXPCAPS_POSITIONALLIGHTS 0x00000010u
#define D3DVTXPCAPS_LOCALVIEWER 0x00000020u
#define D3DVTXPCAPS_TWEENING 0x00000040u

#define D3DVS_VERSION_1_1 0xFFFE0101u
#define D3DPS_VERSION_1_1 0xFFFF0101u

static float f(float v) { return v; }

void d3d8_caps_fill(D3DCAPS8 *c, uint32_t adapter, uint32_t devtype,
                    const D3D8CapsLimits *hw) {
  memset(c, 0, sizeof *c);

  c->DeviceType = devtype;
  c->AdapterOrdinal = adapter;

  c->Caps = D3DCAPS_READ_SCANLINE;
  c->Caps2 = D3DCAPS2_FULLSCREENGAMMA | D3DCAPS2_CANRENDERWINDOWED |
             D3DCAPS2_CANCALIBRATEGAMMA | D3DCAPS2_CANMANAGERESOURCE |
             D3DCAPS2_DYNAMICTEXTURES;
  c->Caps3 = D3DCAPS3_ALPHA_FULLSCREEN_FLIP_OR_DISCARD;
  c->PresentationIntervals = D3DPRESENT_INTERVAL_DEFAULT |
                             D3DPRESENT_INTERVAL_ONE |
                             D3DPRESENT_INTERVAL_IMMEDIATE;
  c->CursorCaps = D3DCURSORCAPS_COLOR;

  c->DevCaps = D3DDEVCAPS_EXECUTESYSTEMMEMORY | D3DDEVCAPS_EXECUTEVIDEOMEMORY |
               D3DDEVCAPS_TLVERTEXSYSTEMMEMORY |
               D3DDEVCAPS_TLVERTEXVIDEOMEMORY | D3DDEVCAPS_TEXTURESYSTEMMEMORY |
               D3DDEVCAPS_TEXTUREVIDEOMEMORY | D3DDEVCAPS_DRAWPRIMTLVERTEX |
               D3DDEVCAPS_CANRENDERAFTERFLIP | D3DDEVCAPS_DRAWPRIMITIVES2 |
               D3DDEVCAPS_DRAWPRIMITIVES2EX | D3DDEVCAPS_HWTRANSFORMANDLIGHT |
               D3DDEVCAPS_HWRASTERIZATION | D3DDEVCAPS_PUREDEVICE;

  c->PrimitiveMiscCaps =
      D3DPMISCCAPS_MASKZ | D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW |
      D3DPMISCCAPS_CULLCCW | D3DPMISCCAPS_COLORWRITEENABLE |
      D3DPMISCCAPS_CLIPPLANESCALEDPOINTS | D3DPMISCCAPS_CLIPTLVERTS |
      D3DPMISCCAPS_TSSARGTEMP | D3DPMISCCAPS_BLENDOP;

  c->RasterCaps = D3DPRASTERCAPS_DITHER | D3DPRASTERCAPS_ZTEST |
                  D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGTABLE |
                  D3DPRASTERCAPS_MIPMAPLODBIAS | D3DPRASTERCAPS_ZBIAS |
                  D3DPRASTERCAPS_FOGRANGE | D3DPRASTERCAPS_ANISOTROPY |
                  D3DPRASTERCAPS_WBUFFER | D3DPRASTERCAPS_WFOG |
                  D3DPRASTERCAPS_ZFOG | D3DPRASTERCAPS_COLORPERSPECTIVE;

  c->ZCmpCaps = D3DPCMPCAPS_ALL;
  c->AlphaCmpCaps = D3DPCMPCAPS_ALL;
  c->SrcBlendCaps = D3DPBLENDCAPS_ALL;
  c->DestBlendCaps = D3DPBLENDCAPS_ALL;

  c->ShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB |
                 D3DPSHADECAPS_SPECULARGOURAUDRGB |
                 D3DPSHADECAPS_ALPHAGOURAUDBLEND | D3DPSHADECAPS_FOGGOURAUD;

  /* NONPOW2CONDITIONAL but NOT POW2: the engine's textures are power-of-two
     anyway, and claiming the restriction would make it pad ones that are
     not. SQUAREONLY is likewise absent -- it is a restriction, not a
     feature. */
  c->TextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA |
                   D3DPTEXTURECAPS_ALPHAPALETTE |
                   D3DPTEXTURECAPS_NONPOW2CONDITIONAL |
                   D3DPTEXTURECAPS_CUBEMAP | D3DPTEXTURECAPS_VOLUMEMAP |
                   D3DPTEXTURECAPS_MIPMAP | D3DPTEXTURECAPS_MIPVOLUMEMAP |
                   D3DPTEXTURECAPS_MIPCUBEMAP;

  c->TextureFilterCaps =
      D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR |
      D3DPTFILTERCAPS_MINFANISOTROPIC | D3DPTFILTERCAPS_MIPFPOINT |
      D3DPTFILTERCAPS_MIPFLINEAR | D3DPTFILTERCAPS_MAGFPOINT |
      D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MAGFANISOTROPIC;
  c->CubeTextureFilterCaps = c->TextureFilterCaps;
  c->VolumeTextureFilterCaps = c->TextureFilterCaps;

  c->TextureAddressCaps = D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_MIRROR |
                          D3DPTADDRESSCAPS_CLAMP | D3DPTADDRESSCAPS_BORDER |
                          D3DPTADDRESSCAPS_INDEPENDENTUV |
                          D3DPTADDRESSCAPS_MIRRORONCE;
  c->VolumeTextureAddressCaps = c->TextureAddressCaps;

  c->LineCaps = D3DLINECAPS_TEXTURE | D3DLINECAPS_ZTEST | D3DLINECAPS_BLEND |
                D3DLINECAPS_ALPHACMP | D3DLINECAPS_FOG;

  c->MaxTextureWidth = hw->max_texture_dim;
  c->MaxTextureHeight = hw->max_texture_dim;
  c->MaxVolumeExtent = hw->max_volume_extent;
  c->MaxTextureRepeat = 8192;
  c->MaxTextureAspectRatio = hw->max_texture_dim;
  c->MaxAnisotropy = hw->max_anisotropy;

  /* MaxVertexW is the largest w a vertex may carry; 1e10 is what every
     hardware T&L device of the era reported. */
  c->MaxVertexW = f(1.0e10f);

  /* No guard band: clipping happens at the viewport edge. Reporting one we
     do not implement would let the engine skip its own clipping. */
  c->GuardBandLeft = c->GuardBandTop = f(0.0f);
  c->GuardBandRight = c->GuardBandBottom = f(0.0f);
  c->ExtentsAdjust = f(0.0f);

  c->StencilCaps = D3DSTENCILCAPS_ALL;
  c->FVFCaps = 8u | D3DFVFCAPS_PSIZE; /* 8 texture coordinate sets */
  c->TextureOpCaps = D3DTEXOPCAPS_COMMON;
  c->MaxTextureBlendStages = 8;
  c->MaxSimultaneousTextures = hw->max_simultaneous_textures;

  c->VertexProcessingCaps = D3DVTXPCAPS_TEXGEN | D3DVTXPCAPS_MATERIALSOURCE7 |
                            D3DVTXPCAPS_DIRECTIONALLIGHTS |
                            D3DVTXPCAPS_POSITIONALLIGHTS |
                            D3DVTXPCAPS_LOCALVIEWER | D3DVTXPCAPS_TWEENING;
  c->MaxActiveLights = 8;
  c->MaxUserClipPlanes = 6;
  c->MaxVertexBlendMatrices = 4;
  c->MaxVertexBlendMatrixIndex = 0;

  c->MaxPointSize = f(64.0f);
  c->MaxPrimitiveCount = 0x000FFFFFu;
  c->MaxVertexIndex = 0x00FFFFFFu;
  c->MaxStreams = 16;
  c->MaxStreamStride = 256;

  c->VertexShaderVersion = D3DVS_VERSION_1_1;
  c->MaxVertexShaderConst = D3D8_MAX_VS_CONSTANTS; /* one number, not two */
  c->PixelShaderVersion = D3DPS_VERSION_1_1;
  c->MaxPixelShaderValue = f(1.0f);
}

/*
 * Print the block, once per caller, in the SAME words tools/proxy_d3d8 prints
 * the real driver's.
 *
 * This file's own header says the profile is a promise and names what settles
 * it -- the stock game under Wine. That comparison was impossible until the
 * proxy existed. It is possible now, so the profile stops being unverifiable:
 * `diff` the two CAPS blocks and every promise this host makes that the
 * engine's own machine did not is a line of output.
 *
 * Printed unconditionally rather than behind a flag. It is 53 lines once per
 * run, and a diagnostic nobody remembers to switch on is a diagnostic that
 * never runs.
 */
void d3d8_caps_dump(const D3DCAPS8 *c, const char *who) {
#define F(name) printf("CAPS %-28s = 0x%08x\n", #name, (unsigned)c->name);
#define G(name) printf("CAPS %-28s = %.6f\n", #name, (double)c->name);
  printf("CAPS BLOCK from %s -- %d field(s); compare with the stock game's "
         "(tools/build_stocklog.py)\n",
         who, D3D8_CAPS_FIELD_COUNT);
  D3D8_CAPS_FIELDS
#undef F
#undef G
}

void d3d8_caps_limits_default(D3D8CapsLimits *hw) {
  hw->max_texture_dim = 4096;
  hw->max_volume_extent = 512;
  hw->max_anisotropy = 16;
  hw->max_simultaneous_textures = 8;
}
