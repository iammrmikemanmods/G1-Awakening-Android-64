#include "g1/UIBitmap.hpp"
#include <cassert>
using namespace g1;
void u16(Bytes&b,int p,unsigned v){for(int j=0;j<2;++j)b[p+j]=v>>(8*j);}
void u32(Bytes&b,int p,unsigned v){for(int j=0;j<4;++j)b[p+j]=v>>(8*j);}
template<class F>bool rejected(F f){try{f();return false;}catch(const std::exception&){return true;}}
Bytes bmp(bool top=false,unsigned padding=0){Bytes b(54+16+padding);b[0]='B';b[1]='M';u32(b,2,b.size());u32(b,10,54);u32(b,14,40);u32(b,18,2);u32(b,22,top?unsigned(-2):2);u16(b,26,1);u16(b,28,24);u32(b,34,16+padding);
 // bottom row: green, blue; top row: red, transparent magenta.
 const unsigned char rows[16]={0,255,0,255,0,0,0,0, 0,0,255,255,0,255,0,0};
 for(int y=0;y<2;++y)for(int x=0;x<8;++x)b[54+y*8+x]=rows[(top?1-y:y)*8+x];return b;}
int main(){
 for(bool top:{false,true})for(unsigned pad=0;pad<4;++pad){const auto source=bmp(top,pad),copy=source;auto im=decode_ui_bmp(source);
  assert(im.width==2&&im.height==2&&im.pixels==Bytes({255,0,0,255,255,0,255,0,0,255,0,255,0,0,255,255}));assert(source==copy);}
 auto b=bmp(false,1);b.back()=1;assert(rejected([&]{decode_ui_bmp(b);}));
 b=bmp();b.pop_back();assert(rejected([&]{decode_ui_bmp(b);}));
 b=bmp();u16(b,28,16);assert(rejected([&]{decode_ui_bmp(b);}));
 b=bmp();u32(b,18,9000);assert(rejected([&]{decode_ui_bmp(b);}));
 b=bmp();u16(b,28,8);u32(b,10,0);assert(rejected([&]{decode_ui_bmp(b);}));
 for(unsigned size=0;size<54;++size)assert(rejected([&]{decode_ui_bmp(Bytes(size));}));
}
