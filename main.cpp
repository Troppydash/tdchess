#include <iostream>

// use pext extension for performance, this include must be first
#define CHESS_USE_PEXT
#include "chess.h"

#include "engine.h"
#include "uci.h"


void perft()
{
    engine engine;
    chess::Board board{};
    engine.perft(board, 7);
}

int main()
{
    uci_handler handler{};
    handler.loop();

    return 0;
}
