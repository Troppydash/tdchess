#pragma once

#include "../helper.h"
#include "chess.h"
#include "param.h"
#include <cinttypes>

template <typename I, I LIMIT> struct history_entry
{
    I value = 0;

    I get_value() const
    {
        return value;
    }

    void add_bonus(int bonus)
    {
        I clamped_bonus = helper::clamp(bonus, -LIMIT, LIMIT);
        value += clamped_bonus - static_cast<int32_t>(value) * std::abs(clamped_bonus) / LIMIT;
    }

    void decay()
    {
        value = value * 7 / 8;
    }
};

using history_heuristic = history_entry<int16_t, 20000>[2][64][64];
using capture_heuristic = history_entry<int16_t, 20000>[12][64][7];
using killer_heuristic =
    std::array<std::pair<chess::Move, bool>, param::NUMBER_KILLERS>[param::MAX_DEPTH];

constexpr int LOW_PLY = 6;
using low_ply_history = history_entry<int16_t, 20000>[2][LOW_PLY][64][64];

// indexed by [piece][to]
using continuation_history = history_entry<int16_t, 8000>[12][64];
using continuation_history_full = history_entry<int16_t, 8000>[2][2][13][64][12][64];
constexpr int NUM_CONTINUATION = 6;

constexpr int CORRECTION_LIMIT = 4096;
constexpr int PAWN_STRUCTURE_SIZE = 1 << 13;
constexpr int PAWN_STRUCTURE_SIZE_M1 = PAWN_STRUCTURE_SIZE - 1;
using pawn_history = history_entry<int16_t, 20000>[PAWN_STRUCTURE_SIZE][12][64];

constexpr int NON_PAWN_SIZE = 1 << 13;
constexpr int NON_PAWN_SIZE_M1 = NON_PAWN_SIZE - 1;
using pawn_correction_history = history_entry<int16_t, CORRECTION_LIMIT>[2][NON_PAWN_SIZE];
using non_pawn_correction_history = history_entry<int16_t, CORRECTION_LIMIT>[2][NON_PAWN_SIZE];

using continuation_correction_history = history_entry<int16_t, CORRECTION_LIMIT>[12][64];
using continuation_correction_history_full =
    history_entry<int16_t, CORRECTION_LIMIT>[13][64][12][64];

using countermove_history = chess::Move[12][64];

struct heuristics
{
    history_heuristic main_history;
    capture_heuristic capture_history;
    killer_heuristic killers;
    low_ply_history low_ply;
    continuation_history_full continuation;
    pawn_history pawn;

    countermove_history counter;

    // correction history
    pawn_correction_history correction_history;
    non_pawn_correction_history white_corrhist;
    non_pawn_correction_history black_corrhist;
    continuation_correction_history_full cont_corr;

    // king_history king;

    heuristics()
        : main_history{}, capture_history{}, killers{}, low_ply{}, continuation{}, pawn{},
          counter{}, correction_history{}, white_corrhist{}, black_corrhist{}, cont_corr{}
    // king{}
    {
    }

    bool is_capture(const chess::Board &position, const chess::Move &move) const
    {
        return position.isCapture(move) || (move.typeOf() == chess::Move::PROMOTION &&
                                            (move.promotionType() == chess::PieceType::QUEEN));
    }

    chess::PieceType get_capture(const chess::Board &position, const chess::Move &move) const
    {
        if (move.typeOf() == chess::Move::ENPASSANT)
            return chess::PieceType::PAWN;

        if (move.typeOf() == chess::Move::PROMOTION)
        {
            if (position.at(move.to()).type() != chess::PieceType::NONE)
                return position.at(move.to()).type();

            return chess::PieceType::PAWN;
        }

        return position.at(move.to()).type();
    }

    void update_main_history(const chess::Board &position, const chess::Move &move, int32_t ply,
                             uint64_t pawn_key, int bonus)
    {
        // update lowply
        if (ply < LOW_PLY)
        {
            low_ply[position.sideToMove()][ply][move.from().index()][move.to().index()].add_bonus(
                bonus);
        }

        // update main
        main_history[position.sideToMove()][move.from().index()][move.to().index()].add_bonus(
            bonus);

        // update pawn history
        pawn[pawn_key & PAWN_STRUCTURE_SIZE_M1][position.at(move.from())][move.to().index()]
            .add_bonus(bonus);

        // king[get_king_bucket(position, chess::Color::WHITE)][get_king_bucket(
        //     position, chess::Color::BLACK)][position.at(move.from())][move.to().index()]
        //     .add_bonus(bonus);
    }

    static constexpr chess::Piece get_prev_piece(const chess::Board &position, chess::Move move)
    {
        if (move.typeOf() == chess::Move::NORMAL || move.typeOf() == chess::Move::ENPASSANT)
            return position.at(move.to());

        if (move.typeOf() == chess::Move::CASTLING)
            return chess::Piece{~position.sideToMove(), chess::PieceType::KING};

        if (move.typeOf() == chess::Move::PROMOTION)
            return chess::Piece{~position.sideToMove(), chess::PieceType::PAWN};

        std::cout << "invalid move\n";
        exit(0);
    }

    static constexpr chess::Piece get_prev_piece_threat(const chess::Board &position,
                                                        chess::Move move)
    {
        if (move.typeOf() == chess::Move::NORMAL || move.typeOf() == chess::Move::ENPASSANT)
            return position.at(move.to());

        if (move.typeOf() == chess::Move::CASTLING)
            return chess::Piece{~position.sideToMove(), chess::PieceType::ROOK};

        if (move.typeOf() == chess::Move::PROMOTION)
            return position.at(move.to());

        std::cout << "invalid move\n";
        exit(0);
    }

    // clang-format off
    constexpr static int KING_BUCKET[64] = {
        0,0,1,1,2,2,3,3,
        0,0,1,1,2,2,3,3,
        4,4,5,5,6,6,7,7,
        4,4,5,5,6,6,7,7,
        8,8,9,9,10,10,11,11,
        8,8,9,9,10,10,11,11,
        12,12,13,13,14,14,15,15,
        12,12,13,13,14,14,15,15
    };
    // clang-format on

    int get_king_bucket(const chess::Board &pos, chess::Color c) const
    {
        return KING_BUCKET[pos.kingSq(c).index()];
    }

    void update_capture_history(const chess::Board &position, const chess::Move &move, int bonus)
    {
        capture_history[position.at(move.from())][move.to().index()][get_capture(position, move)]
            .add_bonus(bonus);
    }

    void store_killer(const chess::Move &killer, int32_t ply, bool is_mate)
    {
        if (killers[ply][0].first != killer)
        {
            killers[ply][1] = killers[ply][0];
            killers[ply][0].first = killer;
        }
    }

    void update_corr_hist_score(const chess::Board &position, uint64_t pawn_key, uint64_t white_key,
                                uint64_t black_key, int bonus)
    {
        correction_history[position.sideToMove()][pawn_key & NON_PAWN_SIZE_M1].add_bonus(bonus);
        white_corrhist[position.sideToMove()][white_key & NON_PAWN_SIZE_M1].add_bonus(bonus);
        black_corrhist[position.sideToMove()][black_key & NON_PAWN_SIZE_M1].add_bonus(bonus);
    }

    void begin()
    {
        // for (auto &side : correction_history)
        //     for (auto &entry : side)
        //         entry.decay();
        //
        // for (auto &side : white_corrhist)
        //     for (auto &entry : side)
        //         entry.decay();
        //
        // for (auto &side : black_corrhist)
        //     for (auto &entry : side)
        //         entry.decay();
        //
        // for (auto &a : cont_corr)
        //     for (auto &b : a)
        //         for (auto &c : b)
        //             for (auto &entry : c)
        //                 entry.decay();

        for (auto &killer : killers)
            for (auto &k : killer)
                k.first = chess::Move::NO_MOVE;

        for (auto &a : low_ply)
            for (auto &b : a)
                for (auto &c : b)
                    for (auto &entry : c)
                        entry.value = 0;
    }
};
