#include "filter.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <memory>
#include <iostream>
#include <iomanip>
#include <exception>
#include <stdexcept>

Filter::Filter(const char *src)
   : _filename(src)
{
}

Filter::~Filter()
{
   close();
}

void Filter::init()
{
   //   frame = av_frame_alloc();
   _frame = avcodec_alloc_frame();
   _filtFrame = avcodec_alloc_frame();
   if (!_frame || !_filtFrame) {
      perror("Could not allocate frame");
      exit(1);
   }
   avcodec_register_all();
   av_register_all();
   avfilter_register_all();
   if (openInputFile() < 0)
      throw std::runtime_error("Could not open the input file");
   if (initFilters() < 0)
      throw std::runtime_error("Could not init the filters");
}

int Filter::openInputFile()
{
   int ret;
   AVCodec *dec;
   if ((ret = avformat_open_input(&_fmtCtx, _filename, NULL, NULL)) < 0) {
      av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
      return ret;
   }
   if ((ret = avformat_find_stream_info(_fmtCtx, NULL)) < 0) {
      av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
      return ret;
   }
   /* select the video stream */
   ret = av_find_best_stream(_fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
   if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Cannot find a video stream in the input file\n");
      return ret;
   }
   _videoStreamIndex = ret;
   _decCtx = _fmtCtx->streams[_videoStreamIndex]->codec;
   /* init the video decoder */
   if ((ret = avcodec_open2(_decCtx, dec, NULL)) < 0) {
      av_log(NULL, AV_LOG_ERROR, "Cannot open video decoder\n");
      return ret;
   }
   return 0;
}

int Filter::initFilters()
{
   char args[512];
   int ret;
   enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE };
   AVBufferSinkParams *buffersink_params;
   AVFilter *buffersrc  = avfilter_get_by_name("buffer");
   AVFilter *buffersink = avfilter_get_by_name("ffbuffersink");
   AVFilterInOut *outputs = avfilter_inout_alloc();
   AVFilterInOut *inputs  = avfilter_inout_alloc();

   _filterGraph = avfilter_graph_alloc();
   /* buffer video source: the decoded frames from the decoder will be inserted here. */
   snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            _decCtx->width, _decCtx->height, _decCtx->pix_fmt,
            _decCtx->time_base.num, _decCtx->time_base.den,
            _decCtx->sample_aspect_ratio.num, _decCtx->sample_aspect_ratio.den);

   ret = avfilter_graph_create_filter(&_buffersrcCtx, buffersrc, "in", args, NULL, _filterGraph);
   if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
      return ret;
   }
   /* buffer video sink: to terminate the filter chain. */
   buffersink_params = av_buffersink_params_alloc();
   buffersink_params->pixel_fmts = pix_fmts;
   ret = avfilter_graph_create_filter(&_buffersinkCtx, buffersink, "out", NULL, buffersink_params, _filterGraph);
   av_free(buffersink_params);
   if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
      return ret;
   }
   /* Endpoints for the filter graph. */
   outputs->name       = av_strdup("in");
   outputs->filter_ctx = +_buffersrcCtx;
   outputs->pad_idx    = 0;
   outputs->next       = NULL;
   inputs->name       = av_strdup("out");
   inputs->filter_ctx = _buffersinkCtx;
   inputs->pad_idx    = 0;
   inputs->next       = NULL;
   if ((ret = avfilter_graph_parse(_filterGraph, _filterDescr,
                                   &inputs, &outputs, NULL)) < 0)
      return ret;
   if ((ret = avfilter_graph_config(_filterGraph, NULL)) < 0)
      return ret;
   return 0;
}

void Filter::displayPicref(AVFilterBufferRef *picref, AVRational time_base)
{
   int x, y;
   uint8_t *p0, *p;
   int64_t delay;
   if (picref->pts != AV_NOPTS_VALUE) {
      if (_lastPts != AV_NOPTS_VALUE) {
         /* sleep roughly the right amount of time;
            * usleep is in microseconds, just like AV_TIME_BASE. */
         delay = av_rescale_q(picref->pts - _lastPts, time_base, AV_TIME_BASE_Q);
         if (delay > 0 && delay < 1000000)
            usleep(delay);
      }
      _lastPts = picref->pts;
   }
   /* Trivial ASCII grayscale display. */
   p0 = picref->data[0];
   puts("\033c");
   for (y = 0; y < picref->video->h; y++) {
      p = p0;
      for (x = 0; x < picref->video->w; x++)
         putchar(" .-+#"[*(p++) / 52]);
      putchar('\n');
      p0 += picref->linesize[0];
   }
   fflush(stdout);
}

void Filter::close()
{
   avfilter_graph_free(&_filterGraph);
   if (_decCtx)
      avcodec_close(_decCtx);
   avformat_close_input(&_fmtCtx);
   av_freep(&_frame);
}

//int main(int argc, char **argv)
void Filter::filterig()
{
   init();

   /* read all packets */
   while (1) {
      AVFilterBufferRef *picref;
      if (av_read_frame(_fmtCtx, &_packet) < 0)
         break;
      if (_packet.stream_index == _videoStreamIndex) {
         avcodec_get_frame_defaults(_frame);
         _gotFrame = 0;
         if (avcodec_decode_video2(_decCtx, _frame, &_gotFrame, &_packet) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error decoding video\n");
            break;
         }
         if (_gotFrame) {
            _frame->pts = av_frame_get_best_effort_timestamp(_frame);
            /* push the decoded frame into the filtergraph */
            if (av_buffersrc_add_frame(+_buffersrcCtx, _frame, 0) < 0) {
               av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
               break;
            }
            /* pull filtered pictures from the filtergraph */
            while (1) {
               int ret = av_buffersink_get_buffer_ref(_buffersinkCtx, &picref, 0);
               if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                  break;
               if (ret < 0)
                  return;
               if (picref) {
                  displayPicref(picref, _buffersinkCtx->inputs[0]->time_base);
                  avfilter_unref_bufferp(&picref);
               }
            }
         }
      }
      av_free_packet(&_packet);
   }
   close();
}
