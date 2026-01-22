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
    uint32_t nodes_searched;
    uint8_t tt_occupancy;
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
        uint32_t nps = nodes_searched * 1000 / std::max(1L, total_time.count());

        printf(
            "info depth %d seldepth %d multipv 1 score cp %d nodes %d nps %d time %ld ttocc %d pv",
            result.depth,
            result.depth,
            result.score,
            nodes_searched,
            nps,
            total_time.count(),
            tt_occupancy
        );

        for (auto &m: result.pv_line)
        {
            std::cout << " " << chess::uci::moveToUci(m);
        }
        std::cout << std::endl;
    }
};

struct engine_param
{
    int lmr[param::MAX_DEPTH][100];
    int lmr_depth_ration = 4;
    int lmr_move_ratio = 12;

    explicit engine_param()
    {
        for (int depth = 0; depth < param::MAX_DEPTH; ++depth)
        {
            for (int move = 0; move < 100; ++move)
            {
                lmr[depth][move] =
                        std::max(2, depth / std::max(1, lmr_depth_ration)) + move / std::max(1, lmr_move_ratio);
            }
        }
    }
};

struct engine
{
    chess::Board m_position;
    timer m_timer;
    engine_stats m_stats;
    table m_table;
    engine_param m_param;

    // must be set via methods
    explicit engine(const int table_size_in_mb = 512)
        : m_stats(), m_table(table_size_in_mb)
    {
        // init tables
        pesto::init();
    };

    [[nodiscard]] int32_t evaluate() const
    {
        return pesto::evaluate(m_position);
    }

    int32_t negamax(
        int32_t alpha, int32_t beta,
        int16_t depth, uint8_t ply,
        std::vector<chess::Move> &pv_line
    )
    {
        m_stats.nodes_searched += 1;
        if (m_stats.nodes_searched % 2048 == 0)
            m_timer.check();

        if (m_timer.is_stopped())
            return 0;

        // check draw
        if (m_position.isInsufficientMaterial())
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


        bool is_root = ply == 0;
        bool is_pv_node = (beta - alpha) != 1;

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
        chess::Movelist moves;
        chess::movegen::legalmoves(moves, m_position);
        legal_moves = moves.size();

        int32_t best_score = std::numeric_limits<int32_t>::min();
        chess::Move best_move = chess::Move::NULL_MOVE;
        std::vector<chess::Move> child_pv_line;
        for (int i = 0; i < moves.size(); ++i)
        {
            const chess::Move &move = moves[i];
            m_position.makeMove(move);

            int32_t score;
            if (explored_moves == 0)
            {
                score = -negamax(-beta, -alpha, depth - 1, ply + 1, child_pv_line);
            } else
            {
                int16_t reduction = 0;
                if (!is_pv_node && explored_moves >= 4 && depth >= 3)
                    reduction = m_param.lmr[depth][explored_moves];

                // [pv search]
                score = -negamax(-(alpha + 1), -alpha, depth - 1 - reduction, ply + 1, child_pv_line);
                if (alpha < score && reduction > 0)
                {
                    child_pv_line.clear();
                    score = -negamax(-(alpha + 1), -alpha, depth - 1, ply + 1, child_pv_line);
                    if (alpha < score)
                    {
                        child_pv_line.clear();
                        score = -negamax(-beta, -alpha, depth - 1, ply + 1, child_pv_line);
                    }
                } else if (alpha < score && score < beta)
                {
                    child_pv_line.clear();
                    score = -negamax(-beta, -alpha, depth - 1, ply + 1, child_pv_line);
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
                break;
            }

            if (score > alpha)
            {
                tt_flag = param::EXACT_FLAG;
                alpha = score;
                pv_line.clear();
                pv_line.push_back(move);
                for (auto &m: child_pv_line)
                    pv_line.push_back(m);
            }

            child_pv_line.clear();
            explored_moves += 1;
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
            int32_t score = negamax(alpha, beta, depth, 0, pv_line);
            // TODO: use this extra info
            if (m_timer.is_stopped())
                break;

            // TODO: asp window

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
