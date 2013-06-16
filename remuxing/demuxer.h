#ifndef DEMUXER_HPP
#define DEMUXER_HPP
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

class Demuxer
{
public:
   Demuxer(const char *src);
   void demux();
   
private: 
   int decode_packet(int *got_frame, int cached);
   int open_codec_context(int *stream_idx, AVFormatContext *fmt_ctx, enum AVMediaType type);
   int get_format_from_sample_fmt(const char **fmt, enum AVSampleFormat sample_fmt);
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
};

#endif // DEMUXER_HPP
