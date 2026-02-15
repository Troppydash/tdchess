#pragma once

#include "../../lib/Fathom/src/tbprobe.h"
#include "../hpplib/chess.h"
#include "param.h"
#include "timer.h"

struct tb_cache_entry
{
    uint64_t hash = 0;
    int16_t score = 0;
    int16_t move50 = 0;
};

constexpr int TB_MASK_BITS = 16;
constexpr uint64_t TB_ENTRIES = 1 << TB_MASK_BITS; // 1mb
constexpr uint64_t TB_MASK = TB_ENTRIES - 1;

struct endgame_table
{
    std::vector<tb_cache_entry> m_entries;

    explicit endgame_table() : m_entries(TB_ENTRIES)
    {
    }

    bool load_file(const std::string &path)
    {
        bool success = tb_init(path.c_str());
        if (!success)
        {
            std::cout << "info failed to load database\n";
        }

        return success;
    }

    bool is_stored(const chess::Board &position) const
    {
        int pieces = position.occ().count();
        return 3 <= pieces && pieces <= 5 &&
               !(position.castlingRights().has(chess::Color::WHITE) ||
                 position.castlingRights().has(chess::Color::BLACK));
    }

    std::pair<std::vector<chess::Move>, int32_t> probe_dtm(const chess::Board &reference,
                                                           timer &timer)
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

            // timer check in case long position, here to ensure that at least one pv is found
            if (ply % 2 == 0)
            {
                timer.check();
                if (timer.is_stopped())
                    break;
            }

            // hack to prevent timer overrun
            if (ply > 4)
                break;
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
        unsigned ep =
            position.enpassantSq() == chess::Square::NO_SQ ? 0 : position.enpassantSq().index();
        unsigned result = tb_probe_root(
            position.us(chess::Color::WHITE).getBits(), position.us(chess::Color::BLACK).getBits(),
            position.pieces(chess::PieceType::KING).getBits(),
            position.pieces(chess::PieceType::QUEEN).getBits(),
            position.pieces(chess::PieceType::ROOK).getBits(),
            position.pieces(chess::PieceType::BISHOP).getBits(),
            position.pieces(chess::PieceType::KNIGHT).getBits(),
            position.pieces(chess::PieceType::PAWN).getBits(), position.halfMoveClock(), 0, ep,
            position.sideToMove() == chess::Color::WHITE, nullptr);

        if (result == TB_RESULT_FAILED || result == TB_RESULT_STALEMATE ||
            result == TB_RESULT_CHECKMATE)
        {
            std::cout << "info failed probe dtz\n";
            std::cout << position << std::endl;
            std::cout << position.getFen() << std::endl;
            throw std::runtime_error("failed probe");
        }

        int wdl = TB_GET_WDL(result);
        int from = TB_GET_FROM(result);
        int to = TB_GET_TO(result);
        int promotes = TB_GET_PROMOTES(result);
        int ep_ = TB_GET_EP(result);

        chess::PieceType promote_type;
        switch (promotes)
        {
        case TB_PROMOTES_QUEEN:
            promote_type = chess::PieceType::QUEEN;
            break;
        case TB_PROMOTES_ROOK:
            promote_type = chess::PieceType::ROOK;
            break;
        case TB_PROMOTES_BISHOP:
            promote_type = chess::PieceType::BISHOP;
            break;
        case TB_PROMOTES_KNIGHT:
            promote_type = chess::PieceType::KNIGHT;
            break;
        default:
            promote_type = chess::PieceType::NONE;
        }

        chess::Movelist moves;
        chess::movegen::legalmoves(moves, position);
        for (auto &m : moves)
        {
            if (m.from().index() == from && m.to().index() == to)
            {
                if (m.typeOf() == chess::Move::PROMOTION)
                {
                    if (m.promotionType() == promote_type)
                        return {m, wdl};
                }
                else if (m.typeOf() == chess::Move::ENPASSANT)
                {
                    return {m, wdl};
                }
                else
                {
                    return {m, wdl};
                }
            }
        }

        std::cout << ep << "," << ep_ << std::endl;
        std::cout << position << std::endl;

        throw std::runtime_error{"impossible"};
    }

    int16_t probe_wdl(const chess::Board &position)
    {
        // technically this is wrong, since we don't have dtz data, but yolo

        tb_cache_entry &cache = m_entries[position.hash() & TB_MASK];
        if (cache.hash == position.hash())
        {
            // assuming the wdl is computed against the dtz clock, fix later if needed

            if (std::abs(cache.score) == 2 && position.halfMoveClock() <= cache.move50)
                return cache.score;

            if (std::abs(cache.score) == 1 && position.halfMoveClock() >= cache.move50)
                return cache.score;

            if (cache.score == 0)
                return cache.score;
        }

        unsigned ep =
            position.enpassantSq() == chess::Square::NO_SQ ? 0 : position.enpassantSq().index();
        unsigned result = tb_probe_wdl(position.us(chess::Color::WHITE).getBits(),
                                       position.us(chess::Color::BLACK).getBits(),
                                       position.pieces(chess::PieceType::KING).getBits(),
                                       position.pieces(chess::PieceType::QUEEN).getBits(),
                                       position.pieces(chess::PieceType::ROOK).getBits(),
                                       position.pieces(chess::PieceType::BISHOP).getBits(),
                                       position.pieces(chess::PieceType::KNIGHT).getBits(),
                                       position.pieces(chess::PieceType::PAWN).getBits(), 0, 0, ep,
                                       position.sideToMove() == chess::Color::WHITE);

        if (result == TB_RESULT_FAILED)
        {
            std::cout << "info failed probe wdl\n";
            std::cout << position << std::endl;
            std::cout << position.getFen() << std::endl;
            throw std::runtime_error("failed probe");
        }

        int16_t ret = 0;
        switch (result)
        {
        case TB_LOSS:
            ret = -2;
            break;
        case TB_BLESSED_LOSS:
            ret = -1;
            break;
        case TB_DRAW:
            ret = 0;
            break;
        case TB_CURSED_WIN:
            ret = 1;
            break;
        case TB_WIN:
            ret = 2;
            break;
        default:
            throw std::runtime_error{"impossible wdl value"};
        }


        cache.hash = position.hash();
        cache.score = ret;
        cache.move50 = position.halfMoveClock();

        return ret;
    }

    virtual ~endgame_table()
    {
        tb_free();
    }
};
