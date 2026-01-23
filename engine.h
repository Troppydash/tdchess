#pragma once
#include <chrono>
#include <utility>

#include "chess.h"
#include "param.h"
#include "evaluation.h"
#include "timer.h"
#include "table.h"

// TODO: copy from board
struct tt
{
};


struct search_result
{
    std::vector<chess::Move> pv_line;
    int16_t depth;
    int32_t score;

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
    std::chrono::milliseconds total_time;

    void display_delta(const engine_stats &old, const search_result &result) const
    {
        long delta = (total_time - old.total_time).count();
        uint32_t depth_nps = (nodes_searched - old.nodes_searched) * 1000 / std::max(1L, delta);
        uint32_t nps = nodes_searched * 1000 / std::max(1L, total_time.count());

        printf(
            "info depth %2d, nodes %10d, score %10s (%7d), nps %10d/%10d, moves",
            result.depth,
            nodes_searched,
            result.get_score().c_str(),
            result.score,
            depth_nps,
            nps
        );

        for (auto &m: result.pv_line)
        {
            std::cout << " " << chess::uci::moveToUci(m);
        }
        std::cout << std::endl;
    }

    void display_delta_uci(const engine_stats &old, const search_result &result) const
    {
        // long delta = (total_time - old.total_time).count();
        // uint32_t depth_nps = (nodes_searched - old.nodes_searched) * 1000 / std::max(1L, delta);
        long nps = static_cast<long>(nodes_searched) * 1000 / std::max(1L, total_time.count());

        std::cout << "info depth " << result.depth
                << " seldepth " << result.depth
                << " multipv 1"
                << " score cp " << result.score
                << " nodes " << nodes_searched
                << " nps " << nps
                << " time " << total_time.count()
                << " ttoc " << tt_occupancy
                << " pv";

        for (auto &m: result.pv_line)
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
    int lmr_depth_ration = 4;
    int lmr_move_ratio = 12;
    int window_size = 100;

    // move ordering
    std::array<std::array<int16_t, 6>, 7> mvv_lva;
    int16_t mvv_offset = 1 << 14;
    int16_t pv_score = 200;
    int16_t first_killer = -10;
    int16_t second_killer = -20;
    int16_t counter_bonus = 15;

    // TODO: move ordering

    explicit engine_param()
    {
        // set lmr
        for (int depth = 0; depth < param::MAX_DEPTH; ++depth)
            for (int move = 0; move < 100; ++move)
                lmr[depth][move] =
                        std::max(2, depth / std::max(1, lmr_depth_ration)) + move / std::max(1, lmr_move_ratio);

        // set mvv_lva
        mvv_lva = {
            std::array<int16_t, 6>{15, 14, 13, 12, 11, 10}, // victim Pawn
            {25, 24, 23, 22, 21, 20}, // victim Knight
            {35, 34, 33, 32, 31, 30}, // victim Bishop
            {45, 44, 43, 42, 41, 40}, // victim Rook
            {55, 54, 53, 52, 51, 50}, // victim Queen
            {0, 0, 0, 0, 0, 0}, // victim King
            {0, 0, 0, 0, 0, 0}, // No piece
        };
    }
};

struct move_ordering
{
    chess::Move m_killers[param::MAX_DEPTH][2];
    chess::Move m_counter[2][64][64];
    int16_t m_history[2][64][64];
    const engine_param m_param;

    explicit move_ordering(const engine_param &param)
        : m_param(param)
    {
        // init killer/counter/history
        for (auto &i: m_history)
            for (auto &j: i)
                for (int16_t &k: j)
                    k = 0;

        for (auto &m: m_killers)
        {
            m[0] = m[1] = chess::Move::NULL_MOVE;
        }

        for (auto &i: m_counter)
            for (auto &j: i)
                for (auto &k: j)
                    k = chess::Move::NULL_MOVE;
    }


    void score_moves(
        const chess::Board &position,
        chess::Movelist &movelist,
        const chess::Move &pv_move,
        const chess::Move &prev_move,
        int ply
    )
    {
        for (auto &move: movelist)
        {
            int16_t score = 0;
            auto captured = position.at(move.to()).type();

            if (move == pv_move)
                score += m_param.mvv_offset + m_param.pv_score;
            else if (captured != chess::PieceType::NONE)
            {
                auto moved = position.at(move.from()).type();
                score += m_param.mvv_offset + m_param.mvv_lva[captured][moved];
            } else if (move == m_killers[ply][0])
                score += m_param.mvv_offset + m_param.first_killer;
            else if (move == m_killers[ply][1])
                score += m_param.mvv_offset + m_param.second_killer;
            else
            {
                const auto &counter = m_counter[position.sideToMove()]
                        [prev_move.from().index()][prev_move.to().index()];
                int16_t history_score = m_history[position.sideToMove()]
                        [move.from().index()][move.to().index()];

                if (move == counter)
                    score += m_param.counter_bonus;

                score += history_score;
            }

            move.setScore(score);
        }
    }

    void sort_moves(
        chess::Movelist &movelist, int i
    )
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
        for (auto &i: m_history)
        {
            for (auto &j: i)
            {
                for (int16_t &k: j)
                {
                    k /= 2;
                }
            }
        }
    }

    bool is_slient_move(const chess::Board &position, const chess::Move &move) const
    {
        return position.at(move.from()).type() == chess::PieceType::NONE;
    }

    void incr_history(const chess::Board &position, const chess::Move &move, int16_t depth)
    {
        if (is_slient_move(position, move))
            m_history[position.sideToMove()][move.from().index()][move.to().index()] += depth * depth;

        if (m_history[position.sideToMove()][move.from().index()][move.to().index()] >= m_param.mvv_offset)
            age_history();
    }

    void decr_history(const chess::Board &position, const chess::Move &move)
    {
        // if (!move.is_slient())
        //     return;

        if (is_slient_move(position, move))
            if (m_history[position.sideToMove()][move.from().index()][move.to().index()] > 0)
                m_history[position.sideToMove()][move.from().index()][move.to().index()] -= 1;
    }

    void store_killer(const chess::Board &position, const chess::Move &killer, int ply)
    {
        if (is_slient_move(position, killer))
        {
            if (m_killers[ply][0] != killer)
            {
                m_killers[ply][1] = m_killers[ply][0];
                m_killers[ply][0] = killer;
            }
        }
    }

    void incr_counter(const chess::Board &position, const chess::Move &prev_move, const chess::Move &move)
    {
        if (is_slient_move(position, move))
            m_counter[position.sideToMove()][prev_move.from().index()][prev_move.to().index()] = move;
    }
};

struct engine
{
    chess::Board m_position;
    timer m_timer;
    engine_stats m_stats;
    const engine_param m_param;

    table m_table;
    move_ordering m_move_ordering;

    // must be set via methods
    explicit engine(const int table_size_in_mb = 512)
        : m_stats(), m_table(table_size_in_mb), m_move_ordering(m_param)

    {
        // init tables
        pesto::init();
    };

    [[nodiscard]] int32_t evaluate() const
    {
        int32_t tempo = 15;
        return pesto::evaluate(m_position) + tempo;
    }

    int32_t qsearch() const
    {
        // TODO: this
        return 0;
    }


    int32_t negamax(
        int32_t alpha, int32_t beta,
        int16_t depth, uint8_t ply,
        std::vector<chess::Move> &pv_line,
        const chess::Move &prev_move
    )
    {
        m_stats.nodes_searched += 1;
        if (m_stats.nodes_searched % 2048 == 0)
            m_timer.check();

        if (m_timer.is_stopped())
            return 0;

        // check draw
        if (m_position.isInsufficientMaterial() || m_position.isRepetition(2))
            return 0;

        // 50 move limit
        if (m_position.isHalfMoveDraw())
        {
            auto [_, type] = m_position.getHalfMoveDrawType();
            if (type == chess::GameResult::DRAW)
                return 0;

            return -param::INF + ply;
        }

        if (ply >= param::MAX_DEPTH)
            return evaluate();

        if (depth <= 0)
            return evaluate();

        const bool is_root = ply == 0;
        const bool is_pv_node = (beta - alpha) != 1;

        // [tt lookup]
        auto &entry = m_table.probe(m_position.hash());
        auto tt_result = entry.get(m_position.hash(), ply, depth, alpha, beta);
        if (tt_result.hit && !is_root)
        {
            return tt_result.score;
        }

        uint8_t tt_flag = param::ALPHA_FLAG;
        int legal_moves = 0;
        int explored_moves = 0;

        // [tt-move generation]
        chess::Movelist moves;
        bool lazy_move_gen = tt_result.move != chess::Move::NULL_MOVE;
        if (lazy_move_gen)
        {
            tt_result.move.setScore(0);
            moves.add(tt_result.move);
            legal_moves = 1;
        } else
        {
            chess::movegen::legalmoves(moves, m_position);
            m_move_ordering.score_moves(m_position, moves, tt_result.move, prev_move, ply);
            legal_moves = moves.size();
        }

        int32_t best_score = std::numeric_limits<int32_t>::min();
        chess::Move best_move = chess::Move::NULL_MOVE;
        std::vector<chess::Move> child_pv_line;
        for (int i = 0; i < moves.size(); ++i)
        {
            m_move_ordering.sort_moves(moves, i);
            const chess::Move &move = moves[i];
            m_position.makeMove(move);

            int32_t score;
            if (explored_moves == 0)
            {
                score = -negamax(-beta, -alpha, depth - 1, ply + 1, child_pv_line, move);
            } else
            {
                int16_t reduction = 0;
                if (!is_pv_node && explored_moves >= 4 && depth >= 3)
                    reduction = m_param.lmr[depth][explored_moves];

                // [pv search]
                score = -negamax(-(alpha + 1), -alpha, depth - 1 - reduction, ply + 1, child_pv_line, move);
                if (alpha < score && reduction > 0)
                {
                    child_pv_line.clear();
                    score = -negamax(-(alpha + 1), -alpha, depth - 1, ply + 1, child_pv_line, move);
                    if (alpha < score)
                    {
                        child_pv_line.clear();
                        score = -negamax(-beta, -alpha, depth - 1, ply + 1, child_pv_line, move);
                    }
                } else if (alpha < score && score < beta)
                {
                    child_pv_line.clear();
                    score = -negamax(-beta, -alpha, depth - 1, ply + 1, child_pv_line, move);
                }
            }

            m_position.unmakeMove(move);

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
                m_move_ordering.incr_history(m_position, move, depth);
                break;
            } else
            {
                m_move_ordering.decr_history(m_position, move);
            }

            if (score > alpha)
            {
                tt_flag = param::EXACT_FLAG;
                alpha = score;
                pv_line.clear();
                pv_line.push_back(move);
                for (auto &m: child_pv_line)
                    pv_line.push_back(m);

                m_move_ordering.incr_history(m_position, move, depth);
            } else
            {
                m_move_ordering.decr_history(m_position, move);
            }

            child_pv_line.clear();
            explored_moves += 1;

            if (lazy_move_gen && explored_moves == 1)
            {
                chess::movegen::legalmoves(moves, m_position);
                m_move_ordering.score_moves(m_position, moves, tt_result.move, prev_move, ply);
                legal_moves = moves.size();
            }
        }

        // checkmate or draw
        if (legal_moves == 0)
        {
            if (m_position.inCheck())
                return -param::INF + ply;

            // draw
            return 0;
        }

        if (depth > entry.m_depth && !m_timer.is_stopped())
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
        for (const auto &move: moves)
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
        cout << "nps: " << nodes * 1000 / ms << endl;
        std::cout.imbue(original);
    }


    search_result search(const chess::Board &reference, int16_t max_depth, int ms, bool verbose = false,
                         bool uci = false)
    {
        m_position = reference;
        m_timer.start(ms);
        auto reference_time = timer::now();
        m_stats = engine_stats{0, 0, timer::now() - reference_time};

        std::vector<chess::Move> pv_line{};
        int32_t alpha = -param::INF, beta = param::INF;
        int16_t depth = 1;

        engine_stats last_stats = m_stats;

        search_result result{};
        while (depth <= max_depth)
        {
            chess::Move null = chess::Move::NULL_MOVE;
            int32_t score = negamax(alpha, beta, depth, 0, pv_line, null);
            // TODO: use this extra info

            if (m_timer.is_stopped())
                break;

            // [asp window]
            if (score <= alpha || score >= beta)
            {
                alpha = -param::INF;
                beta = param::INF;
                continue;
            }

            alpha = score - m_param.window_size;
            beta = score + m_param.window_size;

            result.score = score;
            result.pv_line = std::vector<chess::Move>(pv_line.begin(), pv_line.end());
            result.depth = depth;


            // display info
            if (verbose)
            {
                m_stats.total_time = timer::now() - reference_time;
                m_stats.tt_occupancy = m_table.occupied();
                if (uci)
                    m_stats.display_delta_uci(last_stats, result);
                else
                    m_stats.display_delta(last_stats, result);
                last_stats = m_stats;
            }


            depth += 1;
            pv_line.clear();
        }

        return result;
    }
};
