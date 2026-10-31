#include "g1/TacticalModel.hpp"
#include "g1/MaterialEnhancement.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
namespace fs=std::filesystem;
void file(const fs::path& p,const void* data,std::size_t size){std::ofstream f(p,std::ios::binary);if(!f.write(static_cast<const char*>(data),size))throw std::runtime_error("probe write failed");}
template<class A>void arr(std::ostream& s,const A&a){s<<'[';bool first=true;for(auto v:a){if(!first)s<<',';s<<v;first=false;}s<<']';}
int main(int argc,char**argv){try{
 if(argc<2||argc>3)throw std::runtime_error("usage: tactical_model_probe iphone.big [private_output_directory]");
 g1::Archive archive(g1::read_file(argv[1]));fs::path out;if(argc==3){out=argv[2];fs::create_directories(out);}
 std::size_t models=0,forms=0,missing=0,draws=0;std::cout<<"{\"gpu_execution\":false,\"animation\":\"original battle-ready sample, not walk/transform\",\"roster\":[";
 for(std::uint8_t type=0;type<23;++type){
  g1::TacticalModel model(archive,type);++models;if(type)std::cout<<',';
  std::cout<<"{\"type\":"<<int(type)<<",\"forms\":[";
  for(std::uint8_t form=0;form<2;++form){if(form)std::cout<<',';if(!model.supports(form)){++missing;std::cout<<"false";continue;}++forms;std::cout<<"true";
   const auto& asset=model.asset();
   for(std::uint8_t facing=0;facing<4;++facing){
    constexpr float u=0.81915204429F,v=0.57357643635F;
    g1::M3GMatrix camera{{2,0,0,0, 0,2*u,-v/100,0, 0,-2*v,-u/100,0, 0,-0.20F,0.5F,1}};
    auto frame=model.prepare(asset,form,facing,{{0,0,0}},camera,{{480,480}});
    if(frame.draws.empty())throw std::runtime_error("model form produced no draw");draws+=frame.draws.size();
    if(!out.empty()){
     fs::path dir=out/("type_"+std::to_string(type)+"_form_"+std::to_string(form)+"_facing_"+std::to_string(facing));fs::create_directories(dir);
     std::ostringstream json;json<<std::setprecision(9)<<"{\"type\":"<<int(type)<<",\"form\":"<<int(form)<<",\"facing\":"<<int(facing)<<",\"cpu_reference\":true,\"width\":480,\"height\":480,\"images\":[";
     bool first=true;for(const auto& [id,img]:asset.geometry.images){if(!first)json<<',';first=false;json<<"{\"id\":"<<id<<",\"width\":"<<img.width<<",\"height\":"<<img.height<<'}';file(dir/("image_"+std::to_string(id)+".rgba"),img.pixels.data(),img.pixels.size());}
     json<<"],\"draws\":[";first=true;
     for(std::size_t i=0;i<frame.draws.size();++i){const auto& d=frame.draws[i];const auto& s=asset.geometry.surfaces.at(d.geometry_index);if(!first)json<<',';first=false;
      json<<"{\"vertices\":"<<s.vertices.size()<<",\"matrix\":";arr(json,d.metal_matrix);
      json<<",\"cull\":"<<int(s.appearance.culling)<<",\"winding\":"<<int(s.appearance.winding)<<",\"alpha\":"<<d.effective_alpha<<",\"texture\":";
      if(s.appearance.texture){const auto&t=*s.appearance.texture;json<<"{\"image\":"<<t.image_object<<",\"function\":"<<int(t.function)<<",\"wrap_s\":"<<int(t.wrap_s)<<",\"wrap_t\":"<<int(t.wrap_t)<<",\"transform\":";arr(json,t.transform);json<<'}';}else json<<"null";
      json<<'}';file(dir/("draw_"+std::to_string(i)+".f32"),s.vertices.data(),s.vertices.size()*sizeof(g1::M3GVertex));
     }json<<"]}";const auto text=json.str();file(dir/"scene.json",text.data(),text.size());
    }
   }
  }std::cout<<"]}";
 }
 std::cout<<"],\"models\":"<<models<<",\"supported_forms\":"<<forms<<",\"unsupported_forms\":"<<missing<<",\"prepared_draws\":"<<draws<<",\"result\":\"passed\"}\n";
 return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
