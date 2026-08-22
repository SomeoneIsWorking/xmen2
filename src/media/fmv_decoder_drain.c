#include "fmv_decoder_drain.h"

static int receive_until_flush_accepted(X2FmvDecoderDrain *drain,
                                        const X2FmvDecoderDrainOps *ops,
                                        void *userdata)
{
    for (;;) {
        X2FmvFlushResult flush = ops->send_flush(userdata);
        X2FmvDrainResult received;
        if (flush == X2_FMV_FLUSH_ACCEPTED) {
            drain->flush_sent = 1;
            return 1;
        }
        if (flush == X2_FMV_FLUSH_FAILED) return -1;
        received = ops->receive(userdata);
        if (received == X2_FMV_DRAIN_PROGRESS) continue;
        if (received == X2_FMV_DRAIN_OUTPUT_BLOCKED) return 0;
        if (received == X2_FMV_DRAIN_COMPLETE) {
            drain->flush_sent = 1;
            drain->decoder_drained = 1;
            return 1;
        }
        /* send_flush said output must be received, so NEEDS_INPUT would
           violate FFmpeg's send/receive contract rather than request work. */
        return -1;
    }
}

static int drain_decoder(X2FmvDecoderDrain *drain,
                         const X2FmvDecoderDrainOps *ops, void *userdata)
{
    while (!drain->decoder_drained) {
        X2FmvDrainResult received = ops->receive(userdata);
        if (received == X2_FMV_DRAIN_PROGRESS) continue;
        if (received == X2_FMV_DRAIN_OUTPUT_BLOCKED) return 0;
        if (received == X2_FMV_DRAIN_COMPLETE) {
            drain->decoder_drained = 1;
            break;
        }
        /* Once the NULL packet was accepted, EAGAIN cannot be satisfied by
           another packet. Refuse instead of looping forever at movie EOF. */
        return -1;
    }
    return 1;
}

static int drain_converter_tail(X2FmvDecoderDrain *drain,
                                const X2FmvDecoderDrainOps *ops,
                                void *userdata)
{
    if (!ops->flush_tail) {
        drain->tail_drained = 1;
        return 1;
    }
    while (!drain->tail_drained) {
        X2FmvDrainResult flushed = ops->flush_tail(userdata);
        if (flushed == X2_FMV_DRAIN_PROGRESS) continue;
        if (flushed == X2_FMV_DRAIN_OUTPUT_BLOCKED) return 0;
        if (flushed == X2_FMV_DRAIN_COMPLETE) {
            drain->tail_drained = 1;
            break;
        }
        return -1;
    }
    return 1;
}

int x2_fmv_decoder_drain(X2FmvDecoderDrain *drain,
                         const X2FmvDecoderDrainOps *ops, void *userdata)
{
    int result;
    if (!drain || !ops || !ops->send_flush || !ops->receive) return -1;
    if (!drain->flush_sent) {
        result = receive_until_flush_accepted(drain, ops, userdata);
        if (result <= 0) return result;
    }
    result = drain_decoder(drain, ops, userdata);
    if (result <= 0) return result;
    return drain_converter_tail(drain, ops, userdata);
}
