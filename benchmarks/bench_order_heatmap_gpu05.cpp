#include "analytics/OrderHeatmapGpu05.hpp"
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace equipego::convolution05;

static DemandGrid05 readGrid(const std::string& path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("cannot read input CSV");
  std::string s;
  std::getline(f, s);
  if (s != "512,512") throw std::runtime_error("expect 512,512 first row");
  DemandGrid05 grid{512,512,{}};
  grid.orders.reserve(512*512);
  for (int row = 0; row < 512; ++row) {
    if (!std::getline(f,s)) throw std::runtime_error("missing data row");
    std::stringstream ss(s); std::string cell;
    for (int c=0;c<512;++c) {
      if (!std::getline(ss,cell,','))throw std::runtime_error("missing cell");
      grid.orders.push_back(std::stoi(cell));
    }
    if (std::getline(ss,cell,',')) throw std::runtime_error("extra cell");
  }
  if (std::getline(f,s) && !s.empty()) throw std::runtime_error("extra rows");
  validate(grid);
  return grid;
}
static DemandGrid05 crop(const DemandGrid05& g, int side) {
  DemandGrid05 out{static_cast<std::size_t>(side),
                   static_cast<std::size_t>(side),
                   std::vector<std::int32_t>(side*side)};
  for (int y=0;y<side;++y)
    for (int x=0;x<side;++x)
      out.orders[y*side+x]=g.orders[((512-side)/2+y)*512+(512-side)/2+x];
  return out;
}

int main(int argc,char** argv) {
  try {
    if(argc!=3) {std::cerr<<"Uso: bench_order_heatmap_gpu05 dataset.csv resultado.csv\n"; return 2;}
    const auto source=readGrid(argv[1]);
    std::ofstream csv(argv[2]);
    if(!csv)throw std::runtime_error("cannot write result");
    csv<<"side,algorithm,rep,h2d_ms,kernel_ms,d2h_ms,total_ms,input_bytes,output_bytes,equivalencia,checksum\n";
    csv<<std::fixed<<std::setprecision(9);
    int rows=0;
    for(int side:{32,64,128,256,512}) {
      const auto grid=crop(source,side);
      const auto expected=gaussianReference05(grid);
      for(auto algo:{GpuAlgorithm05::BasicConstant,GpuAlgorithm05::TiledSharedConstant}) {
        const std::string tag=algo==GpuAlgorithm05::BasicConstant ? "basic_const" : "tiled_shared_const";
        (void)gaussianGpu05(grid,algo);  // GPU context + warming outside timed reps.
        for(int rep=0;rep<10;++rep) {
          const auto out=gaussianGpu05(grid,algo);
          if(out.values != expected)throw std::runtime_error("GPU/CPU non-identical at side="+std::to_string(side));
          const double sum=std::accumulate(out.values.begin(),out.values.end(),0.0);
          csv<<side<<','<<tag<<','<<rep<<','<<out.h2dMs<<','<<out.kernelMs<<','
             <<out.d2hMs<<','<<out.totalMs<<','<<out.inputBytes<<','<<out.outputBytes
             <<",OK,"<<sum<<'\n';
          ++rows;
        }
        std::cout<<"CONVOLUTION05_BENCH_GPU_OK,side="<<side<<",kernel="<<tag<<",reps=10\n";
      }
    }
    if(rows!=100)throw std::runtime_error("expected 100 GPU rows");
    std::cout<<"CONVOLUTION05_GPU_BENCH_COMPLETO,filas=100,comparacion=OK\n";
  } catch(const std::exception& ex) {
    std::cerr<<"CONVOLUTION05_GPU_BENCH_ERROR: "<<ex.what()<<'\n'; return 1;
  }
}
