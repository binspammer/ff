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

using namespace std;

// 5 seconds stream duration 
#define STREAM_DURATION   200.0
#define STREAM_FRAME_RATE 25 // 25 images/s 
#define STREAM_NB_FRAMES  ((int)(STREAM_DURATION * STREAM_FRAME_RATE))
#define STREAM_PIX_FMT    AV_PIX_FMT_YUV422P // default pix_fmt 

int sws_flags = SWS_BICUBIC;

float t, tincr, tincr2;
int16_t *samples;

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
   
   switch ((*codec)->type) {
      case AVMEDIA_TYPE_AUDIO:
         st->id = 1;
         c->sample_fmt  = AV_SAMPLE_FMT_S16;
         c->bit_rate    = 64000;
         c->sample_rate = 44100;
         c->channels    = 2;
         break;
         
      case AVMEDIA_TYPE_VIDEO:
         c->codec_id = codec_id;
         
         c->bit_rate = 120000000;
         // Resolution must be a multiple of two. 
         c->width    = 1920;
         c->height   = 1080;
         /* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
         c->time_base.den = 25; //STREAM_FRAME_RATE;
         c->time_base.num = 1;
         c->gop_size      = 12; // emit one intra frame every twelve frames at most 
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
         break;
         
      default:
         break;
   }
   
   // Some formats want stream headers to be separate. 
   if (oc->oformat->flags & AVFMT_GLOBALHEADER)
      c->flags |= CODEC_FLAG_GLOBAL_HEADER;
   
   return st;
}

//
// video output 

AVFrame *frame;
AVPicture src_picture, dst_picture;
int frame_count;

void open_video(AVFormatContext *oc, AVCodec *codec, AVStream *st)
{
   AVCodecContext *c = st->codec;
   
   av_dump_format(oc, 0, NULL, 1);
   
   // open the codec 
   if ( avcodec_open2(c, codec, NULL)  < 0 )
      throw std::runtime_error("Could not open video codec");
   
   // allocate and init a re-usable frame 
   if ( !avcodec_alloc_frame() )
      throw std::runtime_error("Could not allocate video frame");
   
   // Allocate the encoded raw picture. 
   if( avpicture_alloc(&dst_picture, c->pix_fmt, c->width, c->height) <0 )
      throw std::runtime_error("Could not allocate picture");
   
   // If the output format is not YUV420P, then a temporary YUV420P picture
   // is needed too. It is then converted to the required * output format. 
   if (c->pix_fmt != AV_PIX_FMT_YUV422P
      && avpicture_alloc(&src_picture, AV_PIX_FMT_YUV422P, c->width, c->height) <0 ) 
         throw std::runtime_error("Could not allocate temporary picture");
   
   // copy data and linesize picture pointers to frame 
//   * reinterpret_cast<AVPicture*>(frame) = dst_picture;
   frame = reinterpret_cast<AVFrame*>(&dst_picture);
}

// Prepare a dummy image. 
void fill_yuv_image(AVPicture *pict, int frame_index, int width, int height)
{
   int x, y, i;
   
   i = frame_index;
   
   // Y 
   for (y = 0; y < height; y++)
      for (x = 0; x < width; x++)
         pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;
   
   // Cb and Cr 
   for (y = 0; y < height / 2; y++) {
      for (x = 0; x < width / 2; x++) {
         pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
         pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
      }
   }
}

void write_video_frame(AVFormatContext *oc, AVStream *st)
{
   int ret;
   AVCodecContext *c = st->codec;
   
   if (frame_count < STREAM_NB_FRAMES) {
      if (c->pix_fmt != AV_PIX_FMT_YUV422P) {
         // as we only generate a YUV420P picture, we must 
         // convert it to the codec pixel format if needed 
            struct SwsContext *sws_ctx = sws_getContext(c->width, c->height, AV_PIX_FMT_YUV422P,
                                     c->width, c->height, c->pix_fmt, sws_flags, NULL, NULL, NULL);
            if (!sws_ctx) 
               throw std::runtime_error("Could not initialize the conversion context\n");
            
         fill_yuv_image(&src_picture, frame_count, c->width, c->height);
         sws_scale(sws_ctx, (const uint8_t * const *)src_picture.data, 
                   src_picture.linesize, 0, c->height, dst_picture.data, dst_picture.linesize);
      } 
      else 
         fill_yuv_image(&dst_picture, frame_count, c->width, c->height);
   }

   if (oc->oformat->flags & AVFMT_RAWPICTURE) {
      // Raw video case - directly store the picture in the packet 
      AVPacket pkt;
      av_init_packet(&pkt);
      
      pkt.flags        |= AV_PKT_FLAG_KEY;
      pkt.stream_index  = st->index;
      pkt.data          = dst_picture.data[0];
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
      
      ret = avcodec_encode_video2(c, &pkt, frame, &got_output);
      if (ret < 0) 
         throw std::runtime_error("Error encoding video frame");
      
      // If size is zero, it means the image was buffered. 
      if (got_output) {
         if (c->coded_frame->key_frame)
            pkt.flags |= AV_PKT_FLAG_KEY;
         pkt.stream_index = st->index;
         
         // Write the compressed frame to the media file. 
         ret = av_interleaved_write_frame(oc, &pkt);
      } 
      else 
         ret = 0;
   }
   
   if (ret != 0) 
      throw std::runtime_error("Error while writing video frame");
   
   frame_count++;
}

void close_video(AVFormatContext *oc, AVStream *st)
{
   avcodec_close(st->codec);
   av_free(src_picture.data[0]);
   av_free(dst_picture.data[0]);
   av_free(frame);
}

//
// media file output 

int 
main(int argc, char **argv)
try
{
   AVOutputFormat *fmt;
   AVFormatContext *oc;
   AVStream *video_st;
   AVCodec *video_codec;
   double video_pts;
   int ret;
   const char *filename;
   
   // Initialize libavcodec, and register all codecs and formats. 
   av_register_all();
   
   if (argc != 2) {
      printf("usage: %s output_file\n"
             "API example program to output a media file with libavformat.\n"
             "This program generates a synthetic audio and video stream, encodes and\n"
             "muxes them into a file named output_file.\n"
             "The output format is automatically guessed according to the file extension.\n"
             "Raw images can also be output by using '%%d' in the filename.\n"
             "\n", argv[0]);
      return 1;
   }
   
   filename = argv[1];
   
   // allocate the output media context 
   avformat_alloc_output_context2(&oc, NULL, NULL, filename);
   if (!oc) {
      std::cout <<"Could not deduce output format from file extension: using MPEG" <<std::endl;
      avformat_alloc_output_context2(&oc, NULL, "mpeg", filename);
   }
   if (!oc) 
      return 1;
   
   fmt = oc->oformat;
   
   // Add the audio and video streams using the default format codecs
   // and initialize the codecs. 
   video_st = NULL;
   
   if (fmt->video_codec != AV_CODEC_ID_NONE) 
      video_st = add_stream(oc, &video_codec, fmt->video_codec);
   
   
   // Now that all the parameters are set, we can open the audio and
   // video codecs and allocate the necessary encode buffers.
//   fmt->video_codec = AV_CODEC_ID_DNXHD;
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
   
   if (frame)
      frame->pts = 0;
   
   for (;;) 
   {
      // Compute current audio and video time. 
      if (video_st)
         video_pts = (double)video_st->pts.val * video_st->time_base.num / video_st->time_base.den;
      else
         video_pts = 0.0;
      
      if (!video_st || video_pts >= STREAM_DURATION)
         break;
      
      // write interleaved audio and video frames 
      if (video_st ) {
         write_video_frame(oc, video_st);
         frame->pts += av_rescale_q(1, video_st->codec->time_base, video_st->time_base);
      }
   }
   
   /* Write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on av_codec_close(). */
   av_write_trailer(oc);
   
   // Close each codec. 
   if (video_st)
      close_video(oc, video_st);
   
   if (!(fmt->flags & AVFMT_NOFILE))
      // Close the output file. 
      avio_close(oc->pb);
   
   // free the stream 
   avformat_free_context(oc);
   
   return 0;
}
catch(exception &e)
{
   std::cerr <<e.what() <<std::endl;
}
