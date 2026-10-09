#pragma once

namespace x2::d3d8 {

/*
 * The D3D8 enumerator values this port reads, by their numbers.
 *
 * Only the ones something here actually consults are listed: a value nothing
 * reads would suggest the state is honoured when it is not. They live in one
 * header because two owners now index the same state arrays -- the draw
 * translator and the lighting owner -- and two copies of a number that must
 * agree is exactly the kind of drift this file exists to prevent.
 */
/* D3DRS_*, the ones this reads. */
inline constexpr int D3DRS_ZENABLE = 7;
inline constexpr int D3DRS_ZWRITEENABLE = 14;
inline constexpr int D3DRS_ALPHATESTENABLE = 15;
inline constexpr int D3DRS_SRCBLEND = 19;
inline constexpr int D3DRS_DESTBLEND = 20;
inline constexpr int D3DRS_CULLMODE = 22;
inline constexpr int D3DRS_ZFUNC = 23;
inline constexpr int D3DRS_ZBIAS = 47;
inline constexpr int D3DRS_ALPHAREF = 24;
inline constexpr int D3DRS_ALPHAFUNC = 25;
inline constexpr int D3DRS_ALPHABLENDENABLE = 27;
inline constexpr int D3DRS_LIGHTING = 137;
inline constexpr int D3DRS_AMBIENT = 139;
inline constexpr int D3DRS_COLORVERTEX = 141;
inline constexpr int D3DRS_NORMALIZENORMALS = 143;
inline constexpr int D3DRS_DIFFUSEMATERIALSOURCE = 145;
inline constexpr int D3DRS_AMBIENTMATERIALSOURCE = 147;
inline constexpr int D3DRS_EMISSIVEMATERIALSOURCE = 148;
inline constexpr int D3DRS_TEXTUREFACTOR = 60;

/* D3DTSS_*, the ones this reads. */
inline constexpr int D3DTOP_SELECTARG2 = 3;
inline constexpr int D3DTSS_COLOROP = 1;
inline constexpr int D3DTSS_ADDRESSU = 13;
inline constexpr int D3DTSS_ADDRESSV = 14;
inline constexpr int D3DTSS_MAGFILTER = 16;
inline constexpr int D3DTSS_MINFILTER = 17;
inline constexpr int D3DTSS_MIPFILTER = 18;
inline constexpr int D3DTSS_MIPMAPLODBIAS = 19;
inline constexpr int D3DTSS_MAXANISOTROPY = 21;
inline constexpr int D3DTSS_COLORARG1 = 2;
inline constexpr int D3DTSS_COLORARG2 = 3;
inline constexpr int D3DTSS_ALPHAOP = 4;
inline constexpr int D3DTSS_ALPHAARG1 = 5;
inline constexpr int D3DTSS_ALPHAARG2 = 6;
inline constexpr int D3DTSS_TEXCOORDINDEX = 11;
inline constexpr int D3DTSS_TEXTURETRANSFORMFLAGS = 24;

/* D3DTOP_* */
inline constexpr int D3DTOP_DISABLE = 1;
inline constexpr int D3DTOP_SELECTARG1 = 2;
inline constexpr int D3DTOP_MODULATE = 4;
inline constexpr int D3DTOP_ADD = 7;

/* D3DTS_* */
inline constexpr int D3DTS_VIEW = 2;
inline constexpr int D3DTS_PROJECTION = 3;
inline constexpr int D3DTS_TEXTURE0 = 16;
inline constexpr int D3DTS_WORLD = 256;

} // namespace x2::d3d8
