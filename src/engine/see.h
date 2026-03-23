#pragma once

#include "../hpplib/chess.h"
#include <array>

struct see
{
    static constexpr int16_t PAWN_VALUE = 100;
    static constexpr int16_t KNIGHT_VALUE = 370;
    static constexpr int16_t BISHOP_VALUE = 390;
    static constexpr int16_t ROOK_VALUE = 610;
    static constexpr int16_t QUEEN_VALUE = 1010;

    constexpr static std::array<int16_t, 7> PIECE_VALUES = {
        // pawn, knight, bishop, rook, queen, king, none
        PAWN_VALUE, KNIGHT_VALUE, BISHOP_VALUE, ROOK_VALUE, QUEEN_VALUE, 0, 0};

    // for when the king is treated as a valuable piece
    constexpr static std::array<int16_t, 7> ATTACKED_PIECE_VALUES = {
        // pawn, knight, bishop, rook, queen, king, none
        PAWN_VALUE, KNIGHT_VALUE, BISHOP_VALUE, ROOK_VALUE, QUEEN_VALUE, QUEEN_VALUE+100, 0};

    template <chess::Color::underlying Pinner>
    static chess::Bitboard remove_pinned(const chess::Board &board, chess::Bitboard occ_pinner,
                                         chess::Bitboard occ_king, chess::Bitboard attackers)
    {
        const chess::Color king_side = ~Pinner;
        chess::Square king_sq = board.kingSq(king_side);

        // generates all squares within (pinner, king)
        auto vpin = chess::movegen::pinMask<Pinner, chess::PieceType::ROOK>(board, king_sq,
                                                                            occ_pinner, occ_king);

        auto dpin = chess::movegen::pinMask<Pinner, chess::PieceType::BISHOP>(board, king_sq,
                                                                              occ_pinner, occ_king);

        auto all_pin = (vpin | dpin);

        if (all_pin)
        {
            assert(!(all_pin.getBits() & (1ull << king_sq.index())));
        }

        // remove attackers in pin
        return attackers & ~all_pin;
    }

    /**
     * Recursive pruning static exchange tester
     * @param position
     * @param move
     * @param threshold
     * @return
     */
    static bool test_ge(chess::Board &position, const chess::Move &move, int32_t threshold)
    {
        if (move.typeOf() != chess::Move::NORMAL)
        {
            return 0 >= threshold;
        }

        chess::Square from = move.from();
        chess::Square to = move.to();
        chess::Piece from_piece = position.at(from);
        chess::Piece to_piece = position.at(to);

        int32_t swap = PIECE_VALUES[position.at(to).type()] - threshold;
        if (swap < 0)
            return false;

        swap = PIECE_VALUES[position.at(from).type()] - swap;
        if (swap <= 0)
            return true;

        const auto queens = position.pieces(chess::PieceType::QUEEN);
        const auto rooks = position.pieces(chess::PieceType::ROOK);
        const auto bishops = position.pieces(chess::PieceType::BISHOP);
        const auto knights = position.pieces(chess::PieceType::KNIGHT);
        const auto pawns = position.pieces(chess::PieceType::PAWN);
        const chess::Bitboard occs[2] = {position.us(chess::Color::WHITE),
                                         position.us(chess::Color::BLACK)};

        auto old_pieces = position.pieces_bb_;
        auto old_occ = position.occ_bb_;
        auto old_board = position.board_;
        auto remove_piece = [&](chess::Square s) {
            assert(position.at(s) != chess::Piece::NONE);
            position.removePiece(position.at(s), s);
        };

        remove_piece(from);
        if (to_piece != chess::Piece::NONE)
            remove_piece(to);

        chess::Bitboard occ = position.occ();
        chess::Color stm = position.sideToMove();
        chess::Bitboard attackers = (chess::attacks::attackers(position, chess::Color::WHITE, to) |
                                     chess::attacks::attackers(position, chess::Color::BLACK, to)) &
                                    occ;

        position.pieces_bb_ = old_pieces;
        position.occ_bb_ = old_occ;
        position.board_ = old_board;

        chess::Bitboard stm_attackers;
        chess::Bitboard bb;
        int res = 1;

        while (true)
        {
            stm = ~stm;
            attackers &= occ;

            stm_attackers = attackers & occs[stm];
            if (!stm_attackers)
                break;

            res ^= 1;

            if ((bb = stm_attackers & pawns))
            {
                if ((swap = PAWN_VALUE - swap) < res)
                    break;

                occ.clear(bb.lsb());
                attackers |= chess::attacks::bishop(to, occ) & (bishops | queens);
            }
            else if ((bb = stm_attackers & knights))
            {
                if ((swap = KNIGHT_VALUE - swap) < res)
                    break;

                occ.clear(bb.lsb());
            }
            else if ((bb = stm_attackers & bishops))
            {
                if ((swap = BISHOP_VALUE - swap) < res)
                    break;

                occ.clear(bb.lsb());
                attackers |= chess::attacks::bishop(to, occ) & (bishops | queens);
            }
            else if ((bb = stm_attackers & rooks))
            {
                if ((swap = ROOK_VALUE - swap) < res)
                    break;

                occ.clear(bb.lsb());
                attackers |= chess::attacks::rook(to, occ) & (rooks | queens);
            }
            else if ((bb = stm_attackers & queens))
            {
                if ((swap = QUEEN_VALUE - swap) < res)
                    break;

                occ.clear(bb.lsb());
                attackers |= (chess::attacks::bishop(to, occ) & (bishops | queens)) |
                             (chess::attacks::rook(to, occ) & (rooks | queens));
            }
            else
            {
                // king
                return (attackers & occs[~stm]) ? res ^ 1 : res;
            }
        }

        return static_cast<bool>(res);
    }
};
