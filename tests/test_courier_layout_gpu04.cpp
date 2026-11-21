#include "analytics/CourierLayoutGpu04.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace equipego::performance04;
void check(bool value,const char* message){ if(!value) throw std::runtime_error(message); }
void compare(const std::vector<float>& expected,const std::vector<float>& actual) {
  check(expected.size()==actual.size(),"output size mismatch");
  for(std::size_t i=0;i<expected.size();++i){
    if(expected[i]==-1.f || actual[i]==-1.f){
      check(expected[i]==actual[i],"invalid courier must remain -1"); continue;
    }
    float tol=std::max(0.02f, std::fabs(expected[i])*0.00005f);
    if(!std::isfinite(actual[i]) || std::fabs(expected[i]-actual[i])>tol){
      std::cerr << "MISMATCH index="<<i<<" expected="<<expected[i]
                <<" got="<<actual[i]<<" tolerance="<<tol<<'\n';
      throw std::runtime_error("GPU != CPU");
    }
  }
}
void run(std::size_t n,unsigned int sweeps){
  std::vector<CourierAoS04> input;
  for(std::size_t i=0;i<n;++i) {
    input.push_back(CourierAoS04{float(i%199)*11.5f,float(i%137)*-17.25f,
      2.f+float(i%5),std::int32_t(i%3),3,std::int32_t(i%11==0?2:0)});
  }
  if(n>1) input[1].speedMetersSecond=std::numeric_limits<float>::quiet_NaN();
  auto soa=convertirSoA(input);
  auto ref=calcularAoS(input,900.f,-1700.f,280.f,1,sweeps);
  auto aos=calcularAoSGpu04(input,900.f,-1700.f,280.f,sweeps);
  auto sos=calcularSoAGpu04(soa,900.f,-1700.f,280.f,sweeps);
  compare(ref,aos.values); compare(ref,sos.values);
  check(aos.totalMs>=aos.kernelMs && sos.totalMs>=sos.kernelMs,"bad gpu timing");
  std::cout<<"LAYOUT04_GPU_CASE_OK,n="<<n<<",sweeps="<<sweeps<<'\n';
}
int main(){try{
  for(std::size_t n:{0u,1u,2u,17u,30u,31u,64u,257u,512u,2000u,5000u})run(n,7);
  run(64,2048);
  try{ (void)calcularAoSGpu04({},0,0,0,0); throw std::runtime_error("did not reject zero sweeps"); }
  catch(const std::invalid_argument&){}
  std::cout<<"LAYOUT04_GPU_EQUIVALENCIA_OK\n";
  return 0;
}catch(const std::exception& e){std::cerr<<"LAYOUT04_GPU_TEST_ERROR:"<<e.what()<<'\n';return 1;}}
