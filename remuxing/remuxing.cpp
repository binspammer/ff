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
   
   Filter filter(argv[1]);
//   filter.decode();
   filter.init();
//   Filter::Images& images(filter.getImages());
//   std::cout <<"filter:\n    decoded " <<images.size() <<" frames from source '"<<argv[1]<<"'" <<std::endl;

   Muxer muxer(argv[2]);
   muxer.init();
//   Filter::Images::iterator image(images.begin());
//   for (auto image(images.begin()); image != images.end(); ++image) {
   for(;;)
   {
      Filter::Images& images = filter.readVideoFrames(10);
      muxer.writeVideoFrames((Muxer::Images&)images);
      if(images.empty())
         break;
   }

   return 0;

//for(Filter::Image image; image = filter.readVideoFrames(); );

}
catch(std::exception &e)
{
   std::cout <<e.what()<<std::endl;
}
