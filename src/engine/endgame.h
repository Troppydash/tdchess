#pragma once

#include "../hpplib/chess.h"
#include "../../lib/Fathom/src/tbprobe.h"
#include "param.h"

struct endgame_table
{
    explicit endgame_table() {}

    bool load_file(const std::string &path)
    {
        bool success = tb_init(path.c_str());
        if (!success)
        {
            std::cout << "info failed to load database\n";
        }

        return success;
    }

    bool is_stored(const chess::Board &position)
    {
        int pieces = position.occ().count();
        return 3 <= pieces && pieces <= 5 &&
               !(position.castlingRights().has(chess::Color::WHITE) ||
                 position.castlingRights().has(chess::Color::BLACK));
    }

    std::pair<std::vector<chess::Move>, int32_t> probe_dtm(const chess::Board &reference)
    {
        chess::Board position{reference};
        std::vector<chess::Move> pv_line;

        int16_t ply = 0;
        auto [_, wdl] = probe_dtz(position);

        while (true)
        {
            auto [reason, status] = position.isGameOver();
            if (status != chess::GameResult::NONE)
                break;

            auto [move, _] = probe_dtz(position);
            pv_line.push_back(move);
            ply += 1;

            position.makeMove(move);
        }

        int32_t score = 0;
        switch (wdl)
        {
        case TB_WIN:
            score = param::INF - ply;
            break;
        case TB_CURSED_WIN:
        case TB_DRAW:
        case TB_BLESSED_LOSS:
            score = 0;
            break;
        case TB_LOSS:
            score = -param::INF + ply;
            break;
        default:
            throw std::runtime_error{"impossible wdl"};
        }

        return {pv_line, score};
    }

    std::pair<chess::Move, int> probe_dtz(const chess::Board &position)
    {
        unsigned ep = position.enpassantSq() == chess::Square::NO_SQ ? 0 : position.enpassantSq().index();
        unsigned result = tb_probe_root(
            position.us(chess::Color::WHITE).getBits(), position.us(chess::Color::BLACK).getBits(),
            position.pieces(chess::PieceType::KING).getBits(), position.pieces(chess::PieceType::QUEEN).getBits(),
            position.pieces(chess::PieceType::ROOK).getBits(), position.pieces(chess::PieceType::BISHOP).getBits(),
            position.pieces(chess::PieceType::KNIGHT).getBits(), position.pieces(chess::PieceType::PAWN).getBits(),
            position.halfMoveClock(), 0, ep, position.sideToMove() == chess::Color::WHITE, nullptr);

        if (result == TB_RESULT_FAILED || result == TB_RESULT_STALEMATE || result == TB_RESULT_CHECKMATE)
        {
            std::cout << "info failed probe";
            std::cout << position << std::endl;
            throw std::runtime_error("failed probe");
        }

        int wdl = TB_GET_WDL(result);
        int from = TB_GET_FROM(result);
        int to = TB_GET_TO(result);
        int promotes = TB_GET_PROMOTES(result);
        int ep_ = TB_GET_EP(result);

        chess::Movelist moves;
        chess::movegen::legalmoves(moves, position);
        for (auto &m : moves)
        {
            if (m.from().index() == from && m.to().index() == to)
            {
                if (m.typeOf() == chess::Move::PROMOTION)
                {
                    if (m.promotionType() == promotes)
                        return {m, wdl};
                }
                else if (m.typeOf() == chess::Move::ENPASSANT)
                {
                    if (ep_ == m.to().index())
                        return {m, wdl};
                }
                else
                {
                    return {m, wdl};
                }
            }
        }

        throw std::runtime_error{"impossible"};
    }

    int32_t probe_wdl(const chess::Board &position, int16_t ply)
    {
        unsigned ep = position.enpassantSq() == chess::Square::NO_SQ ? 0 : position.enpassantSq().index();
        unsigned result = tb_probe_wdl(
            position.us(chess::Color::WHITE).getBits(), position.us(chess::Color::BLACK).getBits(),
            position.pieces(chess::PieceType::KING).getBits(), position.pieces(chess::PieceType::QUEEN).getBits(),
            position.pieces(chess::PieceType::ROOK).getBits(), position.pieces(chess::PieceType::BISHOP).getBits(),
            position.pieces(chess::PieceType::KNIGHT).getBits(), position.pieces(chess::PieceType::PAWN).getBits(), 0,
            0, ep, position.sideToMove() == chess::Color::WHITE);

        switch (result)
        {
        case TB_LOSS:
            return -param::SYZYGY + ply;
        case TB_BLESSED_LOSS:
            return -param::SYZYGY50;
        case TB_DRAW:
            return 0;
        case TB_CURSED_WIN:
            return param::SYZYGY50;
        case TB_WIN:
            return param::SYZYGY - ply;
        }

        if (result == TB_RESULT_FAILED)
        {
            std::cout << "info failed probe";
            std::cout << position << std::endl;
            throw std::runtime_error("failed probe");
        }

        throw std::runtime_error("impossible value");
    }

    virtual ~endgame_table()
    {
        tb_free();
    }
};
