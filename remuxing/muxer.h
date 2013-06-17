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

// 5 seconds stream duration 
#define STREAM_DURATION   200.0
#define STREAM_FRAME_RATE 25 // 25 images/s 
#define STREAM_NB_FRAMES  ((int)(STREAM_DURATION * STREAM_FRAME_RATE))
#define STREAM_PIX_FMT    AV_PIX_FMT_YUV422P // default pix_fmt 

class Muxer
{
public:
   Muxer(const char *dst);
   int mux();
   
private:
   AVStream *addStream(AVFormatContext *_oc, AVCodec **codec, enum AVCodecID codec_id);
   void openVideo(AVFormatContext *_oc, AVCodec *codec, AVStream *st);
   void fillYUVImage(AVPicture *pict, int frame_index, int width, int height);
   void writeVideoFrame(AVFormatContext *_oc, AVStream *st);
   void closeVideo(AVFormatContext *_oc, AVStream *st);
   
   
   int _sws_flags = SWS_BICUBIC;
   float _t, _tincr, _incr2;
   int16_t *_samples;
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
