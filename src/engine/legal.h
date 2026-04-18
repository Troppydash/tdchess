#pragma once

#include "chess.h"
#include <cassert>

namespace legal
{

template <chess::Color::underlying c, bool is_capture>
static inline bool is_legal_pawn(const chess::Board &board, const chess::Move move,
                                 chess::Bitboard pin_hv, chess::Bitboard pin_d,
                                 chess::Bitboard checkmask, chess::Bitboard occ_opp)
{
    using namespace chess;

    constexpr auto UP = make_direction(Direction::NORTH, c);
    constexpr auto UP_LEFT = make_direction(Direction::NORTH_WEST, c);
    constexpr auto UP_RIGHT = make_direction(Direction::NORTH_EAST, c);

    constexpr auto RANK_B_PROMO = Rank::rank(Rank::RANK_7, c).bb();
    constexpr auto RANK_PROMO = Rank::rank(Rank::RANK_8, c).bb();
    constexpr auto DOUBLE_PUSH_RANK = Rank::rank(Rank::RANK_3, c).bb();

    const auto pawns = chess::Bitboard::fromSquare(move.from());

    // These pawns can maybe take Left or Right
    const Bitboard pawns_lr = pawns & ~pin_hv;
    const Bitboard unpinned_pawns_lr = pawns_lr & ~pin_d;
    const Bitboard pinned_pawns_lr = pawns_lr & pin_d;

    auto l_pawns = attacks::shift<UP_LEFT>(unpinned_pawns_lr) |
                   (attacks::shift<UP_LEFT>(pinned_pawns_lr) & pin_d);
    auto r_pawns = attacks::shift<UP_RIGHT>(unpinned_pawns_lr) |
                   (attacks::shift<UP_RIGHT>(pinned_pawns_lr) & pin_d);

    // Prune moves that don't capture a piece and are not on the checkmask.
    l_pawns &= occ_opp & checkmask;
    r_pawns &= occ_opp & checkmask;

    // These pawns can walk Forward
    const auto pawns_hv = pawns & ~pin_d;

    const auto pawns_pinned_hv = pawns_hv & pin_hv;
    const auto pawns_unpinned_hv = pawns_hv & ~pin_hv;

    // Prune moves that are blocked by a piece
    const auto single_push_unpinned = attacks::shift<UP>(pawns_unpinned_hv) & ~board.occ();
    const auto single_push_pinned = attacks::shift<UP>(pawns_pinned_hv) & pin_hv & ~board.occ();

    // Prune moves that are not on the checkmask.
    Bitboard single_push = (single_push_unpinned | single_push_pinned) & checkmask;

    auto to = Bitboard::fromSquare(move.to());

    if (move.typeOf() == Move::PROMOTION)
    {
        if (pawns & RANK_B_PROMO)
        {
            Bitboard promo_left = l_pawns & RANK_PROMO;
            Bitboard promo_right = r_pawns & RANK_PROMO;
            Bitboard promo_push = single_push & RANK_PROMO;

            if constexpr (is_capture)
                if (promo_left & to)
                    return true;

            if constexpr (is_capture)
                if (promo_right & to)
                    return true;

            if (promo_push & to)
                return true;
        }

        return false;
    }

    if (move.typeOf() == chess::Move::ENPASSANT)
    {
        const Square ep = board.enpassantSq();
        if (ep != Square::NO_SQ)
        {
            auto m = chess::movegen::generateEPMove(board, checkmask, pin_d, pawns_lr, ep, c);
            for (const auto &move_ : m)
            {
                if (move == move_)
                    return true;
            }
        }

        return false;
    }

    Bitboard double_push =
        ((attacks::shift<UP>(single_push_unpinned & DOUBLE_PUSH_RANK) & ~board.occ()) |
         (attacks::shift<UP>(single_push_pinned & DOUBLE_PUSH_RANK) & ~board.occ())) &
        checkmask;

    single_push &= ~RANK_PROMO;
    l_pawns &= ~RANK_PROMO;
    r_pawns &= ~RANK_PROMO;

    if constexpr (is_capture)
        if (l_pawns & to)
            return true;

    if constexpr (is_capture)
        if (r_pawns & to)
            return true;

    if (single_push & to)
        return true;

    if (double_push & to)
        return true;

    return false;
}

template <chess::Color::underlying c>
static inline bool is_legal(const chess::Board &board, const chess::Move move)
{
    using namespace chess;

    // variables
    const auto piece = board.at(move.from()).type();
    const Bitboard from = chess::Bitboard::fromSquare(move.from());
    const Bitboard to = chess::Bitboard::fromSquare(move.to());

    if (move.typeOf() == chess::Move::CASTLING)
    {
        if (piece != chess::PieceType::KING)
            return false;
    }
    else if (move.typeOf() == chess::Move::ENPASSANT || move.typeOf() == chess::Move::PROMOTION)
    {
        if (piece != PieceType::PAWN)
            return false;
    }

    const auto king_sq = board.kingSq(c);

    Bitboard occ_us = board.us(c);
    Bitboard occ_opp = board.us(~c);
    Bitboard occ_all = occ_us | occ_opp;
    Bitboard opp_empty = ~occ_us;
    const auto [checkmask, checks] = chess::movegen::checkMask<c>(board, king_sq);
    const auto pin_hv =
        chess::movegen::pinMask<c, PieceType::ROOK>(board, king_sq, occ_opp, occ_us);
    const auto pin_d =
        chess::movegen::pinMask<c, PieceType::BISHOP>(board, king_sq, occ_opp, occ_us);

    Bitboard movable_square = ~occ_all;
    auto captured = board.at(move.to());
    if (captured != chess::Piece::NONE)
        movable_square = occ_opp;

    if (piece == chess::PieceType::KING)
    {
        Bitboard seen = chess::movegen::seenSquares<~c>(board, opp_empty);

        if (move.typeOf() == chess::Move::CASTLING)
        {
            if (checks != 0)
                return false;

            Bitboard moves = chess::movegen::generateCastleMoves<c>(board, king_sq, seen, pin_hv);
            return bool(moves & to);
        }

        auto moves = chess::movegen::generateKingMoves(king_sq, seen, movable_square);
        return bool(to & moves);
    }

    if (checks == 2)
        return false;

    movable_square &= checkmask;
    if (piece == PieceType::PAWN)
    {
        bool is_capture = board.at(move.to()) != chess::Piece::NONE;
        if (board.sideToMove() == chess::Color::WHITE)
        {
            if (is_capture)
                return is_legal_pawn<Color::WHITE, true>(board, move, pin_hv, pin_d, checkmask,
                                                         occ_opp);
            return is_legal_pawn<Color::WHITE, false>(board, move, pin_hv, pin_d, checkmask,
                                                      occ_opp);
        }

        if (is_capture)
            return is_legal_pawn<Color::BLACK, true>(board, move, pin_hv, pin_d, checkmask,
                                                     occ_opp);
        return is_legal_pawn<Color::BLACK, false>(board, move, pin_hv, pin_d, checkmask, occ_opp);
    }

    if (!(to & movable_square))
        return false;

    if (piece == PieceType::KNIGHT)
    {
        if (from & (pin_d | pin_hv))
            return false;

        auto moves = chess::movegen::generateKnightMoves(move.from());
        return bool(to & moves);
    }

    if (piece == PieceType::BISHOP)
    {
        if (from & pin_hv)
            return false;

        auto moves = chess::movegen::generateBishopMoves(move.from(), pin_d, occ_all);
        return bool(to & moves);
    }

    if (piece == PieceType::ROOK)
    {
        if (from & pin_d)
            return false;

        auto moves = chess::movegen::generateRookMoves(move.from(), pin_hv, occ_all);
        return bool(to & moves);
    }

    if (piece == PieceType::QUEEN)
    {
        if (from & (pin_d & pin_hv))
            return false;

        auto moves = chess::movegen::generateQueenMoves(move.from(), pin_d, pin_hv, occ_all);
        return bool(to & moves);
    }

    std::cout << "impossible\n";
    exit(0);
}

inline bool is_legal(const chess::Board &board, const chess::Move move)
{
    if (board.sideToMove() == chess::Color::WHITE)
        return is_legal<chess::Color::WHITE>(board, move);
    return is_legal<chess::Color::BLACK>(board, move);
}

inline bool is_legal_full(const chess::Board &board, const chess::Move move)
{
    if (move == chess::Move::NO_MOVE)
        return true;

    // chess::Movelist list;
    // chess::movegen::legalmoves(list, board);
    // return std::find(list.begin(), list.end(), move) != list.end();
    
    // auto moved_piece = board.at(move.from());

    // // from square check
    // if (moved_piece == chess::Piece::NONE || moved_piece.color() != board.sideToMove())
    //     return false;

    // if (move.typeOf() != chess::Move::CASTLING)
    //     // end friendly piece check
    //     if (board.at(move.to()).color() == board.sideToMove())
    //         return false;
    
    return chess::movegen::isLegal(board, move);
    // return is_legal(board, move);
}
} // namespace legal