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

    // endgame_table table{"../syzygy"};
    // auto result =table.probe(chess::Board::fromFen("8/8/4kq2/5q2/8/3Q4/8/4K3 w - - 0 1"));
    // if (result == endgame_table::WIN)
    // {
    //     std::cout << "win\n";
    // } else if (result == endgame_table::LOSS)
    // {
    //     std::cout << "loss\n";
    // }
    // perft();

    // engine engine;
    // chess::Board board{};
    // std::cout << board << std::endl;
    // engine.search(board, 5000, true);

    uci_handler handler{};
    handler.loop();

    return 0;
}
