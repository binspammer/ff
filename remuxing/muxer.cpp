#include "muxer.h"

Muxer::Muxer(const char *dst)
: _filename(dst)
, STREAM_NB_FRAMES(STREAM_DURATION * STREAM_FRAME_RATE)
{
}

using namespace std;



// Add an output stream. 
AVStream* Muxer::addStream(AVCodec **codec, enum AVCodecID codec_id)
{
   AVCodecContext *c;
   AVStream *st;
   
   // find the encoder 
   *codec = avcodec_find_encoder(codec_id);
   if (!(*codec)) 
      throw std::runtime_error("Could not find encoder");
   
   st = avformat_new_stream(_oc, *codec);
   if (!st) 
      throw std::runtime_error("Could not allocate stream");
   
   st->id = _oc->nb_streams-1;
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
   if (_oc->oformat->flags & AVFMT_GLOBALHEADER)
      c->flags |= CODEC_FLAG_GLOBAL_HEADER;
   
   return st;
}

// video output 
void Muxer::openVideo(AVCodec *codec, AVStream *st)
{
   AVCodecContext *c = st->codec;
   
   av_dump_format(_oc, 0, NULL, 1);
   
   // open the codec 
   if ( avcodec_open2(c, codec, NULL)  < 0 )
      throw std::runtime_error("Could not open video codec");
   
   // allocate and init a re-usable frame 
   if ( !avcodec_alloc_frame() )
      throw std::runtime_error("Could not allocate video frame");
   
   // Allocate the encoded raw picture. 
   if( avpicture_alloc(&_dst_picture, c->pix_fmt, c->width, c->height) <0 )
      throw std::runtime_error("Could not allocate picture");
   
   // If the output format is not YUV420P, then a temporary YUV420P picture
   // is needed too. It is then converted to the required * output format. 
   if (c->pix_fmt != AV_PIX_FMT_YUV422P
      && avpicture_alloc(&_src_picture, AV_PIX_FMT_YUV422P, c->width, c->height) <0 )
         throw std::runtime_error("Could not allocate temporary picture");
   
   // copy data and linesize picture pointers to frame 
//   * reinterpret_cast<AVPicture*>(frame) = dst_picture;
   _frame = reinterpret_cast<AVFrame*>(&_dst_picture);
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

void Muxer::writeVideoFrame(AVStream *st)
{
   int ret;
   AVCodecContext *c = st->codec;
   
   if (_frame_count < STREAM_NB_FRAMES)
      if (c->pix_fmt != AV_PIX_FMT_YUV422P) {
         // as we only generate a YUV420P picture, we must 
         // convert it to the codec pixel format if needed 
            struct SwsContext *sws_ctx = sws_getContext(c->width, c->height, AV_PIX_FMT_YUV422P,
                                     c->width, c->height, c->pix_fmt, _sws_flags, NULL, NULL, NULL);
            if (!sws_ctx) 
               throw std::runtime_error("Could not initialize the conversion context\n");
            
         fillYUVImage(&_src_picture, _frame_count, c->width, c->height);
         sws_scale(sws_ctx, (const uint8_t * const *)_src_picture.data,
                   _src_picture.linesize, 0, c->height, _dst_picture.data, _dst_picture.linesize);
      } 
      else 
         fillYUVImage(&_dst_picture, _frame_count, c->width, c->height);
   
   if (_oc->oformat->flags & AVFMT_RAWPICTURE) {
      // Raw video case - directly store the picture in the packet 
      AVPacket pkt;
      av_init_packet(&pkt);
      
      pkt.flags        |= AV_PKT_FLAG_KEY;
      pkt.stream_index  = st->index;
      pkt.data          = _dst_picture.data[0];
      pkt.size          = sizeof(AVPicture);
      
      ret = av_interleaved_write_frame(_oc, &pkt);
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
         throw std::runtime_error("Error encoding video frame");
      
      // If size is zero, it means the image was buffered. 
      if (got_output) {
         if (c->coded_frame->key_frame)
            pkt.flags |= AV_PKT_FLAG_KEY;
         pkt.stream_index = st->index;
         
         // Write the compressed frame to the media file. 
         ret = av_interleaved_write_frame(_oc, &pkt);
      } 
      else 
         ret = 0;
   }
   
   if (ret != 0) 
      throw std::runtime_error("Error while writing video frame");
   
   _frame_count++;
}

void Muxer::closeVideo(AVStream *st)
{
   avcodec_close(st->codec);
   av_free(_src_picture.data[0]);
   av_free(_dst_picture.data[0]);
   av_free(_frame);
}

void Muxer::initAll()
{

  // Initialize libavcodec, and register all codecs and formats.
  av_register_all();

  // allocate the output media context
  avformat_alloc_output_context2(&_oc, NULL, NULL, _filename);
  if (!_oc) {
     std::cout <<"Could not deduce output format from file extension: using MPEG" <<std::endl;
     avformat_alloc_output_context2(&_oc, NULL, "mpeg", _filename);
  }
  if (!_oc)
    std::cout <<"Could not open the context" <<std::endl;
//     return 1;

  _fmt = _oc->oformat;

  // Add the audio and video streams using the default format codecs
  // and initialize the codecs.
  _video_st = NULL;

  if (_fmt->video_codec != AV_CODEC_ID_NONE)
     _video_st = addStream(&_video_codec, _fmt->video_codec);


  // Now that all the parameters are set, we can open the
  // video codecs and allocate the necessary encode buffers.
  if (_video_st)
     openVideo(_video_codec, _video_st);

  av_dump_format(_oc, 0, _filename, 1);

  // open the output file, if needed
  if (!(_fmt->flags & AVFMT_NOFILE)) {
     _ret = avio_open(&_oc->pb, _filename, AVIO_FLAG_WRITE);
     if (_ret < 0)
        throw std::runtime_error("Could not open file");
  }

  // Write the stream header, if any.
  if (avformat_write_header(_oc, NULL) < 0)
     throw std::runtime_error("Error occurred when opening output file");

  if (_frame)
     _frame->pts = 0;

}

void Muxer::closeAll()
{
  /* Write the trailer, if any. The trailer must be written before you
    * close the CodecContexts open when you wrote the header; otherwise
    * av_write_trailer() may try to use memory that was freed on av_codec_close(). */
  av_write_trailer(_oc);

  // Close each codec.
  if (_video_st)
     closeVideo(_video_st);

  if (!(_fmt->flags & AVFMT_NOFILE))
     // Close the output file.
     avio_close(_oc->pb);

  // free the stream
  avformat_free_context(_oc);
}

//
// media file output 

int Muxer::mux()
try
{ 
  initAll();

   for (;;) 
   {
      // Compute current audio and video time. 
      if (_video_st)
         _video_pts = (double)_video_st->pts.val * _video_st->time_base.num / _video_st->time_base.den;
      else
         _video_pts = 0.0;
      
      if (!_video_st || _video_pts >= STREAM_DURATION)
         break;
      
      // write interleaved audio and video frames 
      if (_video_st ) {
         writeVideoFrame(_video_st);
         _frame->pts += av_rescale_q(1, _video_st->codec->time_base, _video_st->time_base);
      }
   }
   
   return 0;
}
catch(exception &e)
{
   std::cerr <<e.what() <<std::endl;
}
