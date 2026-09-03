#ifndef D3D8_RENDER_STATES_H
#define D3D8_RENDER_STATES_H

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
#define D3DRS_ZENABLE            7
#define D3DRS_ZWRITEENABLE      14
#define D3DRS_ALPHATESTENABLE   15
#define D3DRS_SRCBLEND          19
#define D3DRS_DESTBLEND         20
#define D3DRS_CULLMODE          22
#define D3DRS_ZFUNC             23
#define D3DRS_ZBIAS             47
#define D3DRS_ALPHAREF          24
#define D3DRS_ALPHAFUNC         25
#define D3DRS_ALPHABLENDENABLE  27
#define D3DRS_LIGHTING         137
#define D3DRS_AMBIENT          139
#define D3DRS_COLORVERTEX      141
#define D3DRS_NORMALIZENORMALS 143
#define D3DRS_DIFFUSEMATERIALSOURCE 145
#define D3DRS_AMBIENTMATERIALSOURCE 147
#define D3DRS_EMISSIVEMATERIALSOURCE 148
#define D3DRS_TEXTUREFACTOR     60

/* D3DTSS_*, the ones this reads. */
#define D3DTOP_SELECTARG2        3
#define D3DTSS_COLOROP           1
#define D3DTSS_ADDRESSU          13
#define D3DTSS_ADDRESSV          14
#define D3DTSS_MAGFILTER         16
#define D3DTSS_MINFILTER         17
#define D3DTSS_MIPFILTER         18
#define D3DTSS_MIPMAPLODBIAS     19
#define D3DTSS_MAXANISOTROPY     21
#define D3DTSS_COLORARG1          2
#define D3DTSS_COLORARG2          3
#define D3DTSS_ALPHAOP            4
#define D3DTSS_ALPHAARG1          5
#define D3DTSS_ALPHAARG2          6
#define D3DTSS_TEXCOORDINDEX     11
#define D3DTSS_TEXTURETRANSFORMFLAGS 24

/* D3DTOP_* */
#define D3DTOP_DISABLE           1
#define D3DTOP_SELECTARG1        2
#define D3DTOP_MODULATE          4
#define D3DTOP_ADD               7

/* D3DTS_* */
#define D3DTS_VIEW        2
#define D3DTS_PROJECTION  3
#define D3DTS_TEXTURE0   16
#define D3DTS_WORLD     256

#endif /* D3D8_RENDER_STATES_H */
