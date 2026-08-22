#ifndef X2_FMV_DECODER_DRAIN_H
#define X2_FMV_DECODER_DRAIN_H

typedef enum {
    X2_FMV_FLUSH_ACCEPTED,
    X2_FMV_FLUSH_NEEDS_RECEIVE,
    X2_FMV_FLUSH_FAILED
} X2FmvFlushResult;

typedef enum {
    X2_FMV_DRAIN_PROGRESS,
    X2_FMV_DRAIN_OUTPUT_BLOCKED,
    X2_FMV_DRAIN_NEEDS_INPUT,
    X2_FMV_DRAIN_COMPLETE,
    X2_FMV_DRAIN_FAILED
} X2FmvDrainResult;

typedef struct {
    int flush_sent;
    int decoder_drained;
    int tail_drained;
} X2FmvDecoderDrain;

typedef struct {
    X2FmvFlushResult (*send_flush)(void *userdata);
    X2FmvDrainResult (*receive)(void *userdata);
    X2FmvDrainResult (*flush_tail)(void *userdata);
} X2FmvDecoderDrainOps;

/* Returns one when decoder and optional converter tail are drained, zero when
   output backpressure requires another update, and -1 on a contract error. */
int x2_fmv_decoder_drain(X2FmvDecoderDrain *drain,
                         const X2FmvDecoderDrainOps *ops, void *userdata);

#endif
