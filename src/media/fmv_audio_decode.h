#ifndef X2_FMV_AUDIO_DECODE_H
#define X2_FMV_AUDIO_DECODE_H

#include "fmv_audio_sink.h"
#include "fmv_decoder_drain.h"

typedef struct AVCodecContext AVCodecContext;
typedef struct AVPacket AVPacket;
typedef struct X2FmvAudioDecode X2FmvAudioDecode;

/* Takes ownership of codec, including on failure. */
X2FmvAudioDecode *x2_fmv_audio_decode_create(AVCodecContext *codec,
                                             const X2FmvAudioSink *sink,
                                             int *error);
void x2_fmv_audio_decode_close(X2FmvAudioDecode *decode);
int x2_fmv_audio_decode_send_packet(X2FmvAudioDecode *decode,
                                    const AVPacket *packet);
const X2FmvDecoderDrainOps *x2_fmv_audio_decode_drain_ops(void);
int x2_fmv_audio_decode_sample_rate(const X2FmvAudioDecode *decode);
unsigned long x2_fmv_audio_decode_samples(const X2FmvAudioDecode *decode);
int x2_fmv_audio_decode_error(const X2FmvAudioDecode *decode);

#endif
