#ifndef FILTER_H
#define FILTER_H

#include "image.h"

#include "libav.h"

class Filter
{
public:
   Filter(const char* dst);
   virtual ~Filter();
   Images& getImages() { return _images; }
   Images& readVideoFrames(int frameWindow = 1000);
   Image readVideoFrame();

private:
   void init();
   void initFilters();
   void openInputFile();
   void close();

   const char *_filename;
   const char *_filterDescr = "yadif,decimate"; //showinfo,interlace,yadif,scale=78:24
   const enum AVPixelFormat STREAM_PIX_FMT = AV_PIX_FMT_RGB444; // AV_PIX_FMT_GRAY8 AV_PIX_FMT_YUV422P AV_PIX_FMT_BGR32 AV_PIX_FMT_RGB444

   AVFormatContext *_fmtCtx = nullptr;
   AVCodecContext *_decCtx = nullptr;
   AVFrame *_frame = nullptr;
   AVFrame *_filtFrame = nullptr;
   AVPacket _packet;

   AVFilterContext *_buffersinkCtx = nullptr;
   AVFilterContext *_buffersrcCtx = nullptr;
   AVFilterGraph *_filterGraph = nullptr;

   int _videoStreamIndex = -1;
   int64_t _lastPts = AV_NOPTS_VALUE;

   Images _images;
};

#endif // FILTER_H
