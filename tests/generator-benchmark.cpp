// Benchmark del generador de sudokus.
//
// Fase 1: calibracion. Genera muchos puzzles (sin objetivo de dificultad) y mide
//         la distribucion real de nodos del solver para elegir umbrales realistas.
// Fase 2: benchmark. Mide generate() con umbrales sacados de la calibracion
//         (easy = P50, hard = P90), con semilla fija para poder comparar cambios.
//
// Uso: bench_generator [easyNodes hardNodes]
//      Si se pasan argumentos, se saltan los umbrales calibrados.
//
// Requiere que Generator exponga:  uint64_t totalNodes;  (ver parche)

#include <string>
#include <utility>

#include "../src/Solver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
template <class T>
inline void escape(T *)
{
    _ReadWriteBarrier();
}
#else
template <class T>
inline void escape(T *p)
{
    asm volatile("" : : "g"(p) : "memory");
}  
#endif

constexpr std::uint32_t kSeed = 12345;
constexpr int kCalibrationPuzzles = 2000;
constexpr int kWarmup = 2;
constexpr int kReps = 20;
constexpr int kPuzzlesPerSample = 10;

struct Result
{
    double msPerPuzzle; // tiempo medio por puzzle en esta muestra
    int ok;             // puzzles que alcanzaron la dificultad objetivo
    int inner;          // puzzles generados en la muestra
    uint64_t nodes;     // nodos totales visitados (dig + solve, todos los intentos)
    uint64_t checksum;
};

struct Statistics
{
    double min, median, max, mean, p90, stddev;
};

static Statistics calculateStatistics(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    double median = (n % 2 == 0) ? (v[n / 2 - 1] + v[n / 2]) / 2.0 : v[n / 2];

    double sum = 0.0;
    for (double x : v)
        sum += x;
    const double mean = sum / n;

    double sq = 0.0;
    for (double x : v)
        sq += (x - mean) * (x - mean);
    const double stddev = n > 1 ? std::sqrt(sq / (n - 1)) : 0.0;

    const size_t p90i = static_cast<size_t>(std::ceil(0.90 * n)) - 1;
    return {v.front(), median, v.back(), mean, v[p90i], stddev};
}

static uint64_t percentile(std::vector<uint64_t> &sorted, double p)
{
    size_t idx = static_cast<size_t>(std::ceil(p * sorted.size()));
    if (idx > 0)
        --idx;
    return sorted[std::min(idx, sorted.size() - 1)];
}

// ---------------------------------------------------------------------------
// Fase 1: calibracion
// ---------------------------------------------------------------------------
struct Calibration
{
    uint64_t p50, p90, p99, max;
};

static Calibration calibrate()
{
    std::mt19937 gen(kSeed);
    const std::string emptyPuzzle(81, '0');
    std::vector<uint64_t> nodes;
    nodes.reserve(kCalibrationPuzzles);

    for (int i = 0; i < kCalibrationPuzzles; ++i)
    {
        Board board(emptyPuzzle);
        Generator g;
        // targetNodes = 0 -> acepta el primer puzzle valido (1 solo intento)
        g.generate(board, gen, 0);
        nodes.push_back(g.lastPuzzleNodes);
    }

    std::sort(nodes.begin(), nodes.end());
    Calibration c{percentile(nodes, 0.50), percentile(nodes, 0.90),
                  percentile(nodes, 0.99), nodes.back()};

    std::printf("Calibracion (%d puzzles, nodos del solver por puzzle)\n", kCalibrationPuzzles);
    std::printf("   min %llu  P50 %llu  P90 %llu  P99 %llu  max %llu\n\n",
                (unsigned long long)nodes.front(), (unsigned long long)c.p50,
                (unsigned long long)c.p90, (unsigned long long)c.p99,
                (unsigned long long)c.max);
    return c;
}

// ---------------------------------------------------------------------------
// Fase 2: benchmark
// ---------------------------------------------------------------------------
static Result runOnce(int targetNodes, int inner, std::mt19937 &gen)
{
    double totalMs = 0.0;
    int ok = 0;
    uint64_t nodes = 0, checksum = 0;
    const std::string emptyPuzzle(81, '0');

    for (int i = 0; i < inner; ++i)
    {
        Board board(emptyPuzzle);
        Generator g;
        escape(&board);

        const auto t0 = std::chrono::steady_clock::now();
        const bool success = g.generate(board, gen, targetNodes);
        const auto t1 = std::chrono::steady_clock::now();

        escape(&board);

        totalMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        ok += success ? 1 : 0;
        nodes += g.counter;
        for (auto c : board.cells)
            checksum += c;
        checksum += g.lastPuzzleNodes;
    }

    return {totalMs / inner, ok, inner, nodes, checksum};
}

struct Case
{
    const char *name;
    int targetNodes;
};

static void runCase(const Case &c)
{
    // Semilla fija por caso: mismas secuencias de puzzles entre ejecuciones
    std::mt19937 gen(kSeed);

    for (int i = 0; i < kWarmup; ++i)
        runOnce(c.targetNodes, kPuzzlesPerSample, gen);

    std::vector<double> msPerPuzzle, nsPerNode;
    msPerPuzzle.reserve(kReps);
    nsPerNode.reserve(kReps);

    int okTotal = 0, puzzlesTotal = 0;
    uint64_t nodesTotal = 0, checksum = 0;

    for (int i = 0; i < kReps; ++i)
    {
        const Result r = runOnce(c.targetNodes, kPuzzlesPerSample, gen);
        msPerPuzzle.push_back(r.msPerPuzzle);
        if (r.nodes)
            nsPerNode.push_back((r.msPerPuzzle * r.inner * 1e6) / r.nodes);
        okTotal += r.ok;
        puzzlesTotal += r.inner;
        nodesTotal += r.nodes;
        checksum += r.checksum;
    }

    const Statistics s = calculateStatistics(msPerPuzzle);
    const double nsNodeMedian = nsPerNode.empty() ? 0.0 : calculateStatistics(nsPerNode).median;
    const double puzzlesPerSec = s.median > 0.0 ? 1000.0 / s.median : 0.0;
    const double nodesPerPuzzle = static_cast<double>(nodesTotal) / puzzlesTotal;

    std::printf("%-6s objetivo >= %d nodos   exito %d/%d (%.1f%%)\n",
                c.name, c.targetNodes, okTotal, puzzlesTotal,
                100.0 * okTotal / puzzlesTotal);
    std::printf("       ms/puzzle  min %.4f  mediana %.4f  max %.4f  media %.4f  P90 %.4f  desv %.4f\n",
                s.min, s.median, s.max, s.mean, s.p90, s.stddev);
    std::printf("       %.1f puzzles/s   %.0f nodos visitados/puzzle   %.1f ns/nodo   %.2f Mnodos/s\n",
                puzzlesPerSec, nodesPerPuzzle, nsNodeMedian,
                nsNodeMedian > 0.0 ? 1000.0 / nsNodeMedian : 0.0);
    std::printf("       muestras %d x %d puzzles   checksum %llu (ignorar)\n\n",
                kReps, kPuzzlesPerSample, (unsigned long long)checksum);
}

int main(int argc, char **argv)
{
    int easy, hard;
    if (argc >= 3)
    {
        easy = std::atoi(argv[1]);
        hard = std::atoi(argv[2]);
    }
    else
    {
        const Calibration cal = calibrate();
        easy = static_cast<int>(cal.p50);
        hard = static_cast<int>(cal.p90);
    }

    const Case cases[] = {{"easy", easy}, {"hard", hard}};
    for (const auto &c : cases)
        runCase(c);

    return 0;
}