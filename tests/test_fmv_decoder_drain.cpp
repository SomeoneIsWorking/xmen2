#include "fmv_decoder_drain.h"

#include <stdio.h>

typedef struct {
  int delayed;
  int queued;
  int capacity;
  int decoder_outputs;
  int tail;
  int tail_outputs;
} FakeDecoder;

static x2::media::FmvFlushResult accept_flush(void *userdata) {
  (void)userdata;
  return x2::media::FmvFlushResult::Accepted;
}

static x2::media::FmvDrainResult receive_delayed(void *userdata) {
  FakeDecoder *fake = (FakeDecoder *)userdata;
  if (fake->queued == fake->capacity)
    return x2::media::FmvDrainResult::OutputBlocked;
  if (!fake->delayed)
    return x2::media::FmvDrainResult::Complete;
  fake->delayed--;
  fake->queued++;
  fake->decoder_outputs++;
  return x2::media::FmvDrainResult::Progress;
}

static x2::media::FmvDrainResult flush_tail(void *userdata) {
  FakeDecoder *fake = (FakeDecoder *)userdata;
  if (!fake->tail)
    return x2::media::FmvDrainResult::Complete;
  fake->tail--;
  fake->tail_outputs++;
  return x2::media::FmvDrainResult::Progress;
}

int main(void) {
  x2::media::FmvDecoderDrainOps video_ops = {accept_flush, receive_delayed,
                                             NULL};
  x2::media::FmvDecoderDrainOps audio_ops = {accept_flush, receive_delayed,
                                             flush_tail};
  x2::media::FmvDecoderDrain video = {0};
  x2::media::FmvDecoderDrain audio = {0};
  FakeDecoder video_fake = {18, 0, 16, 0, 0, 0};
  FakeDecoder audio_fake = {2, 0, 32, 0, 3, 0};
  int failures = 0;

  failures +=
      x2::media::fmv_decoder_drain(&video, &video_ops, &video_fake) != 0;
  failures += !video.flush_sent || video.decoder_drained;
  failures += video_fake.queued != 16 || video_fake.decoder_outputs != 16;
  video_fake.queued = 0;
  failures +=
      x2::media::fmv_decoder_drain(&video, &video_ops, &video_fake) != 1;
  failures += !video.decoder_drained || !video.tail_drained;
  failures += video_fake.decoder_outputs != 18 || video_fake.delayed != 0;

  failures +=
      x2::media::fmv_decoder_drain(&audio, &audio_ops, &audio_fake) != 1;
  failures += !audio.decoder_drained || !audio.tail_drained;
  failures += audio_fake.decoder_outputs != 2 || audio_fake.tail_outputs != 3;

  printf("FMV decoder drain: %s -- a full 16-frame output queue resumed "
         "after EOF flush; delayed video and audio tail were emitted\n",
         failures ? "FAILED" : "PASSED");
  return failures != 0;
}
