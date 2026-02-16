#include <iostream>

// use pext extension for performance, this include must be first
#if defined(__BMI2__)
#define CHESS_USE_PEXT
#endif
#include "hpplib/chess.h"

#include "engine/engine.h"
#include "engine/uci.h"

#ifdef TDCHESS_UCI
int main()
{
    uci_handler handler{};
    handler.loop();

    return 0;
}
#elif TDCHESS_ELO

#include "elo/arena.h"
#include "elo/book.h"
#include "elo/stats.h"

void improvement_test(const std::string &baseline, const std::string &latest, bool is_short)
{
    openbook book{"../book/baron30.bin"};
    std::string baseline_prefix = "../builds/" + baseline;
    std::string latest_prefix = "../builds/" + latest;
    const agent_settings base{baseline, baseline_prefix + "/tdchess", baseline_prefix + "/nnue.bin",
                              "../syzygy", 512};
    const agent_settings late{
        latest, latest_prefix + "/tdchess", latest_prefix + "/nnue.bin", "../syzygy", 512, false};
    std::vector<agent_settings> agents{late, base};

    arena_settings settings;
    if (is_short)
        settings = arena_settings{latest + "_against_" + baseline, 12, 3 * 1000,
                                  static_cast<int>(0.1 * 1000), false};
    else
        settings = arena_settings{latest + "_against_" + baseline, 12, 60 * 1000,
                                  static_cast<int>(0.6 * 1000)};

    std::vector<int> cores;
    for (int i = 0; i < 6; ++i)
        cores.push_back(2 * i);

    arena arena{settings, book, agents, cores};
    arena.loop(cores.size(), 100);
}

int main()
{
    // sprt s{};
    // // s.set_state(pentanomial{{10, 20, 50, 10, 1}});
    // s.set_state(pentanomial{{1, 20, 10, 20, 5}});
    // s.analytics();

    // chisq sq{{}};
    // sq.save("../test.bin");
    // sq.load("../test.bin");

    improvement_test("1.1.1", "1.2.0", true);

    return 0;
}

#elif TDCHESS_PGO

int main()
{
    std::vector<std::string> positions = {chess::constants::STARTPOS};
    for (auto &position : positions)
    {
        nnue nnue{};
        nnue.load_network("../nets/1.0.5.bin");
        chess::Board start{position};
        endgame_table table{};
        table.load_file("../syzygy");
        engine engine{&table, &nnue, 256};
        search_param param;
        param.movetime = 50000;
        engine.search(start, param, true, true);
        std::cout << "done\n";
    }

    return 0;
}

#elif TDCHESS_TUNE

#include "tuner/spsa.h"

int main()
{
    spsa::loop();
}

#else
#include "elo/agent.h"
#include "engine/nnue.h"

int evaluate_bucket(const chess::Board &position)
{
    constexpr int n = 8;
    constexpr int divisor = 32 / n;
    return (position.occ().count() - 2) / divisor;
}

void position_test()
{
    std::ifstream file{"/home/terry/Downloads/fastchess-linux-x86-64/popularpos_lichess.epd"};
    std::vector<std::string> fens;
    std::string line;
    while (std::getline(file, line))
    {
        fens.push_back(line);
    }

    std::cout << "loaded " << fens.size() << " positions\n";

    for (auto &pos : fens)
    {
        chess::Board start{pos};

        std::cout << start << std::endl;
        std::cout << start.getFen() << std::endl;


        nnue nnue{};
        nnue.load_network("../nets/2026-02-08-1800-370.bin");
        endgame_table endgame{};
        endgame.load_file("../syzygy");
        table tt{64};
        engine engine{&endgame, &nnue, &tt};
        search_param param;
        param.movetime = 10000;
        auto result = engine.search(start, param, true);
        std::cout << chess::uci::moveToUci(result.pv_line[0]) << std::endl;
    }

    // std::vector<std::pair<std::string, std::string>> positions{
    //     {"5rk1/1q2bpp1/4p2p/1N2P3/np5P/2r5/P3QPP1/1B1RR1K1 b - - 1 26", "c3c5 not c3c8, zero
    //     eval"},
    //     {"2r2rk1/1q2bp2/4p1pp/1N2P3/np5P/6Q1/P4PP1/1B1RR1K1 b - - 1 28", "b7b5, negative eval
    //     1"}};
    //
    // for (auto &[pos, target] : positions)
    // {
    //     nnue nnue{};
    //     nnue.load_network("../nets/2026-02-08-1800-370.bin");
    //     chess::Board start{pos};
    //     table tt{512};
    //     engine engine{nullptr, &nnue, &tt};
    //     search_param param;
    //     param.movetime = 5000;
    //     engine.search(start, param, true);
    //
    //     std::cout << "oracle " << target << std::endl;
    // }
}

int main()
{

    position_test();
    return 0;
}
#endif
