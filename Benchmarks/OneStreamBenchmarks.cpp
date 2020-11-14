#include "../Source/OneStream.hpp"
#include "ITTNotifySupport.hpp"
#include <benchmark/benchmark.h>
#include <iostream>
using namespace std;

static auto vtune = VTuneAPIInterface{"OneStream"};
static void DummyBenchmark(benchmark::State& state) {
  auto dummy = 7;
  vtune.startSampling("DummyBenchmark");
  for(auto _ : state) {
    for(auto i = 0u; i < state.range(0); i++) {
      dummy++;
    }
    benchmark::DoNotOptimize(dummy);
  }
  vtune.stopSampling();
}
BENCHMARK(DummyBenchmark)->Range(0, 1024);

BENCHMARK_MAIN();
