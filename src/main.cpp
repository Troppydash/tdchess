#include "engine/engine.h"
#include "engine/uci.h"
#include <iostream>

#ifdef TDCHESS_UCI
int main(int argc, char **argv)
{
    std::string variant{};
    if (argc == 2)
    {
        variant = argv[1];
    }

    uci_handler handler{};
    handler.loop(variant);

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
                              "", 512};
    const agent_settings late{
        latest, latest_prefix + "/tdchess", latest_prefix + "/nnue.bin", "", 512, false};
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

    improvement_test("1.2.21", "1.2.22", true);

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
#include "engine/chessmap.h"
#include "engine/lazysmp.h"
#include "engine/nnue.h"

int evaluate_bucket(const chess::Board &position)
{
    constexpr int n = 8;
    constexpr int divisor = 32 / n;
    return (position.occ().count() - 2) / divisor;
}

void position_test()
{
    // std::ifstream file{"/home/terry/Downloads/fastchess-linux-x86-64/popularpos_lichess.epd"};
    // std::vector<std::string> fens;
    // std::string line;
    // while (std::getline(file, line))
    // {
    //     fens.push_back(line);
    // }
    //
    // std::cout << "loaded " << fens.size() << " positions\n";
    //
    // for (auto &pos : fens)
    // {
    //     chess::Board start{pos};
    //
    //     std::cout << start << std::endl;
    //     std::cout << start.getFen() << std::endl;
    //
    //
    //     nnue nnue{};
    //     nnue.load_network("../nets/2026-02-08-1800-370.bin");
    //     endgame_table endgame{};
    //     endgame.load_file("../syzygy");
    //     table tt{64};
    //     engine engine{&endgame, &nnue, &tt};
    //     search_param param;
    //     param.movetime = 10000;
    //     auto result = engine.search(start, param, true);
    //     std::cout << chess::uci::moveToUci(result.pv_line[0]) << std::endl;
    // }

    // cuckoo_table<10000> t;
    // t.set(1);
    // t.set(1);
    // t.unset(1);
    // t.set(1);
    // int c = t.lookup(1);
    // std::cout << c << std::endl;
    // exit(0);

    // chessmap::net net{};
    // chess::Board pos{"1r3qk1/p2b3p/2pNrnp1/4pp2/B1P5/5P2/PP1Q2PP/2KRR3 b - - 3 22"};
    // // chess::Board pos{};
    // net.initialize(pos);
    // net.evaluate(pos, 0xFFFFFFFFFFFFFFFF);
    // net.show_evaluation(pos);

    // exit(0);

    std::vector<std::pair<std::string, std::string>> positions{
        // {"8/8/8/8/2k5/8/2K5/8 w - - 0 61", ""},
        // {"2r2nk1/4qb1p/p2p2pP/Pp1Pp3/1Q4P1/2rBB3/P1P5/1K1R3R w - - 0 27", "0 draw, or h1e1"},
        // {"2br4/3q3k/2p1p1pp/4Bp2/Pn3PP1/1r1P1BQ1/R6P/6RK b - - 4 31", ""},
        // {"2br4/3q3k/2p1p1pp/4Bp2/Pn3PP1/1r1P2Q1/R5BP/6RK w - - 3 31", ""},
        {"8/5k2/R7/4K1p1/5p1n/7P/6P1/8 w - - 0 49", "even"},
        // {"r6q/pb1n1pk1/1p2p3/1NbnP1Nr/2p1QP2/8/PP1B2BP/R4R1K w - - 9 23", "even"},
        // {"8/p1R4p/6pk/8/6KP/8/3r1P2/1B6 b - - 0 48", "0 draw"},
        // {"1k1br3/pp1R4/3nB3/1Pp2P2/2P5/1K2QP1p/P3N2q/8 b - - 1 36", "d8f8"},
        // {"r3q1k1/1R1b2rp/2p2Bn1/p1np3Q/5P2/b2B2N1/2P3PP/5R1K w - - 2 24", "no zero"}

    };
    endgame_table m_table{};
    m_table.load_file("/Users/troppydash/Downloads/syzygy");

    auto *nnue = new nnue2::net{};
    nnue->incbin_load();
    table tt{4096};

    // for (auto &[pos, target] : positions)
    // {
    //     tt.clear();
    //     engine engine{&m_table, nnue, &tt};
    //
    //     chess::Board start{pos};
    //     search_param param;
    //     param.movetime = 10000;
    //     engine.search(start, param, true);
    //     engine.post_search();
    //
    //     std::cout << "oracle " << target << std::endl;
    // }

    for (auto &[pos, target] : positions)
    {
        tt.clear();

        lazysmp engine{1, nnue, &tt, &m_table};

        chess::Board start{pos};
        search_param param;
        param.movetime = 30000;
        engine.search(start, param, true);

        std::cout << "oracle " << target << std::endl;
    }

    delete nnue;
}

int main()
{

    position_test();
    return 0;
}
#endif
