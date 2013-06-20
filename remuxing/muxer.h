#ifndef MUXER_HPP
#define MUXER_HPP

#include "config.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <memory>
#include <vector>
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
   typedef std::vector<std::shared_ptr<AVFilterBufferRef>> Images;

public:
   Muxer(const char *dst, Images& images);
   virtual ~Muxer();
   void mux();
//   void setImages(Images& images) { _images = images; }

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
   const int _sws_flags = SWS_BICUBIC;

   //   float _t, _tincr, _incr2;
   //   int16_t *_samples;
   const char *_filename;
   AVOutputFormat *_fmt = nullptr;
   AVFormatContext *_oc = nullptr;
   AVCodec *_videoCodec = nullptr;
   AVFrame *_frame = nullptr;
   AVPicture _srcPicture, _dstPicture;
   AVStream *_videoSt = nullptr;
   double _videoPts = 0;
   int _frameCount = 0;
   Images &_images;
};

#endif // MUXER_HPP
