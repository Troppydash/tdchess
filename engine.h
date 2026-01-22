#pragma once
#include <chrono>
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

// TODO: copy from board
struct tt
{
};

// TODO: make engine
struct engine
{
    chess::Board m_position;

    // must be set via methods
    explicit engine() = default;

    int nn_eval()
    {
    }

    int negamax(int alpha, int beta, std::vector<chess::Move> &pv_list)
    {
    }

    uint64_t perft(int depth)
    {
        chess::Movelist moves;
        chess::movegen::legalmoves(moves, m_position);

        if (depth == 1)
        {
            return moves.size();
        }

        uint64_t total = 0;
        for (const auto &move: moves)
        {
            m_position.makeMove(move);
            total += perft(depth - 1);
            m_position.unmakeMove(move);
        }

        return total;
    }

    void perft(const chess::Board &reference, int depth)
    {
        using namespace std;

        m_position = reference;
        cout << "benchmarking perft, depth: " << depth << endl;
        cout << "fen: " << m_position.getFen() << endl;

        const auto t1    = chrono::high_resolution_clock::now();
        const auto nodes = perft(depth);
        const auto t2    = chrono::high_resolution_clock::now();
        const auto ms    = chrono::duration_cast<chrono::milliseconds>(t2 - t1).count();

        auto original = std::cout.getloc();
        std::cout.imbue(std::locale("en_US.UTF-8"));
        cout << "nodes: " << nodes << ", took " << ms << "ms" << endl;
        cout << "nps: " << nodes * 1000 / ms << endl;
        std::cout.imbue(original);
    }

    chess::Move search(const chess::Board &reference, int ms)
    {
        m_position = reference;
    }
};
