#include <iostream>
#include "engine.h"
#include "chess.h"




int main()
{

    board_wrapper board = board_wrapper::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    std::cout << board.display() << std::endl;
    return 0;
}