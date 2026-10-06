#ifndef X2_FMV_SFD_H
#define X2_FMV_SFD_H

typedef struct AVCodecContext AVCodecContext;
typedef struct AVCodecParserContext AVCodecParserContext;
typedef struct AVFormatContext AVFormatContext;
typedef struct AVPacket AVPacket;

typedef struct {
  AVCodecParserContext *video_parser;
  AVPacket *bootstrap_audio;
  AVPacket *bootstrap_video;
  int manual;
} X2FmvSfd;

typedef int (*X2FmvSfdSendPacket)(void *userdata, const AVPacket *packet);

void x2_fmv_sfd_configure_probe(AVFormatContext *format);
int x2_fmv_sfd_prepare(AVFormatContext *format, X2FmvSfd *sfd);
void x2_fmv_sfd_close(X2FmvSfd *sfd);
int x2_fmv_sfd_send_video(X2FmvSfd *sfd, AVCodecContext *codec,
                          const AVPacket *packet, X2FmvSfdSendPacket send,
                          void *userdata);
int x2_fmv_sfd_flush_video(X2FmvSfd *sfd, AVCodecContext *codec,
                           X2FmvSfdSendPacket send, void *userdata);

#endif
