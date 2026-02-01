#pragma once

#include <mutex>

#include "../hpplib/chess.h"
#include "../hpplib/reader.h"

inline int random_range(const int lower, const int upper)
{
    static thread_local std::mt19937 generator{std::random_device{}()};
    std::uniform_int_distribution<int> distribution(lower, upper - 1);
    return distribution(generator);
}

class openbook
{
  private:
    Reader::Book m_book;
    std::mutex m_mutex;

  public:
    explicit openbook(const std::string &path)
    {
        m_book.Load(path.c_str());
    }

    ~openbook()
    {
        m_book.Clear();
    }

    std::pair<std::vector<chess::Move>, chess::Board> generate_game(int depth)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::vector<chess::Move> moves;
        chess::Board position;
        for (int i = 0; i < depth; ++i)
        {
            auto book_moves = get_moves(position);
            if (book_moves.empty())
                break;

            chess::Move &random_move = book_moves[random_range(0, book_moves.size())];
            moves.push_back(random_move);
            position.makeMove(random_move);
        }

        return {moves, position};
    }

  private:
    std::vector<chess::Move> get_moves(const chess::Board &position)
    {
        Reader::BookMoves book_moves = m_book.GetBookMoves(position.zobrist());
        std::vector<chess::Move> moves;
        for (auto &move : book_moves)
        {
            std::string uci = Reader::ConvertBookMoveToUci(move);
            moves.push_back(chess::uci::uciToMove(position, uci));
        }

        return moves;
    }
};
