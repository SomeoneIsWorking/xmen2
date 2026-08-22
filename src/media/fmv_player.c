#include "fmv_player.h"
#include "fmv_policy.h"
#include "fmv_timing.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIDEO_QUEUE_CAPACITY 16
#define AUDIO_HORIZON_SECONDS 0.75
#define VIDEO_HORIZON_SECONDS 0.20

typedef struct {
    uint8_t *bgra;
    double timestamp;
} VideoFrame;

struct X2FmvPlayer {
    AVFormatContext *format;
    AVCodecContext *video_codec;
    AVCodecContext *audio_codec;
    struct SwsContext *scaler;
    SwrContext *resampler;
    AVFrame *frame;
    AVPacket *packet;
    int video_stream;
    int audio_stream;
    int width;
    int height;
    int sample_rate;
    int eof;
    int flushed_video;
    int flushed_audio;
    X2FmvState state;
    X2FmvAudioSink sink;
    X2FmvTimeline timeline;
    VideoFrame video_queue[VIDEO_QUEUE_CAPACITY];
    int video_head;
    int video_count;
    uint8_t *current_bgra;
    int have_current;
    unsigned long decoded_video;
    unsigned long decoded_audio;
    unsigned long displayed_video;
    unsigned long dropped_video;
    unsigned long decode_failures;
};

static void error_text(char *out, size_t size, const char *operation, int code)
{
    char detail[AV_ERROR_MAX_STRING_SIZE];
    if (!out || !size) return;
    av_strerror(code, detail, sizeof(detail));
    snprintf(out, size, "%s: %s", operation, detail);
}

static AVCodecContext *open_codec(AVFormatContext *format, int stream,
                                  char *error, size_t error_size)
{
    AVCodecContext *context;
    const AVCodec *codec;
    int result;
    codec = avcodec_find_decoder(format->streams[stream]->codecpar->codec_id);
    if (!codec) {
        snprintf(error, error_size, "no decoder for codec id %d",
                 format->streams[stream]->codecpar->codec_id);
        return NULL;
    }
    context = avcodec_alloc_context3(codec);
    if (!context) {
        snprintf(error, error_size, "cannot allocate decoder context");
        return NULL;
    }
    result = avcodec_parameters_to_context(context,
                                           format->streams[stream]->codecpar);
    if (result >= 0) result = avcodec_open2(context, codec, NULL);
    if (result < 0) {
        error_text(error, error_size, "cannot open decoder", result);
        avcodec_free_context(&context);
    }
    return context;
}

static void free_video_queue(X2FmvPlayer *player)
{
    int i;
    for (i = 0; i < VIDEO_QUEUE_CAPACITY; ++i) {
        av_free(player->video_queue[i].bgra);
        player->video_queue[i].bgra = NULL;
    }
    player->video_head = 0;
    player->video_count = 0;
}

static int queue_video_frame(X2FmvPlayer *player, const AVFrame *frame)
{
    AVStream *stream = player->format->streams[player->video_stream];
    VideoFrame *slot;
    uint8_t *planes[4];
    int strides[4];
    int index;
    if (player->video_count == VIDEO_QUEUE_CAPACITY) return 0;
    index = (player->video_head + player->video_count) % VIDEO_QUEUE_CAPACITY;
    slot = &player->video_queue[index];
    if (!slot->bgra)
        slot->bgra = (uint8_t *)av_malloc((size_t)player->width
                                         * (size_t)player->height * 4u);
    if (!slot->bgra) return -1;
    planes[0] = slot->bgra;
    planes[1] = planes[2] = planes[3] = NULL;
    strides[0] = player->width * 4;
    strides[1] = strides[2] = strides[3] = 0;
    sws_scale(player->scaler, (const uint8_t *const *)frame->data,
              frame->linesize, 0, player->height, planes, strides);
    slot->timestamp = x2_fmv_timestamp(&player->timeline,
                                      frame->best_effort_timestamp,
                                      AV_NOPTS_VALUE, stream->time_base.num,
                                      stream->time_base.den, frame->duration);
    player->video_count++;
    player->decoded_video++;
    return 1;
}

static int receive_video(X2FmvPlayer *player)
{
    int result;
    int received = 0;
    while (player->video_count < VIDEO_QUEUE_CAPACITY) {
        result = avcodec_receive_frame(player->video_codec, player->frame);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
        if (result < 0) {
            player->decode_failures++;
            return result;
        }
        result = queue_video_frame(player, player->frame);
        av_frame_unref(player->frame);
        if (result < 0) return AVERROR(ENOMEM);
        received += result;
        if (result == 0) break;
    }
    return received;
}

static int receive_audio(X2FmvPlayer *player)
{
    int result;
    int received = 0;
    for (;;) {
        int out_capacity;
        uint8_t *output = NULL;
        result = avcodec_receive_frame(player->audio_codec, player->frame);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
        if (result < 0) {
            player->decode_failures++;
            return result;
        }
        out_capacity = (int)av_rescale_rnd(
            swr_get_delay(player->resampler, player->audio_codec->sample_rate)
                + player->frame->nb_samples,
            player->sample_rate, player->audio_codec->sample_rate, AV_ROUND_UP);
        if (av_samples_alloc(&output, NULL, 2, out_capacity,
                             AV_SAMPLE_FMT_FLT, 0) < 0) {
            av_frame_unref(player->frame);
            return AVERROR(ENOMEM);
        }
        result = swr_convert(player->resampler, &output, out_capacity,
                             (const uint8_t **)player->frame->extended_data,
                             player->frame->nb_samples);
        av_frame_unref(player->frame);
        if (result < 0 || (result > 0 && player->sink.queue_stereo_f32
                && !player->sink.queue_stereo_f32(player->sink.userdata,
                    (const float *)output, (size_t)result,
                    player->sample_rate))) {
            av_freep(&output);
            return result < 0 ? result : AVERROR(ENOMEM);
        }
        av_freep(&output);
        if (result > 0) {
            player->decoded_audio += (unsigned long)result;
            received += result;
        }
    }
    return received;
}

static int send_packet(X2FmvPlayer *player, AVCodecContext *codec,
                       int video, const AVPacket *packet)
{
    int result = avcodec_send_packet(codec, packet);
    if (result == AVERROR(EAGAIN)) {
        result = video ? receive_video(player) : receive_audio(player);
        if (result >= 0) result = avcodec_send_packet(codec, packet);
    }
    if (result < 0 && result != AVERROR_EOF) return result;
    result = video ? receive_video(player) : receive_audio(player);
    return result < 0 ? result : 0;
}

static double queued_video_horizon(const X2FmvPlayer *player)
{
    int tail;
    if (!player->video_count) return -1.0;
    tail = (player->video_head + player->video_count - 1)
         % VIDEO_QUEUE_CAPACITY;
    return player->video_queue[tail].timestamp;
}

static int pump(X2FmvPlayer *player, double playback_seconds)
{
    int result;
    while (!player->eof && player->video_count < VIDEO_QUEUE_CAPACITY) {
        double audio_queued = player->sink.queued_seconds
            ? player->sink.queued_seconds(player->sink.userdata) : 1.0;
        if (audio_queued >= AUDIO_HORIZON_SECONDS
                && queued_video_horizon(player)
                   >= playback_seconds + VIDEO_HORIZON_SECONDS) break;
        result = av_read_frame(player->format, player->packet);
        if (result == AVERROR_EOF) {
            player->eof = 1;
            break;
        }
        if (result < 0) return result;
        if (player->packet->stream_index == player->video_stream)
            result = send_packet(player, player->video_codec, 1, player->packet);
        else if (player->packet->stream_index == player->audio_stream)
            result = send_packet(player, player->audio_codec, 0, player->packet);
        else result = 0;
        av_packet_unref(player->packet);
        if (result < 0) return result;
    }
    if (player->eof && !player->flushed_video) {
        player->flushed_video = 1;
        result = send_packet(player, player->video_codec, 1, NULL);
        if (result < 0) return result;
    }
    if (player->eof && !player->flushed_audio) {
        player->flushed_audio = 1;
        result = send_packet(player, player->audio_codec, 0, NULL);
        if (result < 0) return result;
    }
    return 0;
}

X2FmvPlayer *x2_fmv_open(const char *path, const X2FmvAudioSink *sink,
                         char *error, size_t error_size)
{
    X2FmvPlayer *player = (X2FmvPlayer *)calloc(1, sizeof(*player));
    AVRational rate;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    int result;
    if (!player) return NULL;
    player->video_stream = -1;
    player->audio_stream = -1;
    if (sink) player->sink = *sink;
    result = avformat_open_input(&player->format, path, NULL, NULL);
    if (result >= 0) result = avformat_find_stream_info(player->format, NULL);
    if (result < 0) {
        error_text(error, error_size, "cannot open SFD", result);
        x2_fmv_close(player);
        return NULL;
    }
    player->video_stream = av_find_best_stream(player->format,
                                               AVMEDIA_TYPE_VIDEO, -1, -1,
                                               NULL, 0);
    player->audio_stream = av_find_best_stream(player->format,
                                               AVMEDIA_TYPE_AUDIO, -1, -1,
                                               NULL, 0);
    if (player->video_stream < 0 || player->audio_stream < 0) {
        snprintf(error, error_size, "SFD requires one video and one audio stream");
        x2_fmv_close(player);
        return NULL;
    }
    if (!x2_fmv_codec_policy(player->format->iformat->name,
            avcodec_get_name(player->format->streams[player->video_stream]
                             ->codecpar->codec_id),
            avcodec_get_name(player->format->streams[player->audio_stream]
                             ->codecpar->codec_id))) {
        snprintf(error, error_size,
                 "unsupported movie streams: %s / %s / %s (need mpeg / "
                 "mpeg1video / adpcm_adx)", player->format->iformat->name,
                 avcodec_get_name(player->format->streams[player->video_stream]
                                  ->codecpar->codec_id),
                 avcodec_get_name(player->format->streams[player->audio_stream]
                                  ->codecpar->codec_id));
        x2_fmv_close(player);
        return NULL;
    }
    player->video_codec = open_codec(player->format, player->video_stream,
                                     error, error_size);
    player->audio_codec = open_codec(player->format, player->audio_stream,
                                     error, error_size);
    if (!player->video_codec || !player->audio_codec) {
        x2_fmv_close(player);
        return NULL;
    }
    player->width = player->video_codec->width;
    player->height = player->video_codec->height;
    player->sample_rate = player->audio_codec->sample_rate;
    if (player->width <= 0 || player->height <= 0 || player->sample_rate <= 0) {
        snprintf(error, error_size, "SFD has invalid video/audio dimensions");
        x2_fmv_close(player);
        return NULL;
    }
    player->scaler = sws_getContext(player->width, player->height,
        player->video_codec->pix_fmt, player->width, player->height,
        AV_PIX_FMT_BGRA, SWS_BILINEAR, NULL, NULL, NULL);
    result = swr_alloc_set_opts2(&player->resampler, &stereo,
        AV_SAMPLE_FMT_FLT, player->sample_rate, &player->audio_codec->ch_layout,
        player->audio_codec->sample_fmt, player->audio_codec->sample_rate,
        0, NULL);
    if (result >= 0) result = swr_init(player->resampler);
    player->frame = av_frame_alloc();
    player->packet = av_packet_alloc();
    player->current_bgra = (uint8_t *)av_malloc((size_t)player->width
                                                * player->height * 4u);
    if (!player->scaler || result < 0 || !player->frame || !player->packet
            || !player->current_bgra) {
        if (result < 0) error_text(error, error_size, "audio conversion", result);
        else snprintf(error, error_size, "cannot allocate decoder buffers");
        x2_fmv_close(player);
        return NULL;
    }
    rate = player->format->streams[player->video_stream]->avg_frame_rate;
    if (rate.num <= 0 || rate.den <= 0)
        rate = av_guess_frame_rate(player->format,
                                   player->format->streams[player->video_stream],
                                   NULL);
    x2_fmv_timeline_init(&player->timeline,
                         rate.num > 0 && rate.den > 0
                         ? av_q2d(rate) : 30.0);
    player->state = X2_FMV_READY;
    return player;
}

void x2_fmv_close(X2FmvPlayer *player)
{
    if (!player) return;
    free_video_queue(player);
    av_free(player->current_bgra);
    av_packet_free(&player->packet);
    av_frame_free(&player->frame);
    swr_free(&player->resampler);
    sws_freeContext(player->scaler);
    avcodec_free_context(&player->video_codec);
    avcodec_free_context(&player->audio_codec);
    avformat_close_input(&player->format);
    free(player);
}

void x2_fmv_play(X2FmvPlayer *player)
{
    if (player && player->state != X2_FMV_FINISHED
            && player->state != X2_FMV_FAILED) player->state = X2_FMV_PLAYING;
}

void x2_fmv_pause(X2FmvPlayer *player, int paused)
{
    if (!player || player->state == X2_FMV_FINISHED
            || player->state == X2_FMV_FAILED) return;
    player->state = paused ? X2_FMV_PAUSED : X2_FMV_PLAYING;
}

int x2_fmv_update(X2FmvPlayer *player, double playback_seconds)
{
    int changed = 0;
    if (!player || player->state == X2_FMV_FAILED) return -1;
    if (player->state == X2_FMV_PAUSED) return 0;
    if (pump(player, playback_seconds) < 0) {
        player->state = X2_FMV_FAILED;
        player->decode_failures++;
        return -1;
    }
    while (player->video_count
            && player->video_queue[player->video_head].timestamp
               <= playback_seconds + player->timeline.frame_duration * 0.5) {
        VideoFrame *frame = &player->video_queue[player->video_head];
        memcpy(player->current_bgra, frame->bgra,
               (size_t)player->width * player->height * 4u);
        player->video_head = (player->video_head + 1) % VIDEO_QUEUE_CAPACITY;
        player->video_count--;
        player->displayed_video++;
        if (changed) player->dropped_video++;
        changed = 1;
    }
    if (!player->have_current && player->video_count) {
        VideoFrame *frame = &player->video_queue[player->video_head];
        memcpy(player->current_bgra, frame->bgra,
               (size_t)player->width * player->height * 4u);
        player->have_current = 1;
        changed = 1;
    } else if (changed) player->have_current = 1;
    if (player->eof && player->flushed_video && !player->video_count
            && (!player->sink.queued_seconds
                || player->sink.queued_seconds(player->sink.userdata) <= 0.0))
        player->state = X2_FMV_FINISHED;
    return changed;
}

int x2_fmv_copy_bgra(const X2FmvPlayer *player, void *destination,
                     size_t bytes, size_t pitch)
{
    int y;
    size_t row;
    if (!player || !player->have_current || !destination) return 0;
    row = (size_t)player->width * 4u;
    if (pitch < row || bytes < pitch * (size_t)player->height) return 0;
    for (y = 0; y < player->height; ++y)
        memcpy((uint8_t *)destination + (size_t)y * pitch,
               player->current_bgra + (size_t)y * row, row);
    return 1;
}

int x2_fmv_width(const X2FmvPlayer *player) { return player ? player->width : 0; }
int x2_fmv_height(const X2FmvPlayer *player) { return player ? player->height : 0; }
int x2_fmv_sample_rate(const X2FmvPlayer *player) { return player ? player->sample_rate : 0; }
X2FmvState x2_fmv_state(const X2FmvPlayer *player) { return player ? player->state : X2_FMV_FAILED; }
unsigned long x2_fmv_decoded_frames(const X2FmvPlayer *player) { return player ? player->decoded_video : 0; }

void x2_fmv_report(const X2FmvPlayer *player)
{
    if (!player) return;
    printf("  native FMV: %lu video decoded / %lu displayed / %lu dropped, "
           "%lu audio samples, %lu failure(s); timestamps %u fallback / "
           "%u clamped\n", player->decoded_video, player->displayed_video,
           player->dropped_video, player->decoded_audio,
           player->decode_failures, player->timeline.timestamp_fallbacks,
           player->timeline.timestamp_clamps);
}
