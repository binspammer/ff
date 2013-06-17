#define _XOPEN_SOURCE 600 // for usleep

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
#include <unistd.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

//const char *filterDescr = "scale=78:24";
const char *filterDescr = "yadif, scale=78:24";

AVFormatContext *fmtCtx;
AVCodecContext *decCtx;
AVFilterContext *bufferSinkCtx;
AVFilterContext *bufferSrcCtx;
AVFilterGraph *filterGraph;
int videoStreamIndex = -1;
int64_t lastPts = AV_NOPTS_VALUE;

int openInputFile(const char *filename)
{
   int ret;
   AVCodec *dec;
   
   if ((ret = avformat_open_input(&fmtCtx, filename, NULL, NULL)) < 0) {
      av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
      return ret;
   }
   
   if ((ret = avformat_find_stream_info(fmtCtx, NULL)) < 0) {
      av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
      return ret;
   }
   
   // select the video stream 
   ret = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
   if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Cannot find a video stream in the input file\n");
      return ret;
   }
   videoStreamIndex = ret;
   decCtx = fmtCtx->streams[videoStreamIndex]->codec;
   
   // init the video decoder 
   if ((ret = avcodec_open2(decCtx, dec, NULL)) < 0) {
      av_log(NULL, AV_LOG_ERROR, "Cannot open video decoder\n");
      return ret;
   }
   
   return 0;
}

int initFilters(const char *filtersDescr)
{
   char args[512];
   int ret;
   AVFilter *bufferSrc  = avfilter_get_by_name("buffer");
   AVFilter *bufferSink = avfilter_get_by_name("ffbuffersink");
   AVFilterInOut *outputs = avfilter_inout_alloc();
   AVFilterInOut *inputs  = avfilter_inout_alloc();
   enum AVPixelFormat pixFmts[] = { AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE };
   AVBufferSinkParams *buffersinkParams;
   
   filterGraph = avfilter_graph_alloc();
   
   // buffer video source: the decoded frames from the decoder will be inserted here. 
   snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            decCtx->width, decCtx->height, decCtx->pix_fmt,
            decCtx->time_base.num, decCtx->time_base.den,
            decCtx->sample_aspect_ratio.num, decCtx->sample_aspect_ratio.den);
   
   ret = avfilter_graph_create_filter(&bufferSrcCtx, bufferSrc, "in", args, NULL, filterGraph);
   if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
      return ret;
   }
   
   // buffer video sink: to terminate the filter chain. 
   buffersinkParams = av_buffersink_params_alloc();
   buffersinkParams->pixel_fmts = pixFmts;
   ret = avfilter_graph_create_filter(&bufferSinkCtx, bufferSink, "out",
                                      NULL, buffersinkParams, filterGraph);
   av_free(buffersinkParams);
   if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
      return ret;
   }
   
   // Endpoints for the filter graph. 
   outputs->name       = av_strdup("in");
   outputs->filter_ctx = bufferSrcCtx;
   outputs->pad_idx    = 0;
   outputs->next       = NULL;
   
   inputs->name       = av_strdup("out");
   inputs->filter_ctx = bufferSinkCtx;
   inputs->pad_idx    = 0;
   inputs->next       = NULL;
   
   if ((ret = avfilter_graph_parse(filterGraph, filtersDescr, &inputs, &outputs, NULL)) < 0)
      return ret;
   
   if ((ret = avfilter_graph_config(filterGraph, NULL)) < 0)
      return ret;
   return 0;
}

void displayPicref(AVFilterBufferRef *picref, AVRational time_base)
{
   int x, y;
   uint8_t *p0, *p;
   int64_t delay;
   
   if (picref->pts != AV_NOPTS_VALUE) {
      if (lastPts != AV_NOPTS_VALUE) {
         // sleep roughly the right amount of time; 
         // usleep is in microseconds, just like AV_TIME_BASE.
         delay = av_rescale_q(picref->pts - lastPts, time_base, AV_TIME_BASE_Q);
         if (delay > 0 && delay < 1000000)
            usleep(delay);
      }
      lastPts = picref->pts;
   }
   
   // Trivial ASCII grayscale display. 
   p0 = picref->data[0];
   puts("\033c");
   for (y = 0; y < picref->video->h; y++) 
   {
      p = p0;
      for (x = 0; x < picref->video->w; x++)
         putchar(" .-+#"[*(p++) / 52]);
      putchar('\n');
      p0 += picref->linesize[0];
   }
   fflush(stdout);
}

int main(int argc, char **argv)
{
   int ret;
   AVPacket packet;
   AVFrame *frame = avcodec_alloc_frame();
   int got_frame;
   
   if (!frame) {
      perror("Could not allocate frame");
      exit(1);
   }
   if (argc != 2) {
      fprintf(stderr, "Usage: %s file\n", argv[0]);
      exit(1);
   }
   
   avcodec_register_all();
   av_register_all();
   avfilter_register_all();
   
   if ((ret = openInputFile(argv[1])) < 0)
      goto end;
   if ((ret = initFilters(filterDescr)) < 0)
      goto end;
   
   // read all packets 
   while (1) {
      AVFilterBufferRef *picref;
      if ((ret = av_read_frame(fmtCtx, &packet)) < 0)
         break;
      
      if (packet.stream_index == videoStreamIndex) {
         avcodec_get_frame_defaults(frame);
         got_frame = 0;
         ret = avcodec_decode_video2(decCtx, frame, &got_frame, &packet);
         if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error decoding video\n");
            break;
         }
         
         if (got_frame) {
            frame->pts = av_frame_get_best_effort_timestamp(frame);
            
            // push the decoded frame into the filtergraph 
            if (av_buffersrc_add_frame(bufferSrcCtx, frame, 0) < 0) {
               av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
               break;
            }
            
            // pull filtered pictures from the filtergraph 
            while (1) {
               ret = av_buffersink_get_buffer_ref(bufferSinkCtx, &picref, 0);
               if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                  break;
               if (ret < 0)
                  goto end;
               
               if (picref) {
                  displayPicref(picref, bufferSinkCtx->inputs[0]->time_base);
                  avfilter_unref_bufferp(&picref);
               }
            }
         }
      }
      av_free_packet(&packet);
   }
end:
   avfilter_graph_free(&filterGraph);
   if (decCtx)
      avcodec_close(decCtx);
   avformat_close_input(&fmtCtx);
   av_freep(&frame);
   
   if (ret < 0 && ret != AVERROR_EOF) {
      char buf[1024];
      av_strerror(ret, buf, sizeof(buf));
      fprintf(stderr, "Error occurred: %s\n", buf);
      exit(1);
   }
   
   exit(0);
}
