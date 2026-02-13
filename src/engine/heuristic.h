#pragma once

template <typename I, I LIMIT> struct history_entry
{
    I value = 0;

    I get_value() const
    {
        return value;
    }

    void add_bonus(I bonus)
    {
        I clamped_bonus = helper::clamp(bonus, -LIMIT, LIMIT);
        value += clamped_bonus - value * std::abs(clamped_bonus) / LIMIT;
    }

    void decay()
    {
        value /= 8;
    }
};

using history_heuristic = history_entry<int16_t, 20000>[2][64][64];
using capture_heuristic = history_entry<int16_t, 20000>[12][64][7];
using killer_heuristic =
    std::array<std::pair<chess::Move, bool>, param::NUMBER_KILLERS>[param::MAX_DEPTH];
using counter_moves = chess::Move[12][64];

constexpr int LOW_PLY = 5;
using low_ply_history = history_entry<int16_t, 20000>[2][LOW_PLY][64][64];

// indexed by [piece][to]
using continuation_history = history_entry<int16_t, 20000>[12][64];
using continuation_history_full = history_entry<int16_t, 20000>[12][64][12][64];
constexpr int NUM_CONTINUATION = 2;

constexpr int PAWN_STRUCTURE_SIZE = 1 << 13;
using pawn_history = history_entry<int16_t, 20000>[PAWN_STRUCTURE_SIZE][12][64];

struct heuristics
{
    history_heuristic main_history;
    capture_heuristic capture_history;
    killer_heuristic killers;
    counter_moves counter;
    low_ply_history low_ply;
    continuation_history_full continuation;
    pawn_history pawn;

    heuristics()
        : main_history{}, capture_history{}, killers{}, counter{}, low_ply{}, continuation{}, pawn{}
    {
    }

    bool is_capture(const chess::Board &position, const chess::Move &move) const
    {
        return position.isCapture(move) || (move.typeOf() == chess::Move::PROMOTION &&
                                            move.promotionType() == chess::PieceType::QUEEN);
    }

    chess::PieceType get_capture(const chess::Board &position, const chess::Move &move) const
    {
        if (move.typeOf() == chess::Move::ENPASSANT)
            return chess::PieceType::PAWN;

        return position.at(move.to()).type();
    }

    void update_main_history(const chess::Board &position, const chess::Move &move, int32_t ply,
                             int16_t bonus)
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
        pawn[position.pieces(chess::PieceType::PAWN).getBits() % PAWN_STRUCTURE_SIZE]
            [position.at(move.from())][move.to().index()]
                .add_bonus(bonus);
    }

    void update_capture_history(const chess::Board &position, const chess::Move &move,
                                int16_t bonus)
    {
        capture_history[position.at(move.from())][move.to().index()][get_capture(position, move)]
            .add_bonus(bonus);
    }

    void store_killer(const chess::Move &killer, int32_t ply, bool is_mate)
    {
        if (killers[ply][0].first != killer)
        {
            killers[ply][1] = killers[ply][0];
            killers[ply][0] = {killer, is_mate};
        }
    }

    // void incr_counter(const chess::Board &position, const chess::Move &prev_move,
    //                   const chess::Move &move)
    // {
    //     if (is_quiet(position, move) && prev_move != chess::Move::NO_MOVE &&
    //         position.at(prev_move.to()) != chess::Piece::NONE)
    //     {
    //         counter[position.at(prev_move.to())][prev_move.to().index()] = move;
    //     }
    // }

    void begin()
    {
        // for (auto &a : main_history)
        //     for (auto &b : a)
        //         for (auto &c : b)
        //             c.decay();
        //
        // for (auto &a : capture_history)
        //     for (auto &b : a)
        //         for (auto &c : b)
        //             c.decay();

        // for (auto &a : continuation)
        //     for (auto &b : a)
        //         for (auto &c : b)
        //             for (auto &d : c)
        //                 d.decay();
        //
        // for (auto &k : killers)
        // {
        //     for (auto &n : k)
        //     {
        //         n = {chess::Move::NO_MOVE, false};
        //     }
        // }
    }
};