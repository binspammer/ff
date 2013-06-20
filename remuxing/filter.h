#ifndef FILTER_H
#define FILTER_H

#include "config.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avcodec.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
#include <unistd.h>
}

#include <memory>
#include <vector>

class Filter
{
public:
//   typedef std::vector<std::shared_ptr<AVFilterBufferRef*>> Images;
   //  typedef std::vector<std::shared_ptr<uint8_t*>> Images;
   typedef std::vector<std::shared_ptr<AVFilterBufferRef>> Images;

   Filter(const char* dst);
   virtual ~Filter();
   void decode();
   Images& getImages() { return _images; }

private:
   void init();
   void initFilters();
   void openInputFile();
   void close();
   void displayPicref(AVFilterBufferRef* picref, AVRational time_base);

   const char *_filename;

   AVFormatContext *_fmtCtx = nullptr;
   AVCodecContext *_decCtx = nullptr;
   AVFrame *_frame = nullptr;
   AVFrame *_filtFrame = nullptr;
   AVPacket _packet;
//   const enum AVPixelFormat STREAM_PIX_FMT = AV_PIX_FMT_YUV422P;
   const enum AVPixelFormat STREAM_PIX_FMT = AV_PIX_FMT_GRAY8;

   const char *_filterDescr = "scale=78:24,yadif"; //,interlace
   AVFilterContext *_buffersinkCtx = nullptr;
   AVFilterContext *_buffersrcCtx = nullptr;
   AVFilterGraph *_filterGraph = nullptr;

   int _videoStreamIndex = -1;
   int64_t _lastPts = AV_NOPTS_VALUE;

   Images _images;
};


#endif // FILTER_H
