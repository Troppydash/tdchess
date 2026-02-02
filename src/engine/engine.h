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
    int32_t static_null_base_margin = 85;
    int16_t nmp_depth_limit = 2;
    int16_t nmp_piece_count = 7;
    int16_t nmp_depth_base = 4;
    int16_t nmp_depth_multiplier = 6;

    // move ordering
    std::array<std::array<int16_t, 6>, 7> mvv_lva;
    int16_t mvv_offset = std::numeric_limits<int16_t>::max() - 256;
    int16_t max_history = mvv_offset - 100;
    int16_t pv_score = 65;
    int16_t first_killer = -10;
    int16_t second_killer = -20;
    int16_t counter_bonus = 5;

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

        lmr[0][0] = 0;
        lmr[0][1] = 0;
        lmr[1][0] = 0;
        lmr[1][1] = 0;

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

struct move_ordering
{
    chess::Move m_killers[param::MAX_DEPTH][2];
    chess::Move m_counter[2][64][64];
    int16_t m_history[2][64][64];
    const engine_param m_param;

    explicit move_ordering(const engine_param &param) : m_param(param)
    {
        // init killer/counter/history
        for (auto &i : m_history)
            for (auto &j : i)
                for (int16_t &k : j)
                    k = 0;

        for (auto &m : m_killers)
        {
            m[0] = m[1] = chess::Move::NO_MOVE;
        }

        for (auto &i : m_counter)
            for (auto &j : i)
                for (auto &k : j)
                    k = chess::Move::NO_MOVE;
    }

    void score_moves(const chess::Board &position, chess::Movelist &movelist,
                     const chess::Move &pv_move, const chess::Move &prev_move, int32_t ply)
    {
        for (auto &move : movelist)
        {
            int16_t score = 0;
            auto captured = position.at(move.to()).type();

            if (move == pv_move)
                score += m_param.mvv_offset + m_param.pv_score;
            else if (position.isCapture(move) && captured != chess::PieceType::NONE)
            {
                auto moved = position.at(move.from()).type();
                score += m_param.mvv_offset + m_param.mvv_lva[captured][moved];
            }
            else if (move == m_killers[ply][0])
                score += m_param.mvv_offset + m_param.first_killer;
            else if (move == m_killers[ply][1])
                score += m_param.mvv_offset + m_param.second_killer;
            else
            {
                const auto &counter = m_counter[position.sideToMove()][prev_move.from().index()]
                                               [prev_move.to().index()];
                if (move == counter)
                    score += m_param.counter_bonus;

                int16_t history_score =
                    m_history[position.sideToMove()][move.from().index()][move.to().index()];
                score += history_score;
            }

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
        std::swap(movelist[i], movelist[highest_index]);
    }

    void age_history()
    {
        for (auto &i : m_history)
        {
            for (auto &j : i)
            {
                for (int16_t &k : j)
                {
                    k /= 2;
                }
            }
        }
    }

    bool is_quiet(const chess::Board &position, const chess::Move &move) const
    {
        return !position.isCapture(move);
    }

    void update_history(const chess::Board &position, const chess::Move &move, int16_t bonus)
    {
        const int16_t MAX_HISTORY = m_param.max_history;
        int16_t clamped_bonus = helper::clamp(bonus, -MAX_HISTORY, MAX_HISTORY);
        auto &hist = m_history[position.sideToMove()][move.from().index()][move.to().index()];
        hist += clamped_bonus - hist * std::abs(clamped_bonus) / MAX_HISTORY;
    }

    void store_killer(const chess::Board &position, const chess::Move &killer, int32_t ply)
    {
        if (is_quiet(position, killer))
        {
            if (m_killers[ply][0] != killer)
            {
                m_killers[ply][1] = m_killers[ply][0];
                m_killers[ply][0] = killer;
            }
        }
    }

    void incr_counter(const chess::Board &position, const chess::Move &prev_move,
                      const chess::Move &move)
    {
        if (is_quiet(position, move))
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
    int32_t ply;
    chess::Move move;
    bool in_check;
    int32_t static_eval;
    int32_t tt_pv;
    bool tt_hit;
    bool is_null;
    std::array<chess::Move, param::QUIET_MOVES> quiet_moves;
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
    table m_table;
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
    explicit engine(const int table_size_in_mb) : engine(nullptr, nullptr, table_size_in_mb)
    {
    }

    explicit engine(endgame_table *endgame, nnue *nnue, const int table_size_in_mb)
        : m_stats(), m_table(table_size_in_mb), m_move_ordering(m_param), m_endgame(endgame),
          m_nnue(nnue)
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
        auto &entry = m_table.probe(m_position.hash());
        auto tt_result = entry.get(m_position.hash(), ply, param::QDEPTH, alpha, beta);
        ss->tt_hit = tt_result.hit;
        if (tt_result.hit)
        {
            return tt_result.score;
        }

        int32_t best_score = -param::VALUE_INF;
        int32_t futility_base = -param::VALUE_INF;
        ss->in_check = m_position.inCheck();
        if (ss->in_check)
        {
            // ignore
            best_score = futility_base = -param::VALUE_INF;
        }
        else
        {
            best_score = evaluate();
            if (best_score >= beta)
            {
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
            tt_result.move.setScore(0);
            moves.add(tt_result.move);
        }
        else
        {
            if (ss->in_check)
                chess::movegen::legalmoves(moves, m_position);
            else
                chess::movegen::legalmoves<chess::movegen::MoveGenType::CAPTURE>(moves, m_position);
            m_move_ordering.score_moves(m_position, moves, tt_result.move, (ss - 1)->move, ply);
        }
        for (int move_count = 0; move_count < moves.size(); ++move_count)
        {
            m_move_ordering.sort_moves(moves, move_count);
            const chess::Move &move = moves[move_count];

            if (!param::IS_LOSS(best_score))
            {
                // [fut prune]
                if (m_position.givesCheck(move) == chess::CheckType::NO_CHECK &&
                    move.to() != (ss - 1)->move.to() && !param::IS_LOSS(futility_base) &&
                    move.typeOf() != chess::Move::PROMOTION)
                {
                    if (move_count > 2)
                        continue;

                    int32_t futility_value =
                        futility_base + see::PIECE_VALUES[m_position.at(move.to())];
                    if (futility_value <= alpha)
                    {
                        best_score = std::max(best_score, futility_value);
                        goto lazy_movegen_check;
                    }

                    if (see::test(m_position, move) < alpha - futility_base)
                    {
                        best_score = std::max(best_score, std::min(futility_base, alpha));
                        goto lazy_movegen_check;
                    }
                }

                // [see pruning]
                if (see::test(m_position, move) < -80)
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
            }

            if (score >= beta)
                break;

            if (score > alpha)
            {
                alpha = score;
            }

        lazy_movegen_check:
            if (lazy_movegen && move_count == 0)
            {
                if (ss->in_check)
                    chess::movegen::legalmoves(moves, m_position);
                else
                    chess::movegen::legalmoves<chess::movegen::MoveGenType::CAPTURE>(moves,
                                                                                     m_position);
                m_move_ordering.score_moves(m_position, moves, tt_result.move, (ss - 1)->move, ply);
            }
        }

        // [mate check]
        if (ss->in_check && moves.size() == 0)
        {
            return param::MATED_IN(ply);
        }

        // average out the best score
        if (!param::IS_DECISIVE(best_score) && best_score > beta)
            best_score = (best_score + beta) / 2;

        if (!m_timer.is_stopped() && entry.can_write(param::QDEPTH))
        {
            entry.set(m_position.hash(), best_score, best_move, ply, param::QDEPTH,
                      best_score >= beta ? param::BETA_FLAG : param::ALPHA_FLAG);
        }

        return best_score;
    }

    template <search_node_type node_type>
    int32_t negamax(int32_t alpha, int32_t beta, int32_t depth, search_stack *ss,
                    const bool cut_node)
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
        const bool is_all_node = !(is_pv_node || cut_node);

        assert(!(is_pv_node && cut_node));
        assert(!(is_all_node && cut_node));
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

        // [check extension]
        ss->in_check = m_position.inCheck();
        if (ss->in_check && ply < depth * 2)
            depth += 1;

        // [qsearch]
        if (depth <= 0)
        {
            m_stats.nodes_searched -= 1;
            return qsearch<is_pv_node ? PV : NonPV>(alpha, beta, ss);
        }

        // [tt lookup]
        auto &entry = m_table.probe(m_position.hash());
        auto tt_result = entry.get(m_position.hash(), ply, depth, alpha, beta);
        ss->tt_hit = tt_result.hit;
        if (tt_result.hit && !is_root)
        {
            return tt_result.score;
        }

        // check syzygy
        if (m_endgame != nullptr && !is_root && m_endgame->is_stored(m_position))
        {
            int32_t score = m_endgame->probe_wdl(m_position, ply);
            entry.set(m_position.hash(), score, chess::Move::NO_MOVE, ply, param::TB_DEPTH,
                      param::EXACT_FLAG);
            return score;
        }

        ss->static_eval = evaluate();

        // [static null move pruning]
        if (!ss->in_check && !is_pv_node && std::abs(beta) < param::CHECKMATE)
        {
            int32_t static_score = ss->static_eval;
            int32_t margin = m_param.static_null_base_margin * depth;
            if (static_score - margin >= beta)
                return static_score - margin;
        }

        // [null move pruning]
        const bool has_non_pawns = m_position.hasNonPawnMaterial(chess::Color::WHITE) &&
                                   m_position.hasNonPawnMaterial(chess::Color::BLACK);
        if (cut_node && !ss->in_check && !ss->is_null && has_non_pawns && beta < param::CHECKMATE)
        {
            int32_t reduction = m_param.nmp_depth_base + depth / m_param.nmp_depth_multiplier;

            (ss + 1)->is_null = true;
            m_position.makeNullMove();
            int32_t null_score = -negamax<NonPV>(-beta, -beta + 1, depth - reduction, ss + 1, true);
            m_position.unmakeNullMove();
            (ss + 1)->is_null = false;

            if (m_timer.is_stopped())
                return 0;

            if (null_score >= beta && null_score < param::CHECKMATE)
            {
                return beta;
            }
        }

        uint8_t tt_flag = param::ALPHA_FLAG;
        int legal_moves = 0;
        int explored_moves = 0;

        // [tt-move generation]
        chess::Movelist moves{};
        bool lazy_move_gen = tt_result.move != chess::Move::NO_MOVE;
        if (lazy_move_gen)
        {
            tt_result.move.setScore(0);
            moves.add(tt_result.move);
            legal_moves = 1;
        }
        else
        {
            chess::movegen::legalmoves(moves, m_position);
            m_move_ordering.score_moves(m_position, moves, tt_result.move, prev_move, ply);
            legal_moves = moves.size();
        }

        int32_t best_score = -param::VALUE_INF;
        chess::Move best_move = chess::Move::NO_MOVE;

        // track quiet moves for malus
        int quiet_count = 0;

        for (int i = 0; i < moves.size(); ++i)
        {
            m_move_ordering.sort_moves(moves, i);
            const chess::Move &move = moves[i];

            ss->move = move;
            explored_moves += 1;

            // [late move reduction]
            int32_t reduction = 0;
            if (depth >= 2)
                reduction += m_param.lmr[depth][explored_moves] * 1024;

            if (cut_node)
                reduction += 1300;

            bool tactical = m_position.isCapture(move);
            if (tactical)
                reduction -= 300;

            if (move == tt_result.move)
                reduction -= 700;

            int32_t depth_reduction = std::min(depth - 1 - reduction / 1024, depth + 1);

            make_move(move);

            int32_t score;
            if (is_pv_node)
            {
                // PV-NODE, goal is to full search to get exact score
                if (explored_moves == 1)
                {
                    score = -negamax<PV>(-beta, -alpha, depth - 1, ss + 1, false);
                }
                else
                {
                    // [pv search]
                    score = -negamax<NonPV>(-(alpha + 1), -alpha, depth - 1, ss + 1, true);
                    if (alpha < score && score < beta)
                    {
                        score = -negamax<PV>(-beta, -alpha, depth - 1, ss + 1, false);
                    }
                }
            }
            else if (cut_node)
            {
                // CUT-NODE, goal is to find one move that fails high
                if (explored_moves == 1)
                {
                    // FIRST SEARCH WITH ALL_NODE
                    score = -negamax<NonPV>(-beta, -alpha, depth - 1, ss + 1, false);
                }
                else
                {
                    // LATER SEARCH WITH CUT_NODE

                    // [pv search]
                    score = -negamax<NonPV>(-(alpha + 1), -alpha, depth_reduction, ss + 1, true);

                    if (score > alpha && depth_reduction < depth - 1)
                        score = -negamax<NonPV>(-(alpha + 1), -alpha, depth - 1, ss + 1, true);
                }
            }
            else
            {
                // ALL-NODE, goal is to prove that all moves fail-low
                if (explored_moves == 1)
                {
                    // CUT_NODE
                    score = -negamax<NonPV>(-beta, -alpha, depth - 1, ss + 1, true);
                }
                else
                {
                    // CUT_NODE

                    // [pv search]
                    score = -negamax<NonPV>(-(alpha + 1), -alpha, depth_reduction, ss + 1, true);

                    if (score > alpha && depth_reduction < depth - 1)
                        score = -negamax<NonPV>(-(alpha + 1), -alpha, depth - 1, ss + 1, true);
                }
            }

            unmake_move(move);

            if (m_timer.is_stopped())
                return 0;

            if (score > best_score)
            {
                best_score = score;
                best_move = move;
            }

            if (score >= beta)
            {
                tt_flag = param::BETA_FLAG;
                m_move_ordering.incr_counter(m_position, prev_move, move);
                m_move_ordering.store_killer(m_position, move, ply);

                if (m_move_ordering.is_quiet(m_position, move))
                {
                    const int16_t bonus = 300 * depth - 250;
                    m_move_ordering.update_history(m_position, move, bonus);

                    // malus apply
                    for (int j = 0; j < quiet_count; ++j)
                        m_move_ordering.update_history(m_position, ss->quiet_moves[j], -bonus);
                }

                break;
            }

            // malus save
            if (m_move_ordering.is_quiet(m_position, move) && quiet_count < param::QUIET_MOVES)
                ss->quiet_moves[quiet_count++] = move;

            if (score > alpha)
            {
                tt_flag = param::EXACT_FLAG;
                alpha = score;
                m_line.update(ply, move);
            }

            if (lazy_move_gen && explored_moves == 1)
            {
                chess::movegen::legalmoves(moves, m_position);
                m_move_ordering.score_moves(m_position, moves, tt_result.move, prev_move, ply);
                m_move_ordering.sort_moves(moves, 0);
                legal_moves = moves.size();
            }
        }

        // checkmate or draw
        if (legal_moves == 0)
        {
            if (m_position.inCheck())
                return param::MATED_IN(ply);

            // draw
            return 0;
        }

        if (depth >= entry.m_depth && !m_timer.is_stopped())
        {
            entry.set(m_position.hash(), best_score, best_move, ply, depth, tt_flag);
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

    search_result search(const chess::Board &reference, search_param param, bool verbose = false,
                         bool uci = false)
    {
        auto control = param.time_control(reference.sideToMove());

        m_timer.start(control.time);
        m_position = reference;
        auto reference_time = timer::now();
        m_stats = engine_stats{0, 0, 0, timer::now() - reference_time};

        if (m_nnue != nullptr)
        {
            m_nnue->initialize(m_position);
        }

        int32_t alpha = -param::INF, beta = param::INF;
        int32_t depth = 1;

        engine_stats last_stats = m_stats;

        search_result result{};

        if (m_endgame != nullptr && m_endgame->is_stored(m_position))
        {
            // root search
            auto probe = m_endgame->probe_dtm(m_position);
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
            int32_t score = negamax<Root>(alpha, beta, depth, &m_stack[SEARCH_STACK_PREFIX], false);
            if (m_timer.is_stopped())
            {
                break;
            }

            // [asp window]
            if (score <= alpha || score >= beta)
            {
                alpha = -param::INF;
                beta = param::INF;
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
                m_stats.tt_occupancy = m_table.occupied();
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
