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
    openbook book{"../book/Book.bin"};
    std::string baseline_prefix = "../builds/" + baseline;
    std::string latest_prefix = "../builds/" + latest;
    const agent_settings base{
        baseline, baseline_prefix + "/tdchess", baseline_prefix + "/nnue.bin", "../syzygy", 128,
        false};
    const agent_settings late{
        latest, latest_prefix + "/tdchess", latest_prefix + "/nnue.bin", "../syzygy", 128, false};
    std::vector<agent_settings> agents{late, base};

    arena_settings settings;
    if (is_short)
        settings = arena_settings{latest + "_against_" + baseline, 11, 10 * 1000,
                                  static_cast<int>(0.1 * 1000)};
    else
        settings = arena_settings{latest + "_against_" + baseline, 11, 60 * 1000,
                                  static_cast<int>(0.6 * 1000)};

    std::vector<int> cores;
    for (int i = 0; i < 10; ++i)
        cores.push_back(i);

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

    improvement_test("1.0.8", "1.0.8-beta", true);

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
        param.movetime = 10000;
        engine.search(start, param, true, true);
        std::cout << "done\n";
    }

    return 0;
}

#else
#include "engine/nnue.h"
#include "elo/agent.h"

int evaluate_bucket(const chess::Board &position)
{
    constexpr int n = 8;
    constexpr int divisor = 32 / n;
    return (position.occ().count() - 2) / divisor;
}
int main()
{
    // agent_settings settings{"test", "../builds/1.0.8-beta/tdchess",
    //                         "../builds/1.0.8-beta/nnue.bin", "../syzygy", 128, true};
    // agent agent{settings};
    //
    // agent.new_game();
    //
    // int movetime = 200;
    //
    // chess::Board position{};
    // std::vector<chess::Move> moves{};
    // // random position
    // while (true)
    // {
    //     auto is_over = position.isGameOver();
    //     if (is_over.second != chess::GameResult::NONE)
    //         break;
    //
    //     search_param param{};
    //     param.movetime = movetime;
    //     auto start = std::chrono::high_resolution_clock::now();
    //     chess::Move move = agent.search(moves, param, 1);
    //     auto end = std::chrono::high_resolution_clock::now();
    //
    //     std::cout << chess::uci::moveToUci(move) << std::endl;
    //     std::cout << "movetime " << movetime << " actual time "
    //               << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
    //               << std::endl;
    //
    //     // always make the move
    //     moves.push_back(move);
    //     position.makeMove(move);
    // }

    nnue nnue{};
    nnue.load_network("../nets/1.0.8-alpha.bin");
    chess::Board start{"r2k3r/pb2b2p/4p3/1P6/3p4/3B4/PPp3PP/R1B2RK1 w - - 2 21"};
    endgame_table table{};
    table.load_file("../syzygy");
    engine engine{&table, &nnue, 256};
    search_param param;
    param.movetime = 10000;
    engine.search(start, param, true, true);


    return 0;
}
#endif
