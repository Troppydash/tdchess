#pragma once
#include "../hpplib/chess.h"
#include <cassert>

namespace legal
{

template <chess::Color::underlying c>
inline bool is_legal(const chess::Board &board, const chess::Move move)
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
        // TODO: better
        Movelist moves;
        if (board.isCapture(move))
        {
            chess::movegen::generatePawnMoves<c, chess::movegen::MoveGenType::CAPTURE>(
                board, moves, pin_d, pin_hv, checkmask, occ_opp);
        }
        else
        {
            chess::movegen::generatePawnMoves<c, chess::movegen::MoveGenType::QUIET>(
                board, moves, pin_d, pin_hv, checkmask, occ_opp);
        }

        return std::find(moves.begin(), moves.end(), move) != moves.end();
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

    assert(false);
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

    auto moved_piece = board.at(move.from());

    // from square check
    if (moved_piece == chess::Piece::NONE || moved_piece.color() != board.sideToMove())
        return false;

    if (move.typeOf() != chess::Move::CASTLING)
        // end friendly piece check
        if (board.at(move.to()).color() == board.sideToMove())
            return false;

    return is_legal(board, move);
}
} // namespace legal