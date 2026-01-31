#include <iostream>

// use pext extension for performance, this include must be first
#define CHESS_USE_PEXT
#include "elo/agent.h"
#include "elo/elo.h"
#include "hpplib/chess.h"

#include "engine/engine.h"
#include "engine/uci.h"

void perft()
{
    engine engine{128};
    chess::Board board{};
    engine.perft(board, 7);
}

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
    openbook book{"../book/Book.bin"};
    std::string baseline_prefix = "../builds/" + baseline;
    std::string latest_prefix = "../builds/" + latest;
    const agent_settings base{
        baseline, baseline_prefix + "/tdchess", baseline_prefix + "/nnue.bin", "../syzygy", 256,
        false};
    const agent_settings late{
        latest, latest_prefix + "/tdchess", latest_prefix + "/nnue.bin", "../syzygy", 256, false};
    std::vector<agent_settings> agents{late, base};

    arena_settings settings;
    if (is_short)
        settings = arena_settings{latest + "_against_" + baseline, 11, 10 * 1000,
                                  static_cast<int>(0.1 * 1000)};
    else
        settings = arena_settings{latest + "_against_" + baseline, 11, 60 * 1000,
                                  static_cast<int>(0.6 * 1000)};

    arena arena{settings, book, agents, {0, 2, 4, 6, 8, 10}};
    arena.loop(6, 100);
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

    improvement_test("1.0.4", "1.0.4", true);

    return 0;
}

#else

#include "engine/nnue.h"

int main()
{
    agent ag{"../builds/1.0.2/tdchess", "../builds/1.0.2/nnue.bin", "../syzygy", 128};
    ag.initialize(true);

    auto result = ag.search({}, 10000, 18, 2, true);
    std::cout << chess::uci::moveToUci(result) << std::endl;

    // ag.search(0, 0, 0, true);

    // nnue nnue{};
    // nnue.load_network("../nets/1.0.1.bin");
    // chess::Board start{"rnbq1rk1/2p1bppp/1p2pn2/p1Pp4/1P1P4/2NB1P2/P3N1PP/R1BQK2R w KQ - 0 10"};
    // nnue.initialize(start);
    // std::cout << nnue.evaluate(start.sideToMove()) << std::endl;
    // std::cout << nnue.evaluate(1) << std::endl;
    //
    // exit(0);
    // engine engine{nullptr, &nnue, 1024};
    // engine.search(start, 1000, 50000, true, true);

    return 0;
}
#endif
