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

// 5 seconds stream duration
#define STREAM_DURATION   200.0
#define STREAM_FRAME_RATE 25 // 25 images/s
#define STREAM_NB_FRAMES  ((int)(STREAM_DURATION * STREAM_FRAME_RATE))
#define STREAM_PIX_FMT    AV_PIX_FMT_YUV422P // default pix_fmt

//const char *_filterDescr = "scale=78:24";
//const char *_filterDescr = "yadif=1:0:0";
//const char *_filterDescr = "yadif=1:-1:0";
//const char *_filterDescr = "yadif=2:0:0";
const char *_filterDescr = "yadif";

AVFormatContext *_fmtCtx;
AVCodecContext *_decCtx;
AVFilterContext *_bufferSinkCtx;
AVFilterContext *_bufferSrcCtx;
AVFilterGraph *_filterGraph;
int _videoStreamIndex = -1;
int64_t _lastPts = AV_NOPTS_VALUE;

// muxing
int _sws_flags = SWS_BICUBIC;
float _t, _tincr, _tincr2;
int16_t *_samples;
AVFrame *_frame;
AVPicture _src_picture, _dst_picture;
int _frame_count;

int openInputFile(const char *filename)
{
   int ret;
   AVCodec *dec;
   
   if ((ret = avformat_open_input(&_fmtCtx, filename, NULL, NULL)) < 0) {
      av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
      return ret;
   }
   
   if ((ret = avformat_find_stream_info(_fmtCtx, NULL)) < 0) {
      av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
      return ret;
   }
   
   // select the video stream 
   ret = av_find_best_stream(_fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
   if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Cannot find a video stream in the input file\n");
      return ret;
   }
   _videoStreamIndex = ret;
   _decCtx = _fmtCtx->streams[_videoStreamIndex]->codec;
   
   // init the video decoder 
   if ((ret = avcodec_open2(_decCtx, dec, NULL)) < 0) {
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
//   enum AVPixelFormat pixFmts[] = { AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE };
//   enum AVPixelFormat pixFmts[] = { AV_PIX_FMT_RGBA, AV_PIX_FMT_NONE };
   enum AVPixelFormat pixFmts[] = { AV_PIX_FMT_YUV422P, AV_PIX_FMT_NONE };
   AVBufferSinkParams *buffersinkParams;
   
   _filterGraph = avfilter_graph_alloc();
   
   // buffer video source: the decoded frames from the decoder will be inserted here. 
   snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            _decCtx->width, _decCtx->height, _decCtx->pix_fmt,
            _decCtx->time_base.num, _decCtx->time_base.den,
            _decCtx->sample_aspect_ratio.num, _decCtx->sample_aspect_ratio.den);
   
   ret = avfilter_graph_create_filter(&_bufferSrcCtx, bufferSrc, "in", args, NULL, _filterGraph);
   if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
      return ret;
   }
   
   // buffer video sink: to terminate the filter chain. 
   buffersinkParams = av_buffersink_params_alloc();
   buffersinkParams->pixel_fmts = pixFmts;
   ret = avfilter_graph_create_filter(&_bufferSinkCtx, bufferSink, "out",
                                      NULL, buffersinkParams, _filterGraph);
   av_free(buffersinkParams);
   if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
      return ret;
   }
   
   // Endpoints for the filter graph. 
   outputs->name       = av_strdup("in");
   outputs->filter_ctx = _bufferSrcCtx;
   outputs->pad_idx    = 0;
   outputs->next       = NULL;
   
   inputs->name       = av_strdup("out");
   inputs->filter_ctx = _bufferSinkCtx;
   inputs->pad_idx    = 0;
   inputs->next       = NULL;
   
   if ((ret = avfilter_graph_parse(_filterGraph, filtersDescr, &inputs, &outputs, NULL)) < 0)
      return ret;
   
   if ((ret = avfilter_graph_config(_filterGraph, NULL)) < 0)
      return ret;
   return 0;
}

// Add an output stream.
AVStream *add_stream(AVFormatContext *oc, AVCodec **codec, enum AVCodecID codec_id)
{
   AVCodecContext *c;
   AVStream *st;

   // find the encoder
   *codec = avcodec_find_encoder(codec_id);
   if (!(*codec))
      throw std::runtime_error("Could not find encoder");

   st = avformat_new_stream(oc, *codec);
   if (!st)
      throw std::runtime_error("Could not allocate stream");

   st->id = oc->nb_streams-1;
   c = st->codec;

   if ((*codec)->type == AVMEDIA_TYPE_VIDEO) {
     c->codec_id = codec_id;

     c->bit_rate = 120000000;
     // Resolution must be a multiple of two.
     c->width    = 1920;
     c->height   = 1080;
     /* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which _frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
     c->time_base.den = 25; //STREAM_FRAME_RATE;
     c->time_base.num = 1;
     c->gop_size      = 12; // emit one intra _frame every twelve frames at most
     c->pix_fmt       = STREAM_PIX_FMT;
     if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
       // just for testing, we also add B frames
       c->max_b_frames = 2;
     }
     if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
       /* Needed to avoid using macroblocks in which some coeffs overflow.
             * This does not happen with normal video, it just happens here as
             * the motion of the chroma plane does not match the luma plane. */
       c->mb_decision = 2;
     }
   }

   // Some formats want stream headers to be separate.
   if (oc->oformat->flags & AVFMT_GLOBALHEADER)
      c->flags |= CODEC_FLAG_GLOBAL_HEADER;

   return st;
}

void open_video(AVFormatContext *oc, AVCodec *codec, AVStream *st)
{
   AVCodecContext *c = st->codec;

   av_dump_format(oc, 0, NULL, 1);

   // open the codec
   if ( avcodec_open2(c, codec, NULL)  < 0 )
      throw std::runtime_error("Could not open video codec");

   // allocate and init a re-usable _frame
   if ( !avcodec_alloc_frame() )
      throw std::runtime_error("Could not allocate video _frame");

   // Allocate the encoded raw picture.
   if( avpicture_alloc(&_dst_picture, c->pix_fmt, c->width, c->height) <0 )
      throw std::runtime_error("Could not allocate picture");

   // If the output format is not YUV422P, then a temporary YUV422P picture
   // is needed too. It is then converted to the required * output format.
   if (c->pix_fmt != AV_PIX_FMT_YUV422P
      && avpicture_alloc(&_src_picture, AV_PIX_FMT_YUV422P, c->width, c->height) <0 )
         throw std::runtime_error("Could not allocate temporary picture");

   // copy data and linesize picture pointers to _frame
//   * reinterpret_cast<AVPicture*>(_frame) = _dst_picture;
   _frame = reinterpret_cast<AVFrame*>(&_dst_picture);
}

void write_video_frame(AVFormatContext *oc, AVStream *st)
{
   int ret;
   AVCodecContext *c = st->codec;

//   if (_frame_count < STREAM_NB_FRAMES)
//      if (c->pix_fmt != AV_PIX_FMT_YUV422P) {
//         // as we only generate a YUV420P picture, we must
//         // convert it to the codec pixel format if needed
//            struct SwsContext *sws_ctx = sws_getContext(c->width, c->height, AV_PIX_FMT_YUV422P,
//                                     c->width, c->height, c->pix_fmt, _sws_flags, NULL, NULL, NULL);
//            if (!sws_ctx)
//               throw std::runtime_error("Could not initialize the conversion context\n");

//         fill_yuv_image(&_src_picture, _frame_count, c->width, c->height);
//         sws_scale(sws_ctx, (const uint8_t * const *)_src_picture.data,
//                   _src_picture.linesize, 0, c->height, _dst_picture.data, _dst_picture.linesize);
//      }
//      else
//         fill_yuv_image(&_dst_picture, _frame_count, c->width, c->height);

   if (oc->oformat->flags & AVFMT_RAWPICTURE) {
      // Raw video case - directly store the picture in the packet
      AVPacket pkt;
      av_init_packet(&pkt);

      pkt.flags        |= AV_PKT_FLAG_KEY;
      pkt.stream_index  = st->index;
      pkt.data          = _dst_picture.data[0];
      pkt.size          = sizeof(AVPicture);

      ret = av_interleaved_write_frame(oc, &pkt);
   }
   else {
      // encode the image
      AVPacket pkt;
      int got_output;

      av_init_packet(&pkt);
      pkt.data = NULL;    // packet data will be allocated by the encoder
      pkt.size = 0;

      ret = avcodec_encode_video2(c, &pkt, _frame, &got_output);
      if (ret < 0)
         throw std::runtime_error("Error encoding video _frame");

      // If size is zero, it means the image was buffered.
      if (got_output) {
         if (c->coded_frame->key_frame)
            pkt.flags |= AV_PKT_FLAG_KEY;
         pkt.stream_index = st->index;

         // Write the compressed _frame to the media file.
         ret = av_interleaved_write_frame(oc, &pkt);
      }
      else
         ret = 0;
   }

   if (ret != 0)
      throw std::runtime_error("Error while writing video _frame");

   _frame_count++;
}

void close_video(AVFormatContext *oc, AVStream *st)
{
   avcodec_close(st->codec);
   av_free(_src_picture.data[0]);
   av_free(_dst_picture.data[0]);
   av_free(_frame);
}



int
main(int argc, char **argv)
try
{
   int ret;
   AVPacket packet;
   AVFrame *_frame = avcodec_alloc_frame();
   int got_frame;
   
   AVOutputFormat *fmt;
   AVFormatContext *oc;
   AVStream *video_st;
   AVCodec *video_codec;
   double video_pts;
//   int ret;
   const char *filename;

   if (!_frame) {
      perror("Could not allocate _frame");
      exit(1);
   }
   if (argc < 2) {
      fprintf(stderr, "Usage: %s file\n", argv[0]);
      exit(1);
   }
   
   avcodec_register_all();
   av_register_all();
   avfilter_register_all();
   
   if ((ret = openInputFile(argv[1])) < 0)
      goto end;
   if ((ret = initFilters(_filterDescr)) < 0)
      goto end;

   filename = argv[2];
//   filename = "filtered.dnxhd";
//   filename = "filtered.mov";

//   fmt = av_guess_format("mp4", NULL, NULL);
   // allocate the output media context
   avformat_alloc_output_context2(&oc, NULL, "dnxhd", NULL);
//   avformat_alloc_output_context2(&oc, NULL, NULL, filename);
   if (!oc) {
      std::cout <<"Could not deduce output format from file extension: using MPEG" <<std::endl;
      avformat_alloc_output_context2(&oc, NULL, "mpeg", filename);
   }
   if (!oc)
      return 1;
   fmt = oc->oformat;

   // Add video streams using the default format codecs and initialize the codecs.
   video_st = NULL;
//   fmt->video_codec = AV_CODEC_ID_DNXHD;
   if (fmt->video_codec != AV_CODEC_ID_NONE)
      video_st = add_stream(oc, &video_codec, fmt->video_codec);
   // Now that all the parameters are set, we can open the video
   // codecs and allocate the necessary encode buffers.
   if (video_st)
      open_video(oc, video_codec, video_st);

   av_dump_format(oc, 0, filename, 1);

   // open the output file, if needed
   if (!(fmt->flags & AVFMT_NOFILE)) {
      ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
      if (ret < 0)
         throw std::runtime_error("Could not open file");
   }

   // Write the stream header, if any.
   if (avformat_write_header(oc, NULL) < 0)
      throw std::runtime_error("Error occurred when opening output file");

   if (_frame)
      _frame->pts = 0;

   // read all packets 
   while (1) {
      AVFilterBufferRef *picref;
      if ((ret = av_read_frame(_fmtCtx, &packet)) < 0)
         break;
      
      if (packet.stream_index == _videoStreamIndex) {
         avcodec_get_frame_defaults(_frame);
         got_frame = 0;
         ret = avcodec_decode_video2(_decCtx, _frame, &got_frame, &packet);
         if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error decoding video\n");
            break;
         }
         
         if (got_frame) {
            _frame->pts = av_frame_get_best_effort_timestamp(_frame);
            
            // push the decoded _frame into the filtergraph
            if (av_buffersrc_add_frame(_bufferSrcCtx, _frame, 0) < 0) {
               av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
               break;
            }
            
            // pull filtered pictures from the filtergraph 
            while (1) {
               ret = av_buffersink_get_buffer_ref(_bufferSinkCtx, &picref, 0);
               if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                  break;
               if (ret < 0)
                  goto end;
               
               if (picref) {
                 if (video_st ) {
                   *_dst_picture.data = *picref->data;
                   *_dst_picture.linesize = *picref->linesize;
                    write_video_frame(oc, video_st);
                    _frame->pts += av_rescale_q(1, video_st->codec->time_base, video_st->time_base);
                 }
//                  displayPicref(picref, _bufferSinkCtx->inputs[0]->time_base);
                  avfilter_unref_bufferp(&picref);
               }
            }
         }
      }
      av_free_packet(&packet);
   }

end:
   avfilter_graph_free(&_filterGraph);
   if (_decCtx)
      avcodec_close(_decCtx);
   avformat_close_input(&_fmtCtx);
   av_freep(&_frame);

   // Close each codec.
   if (video_st)
      close_video(oc, video_st);

   if (!(fmt->flags & AVFMT_NOFILE))
      // Close the output file.
      avio_close(oc->pb);

   // free the stream
   avformat_free_context(oc);

   if (ret < 0 && ret != AVERROR_EOF) {
      char buf[1024];
      av_strerror(ret, buf, sizeof(buf));
      fprintf(stderr, "Error occurred: %s\n", buf);
      exit(1);
   }
   
   exit(0);
}
catch(std::exception &e)
{
  std::cout <<e.what() <<std::endl;
}
