#include <iostream>

// use pext extension for performance, this include must be first
#define CHESS_USE_PEXT
#include "elo/agent.h"
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
#include "elo/elo.h"

int main()
{
    openbook book{"../book/Book.bin"};
    const agent_settings v102{"1.0.2", "../builds/1.0.2/tdchess", "../builds/1.0.2/nnue.bin", "../syzygy", 256, false};
    std::vector<agent_settings> agents{v102, v102};
    arena arena{"basic", book, agents, {0, 2, 4}};
    arena.full_round();

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
