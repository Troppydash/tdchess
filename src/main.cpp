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
        latest, latest_prefix + "/tdchess", latest_prefix + "/nnue.bin", "../syzygy", 128};
    std::vector<agent_settings> agents{late, base};

    arena_settings settings;
    if (is_short)
        settings = arena_settings{latest + "_against_" + baseline, 11, 10 * 1000,
                                  static_cast<int>(0.3 * 1000)};
    else
        settings = arena_settings{latest + "_against_" + baseline, 11, 60 * 1000,
                                  static_cast<int>(0.6 * 1000)};

    std::vector<int> cores;
    for (int i = 0; i < 6; ++i)
        cores.push_back(2*i);

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

    improvement_test("1.0.9", "1.0.9-alpha", true);

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
#include "elo/agent.h"
#include "elo/arena.h"
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
    nnue.load_network("../nets/1.0.9.bin");
    chess::Board start{"8/3q4/8/1kpr2PP/p4Q2/4Q1K1/8/8 w - - 3 59"};
    // should be e3e2
    endgame_table etable{};
    etable.load_file("../syzygy");
    table tt{256};
    engine engine{&etable, &nnue, &tt};
    search_param param;
    param.movetime = 10000;
    engine.search(start, param, true, true);

    return 0;
}
#endif
