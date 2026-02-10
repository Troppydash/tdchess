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
    const agent_settings base{
        baseline, baseline_prefix + "/tdchess", baseline_prefix + "/nnue.bin", "../syzygy", 512};
    const agent_settings late{latest, latest_prefix + "/tdchess", latest_prefix + "/nnue.bin",
                              "../syzygy", 512, false};
    std::vector<agent_settings> agents{late, base};

    arena_settings settings;
    if (is_short)
        settings = arena_settings{latest + "_against_" + baseline, 12, 10 * 1000,
                                  static_cast<int>(0.2 * 1000), false};
    else
        settings = arena_settings{latest + "_against_" + baseline, 12, 60 * 1000,
                                  static_cast<int>(0.6 * 1000)};

    std::vector<int> cores;
    for (int i = 0; i < 6; ++i)
        cores.push_back(2 * i);

    arena arena{settings, book, agents, cores};
    arena.loop(cores.size(), 200);
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

    improvement_test("1.1.1", "1.1.3", true);

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

#else
#include "elo/agent.h"
#include "engine/nnue.h"

int evaluate_bucket(const chess::Board &position)
{
    constexpr int n = 8;
    constexpr int divisor = 32 / n;
    return (position.occ().count() - 2) / divisor;
}

int main()
{

    // search_param param{};
    // param.wtime = 238699;
    // param.winc = 2000;
    // param.move_overhead = 1300;
    //
    // std::cout << param.time_control(5, 0).time << std::endl;

    // agent_settings settings{"test",
    //                         "../builds/1.0.8-delta/tdchess",
    //                         "../builds/1.0.8-delta/nnue.bin",
    //                         "../syzygy",
    //                         128,
    //                         true};
    // agent agent{settings};
    //
    // agent.new_game();
    // int64_t movetime = 50;
    //
    // chess::Board position{};
    // std::vector<chess::Move> moves{};
    //
    // // arena_clock clock0{60*1000, 100};
    // // arena_clock clock1{60*1000, 100};
    //
    // // random position
    // while (true)
    // {
    //     auto is_over = position.isGameOver();
    //     if (is_over.second != chess::GameResult::NONE)
    //         break;
    //
    //     search_param param{};
    //     param.movetime = movetime;
    //
    //     chess::Move move;
    //     agent.new_game();
    //     auto start = std::chrono::high_resolution_clock::now();
    //     move = agent.search(moves, param, 1);
    //     auto end = std::chrono::high_resolution_clock::now();
    //     std::cout << "movetime " << movetime <<  " actual time "
    //               << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
    //               << std::endl;
    //
    //     // always make the move
    //     moves.push_back(move);
    //     position.makeMove(move);
    // }

    nnue nnue{};
    nnue.load_network("../nets/2026-02-08-1800-370.bin");
    chess::Board start;
    // chess::Board start{"8/3q4/8/1kpr2PP/p4Q2/4Q1K1/8/8 w - - 3 59"};
    // chess::Board start{"5B2/2b3p1/2k2pP1/4pP2/2P1P3/prPR1K2/8/8 w - - 0 57"};
    // chess::Board start{"8/4B3/p7/1p3p1p/1P2k1b1/P3P3/5K2/8 w - - 4 57"};
    // chess::Board start{"8/8/8/1p6/1P6/P3R1pP/1k2p1K1/5r2 b - - 11 71"};
    // chess::Board start{"8/5pk1/p5p1/1p1Rpq2/1P5p/P3PPnP/Q5P1/1r1B2K1 b - - 1 36"};

    // chess::Board start{"8/8/p7/1pB2p1p/1P2k1b1/P3P3/5K2/8 b - - 5 57"};
    // chess::Board start{"8/8/4Bb1p/2k2PpP/1p2K1P1/8/8/8 b - - 1 89"};
    // should be e3e2

    // chess::Board start{"1r6/6k1/ppQ1nrp1/4p3/P1Pp1bP1/1N1B4/1P6/1K6 b - - 1 34"};
    // chess::Board start{"7r/8/pQ2nrpk/4p3/P1Pp1bP1/1N1B4/1P6/1K6 b - - 0 36"};


    // nnue.initialize(start);
    // constexpr int n = 8;
    // constexpr int divisor = 32 / n;
    // int bucket= (start.occ().count() - 2) / divisor;
    //
    // std::cout << nnue.evaluate(start.sideToMove(), bucket) << std::endl;
    // exit(0);

    // chess::Board start{"8/8/p7/Bp5b/1P6/3k1pK1/8/8 w - - 52 109"};


    // endgame_table etable{};
    // etable.load_file("../syzygy");
    table tt{512};
    engine engine{nullptr, &nnue, &tt};
    search_param param;
    param.movetime = 100000;
    engine.search(start, param, true, true);

    return 0;
}
#endif
