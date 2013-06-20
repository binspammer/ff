#include "muxer.h"

using namespace std;

Muxer::Muxer(const char *dst, Images& images)
   : STREAM_NB_FRAMES(STREAM_DURATION * STREAM_FRAME_RATE)
   , _filename(dst)
   , _images(images)
{
}

Muxer::~Muxer()
{
   close();
}

void Muxer::mux()
try
{
   init();

   for (;;)
   {
      // Compute current video time.
      if (_videoSt)
         _videoPts = (double)_videoSt->pts.val * _videoSt->time_base.num / _videoSt->time_base.den;
      else
         _videoPts = 0.0;

      if (!_videoSt || _videoPts >= STREAM_DURATION)
         break;

      // write interleaved audio and video frames
      if (_videoSt ) {
         writeVideoFrame();
         _frame->pts += av_rescale_q(1, _videoSt->codec->time_base, _videoSt->time_base);
      }
   }
}
catch(exception &e)
{
   std::cerr <<e.what() <<std::endl;
}

void Muxer::init()
{
   // Initialize libavcodec, and register all codecs and formats
   av_register_all();

   // allocate the output media context
//   avformat_alloc_output_context2(&_oc, NULL, "dnxhd", _filename);
   _fmt = av_guess_format(MUXER, NULL, NULL);
   _fmt->video_codec = VIDEO_CODEC;
   avformat_alloc_output_context2(&_oc, _fmt, NULL, _filename);
   if (!_oc) {
      std::cout <<"Could not deduce output format from file extension: using MPEG" <<std::endl;
      avformat_alloc_output_context2(&_oc, NULL, "mpeg", _filename);
   }
   if (!_oc)
      throw std::runtime_error("Could not open the context");

//   _fmt = _oc->oformat;

   // Add the video streams using the default format codecs and initialize the codecs.
   if (_fmt->video_codec != AV_CODEC_ID_NONE)
      _videoSt = addStream(_fmt->video_codec);

   // Now that all the parameters are set, we can open the video codecs and allocate the necessary encode buffers.
   if (_videoSt)
      openVideo();

   av_dump_format(_oc, 0, _filename, 1);

   // open the output file, if needed
   if ( !(_fmt->flags & AVFMT_NOFILE)
      && avio_open(&_oc->pb, _filename, AVIO_FLAG_WRITE) < 0 )
         throw std::runtime_error("Could not open file");

   // Write the stream header, if any.
   if (avformat_write_header(_oc, NULL) < 0)
      throw std::runtime_error("Error occurred when opening output file");

   if (_frame)
      _frame->pts = 0;

}

void Muxer::close()
{
   // Write the trailer, if any. The trailer must be written before you close the CodecContexts open when you
   // wrote the header; otherwise av_write_trailer() may try to use memory that was freed on av_codec_close()
   av_write_trailer(_oc);

   // Close each codec.
   if (_videoSt)
      closeVideo();

   if (!(_fmt->flags & AVFMT_NOFILE))
      // Close the output file.
      avio_close(_oc->pb);

   // free the stream
   avformat_free_context(_oc);
}

// video output 
void Muxer::openVideo()
{
   // open the codec
   AVCodecContext *c = _videoSt->codec;
   if ( avcodec_open2(c, _videoCodec, NULL)  < 0 )
      throw std::runtime_error("Could not open video codec");
   
   // allocate and init a re-usable frame
   if ( !avcodec_alloc_frame() )
      throw std::runtime_error("Could not allocate video frame");
   
   // Allocate the encoded raw picture
   if( avpicture_alloc(&_dstPicture, c->pix_fmt, c->width, c->height) <0 )
      throw std::runtime_error("Could not allocate picture");
   
   // If the output format is not the destination one, then a temporary
   // picture is needed. It is then converted to the required output format
   if (c->pix_fmt != STREAM_PIX_FMT
       && avpicture_alloc(&_srcPicture, STREAM_PIX_FMT, c->width, c->height) <0 )
      throw std::runtime_error("Could not allocate temporary picture");
   
   // copy data and linesize picture pointers to frame
   _frame = reinterpret_cast<AVFrame*>(&_dstPicture);
}

void Muxer::closeVideo()
{
   avcodec_close(_videoSt->codec);
//   av_freep(&_srcPicture.data[0]);
//   av_freep(&_dstPicture.data[0]);
//   av_freep(&_frame);
}

// media file output
void Muxer::writeVideoFrame()
{
   int ret;
   AVCodecContext *c = _videoSt->codec;
   
   if (_frameCount < STREAM_NB_FRAMES) {
      if (c->pix_fmt != STREAM_PIX_FMT) {
         // as we only generate a YUV422P picture, we must convert it to the codec pixel format if needed
         struct SwsContext *sws_ctx = sws_getContext(c->width, c->height, STREAM_PIX_FMT,
                                          c->width, c->height, c->pix_fmt, _sws_flags, NULL, NULL, NULL);
         if (!sws_ctx)
            throw std::runtime_error("Could not initialize the conversion context\n");

         fillYUVImage(&_srcPicture, _frameCount, c->width, c->height);
         sws_scale(sws_ctx, (const uint8_t * const *)_srcPicture.data,
                   _srcPicture.linesize, 0, c->height, _dstPicture.data, _dstPicture.linesize);
      }
      else
         fillYUVImage(&_dstPicture, _frameCount, c->width, c->height);
   }

   AVPacket pkt;
   av_init_packet(&pkt);
   if (_oc->oformat->flags & AVFMT_RAWPICTURE) {
      // Raw video case - directly store the picture in the packet
      pkt.flags        |= AV_PKT_FLAG_KEY;
      pkt.stream_index  = _videoSt->index;
      pkt.data          = _dstPicture.data[0];
      pkt.size          = sizeof(AVPicture);
      
      ret = av_interleaved_write_frame(_oc, &pkt);
   }
   else {
      // encode the image
      pkt.data = NULL;    // packet data will be allocated by the encoder
      pkt.size = 0;
      
      int got_output;
      if (avcodec_encode_video2(c, &pkt, _frame, &got_output) < 0)
         throw std::runtime_error("Error encoding video frame");
      
      // If size is zero, it means the image was buffered.
      if (got_output) {
         if (c->coded_frame->key_frame)
            pkt.flags |= AV_PKT_FLAG_KEY;
         pkt.stream_index = _videoSt->index;
         
         // Write the compressed frame to the media file.
         ret = av_interleaved_write_frame(_oc, &pkt);
      }
      else
         ret = 0;
   }
   
   if (ret != 0)
      throw std::runtime_error("Error while writing video frame");
   
   _frameCount++;
}

// Add an output stream.
AVStream* Muxer::addStream(enum AVCodecID codec_id)
{
   AVCodecContext *c;
   AVStream *st;

   // find the encoder
   _videoCodec = avcodec_find_encoder(codec_id);
   if (!(_videoCodec))
      throw std::runtime_error("Could not find encoder");

   st = avformat_new_stream(_oc, _videoCodec);
   if (!st)
      throw std::runtime_error("Could not allocate stream");

   st->id = _oc->nb_streams-1;
   c = st->codec;

   switch ((_videoCodec)->type) {
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
   if (_oc->oformat->flags & AVFMT_GLOBALHEADER)
      c->flags |= CODEC_FLAG_GLOBAL_HEADER;

   return st;
}

// Prepare a dummy image.
void Muxer::fillYUVImage(AVPicture *pict, int frame_index, int width, int height)
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



