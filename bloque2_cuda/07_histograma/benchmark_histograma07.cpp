// EquipeGo - Bloque 2, feature 07: benchmark reproducible del histograma.
// CPU:  g++ -O3 -std=c++17 -pthread -Isrc benchmark_histograma07.cpp -o bench07_cpu
// CUDA: nvcc -O3 -std=c++17 -arch=sm_75 -Xcompiler=-pthread -DEQUIPEGO_BENCH_CUDA
//       -Isrc benchmark_histograma07.cpp DeliveryHistogramGpu.cpp DeliveryHistogramCuda.cu -o bench07_gpu
// El benchmark usa datos sinteticos deterministas a escala, NO inventa entregas reales.
#include "analytics/DeliveryHistogram.hpp"
#ifdef EQUIPEGO_BENCH_CUDA
#include "analytics/DeliveryHistogramGpu.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <memory>
#include <iterator>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using equipego::analytics::HistogramaEntregas;
using Clock = std::chrono::steady_clock;
constexpr unsigned kSeed = 2026;
constexpr std::size_t kSizes[] = {10000, 100000, 1000000, 5000000, 10000000};

struct Args {
  std::string out;
  std::string machine;
  std::size_t maxThreads = std::max(1u, std::thread::hardware_concurrency());
  int reps = 10;
  bool gpu = false;
  bool quick = false;
};

std::size_t positiveSize(const std::string& s) {
  const auto v = std::stoull(s);
  if (v == 0 || v > 256) throw std::invalid_argument("El limite de hilos debe estar entre 1 y 256");
  return static_cast<std::size_t>(v);
}

Args parse(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    std::string flag = argv[i];
    auto next = [&]() -> std::string {
      if (++i >= argc) throw std::invalid_argument("Falta valor para " + flag);
      return argv[i];
    };
    if (flag == "--out") a.out = next();
    else if (flag == "--machine") a.machine = next();
    else if (flag == "--max-threads") a.maxThreads = positiveSize(next());
    else if (flag == "--reps") a.reps = std::stoi(next());
    else if (flag == "--gpu") a.gpu = true;
    else if (flag == "--quick") a.quick = true;
    else throw std::invalid_argument("Opcion desconocida: " + flag);
  }
  if (a.out.empty() || a.machine.empty()) {
    throw std::invalid_argument("Uso: --out archivo.csv --machine laptop|colab [--max-threads N] [--reps 10] [--gpu]");
  }
  if (a.machine != "laptop" && a.machine != "colab") {
    throw std::invalid_argument("--machine debe ser laptop o colab");
  }
  if (!a.quick && a.reps < 10) throw std::invalid_argument("La rubrica pide 10+ repeticiones; use --reps 10");
  if (a.reps < 1 || a.reps > 200) throw std::invalid_argument("Repeticiones fuera de rango");
#ifndef EQUIPEGO_BENCH_CUDA
  if (a.gpu) throw std::invalid_argument("Este ejecutable no se compilo con CUDA");
#endif
  return a;
}

std::vector<long long> makeInput(std::size_t n) {
  std::mt19937_64 random(kSeed);
  std::uniform_int_distribution<int> minutes(0, 90);
  std::uniform_int_distribution<int> millisecond(0, 59999);
  std::vector<long long> result(n);
  for (std::size_t i = 0; i < n; ++i) {
    result[i] = static_cast<long long>(minutes(random)) * 60000LL + millisecond(random);
  }
  return result;
}

double ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

struct Work {
  enum class Kind { Sequential, Multithread, GpuGlobal, GpuShared } kind;
  std::size_t threads = 0;
};

std::vector<std::size_t> threadSweep(std::size_t maximum) {
  std::vector<std::size_t> result;
  std::size_t t = 1;
  while (t <= maximum) {
    result.push_back(t);
    if (t > maximum / 2) break;
    t *= 2;
  }
  if (result.back() != maximum) result.push_back(maximum);
  return result;
}

void csvLine(std::ostream& out, const Args& args, const std::string& variant,
             std::size_t n, std::size_t threads, int rep,
             double timeMs, double kernelMs, double totalMs,
             double requestMs, bool ok) {
  out << args.machine << ',' << variant << ',' << n << ',' << threads << ','
      << rep << ',' << std::fixed << std::setprecision(9)
      << timeMs << ',' << kernelMs << ',' << totalMs << ',' << requestMs
      << ',' << (ok ? 1 : 0) << ',' << kSeed << '\n';
}
}

int main(int argc, char** argv) {
  try {
    const auto args = parse(argc, argv);
    std::ofstream out(args.out, std::ios::out | std::ios::trunc);
    if (!out) throw std::runtime_error("No puedo crear CSV: " + args.out);
    out << "machine,variant,size,threads,repetition,time_ms,kernel_ms,gpu_total_ms,request_ms,correct,seed\n";

#ifdef EQUIPEGO_BENCH_CUDA
    std::unique_ptr<equipego::analytics::PropietarioGpuHistograma> propietario;
    if (args.gpu) propietario = std::make_unique<equipego::analytics::PropietarioGpuHistograma>();
#endif

    const auto threads = threadSweep(args.maxThreads);
    const std::vector<std::size_t> sizes = args.quick
        ? std::vector<std::size_t>{1000, 10000}
        : std::vector<std::size_t>(std::begin(kSizes), std::end(kSizes));

    for (const auto n : sizes) {
      std::cout << "Procesando N=" << n << " ..." << std::endl;
      const auto data = makeInput(n);
      const HistogramaEntregas expected = equipego::analytics::histogramaEntregasCPU(data);
      if (std::accumulate(expected.begin(), expected.end(), std::uint64_t{0}) != n) {
        throw std::runtime_error("La suma de bins no coincide con N");
      }

      std::vector<Work> works{{Work::Kind::Sequential, 1}};
      for (const auto t : threads) works.push_back({Work::Kind::Multithread, t});
      if (args.gpu) {
        works.push_back({Work::Kind::GpuGlobal, 256});
        works.push_back({Work::Kind::GpuShared, 256});
      }

      // Calentamiento fuera de las mediciones: incluye la primera inicializacion CUDA.
      for (const auto& w : works) {
        HistogramaEntregas actual{};
        if (w.kind == Work::Kind::Sequential) {
          actual = equipego::analytics::histogramaEntregasCPU(data);
        } else if (w.kind == Work::Kind::Multithread) {
          actual = equipego::analytics::histogramaEntregasCPUParalelo(data, w.threads);
#ifdef EQUIPEGO_BENCH_CUDA
        } else if (w.kind == Work::Kind::GpuGlobal || w.kind == Work::Kind::GpuShared) {
          const auto variante = w.kind == Work::Kind::GpuGlobal
              ? equipego::analytics::VarianteHistogramaGpu::AtomicosGlobales
              : equipego::analytics::VarianteHistogramaGpu::CompartidaPrivada;
          actual = propietario->enviar(data, variante).get().bins;
#endif
        }
        if (actual != expected) throw std::runtime_error("Fallo de correctitud en calentamiento");
      }

      for (int rep = 1; rep <= args.reps; ++rep) {
        std::vector<Work> shuffled = works;
        std::mt19937 rng(kSeed + static_cast<unsigned>(n) + rep);
        std::shuffle(shuffled.begin(), shuffled.end(), rng);
        for (const auto& w : shuffled) {
          HistogramaEntregas actual{};
          double elapsedMs = 0.0, kernelMs = 0.0, totalMs = 0.0, requestMs = 0.0;
          std::string variant;
          if (w.kind == Work::Kind::Sequential) {
            variant = "cpu_1thread";
            const auto start = Clock::now();
            actual = equipego::analytics::histogramaEntregasCPU(data);
            elapsedMs = ms(Clock::now() - start);
          } else if (w.kind == Work::Kind::Multithread) {
            variant = "cpu_multithread";
            const auto start = Clock::now();
            actual = equipego::analytics::histogramaEntregasCPUParalelo(data, w.threads);
            elapsedMs = ms(Clock::now() - start);
#ifdef EQUIPEGO_BENCH_CUDA
          } else {
            const auto gpuVariant = w.kind == Work::Kind::GpuGlobal
                ? equipego::analytics::VarianteHistogramaGpu::AtomicosGlobales
                : equipego::analytics::VarianteHistogramaGpu::CompartidaPrivada;
            variant = w.kind == Work::Kind::GpuGlobal ? "gpu_global" : "gpu_shared";
            const auto start = Clock::now();
            const auto result = propietario->enviar(data, gpuVariant).get();
            requestMs = ms(Clock::now() - start);
            actual = result.bins;
            kernelMs = result.kernelMs;
            totalMs = result.totalMs;
            elapsedMs = totalMs;
#endif
          }
          const bool correct = actual == expected;
          csvLine(out, args, variant, n, w.threads, rep,
                  elapsedMs, kernelMs, totalMs, requestMs, correct);
          if (!correct) throw std::runtime_error("CPU y GPU no coinciden; consultar CSV");
        }
        out.flush();
      }
      std::cout << "N=" << n << " VALIDACION OK (" << args.reps << " repeticiones)" << std::endl;
    }
    std::cout << "CSV generado: " << args.out << std::endl;
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR benchmark07: " << ex.what() << std::endl;
    return 1;
  }
}
