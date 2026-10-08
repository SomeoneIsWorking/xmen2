#pragma once

namespace x2::media {

enum class FmvFlushResult { Accepted, NeedsReceive, Failed };

enum class FmvDrainResult {
  Progress,
  OutputBlocked,
  NeedsInput,
  Complete,
  Failed
};

struct FmvDecoderDrain {
  int flush_sent;
  int decoder_drained;
  int tail_drained;
};

struct FmvDecoderDrainOps {
  FmvFlushResult (*send_flush)(void *userdata);
  FmvDrainResult (*receive)(void *userdata);
  FmvDrainResult (*flush_tail)(void *userdata);
};

/* Returns one when decoder and optional converter tail are drained, zero when
   output backpressure requires another update, and -1 on a contract error. */
int fmv_decoder_drain(FmvDecoderDrain *drain, const FmvDecoderDrainOps *ops,
                      void *userdata);

} // namespace x2::media
