#include "demuxer.h"
#include "muxer.h"
#include <iostream>

using namespace std;

int main(int argc, char **argv)
{
   
   if (argc < 3) {
      fprintf(stderr, "usage: %s input_file video_output_file \n", argv[0]);
      exit(1);
   }
   
   Demuxer demuxer(argv[1]);
   Muxer muxer(argv[2]);
   demuxer.demux();
   
   return 0;
}

