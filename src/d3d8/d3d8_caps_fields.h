/*
 * D3DCAPS8, field by field, ONCE -- so the port and the stock game can be
 * asked the same question in the same words.
 *
 * src/d3d8/d3d8_caps.c is a DECLARED PROFILE: every bit is a promise to the
 * engine, and igCapabilityManager reads it once and branches on it for the
 * rest of the run. Its own header says so, and says what settles it: "any
 * engine behaviour that depends on a bit here and does not match the Wine
 * oracle running the stock game means this block, not the renderer, is what
 * is wrong." Until tools/proxy_d3d8 existed there was no way to ask the
 * oracle. Now there is, and the comparison is only worth anything if both
 * sides print IDENTICAL text -- hence one list, included by both.
 *
 * The proxy is a 32-bit mingw DLL and this host is 64-bit ELF; nothing here
 * may reference either. It is names and types only.
 *
 * F(name)  a DWORD field, printed as hex
 * G(name)  a float field, printed as a decimal
 */
#ifndef D3D8_CAPS_FIELDS_H
#define D3D8_CAPS_FIELDS_H

#define D3D8_CAPS_FIELDS                                                       \
  F(DeviceType)                                                                \
  F(AdapterOrdinal)                                                            \
  F(Caps)                                                                      \
  F(Caps2)                                                                     \
  F(Caps3)                                                                     \
  F(PresentationIntervals)                                                     \
  F(CursorCaps)                                                                \
  F(DevCaps)                                                                   \
  F(PrimitiveMiscCaps)                                                         \
  F(RasterCaps)                                                                \
  F(ZCmpCaps)                                                                  \
  F(SrcBlendCaps)                                                              \
  F(DestBlendCaps)                                                             \
  F(AlphaCmpCaps)                                                              \
  F(ShadeCaps)                                                                 \
  F(TextureCaps)                                                               \
  F(TextureFilterCaps)                                                         \
  F(CubeTextureFilterCaps)                                                     \
  F(VolumeTextureFilterCaps)                                                   \
  F(TextureAddressCaps)                                                        \
  F(VolumeTextureAddressCaps)                                                  \
  F(LineCaps)                                                                  \
  F(MaxTextureWidth)                                                           \
  F(MaxTextureHeight)                                                          \
  F(MaxVolumeExtent)                                                           \
  F(MaxTextureRepeat)                                                          \
  F(MaxTextureAspectRatio)                                                     \
  F(MaxAnisotropy)                                                             \
  G(MaxVertexW)                                                                \
  G(GuardBandLeft)                                                             \
  G(GuardBandTop)                                                              \
  G(GuardBandRight)                                                            \
  G(GuardBandBottom)                                                           \
  G(ExtentsAdjust)                                                             \
  F(StencilCaps)                                                               \
  F(FVFCaps)                                                                   \
  F(TextureOpCaps)                                                             \
  F(MaxTextureBlendStages)                                                     \
  F(MaxSimultaneousTextures)                                                   \
  F(VertexProcessingCaps)                                                      \
  F(MaxActiveLights)                                                           \
  F(MaxUserClipPlanes)                                                         \
  F(MaxVertexBlendMatrices)                                                    \
  F(MaxVertexBlendMatrixIndex)                                                 \
  G(MaxPointSize)                                                              \
  F(MaxPrimitiveCount)                                                         \
  F(MaxVertexIndex)                                                            \
  F(MaxStreams)                                                                \
  F(MaxStreamStride)                                                           \
  F(VertexShaderVersion)                                                       \
  F(MaxVertexShaderConst)                                                      \
  F(PixelShaderVersion)                                                        \
  G(MaxPixelShaderValue)

/* 53 fields of 4 bytes each. Both sides assert this against their own struct,
   so a layout that has drifted is a build error and not a silent misread. */
#define D3D8_CAPS_FIELD_COUNT 53

#endif /* D3D8_CAPS_FIELDS_H */
