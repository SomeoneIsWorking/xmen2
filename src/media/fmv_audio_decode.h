#pragma once

#include "fmv_audio_sink.h"
#include "fmv_decoder_drain.h"

struct AVCodecContext;
struct AVPacket;

namespace x2::media {

struct FmvAudioDecode;

/* Takes ownership of codec, including on failure. */
FmvAudioDecode *fmv_audio_decode_create(AVCodecContext *codec,
                                        const X2FmvAudioSink *sink, int *error);
void fmv_audio_decode_close(FmvAudioDecode *decode);
int fmv_audio_decode_send_packet(FmvAudioDecode *decode,
                                 const AVPacket *packet);
const X2FmvDecoderDrainOps *fmv_audio_decode_drain_ops();
int fmv_audio_decode_sample_rate(const FmvAudioDecode *decode);
unsigned long fmv_audio_decode_samples(const FmvAudioDecode *decode);
int fmv_audio_decode_error(const FmvAudioDecode *decode);

} // namespace x2::media
