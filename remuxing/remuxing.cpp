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
   Filter::Images& images(filter.getImages());
   std::cout <<"filter: decoded " <<images.size() <<" frames from source '"<<argv[1]<<"'" <<std::endl;

   Muxer muxer(argv[2], images);
   muxer.mux();
   return 0;
}
catch(std::exception &e)
{
   std::cout <<e.what()<<std::endl;
}
