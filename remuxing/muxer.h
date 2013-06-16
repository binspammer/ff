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
   AVStream *add_stream(AVFormatContext *oc, AVCodec **codec, enum AVCodecID codec_id);
   void open_video(AVFormatContext *oc, AVCodec *codec, AVStream *st);
   void fill_yuv_image(AVPicture *pict, int frame_index, int width, int height);
   void write_video_frame(AVFormatContext *oc, AVStream *st);   
   void close_video(AVFormatContext *oc, AVStream *st);
   
   
   int sws_flags = SWS_BICUBIC;   
   float t, tincr, tincr2;
   int16_t *samples;
   AVFrame *frame;
   AVOutputFormat *fmt;
   AVFormatContext *oc;
   AVPicture src_picture, dst_picture;
   AVStream *video_st;
   AVCodec *video_codec;
   double video_pts;
   const char *filename;
   int frame_count;
   int ret;
   
};

#endif // MUXER_HPP
