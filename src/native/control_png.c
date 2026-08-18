/*
 * A PNG encoder, written here rather than linked in.
 *
 * The control channel has to hand back a picture that anything can open, and
 * the alternatives were both worse: a raw or BMP dump needs a converter at the
 * far end (so the one thing an agent cannot do is LOOK at it), and a
 * compression library is a dependency for a feature that does not need
 * compression at all.
 *
 * So the image is a valid PNG built from DEFLATE *stored* blocks -- no
 * compression, no tables, no library, about eighty lines. The file is roughly
 * the size of the raw pixels, which for a screenshot on loopback is free.
 * Everything a PNG reader requires is here: the signature, IHDR, the zlib
 * wrapper with its Adler-32, per-scanline filter bytes, IDAT and IEND, each
 * chunk with its CRC-32.
 */
#include "control_png.h"

#include <stdlib.h>
#include <string.h>

static unsigned long crc_table[256];
static int crc_ready;

static void crc_init(void)
{
    unsigned long c;
    int n, k;
    for (n = 0; n < 256; n++) {
        c = (unsigned long)n;
        for (k = 0; k < 8; k++)
            c = (c & 1) ? 0xedb88320UL ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
    crc_ready = 1;
}

static unsigned long crc32_of(const unsigned char *b, size_t n)
{
    unsigned long c = 0xffffffffUL;
    size_t i;
    if (!crc_ready) crc_init();
    for (i = 0; i < n; i++) c = crc_table[(c ^ b[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffUL;
}

static void put_be32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

/* One chunk: length, type, data, CRC over type+data. */
static unsigned char *chunk(unsigned char *p, const char *type,
                            const unsigned char *data, size_t n)
{
    put_be32(p, (unsigned long)n);
    memcpy(p + 4, type, 4);
    if (n) memcpy(p + 8, data, n);
    put_be32(p + 8 + n, crc32_of(p + 4, n + 4));
    return p + 12 + n;
}

unsigned char *control_png_from_bgra(const unsigned char *bgra,
                                     unsigned w, unsigned h, size_t *out_len)
{
    /* Raw zlib stream: 2 header bytes, stored blocks of at most 65535 bytes
       (5 bytes of header each), 4 bytes of Adler-32. */
    size_t raw = (size_t)h * ((size_t)w * 3 + 1);          /* filter byte + RGB */
    size_t blocks = (raw + 65534) / 65535;
    size_t zlen = 2 + raw + blocks * 5 + 4;
    unsigned char *rawbuf, *z, *png, *p;
    unsigned long a = 1, b = 0;
    size_t i, done;
    unsigned y, x;

    if (!w || !h) return NULL;
    rawbuf = (unsigned char *)malloc(raw);
    z = (unsigned char *)malloc(zlen);
    png = (unsigned char *)malloc(8 + 25 + (12 + zlen) + 12);
    if (!rawbuf || !z || !png) { free(rawbuf); free(z); free(png); return NULL; }

    /* Filter byte 0 (None) then RGB, dropping BGRA's alpha and swapping. */
    p = rawbuf;
    for (y = 0; y < h; y++) {
        *p++ = 0;
        for (x = 0; x < w; x++) {
            const unsigned char *s = bgra + ((size_t)y * w + x) * 4;
            *p++ = s[2]; *p++ = s[1]; *p++ = s[0];
        }
    }

    for (i = 0; i < raw; i++) {          /* Adler-32 over the unfiltered data */
        a = (a + rawbuf[i]) % 65521;
        b = (b + a) % 65521;
    }

    p = z;
    *p++ = 0x78; *p++ = 0x01;            /* CM=8, CINFO=7, FCHECK; no preset */
    for (done = 0; done < raw; ) {
        size_t n = raw - done > 65535 ? 65535 : raw - done;
        *p++ = (unsigned char)((done + n >= raw) ? 1 : 0);   /* BFINAL, stored */
        *p++ = (unsigned char)(n & 0xff);
        *p++ = (unsigned char)(n >> 8);
        *p++ = (unsigned char)(~n & 0xff);
        *p++ = (unsigned char)((~n >> 8) & 0xff);
        memcpy(p, rawbuf + done, n);
        p += n; done += n;
    }
    put_be32(p, (b << 16) | a); p += 4;

    {
        unsigned char ihdr[13];
        static const unsigned char sig[8] =
            { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
        put_be32(ihdr, w); put_be32(ihdr + 4, h);
        ihdr[8] = 8;                    /* 8 bits per channel */
        ihdr[9] = 2;                    /* colour type 2: truecolour RGB */
        ihdr[10] = ihdr[11] = ihdr[12] = 0;   /* deflate, adaptive, no interlace */
        p = png;
        memcpy(p, sig, 8); p += 8;
        p = chunk(p, "IHDR", ihdr, sizeof ihdr);
        p = chunk(p, "IDAT", z, (size_t)(zlen));
        p = chunk(p, "IEND", NULL, 0);
    }
    *out_len = (size_t)(p - png);
    free(rawbuf); free(z);
    return png;
}
