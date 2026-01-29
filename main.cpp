#include <iostream>

// use pext extension for performance, this include must be first
#define CHESS_USE_PEXT
#include "chess.h"

#include "engine.h"
#include "nnue.h"
#include "trainer.h"
#include "uci.h"

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
#else

int main()
{
    nnue nnue{};
    nnue.load_network("/media/terry/Games/projects/2026/cppprojects/tdchess-nnue/simple-checkpoints/simple-40/quantised.bin");

    chess::Board start;
    nnue.initialize(start);
    // std::cout << nnue.evaluate(0) << std::endl;
    // std::cout << nnue.evaluate(1) << std::endl;
    //
    // exit(0);
    engine engine{nullptr, &nnue, 1024};
    engine.search(start, 1000, 50000, true, true);

    return 0;
}
#endif
