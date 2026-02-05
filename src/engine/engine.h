#pragma once
#include <chrono>
#include <utility>

#include "../../lib/Fathom/src/tbprobe.h"
#include "../helper.h"
#include "../hpplib/chess.h"
#include "endgame.h"
#include "evaluation.h"
#include "nnue.h"
#include "param.h"
#include "see.h"
#include "table.h"
#include "time_control.h"
#include "timer.h"

struct search_result
{
    std::vector<chess::Move> pv_line;
    int32_t depth;
    int32_t score;

    [[nodiscard]] std::string get_score_uci() const
    {
        if (score > param::CHECKMATE)
        {
            int32_t ply = param::INF - score;
            int mateIn = (ply / 2) + (ply % 2);
            return std::string{"mate "} + std::to_string(mateIn);
        }

        if (score < -param::CHECKMATE)
        {
            int32_t ply = -param::INF - score;
            int mateIn = (ply / 2) + (ply % 2);
            return std::string{"mate "} + std::to_string(mateIn);
        }

        return std::string{"cp "} + std::to_string(score);
    }

    [[nodiscard]] std::string get_score() const
    {
        if (score > param::CHECKMATE)
        {
            int32_t ply = param::INF - score;
            return std::string{"IN "} + std::to_string(ply) + " ply";
        }

        if (score < -param::CHECKMATE)
        {
            int32_t ply = -param::INF - score;
            return std::string{"In "} + std::to_string(ply) + " ply";
        }

        std::stringstream stream;
        stream << std::fixed << std::setprecision(2) << static_cast<double>(score) / 100;
        return stream.str();
    }
};

struct engine_stats
{
    int32_t nodes_searched;
    int16_t tt_occupancy;
    int32_t sel_depth;
    std::chrono::milliseconds total_time;

    void display_delta(const engine_stats &old, const search_result &result) const
    {
        long delta = (total_time - old.total_time).count();
        uint32_t depth_nps = (nodes_searched - old.nodes_searched) * 1000 / std::max(1L, delta);
        uint32_t nps = nodes_searched * 1000 / std::max(1L, total_time.count());

        printf("info depth %2d, nodes %10d, score %10s (%7d), nps %10d/%10d, moves", result.depth,
               nodes_searched, result.get_score().c_str(), result.score, depth_nps, nps);

        for (auto &m : result.pv_line)
        {
            std::cout << " " << chess::uci::moveToUci(m);
        }
        std::cout << std::endl;
    }

    long get_nps() const
    {
        return static_cast<long>(nodes_searched) * 1000 / std::max(1L, total_time.count());
    }

    void display_uci(const search_result &result) const
    {
        std::cout << "info depth " << result.depth << " seldepth " << sel_depth << " multipv 1"
                  << " score " << result.get_score_uci() << " nodes " << nodes_searched << " nps "
                  << get_nps() << " time " << total_time.count() << " hashfull " << tt_occupancy
                  << " pv";

        for (auto &m : result.pv_line)
        {
            std::cout << " " << chess::uci::moveToUci(m);
        }
        std::cout << std::endl;
    }
};

struct engine_param
{
    // pruning
    int32_t lmr[param::MAX_DEPTH][100];
    std::array<int, 6> lmp_margins;
    int lmr_depth_ration = 4;
    int lmr_move_ratio = 12;
    int window_size = 35;
    int32_t static_null_base_margin = 125;
    int16_t nmp_depth_limit = 2;
    int16_t nmp_piece_count = 7;
    int16_t nmp_depth_base = 4;
    int16_t nmp_depth_multiplier = 6;

    // move ordering
    std::array<std::array<int16_t, 6>, 7> mvv_lva;

    // evaluation
    int32_t tempo = 15;

    explicit engine_param()
    {
        lmp_margins = {0, 8, 12, 16, 20, 24};

        // set lmr
        for (int depth = 1; depth < param::MAX_DEPTH; ++depth)
            for (int move = 1; move < 100; ++move)
            {
                lmr[depth][move] = std::round(0.99 + std::log(depth) * std::log(move) / 3.14);
            }

        // set mvv_lva
        mvv_lva = {
            std::array<int16_t, 6>{15, 14, 13, 12, 11, 10}, // victim Pawn
            {25, 24, 23, 22, 21, 20},                       // victim Knight
            {35, 34, 33, 32, 31, 30},                       // victim Bishop
            {45, 44, 43, 42, 41, 40},                       // victim Rook
            {55, 54, 53, 52, 51, 50},                       // victim Queen
            {0, 0, 0, 0, 0, 0},                             // victim King
            {0, 0, 0, 0, 0, 0},                             // No piece
        };
    }
};

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
};

struct move_ordering
{
    std::array<std::pair<chess::Move, bool>, param::NUMBER_KILLERS> m_killers[param::MAX_DEPTH]{};

    chess::Move m_counter[2][64][64]{};

    history_entry<int16_t, param::MAX_HISTORY> m_main_history[2][64][64]{};

    const engine_param m_param;

    explicit move_ordering(const engine_param &param) : m_param(param)
    {
    }

    template <bool IN_Q>
    void score_moves(const chess::Board &position, chess::Movelist &movelist,
                     const chess::Move &pv_move, const chess::Move &prev_move, int32_t ply)
    {
        for (auto &move : movelist)
        {
            int16_t score = 0;

            // pv
            if (move == pv_move)
            {
                score += param::PV_OFFSET;
                goto done;
            }

            if (move.typeOf() == chess::Move::PROMOTION)
            {
                if (move.promotionType() == chess::PieceType::QUEEN)
                {
                    score +=
                        param::PROMOTION_OFFSET + param::PROMOTION_SCORES[move.promotionType()];
                    assert(score < param::PV_OFFSET && score >= param::PROMOTION_OFFSET);
                }
                else
                {
                    // bad moves are underpromotion
                    score += param::BAD_CAPTURE_OFFSET;
                }
                goto done;
            }

            // captures
            if (position.isCapture(move))
            {
                // mvv lva, victim * 16 - attacker, [15000, 700] max 15000, scaled to [-200, 200]
                double mvv_lva =
                    (see::TRADITIONAL_PIECE_VALUES[position.at(move.to()).type()] * 16 -
                     see::TRADITIONAL_PIECE_VALUES[position.at(move.from()).type()]);

                constexpr double KVALUE =
                    see::TRADITIONAL_PIECE_VALUES[static_cast<uint8_t>(chess::PieceType::KING)];
                constexpr double QVALUE =
                    see::TRADITIONAL_PIECE_VALUES[static_cast<uint8_t>(chess::PieceType::QUEEN)];
                constexpr double PVALUE =
                    see::TRADITIONAL_PIECE_VALUES[static_cast<uint8_t>(chess::PieceType::PAWN)];
                constexpr double MAX = QVALUE * 16 - PVALUE;
                constexpr double MIN = PVALUE * 16 - KVALUE;
                constexpr double MID = (MAX + MIN) / 2.0;
                constexpr double SCALE = (MAX - MIN);

                // from [-0.5, 0.5] to [-200, 200]
                mvv_lva = (mvv_lva - MID) / SCALE * 2.0 * 200.0;
                assert(mvv_lva <= 200.0 && mvv_lva >= -200.0);
                if (IN_Q)
                {
                    // ignore fancy in qsearch
                    score += param::GOOD_CAPTURE_OFFSET + std::round((mvv_lva / 2.0) + 100.0);
                    assert(score >= param::GOOD_CAPTURE_OFFSET && score < param::PV_OFFSET);
                    goto done;
                }

                if (see::test_ge(position, move, -500))
                {
                    score += param::GOOD_CAPTURE_OFFSET + std::round((mvv_lva / 2.0) + 100.0);
                    goto done;
                }
                else
                {
                    score += param::BAD_CAPTURE_OFFSET + std::round((mvv_lva / 2.0) + 100.0);
                    goto done;
                }

                // see ranking, [-2000, 2000], scale by 40 to [-200, 200]
                // double see_raw = std::clamp(see::test(position, move), -3200, 3200);
                // double see_score = see_raw / 17.0;
                //
                // double see_weight = 0.1;
                // int16_t average_score = std::round(
                //     (mvv_lva * (1.0 - see_weight) + see_weight * see_score) / 2.0 + 100.0);
                //
                // // additional odds
                // if (see_raw >= -700)
                // {
                //     score += param::GOOD_CAPTURE_OFFSET + average_score;
                //     assert(score < param::PROMOTION_OFFSET && score >=
                //     param::GOOD_CAPTURE_OFFSET);
                // }
                // else
                // {
                //     score += param::BAD_CAPTURE_OFFSET + average_score;
                //     assert(score < param::KILLER_OFFSET && score >= param::BAD_CAPTURE_OFFSET);
                // }
                //
                // goto done;
            }

            // killers
            {
                // current ply
                for (size_t i = 0; i < param::NUMBER_KILLERS; ++i)
                {
                    if (move == m_killers[ply][i].first)
                    {
                        score += param::KILLER_OFFSET + param::KILLER_SCORE[i];
                        // mate killers bonus
                        if (m_killers[ply][i].second)
                            score += param::MATE_KILLER_BONUS;

                        assert(score < param::GOOD_CAPTURE_OFFSET && score >= param::KILLER_OFFSET);
                        goto done;
                    }
                }
            }

            // rest
            {
                if (prev_move != chess::Move::NO_MOVE && is_quiet(position, move))
                {
                    const auto &counter = m_counter[position.sideToMove()][prev_move.from().index()]
                                                   [prev_move.to().index()];
                    if (move == counter)
                        score += param::COUNTER_BONUS;
                }

                if (is_quiet(position, move))
                {
                    score += m_main_history[position.sideToMove()][move.from().index()]
                                           [move.to().index()]
                                               .get_value();
                }

                assert(score < param::BAD_CAPTURE_OFFSET);
            }

        done:
            move.setScore(score);
        }
    }

    void sort_moves(chess::Movelist &movelist, int i)
    {
        int highest_score = movelist[i].score();
        int highest_index = i;
        for (int j = i + 1; j < movelist.size(); ++j)
        {
            if (movelist[j].score() > highest_score)
            {
                highest_score = movelist[j].score();
                highest_index = j;
            }
        }

        if (i != highest_index)
            std::swap(movelist[i], movelist[highest_index]);
    }

    bool is_quiet(const chess::Board &position, const chess::Move &move) const
    {
        return !position.isCapture(move) && move.typeOf() != chess::Move::PROMOTION;
    }

    void update_main_history(const chess::Board &position, const chess::Move &move, int16_t bonus)
    {
        m_main_history[position.sideToMove()][move.from().index()][move.to().index()].add_bonus(
            bonus);
    }

    void store_killer(const chess::Move &killer, int32_t ply, bool is_mate)
    {
        std::pair<chess::Move, bool> insert = {killer, is_mate};
        for (size_t i = 0; i < param::NUMBER_KILLERS && insert.first != chess::Move::NO_MOVE; ++i)
        {
            if (killer == m_killers[ply][i].first)
            {
                m_killers[ply][i] = insert;
                break;
            }

            std::swap(m_killers[ply][i], insert);
        }
    }

    void incr_counter(const chess::Board &position, const chess::Move &prev_move,
                      const chess::Move &move)
    {
        if (is_quiet(position, move) && prev_move != chess::Move::NO_MOVE)
            m_counter[position.sideToMove()][prev_move.from().index()][prev_move.to().index()] =
                move;
    }
};

struct pv_line
{
    // pv_table[ply][i] is the ith pv move at ply
    chess::Move pv_table[param::MAX_DEPTH][param::MAX_DEPTH]{};

    // pv_length[ply] is the number of moves at ply
    int pv_length[param::MAX_DEPTH]{};

    explicit pv_line() = default;

    void ply_init(int32_t ply)
    {
        pv_length[ply] = ply;
    }

    void update(int32_t ply, const chess::Move &move)
    {
        pv_table[ply][ply] = move;
        for (int i = ply + 1; i < pv_length[ply + 1]; i++)
            pv_table[ply][i] = pv_table[ply + 1][i];

        pv_length[ply] = pv_length[ply + 1];
    }

    std::vector<chess::Move> get_moves()
    {
        std::vector<chess::Move> result(pv_length[0]);
        for (int i = 0; i < pv_length[0]; ++i)
        {
            result[i] = pv_table[0][i];
        }
        return result;
    }
};

struct search_stack
{
    int32_t ply = 0;
    chess::Move move = chess::Move::NO_MOVE;
    bool in_check = false;
    int32_t static_eval = param::VALUE_NONE;
    bool tt_pv = false;
    bool tt_hit = false;
    bool is_null = false;
    std::array<chess::Move, param::QUIET_MOVES> quiet_moves{};
};

enum search_node_type
{
    NonPV,
    PV,
    Root
};

struct engine
{
    // current position
    chess::Board m_position;
    // global timer
    timer m_timer;
    // debug stats
    engine_stats m_stats;
    // constants
    const engine_param m_param;
    // tt
    table *m_table;
    // move ordering
    move_ordering m_move_ordering;
    // pv-line
    pv_line m_line;
    // search stack
    constexpr static int SEARCH_STACK_PREFIX = 2;
    std::array<search_stack, param::MAX_DEPTH + SEARCH_STACK_PREFIX> m_stack;
    // endgame table ref
    endgame_table *m_endgame = nullptr;
    // nnue ref
    nnue *m_nnue = nullptr;

    // must be set via methods
    explicit engine(table *table) : engine(nullptr, nullptr, table)
    {
    }

    explicit engine(endgame_table *endgame, nnue *nnue, table *table)
        : m_stats(), m_table(table), m_move_ordering(m_param), m_endgame(endgame), m_nnue(nnue)
    {
        // init tables
        if (m_nnue == nullptr)
            pesto::init();
    }

    [[nodiscard]] int evaluate_bucket() const
    {
        constexpr int n = 8;
        constexpr int divisor = 32 / n;
        return (m_position.occ().count() - 2) / divisor;
    }

    [[nodiscard]] int32_t evaluate() const
    {
        if (m_nnue != nullptr)
        {
            return m_nnue->evaluate(m_position.sideToMove(), evaluate_bucket()) + m_param.tempo;
        }

        return pesto::evaluate(m_position) + m_param.tempo;
    }

    void make_move(const chess::Move &move)
    {
        if (m_nnue != nullptr)
            m_nnue->make_move(m_position, move);
        m_position.makeMove(move);
    }

    void unmake_move(const chess::Move &move)
    {
        m_position.unmakeMove(move);
        if (m_nnue != nullptr)
            m_nnue->unmake_move();
    }

    template <search_node_type node_type>
    int32_t qsearch(int32_t alpha, int32_t beta, search_stack *ss)
    {
        const int32_t ply = ss->ply;
        m_stats.sel_depth = std::max(m_stats.sel_depth, ply + 1);
        m_stats.nodes_searched += 1;
        if (m_stats.nodes_searched % 2048 == 0)
            m_timer.check();

        if (m_timer.is_stopped())
            return 0;

        if (ply >= param::MAX_DEPTH)
            return evaluate();

        // draw check
        if (m_position.isInsufficientMaterial() || m_position.isRepetition(1))
            return 0;

        // 50 move limit
        if (m_position.isHalfMoveDraw())
        {
            auto [_, type] = m_position.getHalfMoveDrawType();
            if (type == chess::GameResult::DRAW)
                return 0;

            return param::MATED_IN(ply);
        }

        // [tt lookup]
        auto &entry = m_table->probe(m_position.hash());
        auto tt_result = entry.get(m_position.hash(), ply, param::QDEPTH, alpha, beta);
        ss->tt_hit = tt_result.hit;
        tt_result.move = tt_result.hit ? tt_result.move : chess::Move::NO_MOVE;
        if (tt_result.can_use)
        {
            return tt_result.score;
        }

        // [static evaluation]
        int32_t best_score = -param::VALUE_INF;
        int32_t futility_base = -param::VALUE_INF;
        int32_t unadjusted_static_eval = param::VALUE_NONE;
        ss->in_check = m_position.inCheck();
        if (ss->in_check)
        {
            best_score = futility_base = -param::VALUE_INF;
        }
        else
        {
            if (ss->tt_hit)
            {
                unadjusted_static_eval = tt_result.static_eval;
                if (!param::IS_VALID(unadjusted_static_eval))
                    unadjusted_static_eval = evaluate();

                ss->static_eval = best_score = unadjusted_static_eval;

                // use tt score to adjust static eval
                bool bound_hit =
                    tt_result.flag == param::EXACT_FLAG ||
                    (tt_result.flag == param::BETA_FLAG && tt_result.score > best_score) ||
                    (tt_result.flag == param::ALPHA_FLAG && tt_result.score < best_score);
                if (param::IS_VALID(tt_result.score) && !param::IS_DECISIVE(tt_result.score) &&
                    bound_hit)
                {
                    best_score = tt_result.score;
                }
            }
            else
            {
                unadjusted_static_eval = evaluate();
                ss->static_eval = best_score = unadjusted_static_eval;
            }

            if (best_score >= beta)
            {
                if (!param::IS_DECISIVE(best_score))
                    best_score = (best_score + beta) / 2;

                if (!ss->tt_hit && entry.can_write(param::UNSEARCHED_DEPTH, m_table->m_generation))
                {
                    entry.set(m_position.hash(), param::BETA_FLAG, best_score, ply,
                              param::UNSEARCHED_DEPTH, chess::Move::NO_MOVE, unadjusted_static_eval,
                              false, m_table->m_generation);
                }

                return best_score;
            }

            if (best_score > alpha)
                alpha = best_score;

            futility_base = ss->static_eval + 300;
        }

        int32_t score;
        chess::Move best_move = chess::Move::NO_MOVE;
        chess::Movelist moves{};
        bool lazy_movegen = tt_result.move != chess::Move::NO_MOVE;
        if (lazy_movegen)
        {
            moves.add(tt_result.move);
        }
        else
        {
            if (ss->in_check)
                chess::movegen::legalmoves(moves, m_position);
            else
                chess::movegen::legalmoves<chess::movegen::MoveGenType::CAPTURE>(moves, m_position);
            m_move_ordering.score_moves<true>(m_position, moves, tt_result.move, (ss - 1)->move,
                                              ply);
        }
        for (int move_count = 0; move_count < moves.size(); ++move_count)
        {
            m_move_ordering.sort_moves(moves, move_count);
            const chess::Move &move = moves[move_count];

            if (!param::IS_LOSS(best_score))
            {
                // [fut prune]
                if (m_position.givesCheck(move) == chess::CheckType::NO_CHECK &&
                    (ss - 1)->move != chess::Move::NO_MOVE && move.to() != (ss - 1)->move.to() &&
                    !param::IS_LOSS(futility_base) && move.typeOf() != chess::Move::PROMOTION)
                {
                    if (move_count > 3)
                        continue;

                    int32_t futility_value =
                        futility_base + see::PIECE_VALUES[m_position.at(move.to()).type()];
                    if (futility_value <= alpha)
                    {
                        best_score = std::max(best_score, futility_value);
                        goto lazy_movegen_check;
                    }

                    if (!see::test_ge(m_position, move, alpha - futility_base))
                    {
                        best_score = std::max(best_score, std::min(futility_base, alpha));
                        goto lazy_movegen_check;
                    }
                }

                // [see pruning]
                if (!see::test_ge(m_position, move, -80))
                    goto lazy_movegen_check;
            }

            ss->move = move;
            make_move(move);
            score = -qsearch<node_type>(-beta, -alpha, ss + 1);
            unmake_move(move);

            if (m_timer.is_stopped())
                return 0;

            if (score > best_score)
            {
                best_score = score;
                best_move = move;

                if (score > alpha)
                {
                    alpha = score;

                    if (score >= beta)
                        break;
                }
            }

        lazy_movegen_check:
            if (lazy_movegen && move_count == 0)
            {
                if (ss->in_check)
                    chess::movegen::legalmoves(moves, m_position);
                else
                    chess::movegen::legalmoves<chess::movegen::MoveGenType::CAPTURE>(moves,
                                                                                     m_position);
                m_move_ordering.score_moves<true>(m_position, moves, tt_result.move, (ss - 1)->move,
                                                  ply);
            }
        }

        // [mate check]
        if (ss->in_check && moves.empty())
        {
            return param::MATED_IN(ply);
        }

        // [draw check]
        if (moves.empty())
        {
            chess::movegen::legalmoves(moves, m_position);
            if (moves.empty())
            {
                return param::VALUE_DRAW;
            }
        }

        // average out the best score
        if (!param::IS_DECISIVE(best_score) && best_score > beta)
            best_score = (best_score + beta) / 2;

        if (!m_timer.is_stopped() && entry.can_write(param::QDEPTH, m_table->m_generation))
        {
            entry.set(m_position.hash(), best_score >= beta ? param::BETA_FLAG : param::ALPHA_FLAG,
                      best_score, ply, param::QDEPTH, best_move, unadjusted_static_eval,
                      ss->tt_hit && ss->tt_pv, m_table->m_generation);
        }

        return best_score;
    }

    template <search_node_type node_type>
    int32_t negamax(int32_t alpha, int32_t beta, int32_t depth, search_stack *ss)
    {
        // constants
        const int32_t ply = ss->ply;
        const chess::Move &prev_move = (ss - 1)->move;
        m_line.ply_init(ply);

        m_stats.nodes_searched += 1;
        if (m_stats.nodes_searched % 2048 == 0)
            m_timer.check();

        if (m_timer.is_stopped())
            return 0;

        if (ply >= param::MAX_DEPTH)
            return evaluate();

        constexpr bool is_root = node_type == Root;
        constexpr bool is_pv_node = (node_type == PV || node_type == Root);
        const bool cut_node = beta - alpha == 1;
        assert(!(is_pv_node && cut_node));
        assert(alpha < beta);

        // check draw
        if (!is_root && (m_position.isInsufficientMaterial() || m_position.isRepetition(1)))
            return 0;

        // 50 move limit
        if (!is_root && m_position.isHalfMoveDraw())
        {
            auto [_, type] = m_position.getHalfMoveDrawType();
            if (type == chess::GameResult::DRAW)
                return 0;

            return param::MATED_IN(ply);
        }

        // [mate distance pruning]
        if (!is_root)
        {
            int32_t mating_value = param::MATE_IN(ply);
            if (mating_value < beta)
            {
                beta = mating_value;
                if (alpha >= beta)
                    return mating_value;
            }

            mating_value = param::MATED_IN(ply);
            if (mating_value > alpha)
            {
                alpha = mating_value;
                if (beta <= alpha)
                    return mating_value;
            }
        }

        // [qsearch]
        if (depth <= 0)
        {
            m_stats.nodes_searched -= 1;
            return qsearch<is_pv_node ? PV : NonPV>(alpha, beta, ss);
        }

        // [tt lookup]
        auto &entry = m_table->probe(m_position.hash());
        auto tt_result = entry.get(m_position.hash(), ply, depth, alpha, beta);
        ss->tt_hit = tt_result.hit;
        ss->tt_pv = is_pv_node || (tt_result.hit && tt_result.is_pv);
        tt_result.move = tt_result.hit ? tt_result.move : chess::Move::NO_MOVE;
        bool is_tt_capture =
            tt_result.move != chess::Move::NO_MOVE && m_position.isCapture(tt_result.move);

        // [tt early return]
        if (!is_root && !is_pv_node && tt_result.can_use)
        {
            return tt_result.score;
        }

        // [tt evaluation fix]
        int32_t unadjusted_static_eval = param::VALUE_NONE;
        int32_t adjusted_static_eval = param::VALUE_NONE;
        ss->in_check = m_position.inCheck();
        if (ss->in_check)
        {
            ss->static_eval = adjusted_static_eval = param::VALUE_NONE;
        }
        else if (ss->tt_hit)
        {
            unadjusted_static_eval = tt_result.static_eval;
            if (!param::IS_VALID(unadjusted_static_eval))
                unadjusted_static_eval = evaluate();

            ss->static_eval = adjusted_static_eval = unadjusted_static_eval;

            // use tt score to adjust static eval
            // bool bound_hit =
            //     tt_result.flag == param::EXACT_FLAG ||
            //     (tt_result.flag == param::BETA_FLAG && tt_result.score > adjusted_static_eval) ||
            //     (tt_result.flag == param::ALPHA_FLAG && tt_result.score < adjusted_static_eval);
            // if (param::IS_VALID(tt_result.score) && bound_hit)
            // {
            //     adjusted_static_eval = tt_result.score;
            // }
        }
        else
        {
            unadjusted_static_eval = evaluate();
            ss->static_eval = adjusted_static_eval = unadjusted_static_eval;

            if (entry.can_write(param::UNSEARCHED_DEPTH, m_table->m_generation))
            {
                entry.set(m_position.hash(), param::NO_FLAG, param::VALUE_NONE, ply,
                          param::UNSEARCHED_DEPTH, chess::Move::NO_MOVE, unadjusted_static_eval,
                          ss->tt_pv, m_table->m_generation);
            }
        }

        // [check syzygy endgame table]
        int32_t best_score = -param::VALUE_INF;
        if (m_endgame != nullptr && !is_root && m_endgame->is_stored(m_position))
        {
            int32_t wdl = m_endgame->probe_wdl(m_position);
            int32_t draw_score = 1;

            int32_t tb_score = param::VALUE_SYZYGY - ply;

            int32_t score = wdl < -draw_score  ? -tb_score
                            : wdl > draw_score ? tb_score
                                               : param::VALUE_DRAW + 2 * wdl * draw_score;

            int8_t flag = wdl < -draw_score  ? param::ALPHA_FLAG
                          : wdl > draw_score ? param::BETA_FLAG
                                             : param::EXACT_FLAG;

            // TODO: fix early return
            int32_t new_depth = param::TB_DEPTH;
            if (entry.can_write(new_depth, m_table->m_generation))
            {
                entry.set(m_position.hash(), flag, score, ply,
                          new_depth, chess::Move::NO_MOVE,
                          param::VALUE_NONE, ss->tt_pv, m_table->m_generation);
            }

            return score;
        }

        if (ss->in_check)
        {
            goto moves;
        }

        // [static null move pruning]
        {
            int32_t margin = m_param.static_null_base_margin * depth;
            if (!ss->tt_pv && param::IS_VALID(adjusted_static_eval) &&
                adjusted_static_eval - margin >= beta && !param::IS_LOSS(beta) && depth <= 14 &&
                (tt_result.move == chess::Move::NO_MOVE || is_tt_capture) &&
                !param::IS_WIN(adjusted_static_eval))
            {
                return (2 * beta + adjusted_static_eval) / 3;
            }
        }

        // [null move pruning]
        {
            const bool has_non_pawns = m_position.hasNonPawnMaterial(chess::Color::WHITE) &&
                                       m_position.hasNonPawnMaterial(chess::Color::BLACK);
            if (cut_node && !ss->is_null && has_non_pawns && beta < param::CHECKMATE)
            {
                int32_t reduction = m_param.nmp_depth_base + depth / m_param.nmp_depth_multiplier;

                // since nmp uses ss+1, we fake that this move is nothing
                ss->move = chess::Move::NO_MOVE;

                (ss + 1)->is_null = true;
                m_position.makeNullMove();
                int32_t null_score = -negamax<NonPV>(-beta, -beta + 1, depth - reduction, ss + 1);
                m_position.unmakeNullMove();
                (ss + 1)->is_null = false;

                if (m_timer.is_stopped())
                    return 0;

                if (null_score >= beta && null_score < param::CHECKMATE)
                {
                    return beta;
                }
            }
        }

        // [prob cut]
        // the score of a lower depth is likely similar to a score of higher depth
        // goal is to convert beta to beta of a lower depth, and reject that
        // we do move gen and eval using this first
        {
            // assume a 350 shift
            int32_t probcut_beta = beta + 300;
            if (!is_root && depth >= 3 && !param::IS_DECISIVE(beta) &&
                // also ignore when tt score is lower than expected beta
                !(tt_result.hit && param::IS_VALID(tt_result.score) &&
                  tt_result.score < probcut_beta))
            {
                // only care about capture/good moves
                chess::Movelist moves{};
                chess::movegen::legalmoves<chess::movegen::MoveGenType::CAPTURE>(moves, m_position);

                // add pv move as well
                if (tt_result.move != chess::Move::NO_MOVE)
                    moves.add(tt_result.move);

                m_move_ordering.score_moves<true>(m_position, moves, tt_result.move, prev_move,
                                                  ply);

                int32_t probcut_depth = std::clamp(depth - 3, 0, depth);
                bool searched_tt = false;
                for (int i = 0; i < moves.size(); ++i)
                {
                    m_move_ordering.sort_moves(moves, i);
                    chess::Move &move = moves[i];

                    if (move == tt_result.move)
                    {
                        if (searched_tt)
                        {
                            continue;
                        }
                        searched_tt = true;
                    }

                    ss->move = move;
                    make_move(move);

                    // check if move exceeds beta first
                    int32_t score = -qsearch<NonPV>(-probcut_beta, -probcut_beta + 1, ss + 1);
                    // full search if qsearch null window worked
                    if (score >= probcut_beta && probcut_depth > 0)
                    {
                        score = -negamax<NonPV>(-probcut_beta, -probcut_beta + 1, probcut_depth,
                                                ss + 1);
                    }

                    unmake_move(move);

                    if (m_timer.is_stopped())
                        return 0;

                    // check if can cut at lower depth
                    if (score >= probcut_beta)
                    {
                        if (entry.can_write(probcut_depth + 1, m_table->m_generation))
                        {
                            entry.set(m_position.hash(), param::BETA_FLAG, score, ply,
                                      probcut_depth + 1, move, unadjusted_static_eval, ss->tt_pv,
                                      m_table->m_generation);
                        }

                        if (!param::IS_DECISIVE(score))
                        {
                            return score - probcut_beta + beta;
                        }
                    }
                }
            }
        }

    moves:

        uint8_t tt_flag = param::ALPHA_FLAG;

        // [tt-move generation]
        chess::Movelist moves{};
        bool lazy_move_gen = tt_result.move != chess::Move::NO_MOVE;
        if (lazy_move_gen)
        {
            moves.add(tt_result.move);
        }
        else
        {
            chess::movegen::legalmoves(moves, m_position);
            m_move_ordering.score_moves<false>(m_position, moves, tt_result.move, prev_move, ply);
        }

        chess::Move best_move = chess::Move::NO_MOVE;

        // track quiet moves for malus
        int quiet_count = 0;

        for (int move_count = 0; move_count < moves.size(); ++move_count)
        {
            m_move_ordering.sort_moves(moves, move_count);
            const chess::Move &move = moves[move_count];
            ss->move = move;

            bool is_capture = m_position.isCapture(move);
            bool is_check = m_position.givesCheck(move) != chess::CheckType::NO_CHECK;

            // [late move reduction]
            int32_t reduction = 0;
            if (!is_pv_node && depth >= 3 && move_count >= 2 && !ss->in_check && !is_capture)
                reduction += m_param.lmr[depth][move_count] * 1024;

            int32_t reduced_depth;
            int32_t score;

            // [low depth pruning]
            bool has_non_pawn = m_position.hasNonPawnMaterial(chess::Color::WHITE) &&
                                m_position.hasNonPawnMaterial(chess::Color::BLACK);
            if (move_count > 0 && has_non_pawn && !param::IS_LOSS(best_score))
            {
                int32_t lmr_depth = depth;

                if (is_capture || is_check)
                {
                    auto captured = m_position.at(move.to()).type();

                    // [fut prune for captures]
                    if (!is_check && lmr_depth < 7 && param::IS_VALID(ss->static_eval))
                    {
                        int32_t fut_value =
                            ss->static_eval + 300 + 300 * lmr_depth + see::PIECE_VALUES[captured];
                        if (fut_value <= alpha)
                            goto lazy_move_gen;
                    }

                    // [see pruning for captures and checks]
                    // need to ensure that we don't prune: sac last non-pawn for stalemate
                    auto non_pawn_pieces_sqs =
                        m_position.pieces(chess::PieceType::KNIGHT, m_position.sideToMove()) |
                        m_position.pieces(chess::PieceType::BISHOP, m_position.sideToMove()) |
                        m_position.pieces(chess::PieceType::ROOK, m_position.sideToMove()) |
                        m_position.pieces(chess::PieceType::QUEEN, m_position.sideToMove());
                    auto moved_sq = chess::Bitboard(1ull << move.from().index());
                    if (alpha >= param::VALUE_DRAW || non_pawn_pieces_sqs != moved_sq)
                    {
                        int32_t margin = 200 + 400 * depth;
                        if (!see::test_ge(m_position, move, -margin))
                            goto lazy_move_gen;
                    }
                }
                else
                {
                    // [fut prune for general]
                    if (!ss->in_check && lmr_depth < 12 && param::IS_VALID(ss->static_eval))
                    {
                        int32_t fut_value = ss->static_eval + 200 + 300 * lmr_depth;
                        if (fut_value <= alpha)
                        {
                            // shift best_score to fut value
                            if (best_score < fut_value && !param::IS_DECISIVE(best_score) &&
                                !param::IS_WIN(fut_value))
                                best_score = fut_value;

                            goto lazy_move_gen;
                        }
                    }

                    // [see general pruning]
                    int32_t margin = 100 + 50 * lmr_depth * lmr_depth;
                    if (!see::test_ge(m_position, move, -margin))
                    {
                        goto lazy_move_gen;
                    }
                }
            }

            // [singular extension]

            // [check extension]
            if (is_check)
            {
                reduction -= 1100;
            }

            reduced_depth = std::min(depth - 1 - reduction / 1024, depth);

            make_move(move);

            if (move_count == 0)
            {
                score = -negamax<PV>(-beta, -alpha, depth - 1, ss + 1);
            }
            else
            {
                score = -negamax<NonPV>(-(alpha + 1), -alpha, reduced_depth, ss + 1);

                if (score > alpha && reduced_depth < depth - 1)
                {
                    score = -negamax<NonPV>(-(alpha + 1), -alpha, depth - 1, ss + 1);
                }

                if (score > alpha && is_pv_node && score < beta)
                {
                    score = -negamax<PV>(-beta, -alpha, depth - 1, ss + 1);
                }
            }

            unmake_move(move);

            if (m_timer.is_stopped())
                return 0;

            if (score > best_score)
            {
                best_score = score;
                best_move = move;

                if (score > alpha)
                {
                    alpha = score;

                    if (score >= beta)
                    {
                        tt_flag = param::BETA_FLAG;
                        m_move_ordering.incr_counter(m_position, prev_move, move);

                        // [killer moves update]
                        if (m_move_ordering.is_quiet(m_position, move) && cut_node)
                        {
                            m_move_ordering.store_killer(move, ply, param::IS_WIN(score));
                        }

                        // [main history update]
                        if (m_move_ordering.is_quiet(m_position, move))
                        {
                            const int16_t main_history_bonus = 300 * depth - 250;
                            m_move_ordering.update_main_history(m_position, move,
                                                                main_history_bonus);

                            // malus apply
                            for (int j = 0; j < quiet_count; ++j)
                            {
                                auto &m = ss->quiet_moves[j];
                                m_move_ordering.update_main_history(m_position, m,
                                                                    -main_history_bonus);
                            }
                        }

                        break;
                    }

                    tt_flag = param::EXACT_FLAG;
                    m_line.update(ply, move);
                }
            }

            // malus save
            if (m_move_ordering.is_quiet(m_position, move) && quiet_count < param::QUIET_MOVES)
                ss->quiet_moves[quiet_count++] = move;

        lazy_move_gen:
            if (lazy_move_gen && move_count == 0)
            {
                chess::movegen::legalmoves(moves, m_position);
                m_move_ordering.score_moves<false>(m_position, moves, tt_result.move, prev_move,
                                                   ply);
                m_move_ordering.sort_moves(moves, 0);
            }
        }

        // checkmate or draw
        if (moves.empty())
        {
            if (m_position.inCheck())
                return param::MATED_IN(ply);

            // draw
            return param::VALUE_DRAW;
        }

        // if no good move found, last move good so add this one too
        if (best_score <= alpha)
            ss->tt_pv = ss->tt_pv || (ss - 1)->tt_pv;

        if (entry.can_write(depth, m_table->m_generation) && !m_timer.is_stopped())
        {
            entry.set(m_position.hash(), tt_flag, best_score, ply, depth, best_move,
                      unadjusted_static_eval, ss->tt_pv, m_table->m_generation);
        }

        // hack to make a move in root
        if (is_root && best_move == chess::Move::NO_MOVE && m_line.pv_length[0] == 0)
        {
            m_line.update(ply, moves[0]);
        }

        return best_score;
    }

    uint64_t perft(int depth)
    {
        chess::Movelist moves;
        chess::movegen::legalmoves(moves, m_position);

        if (depth == 1)
        {
            return moves.size();
        }

        uint64_t total = 0;
        for (const auto &move : moves)
        {
            m_position.makeMove(move);
            total += perft(depth - 1);
            m_position.unmakeMove(move);
        }

        return total;
    }

    void perft(const chess::Board &reference, int depth)
    {
        using namespace std;

        m_position = reference;
        cout << "benchmarking perft, depth: " << depth << endl;
        cout << "fen: " << m_position.getFen() << endl;

        const auto t1 = chrono::high_resolution_clock::now();
        const auto nodes = perft(depth);
        const auto t2 = chrono::high_resolution_clock::now();
        const auto ms = chrono::duration_cast<chrono::milliseconds>(t2 - t1).count();

        auto original = std::cout.getloc();
        std::cout.imbue(std::locale("en_US.UTF-8"));
        cout << "nodes: " << nodes << ", took " << ms << "ms" << endl;
        cout << "nps: " << nodes * 1000 / std::max(1L, ms) << endl;
        std::cout.imbue(original);
    }

    search_result search(const chess::Board &reference, search_param &param, bool verbose = false,
                         bool uci = false)
    {
        // timer info first
        const auto control = param.time_control(reference.fullMoveNumber(), reference.sideToMove());
        std::cout << "info searchtime " << control.time << std::endl;

        m_timer.start(control.time);
        auto reference_time = timer::now();
        m_stats = engine_stats{0, 0, 0, timer::now() - reference_time};

        // update age
        assert(m_table != nullptr);
        m_table->m_generation += 1;

        m_position = reference;

        if (m_nnue != nullptr)
        {
            m_nnue->initialize(m_position);
        }

        int32_t alpha = -param::VALUE_INF, beta = param::VALUE_INF;
        int32_t depth = 1;

        engine_stats last_stats = m_stats;
        search_result result{};

        if (m_endgame != nullptr && m_endgame->is_stored(m_position))
        {
            // root search
            auto probe = m_endgame->probe_dtm(m_position, m_timer);
            result.pv_line = probe.first;
            result.depth = 1;
            result.score = probe.second;

            if (verbose)
            {
                if (uci)
                {
                    m_stats.display_uci(result);
                }
            }

            return result;
        }

        // make search stack
        for (int i = SEARCH_STACK_PREFIX; i >= 0; --i)
        {
            m_stack[i] = {.ply = 0};
        }

        for (int i = 0; i < param::MAX_DEPTH; ++i)
        {
            m_stack[i + SEARCH_STACK_PREFIX] = {.ply = i};
        }

        while (depth <= control.depth)
        {
            int32_t score = negamax<Root>(alpha, beta, depth, &m_stack[SEARCH_STACK_PREFIX]);
            if (m_timer.is_stopped())
            {
                break;
            }

            // [asp window]
            if (score <= alpha || score >= beta)
            {
                alpha = -param::VALUE_INF;
                beta = param::VALUE_INF;
                continue;
            }

            if (std::abs(score) < param::CHECKMATE)
            {
                alpha = score - m_param.window_size;
                beta = score + m_param.window_size;
            }

            result.score = score;
            result.pv_line.clear();
            result.pv_line = m_line.get_moves();
            result.depth = depth;

            // display info
            if (verbose)
            {
                m_stats.total_time = timer::now() - reference_time;
                m_stats.tt_occupancy = m_table->occupied();
                if (uci)
                    m_stats.display_uci(result);
                else
                    m_stats.display_delta(last_stats, result);
                last_stats = m_stats;
            }

            depth += 1;
        }

        return result;
    }
};
