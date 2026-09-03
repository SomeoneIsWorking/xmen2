#include "fmv_audio_decode.h"

#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>

#include <stdlib.h>

struct X2FmvAudioDecode {
  AVCodecContext *codec;
  SwrContext *resampler;
  AVFrame *frame;
  X2FmvAudioSink sink;
  int sample_rate;
  unsigned long decoded_samples;
  int error;
};

static int queue_output(X2FmvAudioDecode *decode, uint8_t *output, int frames) {
  if (frames > 0 && decode->sink.queue_stereo_f32 &&
      !decode->sink.queue_stereo_f32(decode->sink.userdata,
                                     (const float *)output, (size_t)frames,
                                     decode->sample_rate))
    return AVERROR(ENOMEM);
  if (frames > 0)
    decode->decoded_samples += (unsigned long)frames;
  return frames;
}

static int convert_frame(X2FmvAudioDecode *decode) {
  int capacity = (int)av_rescale_rnd(
      swr_get_delay(decode->resampler, decode->codec->sample_rate) +
          decode->frame->nb_samples,
      decode->sample_rate, decode->codec->sample_rate, AV_ROUND_UP);
  uint8_t *output = NULL;
  int result;
  if (av_samples_alloc(&output, NULL, 2, capacity, AV_SAMPLE_FMT_FLT, 0) < 0) {
    av_frame_unref(decode->frame);
    return AVERROR(ENOMEM);
  }
  result = swr_convert(decode->resampler, &output, capacity,
                       (const uint8_t **)decode->frame->extended_data,
                       decode->frame->nb_samples);
  av_frame_unref(decode->frame);
  if (result >= 0)
    result = queue_output(decode, output, result);
  av_freep(&output);
  return result;
}

static X2FmvDrainResult receive_step(void *userdata) {
  X2FmvAudioDecode *decode = (X2FmvAudioDecode *)userdata;
  int result = avcodec_receive_frame(decode->codec, decode->frame);
  if (result == AVERROR(EAGAIN))
    return X2_FMV_DRAIN_NEEDS_INPUT;
  if (result == AVERROR_EOF)
    return X2_FMV_DRAIN_COMPLETE;
  if (result >= 0)
    result = convert_frame(decode);
  if (result < 0) {
    decode->error = result;
    return X2_FMV_DRAIN_FAILED;
  }
  return X2_FMV_DRAIN_PROGRESS;
}

static int receive_all(X2FmvAudioDecode *decode) {
  int received = 0;
  for (;;) {
    X2FmvDrainResult result = receive_step(decode);
    if (result == X2_FMV_DRAIN_PROGRESS) {
      received++;
      continue;
    }
    if (result == X2_FMV_DRAIN_FAILED)
      return decode->error;
    return received;
  }
}

static X2FmvFlushResult send_flush(void *userdata) {
  X2FmvAudioDecode *decode = (X2FmvAudioDecode *)userdata;
  int result = avcodec_send_packet(decode->codec, NULL);
  if (result >= 0 || result == AVERROR_EOF)
    return X2_FMV_FLUSH_ACCEPTED;
  if (result == AVERROR(EAGAIN))
    return X2_FMV_FLUSH_NEEDS_RECEIVE;
  decode->error = result;
  return X2_FMV_FLUSH_FAILED;
}

static X2FmvDrainResult flush_tail(void *userdata) {
  X2FmvAudioDecode *decode = (X2FmvAudioDecode *)userdata;
  int64_t delay = swr_get_delay(decode->resampler, decode->codec->sample_rate);
  int capacity;
  int result;
  uint8_t *output = NULL;
  if (delay <= 0)
    return X2_FMV_DRAIN_COMPLETE;
  capacity = (int)av_rescale_rnd(delay, decode->sample_rate,
                                 decode->codec->sample_rate, AV_ROUND_UP);
  if (capacity <= 0)
    return X2_FMV_DRAIN_COMPLETE;
  if (av_samples_alloc(&output, NULL, 2, capacity, AV_SAMPLE_FMT_FLT, 0) < 0) {
    decode->error = AVERROR(ENOMEM);
    return X2_FMV_DRAIN_FAILED;
  }
  result = swr_convert(decode->resampler, &output, capacity, NULL, 0);
  if (result >= 0)
    result = queue_output(decode, output, result);
  av_freep(&output);
  if (result < 0) {
    decode->error = result;
    return X2_FMV_DRAIN_FAILED;
  }
  return result > 0 ? X2_FMV_DRAIN_PROGRESS : X2_FMV_DRAIN_COMPLETE;
}

static const X2FmvDecoderDrainOps g_drain_ops = {send_flush, receive_step,
                                                 flush_tail};

X2FmvAudioDecode *x2_fmv_audio_decode_create(AVCodecContext *codec,
                                             const X2FmvAudioSink *sink,
                                             int *error) {
  X2FmvAudioDecode *decode = calloc(1, sizeof(*decode));
  AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
  int result = 0;
  if (!decode) {
    avcodec_free_context(&codec);
    if (error)
      *error = AVERROR(ENOMEM);
    return NULL;
  }
  decode->codec = codec;
  decode->sample_rate = codec->sample_rate;
  if (sink)
    decode->sink = *sink;
  result = swr_alloc_set_opts2(&decode->resampler, &stereo, AV_SAMPLE_FMT_FLT,
                               decode->sample_rate, &codec->ch_layout,
                               codec->sample_fmt, codec->sample_rate, 0, NULL);
  if (result >= 0)
    result = swr_init(decode->resampler);
  decode->frame = av_frame_alloc();
  if (result < 0 || !decode->frame) {
    if (result >= 0)
      result = AVERROR(ENOMEM);
    if (error)
      *error = result;
    x2_fmv_audio_decode_close(decode);
    return NULL;
  }
  if (error)
    *error = 0;
  return decode;
}

void x2_fmv_audio_decode_close(X2FmvAudioDecode *decode) {
  if (!decode)
    return;
  av_frame_free(&decode->frame);
  swr_free(&decode->resampler);
  avcodec_free_context(&decode->codec);
  free(decode);
}

int x2_fmv_audio_decode_send_packet(X2FmvAudioDecode *decode,
                                    const AVPacket *packet) {
  int result = avcodec_send_packet(decode->codec, packet);
  if (result == AVERROR(EAGAIN)) {
    result = receive_all(decode);
    if (result >= 0)
      result = avcodec_send_packet(decode->codec, packet);
  }
  if (result < 0 && result != AVERROR_EOF)
    return result;
  return receive_all(decode) < 0 ? decode->error : 0;
}

const X2FmvDecoderDrainOps *x2_fmv_audio_decode_drain_ops(void) {
  return &g_drain_ops;
}

int x2_fmv_audio_decode_sample_rate(const X2FmvAudioDecode *decode) {
  return decode ? decode->sample_rate : 0;
}

unsigned long x2_fmv_audio_decode_samples(const X2FmvAudioDecode *decode) {
  return decode ? decode->decoded_samples : 0;
}

int x2_fmv_audio_decode_error(const X2FmvAudioDecode *decode) {
  return decode ? decode->error : AVERROR(EINVAL);
}
