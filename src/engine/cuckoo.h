#pragma once

#include <cstring>

namespace cuckoo
{

inline int h1(uint64_t key)
{
    return key & 0x1fff;
}

inline int h2(uint64_t key)
{
    return (key >> 16) & 0x1fff;
}

inline uint64_t keys[8192];
inline chess::Move moves[8192];
inline bool IS_INIT = false;

inline void init()
{
    if (IS_INIT)
        return;

    IS_INIT = true;

    memset(keys, 0, sizeof(keys));
    memset(moves, 0, sizeof(moves));

    // Only used to check integrity of cuckoo tables
    int count = 0;

    for (chess::PieceType pt :
         {chess::PieceType::KNIGHT, chess::PieceType::BISHOP, chess::PieceType::ROOK,
          chess::PieceType::QUEEN, chess::PieceType::KING})
    {
        for (chess::Color color : {chess::Color::WHITE, chess::Color::BLACK})
        {

            chess::Piece piece{color, pt};

            for (chess::Square s1 = chess::Square::SQ_A1; s1 < 64; ++s1)
            {
                for (chess::Square s2 = s1 + 1; s2 < 64; ++s2)
                {

                    chess::Bitboard attacks{0};
                    if (pt == chess::PieceType::KNIGHT)
                        attacks = chess::attacks::knight(s1);
                    else if (pt == chess::PieceType::BISHOP)
                        attacks = chess::attacks::bishop(s1, chess::Bitboard{0});
                    else if (pt == chess::PieceType::ROOK)
                        attacks = chess::attacks::rook(s1, chess::Bitboard{0});
                    else if (pt == chess::PieceType::QUEEN)
                        attacks = chess::attacks::queen(s1, chess::Bitboard{0});
                    else if (pt == chess::PieceType::KING)
                        attacks = chess::attacks::king(s1);

                    if (attacks.check(s2.index()))
                    {
                        chess::Move move = chess::Move::make(s1, s2);
                        uint64_t key = chess::Zobrist::piece(piece, s1) ^
                                       chess::Zobrist::piece(piece, s2) ^
                                       chess::Zobrist::sideToMove();

                        int slot = h1(key);

                        while (true)
                        {
                            std::swap(keys[slot], key);
                            std::swap(moves[slot], move);

                            if (move == chess::Move::NO_MOVE)
                                break;

                            // Use the other slot
                            slot = (slot == h1(key)) ? h2(key) : h1(key);
                        }

                        count++;
                    }
                }
            }
        }
    }

    if (count != 3668)
    {
        std::cout << "uh oh rip cuckoo" << std::endl;
        exit(-1);
    }
}

inline bool is_upcoming_rep(const chess::Board &pos, int ply)
{
    const chess::Bitboard occ = pos.occ();
    const auto &states = pos.get_prev_state();
    const int maxDist = std::min((int)pos.halfMoveClock(), (int)states.size());


    for (int i = 3; i <= maxDist; i += 2)
    {
        uint64_t moveKey = pos.hash() ^ states[states.size() - i].hash;

        int hash = h1(moveKey);

        // try the other slot
        if (keys[hash] != moveKey)
            hash = h2(moveKey);

        if (keys[hash] != moveKey)
            continue; // neither slot matches

        chess::Move move = moves[hash];
        chess::Square from = move.from();
        chess::Square to = move.to();

        // Check if the move is obstructed
        if ((chess::movegen::between(from, to) ^ chess::Bitboard::fromSquare(to)) & occ)
            continue;

        // check color. ap the colors are guaranteed correct if within search but it's not here?
        if (pos.at(move.from()).color() != pos.sideToMove())
            continue;

        // rep if within search tree
        if (ply > i)
        {
            return true;
        }

        // otherwise need another
        for (int j = i + 4; j <= maxDist; j += 2)
        {
            if (states[states.size() - j].hash == states[states.size() - i].hash)
                return true;
        }
    }

    return false;
}

} // namespace cuckoo