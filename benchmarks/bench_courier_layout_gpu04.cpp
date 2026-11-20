#include "analytics/CourierLayoutGpu04.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace equipego::performance04;
std::vector<CourierAoS04> load(const std::string& path) {
  std::ifstream file(path);
  if(!file) throw std::runtime_error("input CSV not found");
  std::string line;
  if(!std::getline(file,line) || line!="id,xMeters,yMeters,speedMetersSecond,reserved,capacity,state")
    throw std::runtime_error("CSV header mismatch");
  std::vector<CourierAoS04> result;
  while(std::getline(file,line)){
    if(line.empty())continue;
    std::istringstream iss(line); std::vector<std::string> c;std::string v;
    while(std::getline(iss,v,','))c.push_back(v);
    if(c.size()!=7) throw std::runtime_error("invalid row");
    result.push_back({std::stof(c[1]),std::stof(c[2]),std::stof(c[3]),
      std::stoi(c[4]),std::stoi(c[5]),std::stoi(c[6])});
  }
  if(result.size()!=5000) throw std::runtime_error("expected exactly 5000 records");
  return result;
}
void equivalent(const std::vector<float>& ref,const std::vector<float>& candidate){
  if(ref.size()!=candidate.size())throw std::runtime_error("size mismatch");
  for(std::size_t i=0;i<ref.size();++i){
    if(ref[i]==-1.f || candidate[i]==-1.f){
      if(ref[i]!=candidate[i])throw std::runtime_error("invalid status mismatch");
      continue;
    }
    if(!std::isfinite(candidate[i]) ||
      std::fabs(ref[i]-candidate[i])>std::max(0.02f,std::fabs(ref[i])*0.00005f))
      throw std::runtime_error("GPU vs CPU difference at index "+std::to_string(i));
  }
}
int main(int argc,char** argv){try{
  if(argc!=3){std::cerr<<"usage: bench_courier_layout_gpu04 input.csv output.csv\n";return 2;}
  auto all=load(argv[1]);
  std::ofstream out(argv[2]);if(!out)throw std::runtime_error("output CSV unavailable");
  out<<"n,layout,rep,sweeps,kernel_ms,h2d_ms,d2h_ms,total_ms,per_sweep_kernel_ms,per_sweep_total_ms,valid_count,checksum,equivalencia\n";
  out<<std::fixed<<std::setprecision(8);
  std::size_t rows=0;
  for(std::size_t n:{64u,256u,512u,2000u,5000u}){
    auto sweeps=static_cast<unsigned int>(500000/n);
    std::vector<CourierAoS04> aos(all.begin(),all.begin()+n);
    auto soa=convertirSoA(aos);
    // One CPU oracle per size; excludes reference and layout conversion from GPU timers.
    auto ref=calcularAoS(aos,2100.f,-1600.f,870.f,1,sweeps);
    (void)calcularAoSGpu04(aos,2100.f,-1600.f,870.f,3);
    (void)calcularSoAGpu04(soa,2100.f,-1600.f,870.f,3);
    for(int rep=0;rep<10;++rep){
      for(int turn=0;turn<2;++turn){
        bool isAoS=(turn==(rep%2));
        auto r=isAoS?calcularAoSGpu04(aos,2100.f,-1600.f,870.f,sweeps):
          calcularSoAGpu04(soa,2100.f,-1600.f,870.f,sweeps);
        equivalent(ref,r.values);
        std::size_t valid=0;double checksum=0;
        for(float value:r.values){if(value>=0){++valid;checksum+=value;}}
        out<<n<<','<<(isAoS?"AoS":"SoA")<<','<<rep<<','<<sweeps<<','
           <<r.kernelMs<<','<<r.h2dMs<<','<<r.d2hMs<<','<<r.totalMs<<','
           <<r.kernelMs/sweeps<<','<<r.totalMs/sweeps<<','<<valid<<','<<checksum<<",OK\n";
        ++rows;
      }
    }
    std::cout<<"LAYOUT04_GPU_BENCH_OK,n="<<n<<",reps=10\n";
  }
  if(rows!=100)throw std::runtime_error("unexpected rows");
  std::cout<<"LAYOUT04_GPU_BENCH_COMPLETO,filas=100,equivalencia=OK\n";
  return 0;
}catch(const std::exception& e){std::cerr<<"LAYOUT04_GPU_BENCH_ERROR: "<<e.what()<<'\n';return 1;}}
