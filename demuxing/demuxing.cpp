#define PRId64 ""

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
#include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
//#include <libavutil/timestamp.h>
#include <libswscale/swscale.h>
}

AVFormatContext *fmt_ctx = NULL;
AVCodecContext *video_dec_ctx = NULL;
AVStream *video_stream = NULL;
const char *src_filename = NULL;
const char *video_dst_filename = NULL;
FILE *video_dst_file = NULL;

uint8_t *video_dst_data[4] = {NULL};
int      video_dst_linesize[4];
int video_dst_bufsize;

int video_stream_idx = -1;
AVFrame *frame = NULL;
AVPacket pkt;
int video_frame_count = 0;

int decode_packet(int *got_frame, int cached)
{
   int ret = 0;
   
   if (pkt.stream_index == video_stream_idx) {
      // decode video frame 
      ret = avcodec_decode_video2(video_dec_ctx, frame, got_frame, &pkt);
      if (ret < 0) {
         fprintf(stderr, "Error decoding video frame\n");
         return ret;
      }
      
      if (*got_frame) {
         printf("video_frame%s n:%d coded_n:%d pts:%s\n",
                cached ? "(cached)" : "",
                video_frame_count++, frame->coded_picture_number, 0);
//                av_ts2timestr(frame->pts, &video_dec_ctx->time_base);
         
         // copy decoded frame to destination buffer:
         // this is required since rawvideo expects non aligned data 
         av_image_copy(video_dst_data, video_dst_linesize,
                       (const uint8_t **)(frame->data), frame->linesize,
                       video_dec_ctx->pix_fmt, video_dec_ctx->width, video_dec_ctx->height);
         
         // write to rawvideo file 
         fwrite(video_dst_data[0], 1, video_dst_bufsize, video_dst_file);
      }
   } 
   
   return ret;
}

int open_codec_context(int *stream_idx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
   int ret;
   AVStream *st;
   AVCodecContext *dec_ctx = NULL;
   AVCodec *dec = NULL;
   
   ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
   if (ret < 0) {
      fprintf(stderr, "Could not find %s stream in input file '%s'\n",
              av_get_media_type_string(type), src_filename);
      return ret;
   } 
   else {
      *stream_idx = ret;
      st = fmt_ctx->streams[*stream_idx];
      
      // find decoder for the stream 
      dec_ctx = st->codec;
      dec = avcodec_find_decoder(dec_ctx->codec_id);
      if (!dec) {
         fprintf(stderr, "Failed to find %s codec\n", av_get_media_type_string(type));
         return ret;
      }
      
      if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
         fprintf(stderr, "Failed to open %s codec\n",
                 av_get_media_type_string(type));
         return ret;
      }
   }
   
   return 0;
}

int get_format_from_sample_fmt(const char **fmt, enum AVSampleFormat sample_fmt)
{
   int i;
   struct sample_fmt_entry {
      enum AVSampleFormat sample_fmt; const char *fmt_be, *fmt_le;
   } sample_fmt_entries[] = {
   { AV_SAMPLE_FMT_U8,  "u8",    "u8"    },
   { AV_SAMPLE_FMT_S16, "s16be", "s16le" },
   { AV_SAMPLE_FMT_S32, "s32be", "s32le" },
   { AV_SAMPLE_FMT_FLT, "f32be", "f32le" },
   { AV_SAMPLE_FMT_DBL, "f64be", "f64le" },
};
   *fmt = NULL;
   
   for (i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); i++) {
      struct sample_fmt_entry *entry = &sample_fmt_entries[i];
      if (sample_fmt == entry->sample_fmt) {
         *fmt = AV_NE(entry->fmt_be, entry->fmt_le);
         return 0;
      }
   }
   
   fprintf(stderr, "sample format %s is not supported as output format\n", av_get_sample_fmt_name(sample_fmt));
   return -1;
}

int main (int argc, char **argv)
{
   int ret = 0, got_frame;
   
   if (argc < 3) {
      fprintf(stderr, "usage: %s input_file video_output_file \n"
              "API example program to show how to read frames from an input file.\n"
              "This program reads frames from a file, decodes them, and writes decoded\n"
              "video frames to a rawvideo file named video_output_file\n"
              "\n", argv[0]);
      exit(1);
   }
   src_filename = argv[1];
   video_dst_filename = argv[2];
   
   // register all formats and codecs 
   av_register_all();
   
   // open input file, and allocate format context 
   if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0) 
      fprintf(stderr, "Could not open source file %s\n", src_filename);
   
   // retrieve stream information 
   if (avformat_find_stream_info(fmt_ctx, NULL) < 0) 
      fprintf(stderr, "Could not find stream information\n");
   
   if (open_codec_context(&video_stream_idx, fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0) {
      video_stream = fmt_ctx->streams[video_stream_idx];
      video_dec_ctx = video_stream->codec;
      
      video_dst_file = fopen(video_dst_filename, "wb");
      if (!video_dst_file) {
         fprintf(stderr, "Could not open destination file %s\n", video_dst_filename);
         ret = 1;
         goto end;
      }
      
      // allocate image where the decoded image will be put 
      ret = av_image_alloc(video_dst_data, video_dst_linesize,
                           video_dec_ctx->width, video_dec_ctx->height,
                           video_dec_ctx->pix_fmt, 1);
      if (ret < 0) {
         fprintf(stderr, "Could not allocate raw video buffer\n");
         goto end;
      }
      video_dst_bufsize = ret;
   }
   
   // dump input information to stderr 
   av_dump_format(fmt_ctx, 0, src_filename, 0);
   
   if (!video_stream) {
      fprintf(stderr, "Could not find video stream in the input, aborting\n");
      ret = 1;
      goto end;
   }
   
   frame = avcodec_alloc_frame();
   if (!frame) {
      fprintf(stderr, "Could not allocate frame\n");
      ret = AVERROR(ENOMEM);
      goto end;
   }
   
   // initialize packet, set data to NULL, let the demuxer fill it 
   av_init_packet(&pkt);
   pkt.data = NULL;
   pkt.size = 0;
   
   if (video_stream)
      printf("Demuxing video from file '%s' into '%s'\n", src_filename, video_dst_filename);
   
   // read frames from the file 
   while (av_read_frame(fmt_ctx, &pkt) >= 0) {
      decode_packet(&got_frame, 0);
      av_free_packet(&pkt);
   }
   
   // flush cached frames 
   pkt.data = NULL;
   pkt.size = 0;
   do {
      decode_packet(&got_frame, 1);
   } while (got_frame);
   
   printf("Demuxing succeeded.\n");
   
   if (video_stream) {
      printf("Play the output video file with the command:\n"
             "ffplay -f rawvideo -pix_fmt %s -video_size %dx%d %s\n",
             av_get_pix_fmt_name(video_dec_ctx->pix_fmt), video_dec_ctx->width, video_dec_ctx->height,
             video_dst_filename);
   }
      
end:
   if (video_dec_ctx)
      avcodec_close(video_dec_ctx);
   avformat_close_input(&fmt_ctx);
   if (video_dst_file)
      fclose(video_dst_file);
   av_free(frame);
   av_free(video_dst_data[0]);
   
   return ret < 0;
}
