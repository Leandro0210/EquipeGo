#include "analytics/CourierCoverageGpu06.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace equipego::stencil06;

std::vector<PuntoNormalizado06> leer(const std::string& filename) {
  std::ifstream file(filename);
  if (!file) throw std::runtime_error("STENCIL06_GPU: no hay dataset");
  std::string line;
  if (!std::getline(file,line)) throw std::runtime_error("STENCIL06_GPU: sin header");
  if (!line.empty() && line.back()=='\r') line.pop_back();
  if (line!="courier_id,node_id,x_norm,y_norm")
    throw std::runtime_error("STENCIL06_GPU: header inesperado");
  std::vector<PuntoNormalizado06> pts;
  while (std::getline(file,line)) {
    if (!line.empty() && line.back()=='\r') line.pop_back();
    if (line.empty()) continue;
    std::stringstream ss(line);
    std::string id,node,x,y,extra;
    if (!std::getline(ss,id,',') || !std::getline(ss,node,',') ||
        !std::getline(ss,x,',') || !std::getline(ss,y,',') ||
        std::getline(ss,extra,',') || id.empty() || node.empty())
      throw std::runtime_error("STENCIL06_GPU: CSV invalido");
    pts.push_back({std::stof(x),std::stof(y)});
  }
  if (pts.size()!=5000) throw std::runtime_error("STENCIL06_GPU: se esperan 5000 couriers");
  return pts;
}

int main(int argc, char** argv) {
  try {
    if (argc!=3) { std::cerr<<"Uso: bench_courier_coverage_gpu06 input.csv output.csv\n";return 2; }
    const auto points = leer(argv[1]);
    std::ofstream out(argv[2]);
    if (!out) throw std::runtime_error("STENCIL06_GPU: salida no disponible");
    out << "side,mode,rep,sweeps,kernel_ms,h2d_ms,d2h_ms,total_ms,max_error,checksum,equal,input_bytes,output_bytes\n";
    out << std::fixed << std::setprecision(9);
    std::size_t count=0;
    constexpr unsigned int sweeps=20;
    for (std::size_t side : {32u,64u,128u,256u,512u}) {
      const auto field=construirCampo(points,side);
      const auto cpu=evolucionar(field,sweeps,1);
      for (unsigned rep=0;rep<10;++rep) {
        // Invertir orden por repeticion, minimiza sesgo por calentamiento.
        for (unsigned turno=0;turno<2;++turno) {
          const bool tiled=(turno==(rep%2));
          const auto mode=tiled?VarianteGpu06::TiledShared:VarianteGpu06::BasicoGlobal;
          if (rep==0) (void)evolucionarGpu(field,2,mode); // warm-up sin medir
          const auto result=evolucionarGpu(field,sweeps,mode);
          double maxError=0;
          for (std::size_t i=0;i<cpu.size();++i) {
            maxError=std::max(maxError,static_cast<double>(std::fabs(cpu[i]-result.valores[i])));
          }
          if (maxError>1e-5 || !(result.totalMs>0) || !(result.kernelMs>0))
            throw std::runtime_error("STENCIL06_GPU: divergencia CUDA/CPU o cronometraje invalido");
          out<<side<<','<<(tiled?"tiled_shared":"basic_global")<<','<<rep<<','<<sweeps<<','
             <<result.kernelMs<<','<<result.h2dMs<<','<<result.d2hMs<<','
             <<result.totalMs<<','<<maxError<<','<<suma(result.valores)<<",OK,"
             <<field.valores.size()*sizeof(float)<<','<<field.valores.size()*sizeof(float)<<'\n';
          ++count;
        }
      }
      std::cout<<"STENCIL06_GPU_BENCH_OK,side="<<side<<",reps=10,modos=2\n";
    }
    out.flush();if (!out) throw std::runtime_error("STENCIL06_GPU: CSV no se guardo");
    std::cout<<"STENCIL06_GPU_BENCH_COMPLETO,filas="<<count<<",equivalencia=OK\n";
    return count==100?0:1;
  } catch (const std::exception& e) {
    std::cerr<<"STENCIL06_GPU_BENCH_ERROR: "<<e.what()<<'\n';return 1;
  }
}
