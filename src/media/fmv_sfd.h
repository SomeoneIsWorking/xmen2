#pragma once

struct AVCodecContext;
struct AVCodecParserContext;
struct AVFormatContext;
struct AVPacket;

namespace x2::media {

struct FmvSfd {
  AVCodecParserContext *video_parser;
  AVPacket *bootstrap_audio;
  AVPacket *bootstrap_video;
  int manual;
};

using FmvSfdSendPacket = int (*)(void *userdata, const AVPacket *packet);

void fmv_sfd_configure_probe(AVFormatContext *format);
int fmv_sfd_prepare(AVFormatContext *format, FmvSfd *sfd);
void fmv_sfd_close(FmvSfd *sfd);
int fmv_sfd_send_video(FmvSfd *sfd, AVCodecContext *codec,
                       const AVPacket *packet, FmvSfdSendPacket send,
                       void *userdata);
int fmv_sfd_flush_video(FmvSfd *sfd, AVCodecContext *codec,
                        FmvSfdSendPacket send, void *userdata);

} // namespace x2::media
