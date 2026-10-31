#pragma once
#include "g1/Scene.hpp"
#include <cmath>
namespace g1 {
inline RgbaImage soft_contact_shadow(){
 RgbaImage image;image.width=image.height=64;image.pixels.resize(64*64*4);
 for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){
  const float u=(float(x)+0.5F)/32-1,v=(float(y)+0.5F)/32-1;
  const float radius=u*u+v*v;
  const auto a=static_cast<std::uint8_t>(std::round(std::max(0.0F,std::exp(-4*radius)-std::exp(-4.0F))*75));
  const auto i=(y*64+x)*4;image.pixels[i]=image.pixels[i+1]=image.pixels[i+2]=0;image.pixels[i+3]=a;
 }
 return image;
}
inline std::vector<SceneVertex> contact_shadow_vertices(){
 const SceneVertex corners[]={{{-.14F,0,-.11F,1},{0,0},{}},{{.14F,0,-.11F,1},{1,0},{}},
                              {{-.14F,0,.11F,1},{0,1},{}},{{.14F,0,.11F,1},{1,1},{}}};
 return {corners[0],corners[1],corners[2],corners[2],corners[1],corners[3]};
}
}
