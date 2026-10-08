#include "fmv_decoder_drain.h"

namespace x2::media {

static int receive_until_flush_accepted(FmvDecoderDrain *drain,
                                        const FmvDecoderDrainOps *ops,
                                        void *userdata) {
  for (;;) {
    FmvFlushResult flush = ops->send_flush(userdata);
    FmvDrainResult received;
    if (flush == FmvFlushResult::Accepted) {
      drain->flush_sent = 1;
      return 1;
    }
    if (flush == FmvFlushResult::Failed)
      return -1;
    received = ops->receive(userdata);
    if (received == FmvDrainResult::Progress)
      continue;
    if (received == FmvDrainResult::OutputBlocked)
      return 0;
    if (received == FmvDrainResult::Complete) {
      drain->flush_sent = 1;
      drain->decoder_drained = 1;
      return 1;
    }
    /* send_flush said output must be received, so NEEDS_INPUT would
       violate FFmpeg's send/receive contract rather than request work. */
    return -1;
  }
}

static int drain_decoder(FmvDecoderDrain *drain, const FmvDecoderDrainOps *ops,
                         void *userdata) {
  while (!drain->decoder_drained) {
    FmvDrainResult received = ops->receive(userdata);
    if (received == FmvDrainResult::Progress)
      continue;
    if (received == FmvDrainResult::OutputBlocked)
      return 0;
    if (received == FmvDrainResult::Complete) {
      drain->decoder_drained = 1;
      break;
    }
    /* Once the NULL packet was accepted, EAGAIN cannot be satisfied by
       another packet. Refuse instead of looping forever at movie EOF. */
    return -1;
  }
  return 1;
}

static int drain_converter_tail(FmvDecoderDrain *drain,
                                const FmvDecoderDrainOps *ops, void *userdata) {
  if (!ops->flush_tail) {
    drain->tail_drained = 1;
    return 1;
  }
  while (!drain->tail_drained) {
    FmvDrainResult flushed = ops->flush_tail(userdata);
    if (flushed == FmvDrainResult::Progress)
      continue;
    if (flushed == FmvDrainResult::OutputBlocked)
      return 0;
    if (flushed == FmvDrainResult::Complete) {
      drain->tail_drained = 1;
      break;
    }
    return -1;
  }
  return 1;
}

int fmv_decoder_drain(FmvDecoderDrain *drain, const FmvDecoderDrainOps *ops,
                      void *userdata) {
  int result;
  if (!drain || !ops || !ops->send_flush || !ops->receive)
    return -1;
  if (!drain->flush_sent) {
    result = receive_until_flush_accepted(drain, ops, userdata);
    if (result <= 0)
      return result;
  }
  result = drain_decoder(drain, ops, userdata);
  if (result <= 0)
    return result;
  return drain_converter_tail(drain, ops, userdata);
}

} // namespace x2::media
