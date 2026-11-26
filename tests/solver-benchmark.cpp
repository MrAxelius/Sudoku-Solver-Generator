#include "../src/Solver.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using clk = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// escape(): barrera de compilador para impedir que el optimizador elimine
// trabajo dentro del bucle de medicion.
//   - GCC / Clang: asm volatile vacio con clobber "memory" (sin codigo real).
//   - MSVC: no acepta asm con operandos/clobbers. Se emula con una lectura
//           volatile del puntero (fuerza a que "p" exista) + atomic_signal_fence
//           (barrera de compilador sin instrucciones en hardware, equivalente
//           al clobber "memory").
// ---------------------------------------------------------------------------
template <class T>
inline void escape(T *p)
{
#if defined(_MSC_VER)
    volatile std::uintptr_t vp = reinterpret_cast<std::uintptr_t>(p);
    (void)vp;
    std::atomic_signal_fence(std::memory_order_seq_cst);
#else
    asm volatile("" : : "g"(p) : "memory");
#endif
}

static volatile std::uint64_t g_sink = 0;


struct Puzzle
{
    std::string name;
    std::string cells;
};

static std::vector<Puzzle> loadPuzzles(const std::string &path)
{
    std::vector<Puzzle> out;
    std::ifstream in(path);
    if (!in)
    {
        std::fprintf(stderr, "No se puede abrir '%s'\n", path.c_str());
        return out;
    }

    std::string line;
    int lineNo = 0;
    while (std::getline(in, line))
    {
        ++lineNo;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue;
        line = line.substr(start);

        if (line[0] == '#')
            continue;

        for (char &c : line)
            if (c == '.')
                c = '0';

        bool valid = line.size() == 81 &&
                     std::all_of(line.begin(), line.end(), [](char c)
                                 { return c >= '0' && c <= '9'; });
        if (!valid)
        {
            std::fprintf(stderr, "%s:%d: linea invalida (se esperaban 81 digitos), ignorada\n",
                         path.c_str(), lineNo);
            continue;
        }

        char name[32];
        std::snprintf(name, sizeof(name), "#%02zu", out.size() + 1);
        out.push_back({name, line});
    }
    return out;
}

static const char *const kPuzzles[] = {
    "800000000003600000070090200050007000000045700000100030001000068008500010090000400", // Inkala 2012
    "850002400720000009004000000000107002305000900040000000000080070017000000000036040",
    "005300000800000020070010500400005300010070006003200080060500009004000030000009700",
    "120040000005069010009000500000000070700052090030000002090600050400900801003000904",
    "000570030100000020700023400000080004007004000490000605042000300000700900001800000",
    "700152300000000920000300000100004708000000060000000000009000506040907000800006010",
    "100007090030020008009600500005300900010080002600004000300000010040000007007000300",
    "100034080000800500004060021018000000300102006000000810520070900006009000090640002",
    "000920000006803000190070006230040100001000700008030029700080091000507200000064000",
    "060504030100090008000000000900050006040602070700040005000000000400080001050203040",
    "700000400020070080003008079900500300060020090001097006000300900030040060009001035",
    "000070020800000006010205000905400008000000000300008501000302080400000009070060000",
};

static std::vector<Puzzle> embeddedPuzzles()
{
    std::vector<Puzzle> out;
    for (const char *p : kPuzzles)
    {
        char name[32];
        std::snprintf(name, sizeof(name), "#%02zu", out.size() + 1);
        out.push_back({name, p});
    }
    return out;
}

struct Statistics
{
    double min, median, max, mean, p90, stddev;
};

static Statistics calculateStatistics(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    const size_t n = v.size();

    const double median = (n % 2 == 0) ? (v[n / 2 - 1] + v[n / 2]) / 2.0 : v[n / 2];

    double sum = 0.0;
    for (double x : v)
        sum += x;
    const double mean = sum / n;

    double sq = 0.0;
    for (double x : v)
        sq += (x - mean) * (x - mean);
    const double stddev = n > 1 ? std::sqrt(sq / (n - 1)) : 0.0;

    const size_t p90Index = static_cast<size_t>(std::ceil(0.90 * n)) - 1;

    return {v.front(), median, v.back(), mean, v[p90Index], stddev};
}

struct Batch
{
    double ms;
    std::uint64_t totalNodes;
    int failures;
};

static Batch runBatch(const Board &base, int inner)
{
    std::uint64_t totalNodes = 0, checksum = 0;
    int failures = 0;

    const auto t0 = clk::now();
    for (int i = 0; i < inner; ++i)
    {
        Board board = base;
        Solver solver;
        escape(&board);
        const bool ok = solver.solve(board);
        escape(&board);
        failures += !ok;
        totalNodes += solver.counter;
        checksum += board.cells[80];
    }
    const auto t1 = clk::now();

    g_sink += checksum;
    return {std::chrono::duration<double, std::milli>(t1 - t0).count(), totalNodes, failures};
}

static double measureOverheadMsPerIter(const Board &base)
{
    constexpr int N = 1 << 16;
    double best = 1e30;
    for (int rep = 0; rep < 7; ++rep)
    {
        std::uint64_t checksum = 0;
        const auto t0 = clk::now();
        for (int i = 0; i < N; ++i)
        {
            Board board = base;
            Solver solver;
            escape(&board);
            escape(&solver);
            checksum += board.cells[80] + solver.counter;
        }
        const auto t1 = clk::now();
        g_sink += checksum;
        best = std::min(best, std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    return best / N;
}

static int calibrateInner(const Board &base, double targetMs)
{
    int inner = 1;
    for (;;)
    {
        const Batch b = runBatch(base, inner);
        if (b.ms >= targetMs || inner >= (1 << 22))
            return inner;
        inner *= 2;
    }
}

static bool verifySolution(const std::string &puzzle, Board solved)
{
    for (int i = 0; i < 81; ++i)
    {
        const int v = solved.cells[i];
        if (v < 1 || v > 9)
            return false;
        if (puzzle[i] != '0' && v != puzzle[i] - '0')
            return false;
    }
    return solved.isBoardComplete();
}

int main(int argc, char **argv)
{
    const std::string path = argc > 1 ? argv[1] : "-"; // "-" = puzzles integrados
    const int reps = argc > 2 ? std::max(3, std::atoi(argv[2])) : 30;
    const double targetMs = argc > 3 ? std::max(1.0, std::atof(argv[3])) : 30.0;
    constexpr int warmupBatches = 2;

    const std::vector<Puzzle> puzzles = (path == "-") ? embeddedPuzzles() : loadPuzzles(path);
    if (puzzles.empty())
        return 2;

#ifdef __VERSION__
    std::printf("Compilador: %s\n", __VERSION__);
#elif defined(_MSC_VER)
    std::printf("Compilador: MSVC %d.%d\n", _MSC_VER / 100, _MSC_VER % 100);
#endif

    // Aviso de build sin optimizar. GCC/Clang: __OPTIMIZE__. MSVC: lo pone
    // el CMake via SUDOKU_OPTIMIZED cuando la configuracion es Release.
#if defined(__OPTIMIZE__) || defined(SUDOKU_OPTIMIZED)
    // ok
#elif defined(NDEBUG)
    // MSVC Release sin la define del CMake (por ejemplo, build manual): asumimos /O2.
#else
    std::printf("AVISO: compilado SIN optimizacion (falta /O2 o -O2/-O3). Los resultados no valen.\n");
#endif

    std::printf("Fichero: %s  (%zu puzzles)  muestras=%d  objetivo=%.0f ms/muestra\n\n",
                (path == "-" ? "(integrado)" : path.c_str()), puzzles.size(), reps, targetMs);

    std::printf("%-5s %8s %11s %11s %11s %7s %9s  %s\n",
                "id", "nodos", "mediana us", "min us", "P90 us", "cv %", "ns/nodo", "estado");

    double sumMedianMs = 0.0;
    std::uint64_t sumNodes = 0;
    bool allGood = true;

    const double overheadMsPerIter = measureOverheadMsPerIter(Board(puzzles[0].cells));

    for (const Puzzle &p : puzzles)
    {
        const Board base(p.cells);

        Board once = base;
        Solver s;
        const bool ok = s.solve(once);
        const std::uint64_t nodes = s.counter;
        const bool correct = ok && verifySolution(p.cells, once);

        const int inner = calibrateInner(base, targetMs);
        for (int i = 0; i < warmupBatches; ++i)
            runBatch(base, inner);

        std::vector<double> perSolveMs;
        perSolveMs.reserve(reps);
        bool stable = true;
        int failures = 0;
        for (int r = 0; r < reps; ++r)
        {
            const Batch b = runBatch(base, inner);
            failures += b.failures;
            stable &= (b.totalNodes == nodes * static_cast<std::uint64_t>(inner));
            perSolveMs.push_back(std::max(0.0, b.ms / inner - overheadMsPerIter));
        }

        const Statistics st = calculateStatistics(perSolveMs);
        const double nsPerNode = nodes ? st.median * 1e6 / nodes : 0.0;
        const double cv = st.mean > 0 ? 100.0 * st.stddev / st.mean : 0.0;

        const char *status = !correct          ? "SOLUCION INVALIDA"
                             : failures        ? "FALLO"
                             : !stable         ? "NODOS VARIABLES"
                                               : "OK";
        allGood &= (status[0] == 'O');

        std::printf("%-5s %8llu %11.3f %11.3f %11.3f %7.2f %9.1f  %s\n",
                    p.name.c_str(), static_cast<unsigned long long>(nodes),
                    st.median * 1e3, st.min * 1e3, st.p90 * 1e3, cv, nsPerNode, status);

        sumMedianMs += st.median;
        sumNodes += nodes;
    }

    const double perSec = sumMedianMs > 0 ? puzzles.size() / (sumMedianMs / 1000.0) : 0.0;
    const double nsPerNodeSet = sumNodes ? sumMedianMs * 1e6 / sumNodes : 0.0;

    std::printf("\nCONJUNTO  %zu puzzles | %llu nodos | %.4f ms total | %.1f puzzles/s | %.1f ns/nodo\n",
                puzzles.size(), static_cast<unsigned long long>(sumNodes),
                sumMedianMs, perSec, nsPerNodeSet);

    std::printf("\nFila para benchmark.md:\n");
    std::printf("| <version> | %zu | %llu | %.4f | %.1f | %.1f |\n",
                puzzles.size(), static_cast<unsigned long long>(sumNodes),
                sumMedianMs, perSec, nsPerNodeSet);

    std::printf("\n(checksum %llu, ignorar)\n", static_cast<unsigned long long>(g_sink));
    return allGood ? 0 : 1;
}