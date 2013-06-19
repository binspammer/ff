#ifndef FILTER_H
#define FILTER_H

# if __WORDSIZE == 64
#  define INT64_C(c)    c ## L
#  define UINT64_C(c)   c ## UL
# else
#  define INT64_C(c)    c ## LL
#  define UINT64_C(c)   c ## ULL
# endif

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

class Filter
{
public:
   Filter(const char* dst);
   virtual ~Filter();
   void filterig();

private:
   void init();
   int initFilters();
   int openInputFile();
   void close();
   void displayPicref(AVFilterBufferRef* picref, AVRational time_base);

   const char *_filename;
   const char *_filterDescr = "scale=78:24,yadif"; //,interlace
   AVFormatContext *_fmtCtx = nullptr;
   AVCodecContext *_decCtx = nullptr;
   AVFilterContext *_buffersinkCtx = nullptr;
   AVFilterContext *_buffersrcCtx = nullptr;
   AVFilterGraph *_filterGraph = nullptr;
   int _videoStreamIndex = -1;
   int64_t _lastPts = AV_NOPTS_VALUE;
   int _gotFrame;
   AVPacket _packet;
   AVFrame *_frame = nullptr;
   AVFrame *_filtFrame = nullptr;

};


#endif // FILTER_H
