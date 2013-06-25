#ifndef IMAGE_H
#define IMAGE_H

#include "libav.h"

#include <memory>
#include <vector>

struct ImageImpl
{
   uint8_t *data[4];
   int linesizes[4];

   ~ImageImpl() {
//      free(data);
//      delete [] data;
      av_freep(&data[0]);
   }
};

typedef std::shared_ptr<ImageImpl> Image;
typedef std::vector<Image> Images;
//typedef std::vector<StructImage> StructImages;


//class Image
//{
//public:
//   Image();
//};

#endif // IMAGE_H
