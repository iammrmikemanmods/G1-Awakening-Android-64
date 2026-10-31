#include "g1/TacticalModel.hpp"
#include "g1/Text.hpp"
#include "g1/World.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
namespace fs=std::filesystem;
static void bytes(const fs::path& p,const void* d,std::size_t n){std::ofstream f(p,std::ios::binary);if(!f.write((const char*)d,n))throw std::runtime_error("motion probe write failed");}
template<class C>void array(std::ostream& s,const C& a){s<<'[';bool first=true;for(auto x:a){if(!first)s<<',';s<<x;first=false;}s<<']';}
int main(int argc,char**argv){try{
 if(argc<2||argc>3)throw std::runtime_error("usage: tactical_motion_probe iphone.big [private_output]");
 g1::Archive archive(g1::read_file(argv[1]));const auto world=g1::bootstrap_first_campaign(archive);const auto text=g1::load_text_table(archive);
 std::size_t models=0,bipeds=0,transforms=0,frames=0;float endpoint_error=0;
 constexpr float u=.81915204429F,v=.57357643635F;
 g1::M3GMatrix camera{{2,0,0,0,0,2*u,-v/100,0,0,-2*v,-u/100,0,0,-.20F,.5F,1}};
 for(std::uint8_t type=0;type<23;++type){
  g1::TacticalModel model(archive,type);++models;bipeds+=model.walking_supported(0);transforms+=model.supports(1);
  const auto& asset=model.asset();const bool export_type=argc==3&&(type==0||type==11||type==19);
  fs::path dir;if(export_type){dir=fs::path(argv[2])/std::to_string(type);fs::create_directories(dir);
   for(const auto& [id,img]:asset.geometry.images)bytes(dir/("image_"+std::to_string(id)+".rgba"),img.pixels.data(),img.pixels.size());
   for(std::size_t i=0;i<asset.geometry.surfaces.size();++i){const auto& s=asset.geometry.surfaces[i];bytes(dir/("surface_"+std::to_string(i)+".f32"),s.vertices.data(),s.vertices.size()*sizeof(g1::M3GVertex));}
  }
  for(int mode=0;mode<2;++mode){if(mode==1&&!model.supports(1))continue;
   for(std::uint8_t facing=0;facing<4;++facing)for(int step=0;step<=24;++step){
    g1::TacticalMotion motion;motion.walking=mode==0;motion.transforming=mode==1;
    motion.stride_phase=step/24.0F;motion.vehicle_fraction=step/24.0F;
    const auto frame=model.prepare(asset,0,facing,{{0,0,0}},camera,{{480,480}},motion);++frames;
    if(frame.draws.empty())throw std::runtime_error("empty motion frame");
    for(const auto& d:frame.draws){for(auto x:d.metal_matrix)if(!std::isfinite(x))throw std::runtime_error("Nonfinite motion matrix");
     if(mode==1&&((step==0&&d.assembly_role==1)||(step==24&&d.assembly_role==2))){
      const auto endpoint=model.prepare(asset,step==24?1:0,facing,{{0,0,0}},camera,{{480,480}});
      const auto found=std::find_if(endpoint.draws.begin(),endpoint.draws.end(),[&](const auto& x){return x.geometry_index==d.geometry_index;});
      if(found==endpoint.draws.end())throw std::runtime_error("endpoint surface identity changed");
      for(int j=0;j<16;++j)endpoint_error=std::max(endpoint_error,std::abs(found->metal_matrix[j]-d.metal_matrix[j]));
     }
    }
    if(export_type&&facing==1){
     std::ofstream out(dir/((mode?"transform_":"walk_")+std::to_string(step)+".json"));out<<std::setprecision(9);
     out<<"{\"type\":"<<int(type)<<",\"phase\":"<<step/24.0<<",\"width\":480,\"height\":480,\"cpu_reference\":true,\"images\":[";
     bool first=true;for(const auto& [id,img]:asset.geometry.images){if(!first)out<<',';first=false;out<<"{\"id\":"<<id<<",\"width\":"<<img.width<<",\"height\":"<<img.height<<'}';}
     out<<"],\"draws\":[";first=true;
     for(const auto& d:frame.draws){const auto& s=asset.geometry.surfaces.at(d.geometry_index);if(!first)out<<',';first=false;
      out<<"{\"geometry\":"<<d.geometry_index<<",\"matrix\":";array(out,d.metal_matrix);
      out<<",\"cull\":"<<int(s.appearance.culling)<<",\"winding\":"<<int(s.appearance.winding)<<",\"alpha\":"<<d.effective_alpha
         <<",\"role\":"<<int(d.assembly_role)<<",\"mix\":"<<d.assembly_mix<<",\"highlight\":"<<d.assembly_highlight<<",\"texture\":";
      if(s.appearance.texture){const auto&t=*s.appearance.texture;out<<"{\"image\":"<<t.image_object<<",\"function\":"<<int(t.function)<<",\"wrap_s\":"<<int(t.wrap_s)<<",\"wrap_t\":"<<int(t.wrap_t)<<",\"transform\":";array(out,t.transform);out<<'}';}else out<<"null";
      out<<'}';
     }out<<"]}";
    }
   }
  }
 }
 if(endpoint_error>0.00001F)throw std::runtime_error("Authored transition endpoint differs from original pose: "+std::to_string(endpoint_error));
 std::cout<<"{\"result\":\"passed\",\"models\":"<<models<<",\"biped_rigs\":"<<bipeds<<",\"paired_forms\":"<<transforms<<",\"frames\":"<<frames<<",\"max_endpoint_matrix_error\":"<<endpoint_error<<",\"gpu_execution\":false,\"authored_motion\":true}\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
