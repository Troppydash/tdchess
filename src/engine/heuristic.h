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
        value += clamped_bonus - static_cast<int32_t>(value) * std::abs(clamped_bonus) / LIMIT;
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
using continuation_history_full = history_entry<int16_t, 20000>[2][2][13][64][12][64];
constexpr int NUM_CONTINUATION = 2;

constexpr int PAWN_STRUCTURE_SIZE = 1 << 13;
constexpr int PAWN_STRUCTURE_SIZE_M1 = PAWN_STRUCTURE_SIZE - 1;
using pawn_history = history_entry<int16_t, 20000>[PAWN_STRUCTURE_SIZE][12][64];
using pawn_correction_history = history_entry<int16_t, 8000>[2][PAWN_STRUCTURE_SIZE];

constexpr int NON_PAWN_SIZE = 1 << 13;
constexpr int NON_PAWN_SIZE_M1 = NON_PAWN_SIZE - 1;
using non_pawn_correction_history = history_entry<int16_t, 8000>[2][NON_PAWN_SIZE];

struct heuristics
{
    history_heuristic main_history;
    capture_heuristic capture_history;
    killer_heuristic killers;
    low_ply_history low_ply;
    continuation_history_full continuation;
    pawn_history pawn;
    pawn_correction_history correction_history;
    non_pawn_correction_history white_corrhist;
    non_pawn_correction_history black_corrhist;

    heuristics()
        : main_history{}, capture_history{}, killers{}, low_ply{}, continuation{}, pawn{},
          correction_history{}, white_corrhist{}, black_corrhist{}
    {
    }

    bool is_capture(const chess::Board &position, const chess::Move &move) const
    {
        return position.isCapture(move) || (move.typeOf() == chess::Move::PROMOTION &&
                                            (move.promotionType() == chess::PieceType::QUEEN ||
                                             move.promotionType() == chess::PieceType::KNIGHT));
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
        pawn[get_pawn_key(position) & PAWN_STRUCTURE_SIZE_M1][position.at(move.from())]
            [move.to().index()]
                .add_bonus(bonus);
    }

    [[nodiscard]] uint64_t get_pawn_key(const chess::Board &position) const
    {
        auto pieces = position.pieces(chess::PieceType::PAWN);
        uint64_t pawn_key = 0;
        while (pieces)
        {
            const chess::Square sq = pieces.pop();
            pawn_key ^= chess::Zobrist::piece(position.at(sq), sq);
        }

        return pawn_key;
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

    void update_corr_hist_score(const chess::Board &position, int bonus)
    {
        correction_history[position.sideToMove()][get_pawn_key(position) & PAWN_STRUCTURE_SIZE_M1]
            .add_bonus(bonus);
        white_corrhist[position.sideToMove()]
                      [get_corrhist_key(position, chess::Color::WHITE) & NON_PAWN_SIZE_M1]
                          .add_bonus(bonus);
        black_corrhist[position.sideToMove()]
                      [get_corrhist_key(position, chess::Color::BLACK) & NON_PAWN_SIZE_M1]
                          .add_bonus(bonus);
    }

    uint64_t get_corrhist_key(const chess::Board &position, chess::Color color) const
    {
        auto pieces = position.us(color);
        auto pawns = position.pieces(chess::PieceType::PAWN);
        pieces = pieces & ~pawns;

        uint64_t key = 0;
        while (pieces)
        {
            const chess::Square sq = pieces.pop();
            key ^= chess::Zobrist::piece(position.at(sq), sq);
        }

        return key;
    }

    void begin()
    {
    }
};