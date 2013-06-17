#include "demuxer.h"

Demuxer::Demuxer(const char *src)
: _src_filename(src)
//, video_dst_filename(dst)
{
}


int Demuxer::decodePacket(int *got_frame, int cached)
{
   int ret = 0;
   
   if (_pkt.stream_index == _video_stream_idx) {
      // decode video frame 
      ret = avcodec_decode_video2(_video_dec_ctx, _frame, got_frame, &_pkt);
      if (ret < 0) {
         fprintf(stderr, "Error decoding video frame\n");
         return ret;
      }
      
      if (*got_frame) {
         printf("video_frame%s n:%d coded_n:%d pts:%s\n",
                cached ? "(cached)" : "",
                _video_frame_count++, _frame->coded_picture_number, 0);
//                av_ts2timestr(frame->pts, &video_dec_ctx->time_base);
         
         // copy decoded frame to destination buffer:
         // this is required since rawvideo expects non aligned data 
         av_image_copy(_video_dst_data, _video_dst_linesize,
                       (const uint8_t **)(_frame->data), _frame->linesize,
                       _video_dec_ctx->pix_fmt, _video_dec_ctx->width, _video_dec_ctx->height);
         
         // write to rawvideo file 
         fwrite(_video_dst_data[0], 1, _video_dst_bufsize, _video_dst_file);
      }
   } 
   
   return ret;
}

int Demuxer::openCodecContext(int *stream_idx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
   int ret;
   AVStream *st;
   AVCodecContext *dec_ctx = NULL;
   AVCodec *dec = NULL;
   
   ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
   if (ret < 0) {
      fprintf(stderr, "Could not find %s stream in input file '%s'\n",
              av_get_media_type_string(type), _src_filename);
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

int Demuxer::getFormatFromSampleFmt(const char **fmt, enum AVSampleFormat sample_fmt)
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

void Demuxer::demux()
{
   int ret = 0, got_frame;
   
   // register all formats and codecs 
   av_register_all();
   
   // open input file, and allocate format context 
   if (avformat_open_input(&_fmt_ctx, _src_filename, NULL, NULL) < 0)
      fprintf(stderr, "Could not open source file %s\n", _src_filename);
   
   // retrieve stream information 
   if (avformat_find_stream_info(_fmt_ctx, NULL) < 0)
      fprintf(stderr, "Could not find stream information\n");
   
   if (openCodecContext(&_video_stream_idx, _fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0) {
      _video_stream = _fmt_ctx->streams[_video_stream_idx];
      _video_dec_ctx = _video_stream->codec;
      
      _video_dst_file = fopen(_video_dst_filename, "wb");
      if (!_video_dst_file) {
         fprintf(stderr, "Could not open destination file %s\n", _video_dst_filename);
         ret = 1;
         goto end;
      }
      
      // allocate image where the decoded image will be put 
      ret = av_image_alloc(_video_dst_data, _video_dst_linesize,
                           _video_dec_ctx->width, _video_dec_ctx->height,
                           _video_dec_ctx->pix_fmt, 1);
      if (ret < 0) {
         fprintf(stderr, "Could not allocate raw video buffer\n");
         goto end;
      }
      _video_dst_bufsize = ret;
   }
   
   // dump input information to stderr 
   av_dump_format(_fmt_ctx, 0, _src_filename, 0);
   
   if (!_video_stream) {
      fprintf(stderr, "Could not find video stream in the input, aborting\n");
      ret = 1;
      goto end;
   }
   
   _frame = avcodec_alloc_frame();
   if (!_frame) {
      fprintf(stderr, "Could not allocate frame\n");
      ret = AVERROR(ENOMEM);
      goto end;
   }
   
   // initialize packet, set data to NULL, let the demuxer fill it 
   av_init_packet(&_pkt);
   _pkt.data = NULL;
   _pkt.size = 0;
   
   if (_video_stream)
      printf("Demuxing video from file '%s' into '%s'\n", _src_filename, _video_dst_filename);
   
   // read frames from the file 
   while (av_read_frame(_fmt_ctx, &_pkt) >= 0) {
      decodePacket(&got_frame, 0);
      av_free_packet(&_pkt);
   }
   
   // flush cached frames 
   _pkt.data = NULL;
   _pkt.size = 0;
   do {
      decodePacket(&got_frame, 1);
   } while (got_frame);
   
   printf("Demuxing succeeded.\n");
   
   if (_video_stream) {
      printf("Play the output video file with the command:\n"
             "ffplay -f rawvideo -pix_fmt %s -video_size %dx%d %s\n",
             av_get_pix_fmt_name(_video_dec_ctx->pix_fmt), _video_dec_ctx->width, _video_dec_ctx->height,
             _video_dst_filename);
   }
      
end:
   if (_video_dec_ctx)
      avcodec_close(_video_dec_ctx);
   avformat_close_input(&_fmt_ctx);
   if (_video_dst_file)
      fclose(_video_dst_file);
   av_free(_frame);
   av_free(_video_dst_data[0]);
   
//   return ret < 0;
}
