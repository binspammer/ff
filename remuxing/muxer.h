#ifndef MUXER_HPP
#define MUXER_HPP

#include "image.h"

#include "libav.h"

class Muxer
{
public:
   Muxer(const char *dst);
   virtual ~Muxer();
   void writeVideoFrames(Images& images);
   void writeVideoFrame(Image& image);

private:
   void init();
   void close();
   void openVideo();
   void closeVideo();
   AVStream *addStream(enum AVCodecID codec_id);

   const char *_filename;
   const char *MUXER = "mov";
   const enum AVCodecID VIDEO_CODEC = AV_CODEC_ID_DNXHD;
   const enum AVPixelFormat SRC_STREAM_PIX_FMT = AV_PIX_FMT_RGB444;
   const enum AVPixelFormat STREAM_PIX_FMT = AV_PIX_FMT_YUV422P;
   const int _sws_flags = SWS_BICUBIC;

   AVOutputFormat *_fmt = nullptr;
   AVFormatContext *_oc = nullptr;
   AVCodec *_videoCodec = nullptr;
   AVFrame *_frame = nullptr;
   AVStream *_videoSt = nullptr;
   AVPicture _srcPicture;
   AVPicture _dstPicture;

   double _videoPts = 0.0;
   int _frameCount = 0;
};

#endif // MUXER_HPP
