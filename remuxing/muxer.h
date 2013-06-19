#ifndef MUXER_HPP
#define MUXER_HPP

# if __WORDSIZE == 64
#  define INT64_C(c)    c ## L
#  define UINT64_C(c)   c ## UL
# else
#  define INT64_C(c)    c ## LL
#  define UINT64_C(c)   c ## ULL
# endif

#include <cstdio>
#include <cstring>
#include <cmath>
#include <memory>
#include <iostream>
#include <iomanip>
#include <exception>
#include <stdexcept>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
}

class Muxer
{
public:
   Muxer(const char *dst);
   void mux();
   
private:
   void init();
   void close();
   void openVideo();
   void closeVideo();
   void writeVideoFrame();
   AVStream *addStream(enum AVCodecID codec_id);
   void fillYUVImage(AVPicture *pict, int frame_index, int width, int height);
   
   const int STREAM_DURATION = 5;    // 5 seconds stream duration
   const int STREAM_FRAME_RATE = 25; // 25 images/s
   const int STREAM_NB_FRAMES;       // STREAM_DURATION * STREAM_FRAME_RATE
   const enum AVPixelFormat STREAM_PIX_FMT = AV_PIX_FMT_YUV422P;

   int _sws_flags = SWS_BICUBIC;
//   float _t, _tincr, _incr2;
//   int16_t *_samples;
   AVFrame *_frame;
   AVOutputFormat *_fmt;
   AVFormatContext *_oc;
   AVPicture _src_picture, _dst_picture;
   AVStream *_video_st;
   AVCodec *_video_codec;
   double _video_pts;
   const char *_filename;
   int _frame_count;
   int _ret;
   
};

#endif // MUXER_HPP
