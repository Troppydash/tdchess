#pragma once
#include <utility>

#include "chess.h"

struct board_wrapper
{
    chess::Board m_board;

    explicit board_wrapper(chess::Board board)
        : m_board(std::move(board))
    {
    }

    static board_wrapper from_fen(const std::string &fen)
    {
        return board_wrapper{chess::Board::fromFen(fen)};
    }

    [[nodiscard]] std::string display() const
    {
        std::string out{};

        out += std::format(
            "CA: {}, RE: {}, HA: {}, CH: {}\n",
            m_board.getCastleString(),
            (m_board.isRepetition() ? "Y" : "N"),
            m_board.isHalfMoveDraw() ? "Y" : "N",
            m_board.inCheck() ? "Y" : "N"
        );

        for (int rank = 7; rank >= 0; --rank)
        {
            out += "+";
            for (int i = 0; i < 8; ++i)
            {
                out += "---+";
            }
            out += "\n|";

            for (int file = 0; file < 8; ++file)
            {
                auto piece = m_board.at({chess::Rank{rank}, chess::File{file}});
                char p = ' ';
                if (piece.type() == chess::PieceType::NONE)
                    p = ' ';
                else if (piece.type() == chess::PieceType::BISHOP)
                    p = 'b';
                else if (piece.type() == chess::PieceType::KING)
                    p = 'k';
                else if (piece.type() == chess::PieceType::KNIGHT)
                    p = 'n';
                else if (piece.type() == chess::PieceType::PAWN)
                    p = 'p';
                else if (piece.type() == chess::PieceType::QUEEN)
                    p = 'q';
                else if (piece.type() == chess::PieceType::ROOK)
                    p = 'r';

                if (piece.color() == chess::Color::WHITE)
                    p = std::toupper(p);

                out += " ";
                out += p;
                out += " |";
            }
            out += "\n";
        }

        out += "+";
        for (int i = 0; i < 8; ++i)
        {
            out += "---+";
        }

        return out;
    }
};


// TODO: make engine
struct engine
{

};