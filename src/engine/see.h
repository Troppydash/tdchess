#pragma once

#include "../hpplib/chess.h"
#include <array>

struct see
{
    static constexpr int16_t PAWN_VALUE = 100;
    static constexpr int16_t KNIGHT_VALUE = 370;
    static constexpr int16_t BISHOP_VALUE = 390;
    static constexpr int16_t ROOK_VALUE = 610;
    static constexpr int16_t QUEEN_VALUE = 1210;

    constexpr static std::array<int16_t, 7> PIECE_VALUES = {
        // pawn, knight, bishop, rook, queen, king, none
        PAWN_VALUE, KNIGHT_VALUE, BISHOP_VALUE, ROOK_VALUE, QUEEN_VALUE, 0, 0};

    constexpr static std::array<int16_t, 7> PROMOTION_PIECE_VALUES = {
        // pawn, knight, bishop, rook, queen, king, none
        QUEEN_VALUE, KNIGHT_VALUE, BISHOP_VALUE, ROOK_VALUE, QUEEN_VALUE, 0, 0};

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

        std::pair<chess::Piece, chess::Square> removed[position.occ().count()];
        int n_removed = 0;

        auto remove_piece = [&](chess::Square s) {
            assert(position.at(s) != chess::Piece::NONE);
            removed[n_removed++] = {position.at(s), s};
            position.removePiece(position.at(s), s);
        };

        if (from_piece != chess::Piece::NONE)
            remove_piece(from);
        if (to_piece != chess::Piece::NONE)
            remove_piece(to);

        chess::Bitboard occ = position.occ();
        chess::Color stm = position.sideToMove();
        chess::Bitboard attackers = (chess::attacks::attackers(position, chess::Color::WHITE, to) |
                                     chess::attacks::attackers(position, chess::Color::BLACK, to));

        chess::Bitboard stm_attackers;
        chess::Bitboard bb;
        int res = 1;

        while (true)
        {
            stm = ~stm;
            attackers &= occ;

            stm_attackers = attackers & position.us(stm);
            if (!stm_attackers)
                break;

            // remove opp pinned pieces
            // TODO: verify that this is correct
            if (stm == chess::Color::WHITE)
                stm_attackers = remove_pinned<chess::Color::BLACK>(
                    position, occ & position.us(~stm), occ & position.us(stm), stm_attackers);
            else
                stm_attackers = remove_pinned<chess::Color::WHITE>(
                    position, occ & position.us(~stm), occ & position.us(stm), stm_attackers);

            if (!stm_attackers)
                break;

            res ^= 1;

            if ((bb = stm_attackers & position.pieces(chess::PieceType::PAWN)))
            {
                if ((swap = PAWN_VALUE - swap) < res)
                    break;

                remove_piece(bb.lsb());
                occ.clear(bb.lsb());
                attackers |=
                    chess::attacks::bishop(to, occ) & (position.pieces(chess::PieceType::BISHOP) |
                                                       position.pieces(chess::PieceType::QUEEN));
            }
            else if ((bb = stm_attackers & position.pieces(chess::PieceType::KNIGHT)))
            {
                if ((swap = KNIGHT_VALUE - swap) < res)
                    break;

                remove_piece(bb.lsb());
                occ.clear(bb.lsb());
            }
            else if ((bb = stm_attackers & position.pieces(chess::PieceType::BISHOP)))
            {
                if ((swap = BISHOP_VALUE - swap) < res)
                    break;

                remove_piece(bb.lsb());
                occ.clear(bb.lsb());
                attackers |=
                    chess::attacks::bishop(to, occ) & (position.pieces(chess::PieceType::BISHOP) |
                                                       position.pieces(chess::PieceType::QUEEN));
            }
            else if ((bb = stm_attackers & position.pieces(chess::PieceType::ROOK)))
            {
                if ((swap = ROOK_VALUE - swap) < res)
                    break;

                remove_piece(bb.lsb());
                occ.clear(bb.lsb());
                attackers |=
                    chess::attacks::rook(to, occ) & (position.pieces(chess::PieceType::ROOK) |
                                                     position.pieces(chess::PieceType::QUEEN));
            }
            else if ((bb = stm_attackers & position.pieces(chess::PieceType::QUEEN)))
            {
                if ((swap = QUEEN_VALUE - swap) < res)
                    break;

                remove_piece(bb.lsb());
                occ.clear(bb.lsb());
                attackers |=
                    (chess::attacks::bishop(to, occ) & (position.pieces(chess::PieceType::BISHOP) |
                                                        position.pieces(chess::PieceType::QUEEN))) |
                    (chess::attacks::rook(to, occ) & (position.pieces(chess::PieceType::ROOK) |
                                                      position.pieces(chess::PieceType::QUEEN)));
            }
            else
            {
                for (int i = n_removed - 1; i >= 0; --i)
                    position.placePiece(removed[i].first, removed[i].second);

                // king
                return (attackers & position.them(stm)) ? res ^ 1 : res;
            }
        }

        for (int i = n_removed - 1; i >= 0; --i)
            position.placePiece(removed[i].first, removed[i].second);
        return static_cast<bool>(res);
    }

    static bool test_ge_promote(chess::Board &position, const chess::Move &move, int32_t threshold)
    {
        chess::Square from = move.from();
        chess::Square to = move.to();
        chess::Piece from_piece = position.at(from);
        chess::Piece to_piece = position.at(to);

        int32_t swap = PROMOTION_PIECE_VALUES[position.at(to).type()] - threshold;
        if (swap < 0)
            return false;

        swap = PROMOTION_PIECE_VALUES[position.at(from).type()] - swap;
        if (swap <= 0)
            return true;

        chess::Bitboard fil = chess::Bitboard::fromSquare(from) | chess::Bitboard::fromSquare(to);
        chess::Bitboard occ = position.occ() ^ fil;
        if (from_piece != chess::Piece::NONE)
            position.removePiece(from_piece, from);
        if (to_piece != chess::Piece::NONE)
            position.removePiece(to_piece, to);
        chess::Color stm = position.sideToMove();
        chess::Bitboard attackers = (chess::attacks::attackers(position, chess::Color::WHITE, to) |
                                     chess::attacks::attackers(position, chess::Color::BLACK, to));

        chess::Bitboard stm_attackers;
        chess::Bitboard bb;
        int res = 1;

        while (true)
        {
            stm = ~stm;
            attackers &= occ;

            stm_attackers = attackers & position.us(stm);
            if (!stm_attackers)
                break;

            // remove opp pinned pieces
            if (stm == chess::Color::WHITE)
                stm_attackers = remove_pinned<chess::Color::BLACK>(
                    position, occ & position.us(~stm), occ & position.us(stm), stm_attackers);
            else
                stm_attackers = remove_pinned<chess::Color::WHITE>(
                    position, occ & position.us(~stm), occ & position.us(stm), stm_attackers);

            if (!stm_attackers)
                break;

            res ^= 1;

            if ((bb = stm_attackers & position.pieces(chess::PieceType::KNIGHT)))
            {
                if ((swap = KNIGHT_VALUE - swap) < res)
                    break;

                occ.clear(bb.lsb());
            }
            else if ((bb = stm_attackers & position.pieces(chess::PieceType::BISHOP)))
            {
                if ((swap = BISHOP_VALUE - swap) < res)
                    break;

                occ.clear(bb.lsb());
                attackers |=
                    chess::attacks::bishop(to, occ) & (position.pieces(chess::PieceType::BISHOP) |
                                                       position.pieces(chess::PieceType::QUEEN));
            }
            else if ((bb = stm_attackers & position.pieces(chess::PieceType::ROOK)))
            {
                if ((swap = ROOK_VALUE - swap) < res)
                    break;

                occ.clear(bb.lsb());
                attackers |=
                    chess::attacks::rook(to, occ) & (position.pieces(chess::PieceType::ROOK) |
                                                     position.pieces(chess::PieceType::QUEEN));
            }
            else if ((bb = stm_attackers & position.pieces(chess::PieceType::PAWN)))
            {
                if ((swap = QUEEN_VALUE - swap) < res)
                    break;

                occ.clear(bb.lsb());
                attackers |=
                    chess::attacks::bishop(to, occ) & (position.pieces(chess::PieceType::BISHOP) |
                                                       position.pieces(chess::PieceType::QUEEN));
            }
            else if ((bb = stm_attackers & position.pieces(chess::PieceType::QUEEN)))
            {
                swap = QUEEN_VALUE - swap;
                occ.clear(bb.lsb());

                attackers |=
                    (chess::attacks::bishop(to, occ) & (position.pieces(chess::PieceType::BISHOP) |
                                                        position.pieces(chess::PieceType::QUEEN))) |
                    (chess::attacks::rook(to, occ) & (position.pieces(chess::PieceType::ROOK) |
                                                      position.pieces(chess::PieceType::QUEEN)));
            }
            else
            {
                if (from_piece != chess::Piece::NONE)
                    position.placePiece(from_piece, from);
                if (to_piece != chess::Piece::NONE)
                    position.placePiece(to_piece, to);

                // king
                return (attackers & position.them(stm)) ? res ^ 1 : res;
            }
        }

        if (from_piece != chess::Piece::NONE)
            position.placePiece(from_piece, from);
        if (to_piece != chess::Piece::NONE)
            position.placePiece(to_piece, to);

        return static_cast<bool>(res);
    }
};
