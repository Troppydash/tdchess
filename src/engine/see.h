#pragma once

#include "../hpplib/chess.h"
#include <array>

struct see
{
    constexpr static std::array<int32_t, 7> piece_values = {
        100, 290, 310, 500, 900, 100000000, 0,
    };

    static int32_t test(const chess::Board &position, const chess::Move &move)
    {
        chess::PieceType target = position.at(move.to()).type();
        chess::PieceType attacker = position.at(move.from()).type();

        // gain[depth] is the perspective value of the static exchange at depth
        int32_t gain[32]{};
        int depth = 0;
        int side = position.sideToMove() ^ 1;

        chess::Bitboard seenBB = 0;
        chess::Bitboard occBB = position.occ();
        chess::Bitboard attackerBB = chess::Bitboard(1ull << move.from().index());

        chess::Bitboard attack_def = chess::attacks::attackers(position, chess::Color::WHITE, move.to()) |
                                     chess::attacks::attackers(position, chess::Color::BLACK, move.to());
        chess::Bitboard max_xray = occBB & ~(position.pieces(chess::PieceType::KNIGHT, chess::PieceType::KING));

        gain[depth] = piece_values[target];

        // loop while attackerBB != 0, with one extra at end
        for (bool ok = true; ok; ok = attackerBB != 0)
        {
            depth++;

            // update value
            gain[depth] = piece_values[attacker] - gain[depth - 1];

            // if re-capture is still negative value
            if (gain[depth] < 0)
                break;

            // remove attackers
            attack_def &= ~attackerBB;
            occBB &= ~attackerBB;
            seenBB |= attackerBB;

            // add (new) xray attackers
            if ((attackerBB & max_xray) != 0)
            {
                attack_def |= (chess::attacks::bishop(move.to(), occBB) | chess::attacks::rook(move.to(), occBB) |
                               chess::attacks::queen(move.to(), occBB)) &
                              ~seenBB;
            }

            // pick the new min attacker
            attackerBB = 0;
            for (const auto &att : std::array<chess::PieceType, 6>{chess::PieceType::PAWN, chess::PieceType::KNIGHT,
                                                                   chess::PieceType::BISHOP, chess::PieceType::ROOK,
                                                                   chess::PieceType::QUEEN, chess::PieceType::KING})
            {
                auto subset = attack_def & position.pieces(att, side);
                if (subset != 0)
                {
                    // pick one
                    attackerBB = 1ull << subset.lsb();
                    attacker = att;
                    break;
                }
            }

            side ^= 1;
        }

        // skip the last uninteresting capture
        depth -= 1;
        while (depth > 0)
        {
            gain[depth - 1] = -std::max(-gain[depth - 1], gain[depth]);
            depth -= 1;
        }

        return gain[0];
    }
};
