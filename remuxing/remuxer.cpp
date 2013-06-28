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
      int windowSize(10);
      Images& images = filter.readVideoFrames(windowSize);

      int index(0);
      // Defliciring images here accessing by index in [0,windowSize-1] range
      images[index]->data[0];
      images[index]->linesizes[0];

      if(images.empty()) break;
      muxer.writeVideoFrames((Images&)images);
   }

   return 0;
}
catch(std::exception &e)
{
   std::cout <<e.what()<<std::endl;
}
