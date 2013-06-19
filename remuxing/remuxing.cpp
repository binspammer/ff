#include "filter.h"
#include "demuxer.h"
#include "muxer.h"

#include <iostream>

using namespace std;

int
main(int argc, char **argv)
try
{
   
   if (argc != 3) {
      fprintf(stderr, "usage: %s input_file video_output_file \n", argv[0]);
      exit(1);
   }
   
//   Demuxer demuxer(argv[1]);
//   demuxer.demux();

   Filter filter(argv[1]);
   filter.decode();
   std::cout <<"filter.decode: " <<filter.getImages().size() <<" frames in source" <<std::endl;

   Muxer muxer(argv[2], filter.getImages());
   muxer.mux();
   return 0;
}
catch(std::exception &e)
{
   std::cout <<e.what()<<std::endl;
}
