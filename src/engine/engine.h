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
    int depth;
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
    int sel_depth;
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
    int16_t lmr[param::MAX_DEPTH][100];
    std::array<int, 6> lmp_margins;
    int lmr_depth_ration = 4;
    int lmr_move_ratio = 12;
    int window_size = 35;
    int32_t static_null_base_margin = 85;
    int16_t nmp_depth_limit = 2;
    int16_t nmp_piece_count = 7;
    int16_t nmp_depth_base = 4;
    int16_t nmp_depth_multiplier = 3;

    // move ordering
    std::array<std::array<int16_t, 6>, 7> mvv_lva;
    int16_t mvv_offset = std::numeric_limits<int16_t>::max() - 256;
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
        for (int depth = 0; depth < param::MAX_DEPTH; ++depth)
            for (int move = 0; move < 100; ++move)
                lmr[depth][move] = std::max(2, depth / std::max(1, lmr_depth_ration)) +
                                   move / std::max(1, lmr_move_ratio);

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

template <typename T, int D> class stats_entry
{
  private:
    T value = 0;

  public:
    [[nodiscard]] T get() const
    {
        return value;
    }

    void add(const int bonus)
    {
        int clamped_bonus = std::clamp(bonus, -D, D);
        value = value + clamped_bonus - value * std::abs(clamped_bonus) / D;
    }
};

struct move_ordering
{
    chess::Move m_killers[param::MAX_DEPTH][2]{};
    chess::Move m_counter[2][64][64]{};
    stats_entry<int16_t, param::MVV_OFFSET> m_history[2][64][64]{};

    const engine_param m_param;

    explicit move_ordering(const engine_param &param) : m_param(param)
    {
        for (auto &m : m_killers)
        {
            m[0] = m[1] = chess::Move::NULL_MOVE;
        }

        for (auto &i : m_counter)
            for (auto &j : i)
                for (auto &k : j)
                    k = chess::Move::NULL_MOVE;
    }

    void score_moves(const chess::Board &position, chess::Movelist &movelist,
                     const chess::Move &pv_move, const chess::Move &prev_move, int ply)
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

                score +=
                    m_history[position.sideToMove()][move.from().index()][move.to().index()].get();
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

    bool is_quiet(const chess::Board &position, const chess::Move &move) const
    {
        return !position.isCapture(move);
    }

    void update_history(const chess::Board &position, const chess::Move &move, int bonus)
    {
        m_history[position.sideToMove()][move.from().index()][move.to().index()].add(bonus);
    }

    void store_killer(const chess::Board &position, const chess::Move &killer, int ply)
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

    void ply_init(int ply)
    {
        pv_length[ply] = ply;
    }

    void update(int ply, const chess::Move &move)
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

enum search_node_type
{
    NonPV,
    PV,
    Root
};

struct search_stack
{
    int ply;
    chess::Move current_move;
    bool move_captured;
    bool in_check;
    bool tt_pv;
    bool tt_hit;
    int reduction;
    int32_t static_eval;
};

struct engine
{
    chess::Board m_position;
    timer m_timer;
    engine_stats m_stats;
    const engine_param m_param;

    table m_table;
    move_ordering m_move_ordering;
    pv_line m_line;
    std::array<search_stack, param::MAX_DEPTH + 1> m_stack;

    endgame_table *m_endgame = nullptr;
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

    int32_t qsearch(int32_t alpha, int32_t beta, int base_ply, int ply)
    {
        m_stats.sel_depth = std::max(m_stats.sel_depth, base_ply + ply);
        m_stats.nodes_searched += 1;
        if (m_stats.nodes_searched % 2048 == 0)
            m_timer.check();

        if (m_timer.is_stopped())
            return 0;

        if (base_ply + ply >= param::MAX_DEPTH)
            return evaluate();

        int32_t best_score = evaluate();
        bool in_check = ply <= 2 && m_position.inCheck();

        if (!in_check && best_score >= beta)
            return best_score;

        if (best_score > alpha)
            alpha = best_score;

        chess::Movelist moves;
        if (in_check)
            chess::movegen::legalmoves(moves, m_position);
        else
        {
            chess::movegen::legalmoves<chess::movegen::MoveGenType::CAPTURE>(moves, m_position);
        }

        m_move_ordering.score_moves(m_position, moves, chess::Move::NULL_MOVE,
                                    chess::Move::NULL_MOVE, base_ply);

        for (int i = 0; i < moves.size(); ++i)
        {
            m_move_ordering.sort_moves(moves, i);

            const chess::Move &move = moves[i];
            if (see::test(m_position, move) < 0)
                continue;

            make_move(move);
            int32_t score = -qsearch(-beta, -alpha, base_ply, ply + 1);
            unmake_move(move);

            if (score > best_score)
                best_score = score;

            if (score >= beta)
                break;

            if (score > alpha)
            {
                alpha = score;
            }
        }

        return best_score;
    }

    template <search_node_type nodetype>
    int32_t negamax(int32_t alpha, int32_t beta, int depth, search_stack *ss, bool cutnode)
    {
        // update pv line
        int ply = ss->ply;
        m_line.ply_init(ply);

        // check for timers
        m_stats.nodes_searched += 1;
        if (m_stats.nodes_searched % 2048 == 0)
            m_timer.check();

        if (m_timer.is_stopped())
            return 0;

        if (ply >= param::MAX_DEPTH)
            return evaluate();

        constexpr bool pv_node = nodetype != NonPV;
        constexpr bool root_node = nodetype == Root;
        const bool all_node = !(pv_node || cutnode);

        // qsearch immediately
        if (depth <= 0)
        {
            m_stats.nodes_searched -= 1;
            return qsearch(alpha, beta, ply, 0);
        }

        depth = std::min(depth, param::MAX_DEPTH - 1);

        // check draw by rep
        if (!root_node && (m_position.isInsufficientMaterial()) &&
            alpha < param::VALUE_DRAW)
        {
            alpha = 0;
            if (alpha >= beta)
                return alpha;
        }

        // set stack info
        ss->in_check = m_position.inCheck();
        ss->static_eval = 0;

        if (!root_node)
        {
            // 50 move limit draw
            bool is_half_move = m_position.isHalfMoveDraw();
            if (is_half_move || m_position.isInsufficientMaterial() || m_position.isRepetition(3))
            {
                if (is_half_move)
                {
                    auto [_, type] = m_position.getHalfMoveDrawType();
                    if (type != chess::GameResult::DRAW)
                        return param::MATED_IN(ply);

                    return param::VALUE_DRAW;
                }

                // if (!ss->in_check)
                //     return evaluate();

                return param::VALUE_DRAW;
            }

            // mate distance pruning
            alpha = std::max(param::MATED_IN(ply), alpha);
            beta = std::min(param::MATE_IN(ply + 1), beta);
            if (alpha >= beta)
                return alpha;
        }

        // [tt lookup]
        auto &entry = m_table.probe(m_position.hash());
        auto tt_result = entry.get(m_position.hash(), ply, depth, alpha, beta);
        ss->tt_hit = tt_result.hit;
        // ss->tt_pv = pv_node; // todo: store is pv
        // bool tt_capture =
        // tt_result.move != chess::Move::NULL_MOVE && m_position.isCapture(tt_result.move);
        if (tt_result.hit && !root_node)
        {
            return tt_result.score;
        }

        // check syzygy
        if (m_endgame != nullptr && !root_node && m_endgame->is_stored(m_position))
        {
            int32_t score = m_endgame->probe_wdl(m_position, static_cast<int>(ply));
            entry.set(m_position.hash(), score, chess::Move::NULL_MOVE, ply, param::TB_DEPTH,
                      param::EXACT_FLAG);
            return score;
        }

        const bool has_non_pawn = (m_position.occ().count() -
                                   (2 + m_position.pieces(chess::PieceType::PAWN).count())) > 0;
        if (ss->in_check)
        {
            goto moves_loop;
        }

        ss->static_eval = evaluate();

        // [static null move pruning]
        if (!pv_node && std::abs(beta) < param::CHECKMATE)
        {
            int32_t static_score = ss->static_eval;
            int32_t margin = m_param.static_null_base_margin * depth;
            if (static_score - margin >= beta)
                return static_score - margin;
        }

        // [futility pruning]

        // [null move pruning]
        if (cutnode && has_non_pawn && depth <= 3 && beta > -param::CHECKMATE)
        {
            int16_t reduction = m_param.nmp_depth_base + depth / m_param.nmp_depth_multiplier;
            m_position.makeNullMove();
            int32_t null_score =
                -negamax<NonPV>(-beta, -beta + 1, depth - reduction, ss + 1, false);
            m_position.unmakeNullMove();

            if (m_timer.is_stopped())
                return 0;

            if (null_score >= beta && null_score < param::CHECKMATE)
            {
                return null_score;
            }
        }

    moves_loop:
        const chess::Move &prev_move = (ss - 1)->current_move;
        uint8_t tt_flag = param::ALPHA_FLAG;
        int move_count = 0;

        // [tt-move generation]
        chess::Movelist moves{};
        bool lazy_move_gen = tt_result.move != chess::Move::NULL_MOVE;
        if (lazy_move_gen)
        {
            tt_result.move.setScore(0);
            moves.add(tt_result.move);
        }
        else
        {
            chess::movegen::legalmoves(moves, m_position);
            m_move_ordering.score_moves(m_position, moves, tt_result.move, prev_move, ply);
        }

        int32_t best_score = std::numeric_limits<int32_t>::min();
        chess::Move best_move = chess::Move::NULL_MOVE;

        // track quiet moves for malus
        chess::Move quiet_moves[param::QUIET_MOVES]{};
        size_t quiet_count = 0;

        for (int i = 0; i < moves.size(); ++i)
        {
            m_move_ordering.sort_moves(moves, i);
            const chess::Move &move = moves[i];

            move_count += 1;

            ss->current_move = move;
            // ss->move_captured = m_position.isCapture(move);
            make_move(move);

            int32_t score = best_score;
            int new_depth = depth - 1;
            // int reduction = m_param.lmr[depth][move_count] * 1024;

            // reductions
            // if (ss->tt_pv)
            //     reduction += 1000;

            // if (cutnode)
            //     reduction += 2000;
            //
            // if (tt_capture)
            //     reduction += 1000;
            //
            // if (move == tt_result.move)
            //     reduction -= 2000;
            //
            // if (all_node)
            //     reduction += reduction / (depth + 1);

            if (move_count == 1)
            {
                score = -negamax<PV>(-beta, -alpha, new_depth, ss + 1, false);
            }
            else
            {
                // [late move reduction]
                int16_t reduction = 0;
                if (!pv_node && move_count >= 4 && depth >= 3)
                    reduction = m_param.lmr[depth][move_count];

                // [pv search]
                score = -negamax<NonPV>(-(alpha + 1), -alpha, depth - 1 - reduction, ss + 1, true);
                if (alpha < score && reduction > 0)
                {
                    score = -negamax<NonPV>(-(alpha + 1), -alpha, depth - 1, ss + 1, true);
                    if (alpha < score)
                    {
                        score = -negamax<NonPV>(-beta, -alpha, depth - 1, ss + 1, false);
                    }
                }
                else if (alpha < score && score < beta)
                {
                    score = -negamax<NonPV>(-beta, -alpha, depth - 1, ss + 1, false);
                }

                //
                // // [late move reduction]
                // if (depth >= 2 && move_count > 1)
                // {
                //     int d = std::max(1, std::min(new_depth - reduction / 1024, new_depth + 2)) +
                //             static_cast<int>(pv_node);
                //
                //     ss->reduction = new_depth - d;
                //     score = -negamax<NonPV>(-(alpha + 1), -alpha, d, ss + 1, true);
                //     ss->reduction = 0;
                //
                //     if (score > alpha)
                //     {
                //         // if (d < new_depth && score > best_score + 50)
                //         //     new_depth += 1;
                //         //
                //         // if (score < best_score)
                //         //     new_depth -= 1;
                //
                //         // if (new_depth > d)
                //         score = -negamax<NonPV>(-beta, -alpha, new_depth, ss + 1, !cutnode);
                //     }
                // }
                // // [full depth search if no lmr]
                // else if (!pv_node || move_count > 1)
                // {
                //     if (tt_result.move == chess::Move::NULL_MOVE)
                //         reduction += 1000;
                //
                //     int d = new_depth - (reduction > 4000) - (reduction > 5000 && new_depth > 2);
                //     score = -negamax<NonPV>(-(alpha + 1), -alpha, d, ss + 1, !cutnode);
                // }
            }

            unmake_move(move);

            if (m_timer.is_stopped())
                return 0;

            if (score > best_score)
            {
                best_score = score;
                if (score > alpha)
                {
                    best_move = move;
                    if (score >= beta)
                    {
                        tt_flag = param::BETA_FLAG;
                        break;
                    }

                    alpha = score;
                    tt_flag = param::EXACT_FLAG;
                    m_line.update(ply, move);
                }
            }

            // malus save
            if (move != best_move && m_move_ordering.is_quiet(m_position, move) &&
                quiet_count < param::QUIET_MOVES)
                quiet_moves[quiet_count++] = move;

            if (lazy_move_gen && move_count == 1)
            {
                chess::movegen::legalmoves(moves, m_position);
                m_move_ordering.score_moves(m_position, moves, tt_result.move, prev_move, ply);
                m_move_ordering.sort_moves(moves, 0);
            }
        }

        // checkmate or draw
        if (move_count == 0)
        {
            if (ss->in_check)
                return param::MATED_IN(ply);

            return param::VALUE_DRAW;
        }

        if (best_move != chess::Move::NULL_MOVE)
        {
            // bonus
            m_move_ordering.incr_counter(m_position, prev_move, best_move);
            m_move_ordering.store_killer(m_position, best_move, ply);
            if (m_move_ordering.is_quiet(m_position, best_move))
            {
                const int bonus = 300 * depth - 250;
                m_move_ordering.update_history(m_position, best_move, bonus);

                // malus apply
                for (size_t j = 0; j < quiet_count; ++j)
                    m_move_ordering.update_history(m_position, quiet_moves[j], -bonus / 2);
            }
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

        for (int i = 0; i < m_stack.size(); ++i)
        {
            m_stack[i] = {.ply = i - 1,
                          .current_move = chess::Move::NULL_MOVE,
                          .move_captured = false,
                          .in_check = false,
                          .tt_pv = false,
                          .tt_hit = false,
                          .reduction = 0};
        }

        int32_t alpha = -param::INF, beta = param::INF;
        int depth = 1;

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

        while (depth <= control.depth)
        {
            int32_t score = negamax<Root>(alpha, beta, depth, &m_stack[1], false);
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
