#include "fmv_sfd.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <stdint.h>
#include <string.h>

#define X2_SFD_AUDIO_STREAM_ID 0x1c0
#define X2_SFD_VIDEO_STREAM_ID 0x1e0
#define X2_SFD_PROBE_BYTES (1024 * 1024)
#define X2_SFD_PROBE_TIME_US (2 * AV_TIME_BASE)

void x2_fmv_sfd_configure_probe(AVFormatContext *format) {
  unsigned i;
  if (!format || !format->iformat || strcmp(format->iformat->name, "mpeg") != 0)
    return;
  format->probesize = X2_SFD_PROBE_BYTES;
  format->max_analyze_duration = X2_SFD_PROBE_TIME_US;
  for (i = 0; i < format->nb_streams; ++i) {
    AVCodecParameters *parameters = format->streams[i]->codecpar;
    if (format->streams[i]->id == X2_SFD_VIDEO_STREAM_ID &&
        parameters->codec_type == AVMEDIA_TYPE_VIDEO &&
        parameters->codec_id == AV_CODEC_ID_NONE)
      parameters->codec_id = AV_CODEC_ID_MPEG1VIDEO;
    else if (format->streams[i]->id == X2_SFD_AUDIO_STREAM_ID &&
             parameters->codec_type == AVMEDIA_TYPE_AUDIO &&
             parameters->codec_id == AV_CODEC_ID_NONE)
      parameters->codec_id = AV_CODEC_ID_ADPCM_ADX;
  }
}

static int label_streams(AVFormatContext *format) {
  unsigned i;
  int changed = 0;
  if (!format || !format->iformat || strcmp(format->iformat->name, "mpeg") != 0)
    return 0;
  for (i = 0; i < format->nb_streams; ++i) {
    AVCodecParameters *parameters = format->streams[i]->codecpar;
    if (format->streams[i]->id == X2_SFD_VIDEO_STREAM_ID &&
        parameters->codec_type == AVMEDIA_TYPE_VIDEO &&
        parameters->codec_id == AV_CODEC_ID_NONE) {
      parameters->codec_id = AV_CODEC_ID_MPEG1VIDEO;
      changed = 1;
    } else if (format->streams[i]->id == X2_SFD_AUDIO_STREAM_ID &&
               parameters->codec_type == AVMEDIA_TYPE_AUDIO &&
               parameters->codec_id == AV_CODEC_ID_NONE) {
      parameters->codec_id = AV_CODEC_ID_ADPCM_ADX;
      changed = 1;
    }
  }
  return changed;
}

static int prime_stream(AVFormatContext *format, X2FmvSfd *sfd) {
  AVPacket *packet;
  unsigned packets = 0;
  int found = 0;
  if (!format || !sfd || !sfd->bootstrap_audio || !sfd->bootstrap_video ||
      av_seek_frame(format, -1, 0, AVSEEK_FLAG_BYTE) < 0)
    return 0;
  avformat_flush(format);
  packet = av_packet_alloc();
  if (!packet)
    return 0;
  while (packets++ < 256 && av_read_frame(format, packet) >= 0) {
    AVStream *stream =
        packet->stream_index >= 0 &&
                (unsigned)packet->stream_index < format->nb_streams
            ? format->streams[packet->stream_index]
            : NULL;
    if (stream && stream->id == X2_SFD_VIDEO_STREAM_ID && packet->size >= 7) {
      int i;
      for (i = 0; i + 7 <= packet->size; ++i) {
        const uint8_t *data = packet->data + i;
        if (data[0] == 0 && data[1] == 0 && data[2] == 1 && data[3] == 0xb3) {
          stream->codecpar->width = (data[4] << 4) | (data[5] >> 4);
          stream->codecpar->height = ((data[5] & 0x0f) << 8) | data[6];
          if (stream->codecpar->format < 0)
            stream->codecpar->format = AV_PIX_FMT_YUV420P;
          found = stream->codecpar->width > 0 && stream->codecpar->height > 0;
          if (found && av_packet_ref(sfd->bootstrap_video, packet) < 0)
            found = 0;
          break;
        }
      }
    } else if (stream && stream->id == X2_SFD_AUDIO_STREAM_ID &&
               packet->size > 0 && sfd->bootstrap_audio->size == 0) {
      if (av_packet_ref(sfd->bootstrap_audio, packet) < 0)
        break;
    }
    av_packet_unref(packet);
    if (found)
      break;
  }
  av_packet_free(&packet);
  return found;
}

int x2_fmv_sfd_prepare(AVFormatContext *format, X2FmvSfd *sfd) {
  if (!sfd || !label_streams(format))
    return 0;
  sfd->bootstrap_video = av_packet_alloc();
  sfd->bootstrap_audio = av_packet_alloc();
  if (!sfd->bootstrap_video || !sfd->bootstrap_audio ||
      !prime_stream(format, sfd)) {
    x2_fmv_sfd_close(sfd);
    return 0;
  }
  sfd->manual = 1;
  return 1;
}

void x2_fmv_sfd_close(X2FmvSfd *sfd) {
  if (!sfd)
    return;
  av_packet_free(&sfd->bootstrap_audio);
  av_packet_free(&sfd->bootstrap_video);
  av_parser_close(sfd->video_parser);
  sfd->manual = 0;
}

int x2_fmv_sfd_send_video(X2FmvSfd *sfd, AVCodecContext *codec,
                          const AVPacket *packet, X2FmvSfdSendPacket send,
                          void *userdata) {
  const uint8_t *input = packet->data;
  int remaining = packet->size;
  if (!sfd->video_parser)
    return send(userdata, packet);
  while (remaining > 0) {
    uint8_t *output = NULL;
    int output_size = 0;
    const int consumed =
        av_parser_parse2(sfd->video_parser, codec, &output, &output_size, input,
                         remaining, packet->pts, packet->dts, packet->pos);
    if (consumed < 0)
      return consumed;
    input += consumed;
    remaining -= consumed;
    if (output_size > 0) {
      AVPacket parsed = *packet;
      parsed.data = output;
      parsed.size = output_size;
      const int result = send(userdata, &parsed);
      if (result < 0)
        return result;
    }
    if (consumed == 0)
      break;
  }
  return 0;
}

int x2_fmv_sfd_flush_video(X2FmvSfd *sfd, AVCodecContext *codec,
                           X2FmvSfdSendPacket send, void *userdata) {
  uint8_t *output = NULL;
  int output_size = 0;
  int result;
  if (!sfd->video_parser)
    return 0;
  result = av_parser_parse2(sfd->video_parser, codec, &output, &output_size,
                            NULL, 0, AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
  if (result < 0)
    return result;
  if (output_size > 0) {
    AVPacket parsed = {0};
    parsed.data = output;
    parsed.size = output_size;
    return send(userdata, &parsed);
  }
  return 0;
}
