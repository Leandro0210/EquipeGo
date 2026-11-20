#include "analytics/CourierLayoutGpu04.hpp"
#include <cuda_runtime.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
namespace equipego::performance04 {
namespace {
constexpr unsigned int THREADS = 256;
void check(cudaError_t e, const char* call) {
  if(e != cudaSuccess) throw std::runtime_error(std::string(call)+": "+cudaGetErrorString(e));
}
struct Buffer {
  void* p = nullptr;
  Buffer() = default;
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  ~Buffer(){ if(p) cudaFree(p); }
  void release(){ if(p){ check(cudaFree(p),"cudaFree"); p=nullptr; } }
  void alloc(std::size_t bytes) { if(bytes) check(cudaMalloc(&p,bytes),"cudaMalloc"); }
  template<class T> T* as() const { return static_cast<T*>(p); }
  template<class T> void upload(const std::vector<T>& v){ if(!v.empty()) check(cudaMemcpy(p,v.data(),v.size()*sizeof(T),cudaMemcpyHostToDevice),"cudaMemcpy H2D"); }
};
struct Event {
  cudaEvent_t e = nullptr;
  Event(){ check(cudaEventCreate(&e),"cudaEventCreate"); }
  ~Event(){ if(e) cudaEventDestroy(e); }
  void release(){ if(e){ check(cudaEventDestroy(e),"cudaEventDestroy"); e=nullptr; } }
};
struct DeviceSoA {
  const float* x; const float* y; const float* speed;
  const std::int32_t* reserved; const std::int32_t* capacity; const std::int32_t* state;
};
__device__ __forceinline__ float approximate(float x, float y, float speed,
  float rx, float ry, float tramo) {
  float dx=x-rx, dy=y-ry;
  return (sqrtf(dx*dx+dy*dy)+tramo)/speed;
}
__global__ void aosKernel(const CourierAoS04* input, float* out, std::size_t n,
  float rx, float ry, float tramo, unsigned int sweeps) {
  std::size_t i = static_cast<std::size_t>(blockIdx.x)*blockDim.x+threadIdx.x;
  if(i>=n) return;
  CourierAoS04 c=input[i];
  if(!(c.state==0 && c.capacity>0 && c.reserved>=0 && c.reserved<c.capacity &&
       isfinite(c.xMeters) && isfinite(c.yMeters) &&
       isfinite(c.speedMetersSecond) && c.speedMetersSecond>0.f)) {
    out[i]=-1.f; return;
  }
  float sum=0.f;
  for(unsigned int s=0;s<sweeps;++s) {
    float shift=rx+static_cast<float>(s)*0.03125f;
    float t=approximate(c.xMeters,c.yMeters,c.speedMetersSecond,shift,ry,tramo);
    sum = s==0 ? t : sum+t;
  }
  out[i]=sum;
}
__global__ void soaKernel(DeviceSoA input, float* out, std::size_t n,
  float rx, float ry, float tramo, unsigned int sweeps) {
  std::size_t i=static_cast<std::size_t>(blockIdx.x)*blockDim.x+threadIdx.x;
  if(i>=n) return;
  const float x=input.x[i], y=input.y[i], speed=input.speed[i];
  if(!(input.state[i]==0 && input.capacity[i]>0 && input.reserved[i]>=0 &&
       input.reserved[i]<input.capacity[i] && isfinite(x) && isfinite(y) &&
       isfinite(speed) && speed>0.f)) {
    out[i]=-1.f; return;
  }
  float sum=0.f;
  for(unsigned int s=0;s<sweeps;++s) {
    float shift=rx+static_cast<float>(s)*0.03125f;
    float t=approximate(x,y,speed,shift,ry,tramo);
    sum = s==0 ? t : sum+t;
  }
  out[i]=sum;
}
using Clock=std::chrono::steady_clock;
double millis(Clock::time_point a, Clock::time_point b){
  return std::chrono::duration<double,std::milli>(b-a).count();
}
float kernelTime(cudaEvent_t a,cudaEvent_t b){
  check(cudaEventRecord(b),"cudaEventRecord end");
  check(cudaEventSynchronize(b),"cudaEventSynchronize");
  float ms=0; check(cudaEventElapsedTime(&ms,a,b),"cudaEventElapsedTime");
  return ms;
}
void checkN(std::size_t n,unsigned int sweeps){
  if(sweeps==0) throw std::invalid_argument("barridos=0");
  if(n>(std::size_t)std::numeric_limits<unsigned int>::max()*THREADS)
    throw std::invalid_argument("demasiados repartidores");
}
} // namespace
ResultadoGpu04 calcularAoSGpu04(const std::vector<CourierAoS04>& input,
    float rx,float ry,float tramo,unsigned int sweeps){
  checkN(input.size(),sweeps);
  ResultadoGpu04 result; result.values.resize(input.size(),-1.f);
  if(input.empty()) return result;
  auto start=Clock::now();
  check(cudaSetDevice(0),"cudaSetDevice");
  Buffer data, output;
  data.alloc(input.size()*sizeof(CourierAoS04)); output.alloc(input.size()*sizeof(float));
  auto h0=Clock::now(); data.upload(input); auto h1=Clock::now();
  result.h2dMs=millis(h0,h1);
  Event e0,e1;
  check(cudaEventRecord(e0.e),"cudaEventRecord start");
  unsigned int blocks=static_cast<unsigned int>((input.size()+THREADS-1)/THREADS);
  aosKernel<<<blocks,THREADS>>>(data.as<CourierAoS04>(),output.as<float>(),input.size(),rx,ry,tramo,sweeps);
  check(cudaGetLastError(),"aosKernel launch");
  result.kernelMs=kernelTime(e0.e,e1.e);
  h0=Clock::now();
  check(cudaMemcpy(result.values.data(),output.p,input.size()*sizeof(float),cudaMemcpyDeviceToHost),"D2H");
  h1=Clock::now(); result.d2hMs=millis(h0,h1);
  e1.release(); e0.release(); data.release(); output.release();
  result.totalMs=millis(start,Clock::now());
  return result;
}
ResultadoGpu04 calcularSoAGpu04(const CourierSoA04& input,
    float rx,float ry,float tramo,unsigned int sweeps){
  validarSoA(input); checkN(input.size(),sweeps);
  ResultadoGpu04 result; result.values.resize(input.size(),-1.f);
  if(input.size()==0) return result;
  auto start=Clock::now(); check(cudaSetDevice(0),"cudaSetDevice");
  Buffer x,y,speed,reserved,capacity,state,output;
  std::size_t n=input.size();
  x.alloc(n*sizeof(float)); y.alloc(n*sizeof(float)); speed.alloc(n*sizeof(float));
  reserved.alloc(n*sizeof(std::int32_t)); capacity.alloc(n*sizeof(std::int32_t));
  state.alloc(n*sizeof(std::int32_t)); output.alloc(n*sizeof(float));
  auto h0=Clock::now();
  x.upload(input.xMeters); y.upload(input.yMeters); speed.upload(input.speedMetersSecond);
  reserved.upload(input.reserved); capacity.upload(input.capacity); state.upload(input.state);
  auto h1=Clock::now(); result.h2dMs=millis(h0,h1);
  DeviceSoA device{x.as<float>(),y.as<float>(),speed.as<float>(),
      reserved.as<std::int32_t>(),capacity.as<std::int32_t>(),state.as<std::int32_t>()};
  Event e0,e1;
  check(cudaEventRecord(e0.e),"cudaEventRecord start");
  unsigned int blocks=static_cast<unsigned int>((n+THREADS-1)/THREADS);
  soaKernel<<<blocks,THREADS>>>(device,output.as<float>(),n,rx,ry,tramo,sweeps);
  check(cudaGetLastError(),"soaKernel launch");
  result.kernelMs=kernelTime(e0.e,e1.e);
  h0=Clock::now();
  check(cudaMemcpy(result.values.data(),output.p,n*sizeof(float),cudaMemcpyDeviceToHost),"D2H");
  h1=Clock::now(); result.d2hMs=millis(h0,h1);
  e1.release(); e0.release(); x.release(); y.release(); speed.release();
  reserved.release(); capacity.release(); state.release(); output.release();
  result.totalMs=millis(start,Clock::now());
  return result;
}
} // namespace equipego::performance04
