#include "waldo.hpp"
#include "wimmel.hpp"
#include "image.hpp"

namespace Waldo {

int16_t counter_waldo = 0;
int scale = 4;
uint32_t count = 3;

int16_t x_pos = 500;
int16_t y_pos = 1000;

bool initialized = false;

auto IMAGE_SIZE = sizeof(wimmel_pixel_map);

void updateWaldo(TFT_eSprite img, int32_t x, int32_t y)
{
  initWaldo();

  y *= 7;
  x *= 3;
  // Invert and scale/smooth x & y values
  y /= -100;
  x /= -100;
  x_pos += x;
  y_pos += y;

  for(auto i = 0; i < 120; ++i){
      for(auto t = 0; t < 320; t+=1){
        auto pos = x_pos+t/scale+((i+y_pos)/scale)*1000;
        if (pos >= IMAGE_SIZE){
          pos = 0;
        }
        if(pos < 0){
          pos = 0;
        }
        uint8_t pixel = wimmel_pixel_map[pos];
        uint16_t color = Tools::get16from8(pixel);
        Image::bitmap[320*i+t] = color;
      }
  }
  img.pushImage(0, 0, 320, 120,Image::bitmap);
  for(auto i = 0; i < 120; ++i){
      for(auto t = 0; t < 320; t+=1){
        auto pos = x_pos+t/scale+((i+y_pos+120)/scale)*1000;
        if (pos >= IMAGE_SIZE){
          pos = 0;
        }
        if(pos < 0){
          pos = 0;
        }
        uint8_t pixel = wimmel_pixel_map[pos];
        Image::bitmap[320*i+t] = Tools::get16from8(pixel);
        }
  }
  img.pushImage(0, 120, 320, 120,Image::bitmap);

  count+=2;
}

void initWaldo()
{
  if(initialized){return;}
  initialized = true;
}

}

