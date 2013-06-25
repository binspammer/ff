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
      cerr <<"usage: " <<argv[0] <<argv[0] <<"input_file video_output_file" <<std::endl;
      exit(1);
   }
   
   Filter filter(argv[1]);
   Muxer muxer(argv[2]);

   for(;;)
   {
      Images& images = filter.readVideoFrames(10);
      if(images.empty()) break;
      muxer.writeVideoFrames((Images&)images);
   }

   return 0;
}
catch(std::exception &e)
{
   std::cout <<e.what()<<std::endl;
}
