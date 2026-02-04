#pragma once

#include "../hpplib/chess.h"
#include <array>

struct see
{
    constexpr static std::array<int32_t, 7> TRADITIONAL_PIECE_VALUES = {
        // pawn, knight, bishop, rook, queen, king, none
        200, 320, 330, 500, 900, 0, 0,
    };

    constexpr static std::array<int32_t, 7> PIECE_VALUES = {
        // pawn, knight, bishop, rook, queen, king, none
        200, 781, 825, 1276, 2538, 100000000, 0,
    };

    constexpr static std::array<int32_t, 7> PROMOTE_PIECE_VALUES = {
        // pawn, knight, bishop, rook, queen, king, none
        2700, 781, 825, 1276, 2538, 100000000, 0,
    };

    static int32_t test(const chess::Board &position, const chess::Move &move)
    {
        if (move.to().rank() == chess::Rank::RANK_1 || move.to().rank() == chess::Rank::RANK_8)
        {
            return promote_test(position, move);
        }

        chess::PieceType target = position.at(move.to()).type();
        chess::PieceType attacker = position.at(move.from()).type();

        // gain[depth] is the perspective value of the static exchange at depth
        static thread_local int32_t gain[32]{};
        int32_t depth = 0;
        int side = position.sideToMove() ^ 1;

        chess::Bitboard seenBB = 0;
        chess::Bitboard occBB = position.occ();
        chess::Bitboard attackerBB = chess::Bitboard(1ull << move.from().index());

        chess::Bitboard attack_def =
            chess::attacks::attackers(position, chess::Color::WHITE, move.to()) |
            chess::attacks::attackers(position, chess::Color::BLACK, move.to());
        chess::Bitboard max_xray =
            occBB & ~(position.pieces(chess::PieceType::KNIGHT, chess::PieceType::KING));

        gain[depth] = PIECE_VALUES[target];

        // loop while attackerBB != 0, with one extra at end
        for (bool ok = true; ok; ok = attackerBB != 0)
        {
            depth++;

            // update value
            gain[depth] = PIECE_VALUES[attacker] - gain[depth - 1];

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
                attack_def |= (chess::attacks::bishop(move.to(), occBB) |
                               chess::attacks::rook(move.to(), occBB) |
                               chess::attacks::queen(move.to(), occBB)) &
                              ~seenBB;
            }

            // pick the new min attacker
            attackerBB = 0;
            for (const auto &att : std::array<chess::PieceType, 6>{
                     chess::PieceType::PAWN, chess::PieceType::KNIGHT, chess::PieceType::BISHOP,
                     chess::PieceType::ROOK, chess::PieceType::QUEEN, chess::PieceType::KING})
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

    static int32_t promote_test(const chess::Board &position, const chess::Move &move)
    {
        chess::PieceType target = position.at(move.to()).type();
        chess::PieceType attacker = position.at(move.from()).type();

        // gain[depth] is the perspective value of the static exchange at depth
        static thread_local int32_t gain[32]{};
        int32_t depth = 0;
        int side = position.sideToMove() ^ 1;

        chess::Bitboard seenBB = 0;
        chess::Bitboard occBB = position.occ();
        chess::Bitboard attackerBB = chess::Bitboard(1ull << move.from().index());

        chess::Bitboard attack_def =
            chess::attacks::attackers(position, chess::Color::WHITE, move.to()) |
            chess::attacks::attackers(position, chess::Color::BLACK, move.to());
        chess::Bitboard max_xray =
            occBB & ~(position.pieces(chess::PieceType::KNIGHT, chess::PieceType::KING));

        gain[depth] = PROMOTE_PIECE_VALUES[target];

        // loop while attackerBB != 0, with one extra at end
        for (bool ok = true; ok; ok = attackerBB != 0)
        {
            depth++;

            // update value
            gain[depth] = PROMOTE_PIECE_VALUES[attacker] - gain[depth - 1];

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
                attack_def |= (chess::attacks::bishop(move.to(), occBB) |
                               chess::attacks::rook(move.to(), occBB) |
                               chess::attacks::queen(move.to(), occBB)) &
                              ~seenBB;
            }

            // pick the new min attacker
            attackerBB = 0;
            for (const auto &att : std::array<chess::PieceType, 6>{
                     chess::PieceType::KNIGHT, chess::PieceType::BISHOP, chess::PieceType::ROOK,
                     chess::PieceType::QUEEN, chess::PieceType::PAWN, chess::PieceType::KING})
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
