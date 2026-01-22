#pragma once
#include <chrono>
#include <utility>

#include "chess.h"

struct board_wrapper
{
    chess::Board m_board;

    explicit board_wrapper(chess::Board board)
        : m_board(std::move(board))
    {
    }

    static board_wrapper from_fen(const std::string &fen)
    {
        return board_wrapper{chess::Board::fromFen(fen)};
    }

    [[nodiscard]] std::string display() const
    {
        std::string out{};

        out += std::format(
            "CA: {}, RE: {}, HA: {}, CH: {}\n",
            m_board.getCastleString(),
            (m_board.isRepetition() ? "Y" : "N"),
            m_board.isHalfMoveDraw() ? "Y" : "N",
            m_board.inCheck() ? "Y" : "N"
        );

        for (int rank = 7; rank >= 0; --rank)
        {
            out += "+";
            for (int i = 0; i < 8; ++i)
            {
                out += "---+";
            }
            out += "\n|";

            for (int file = 0; file < 8; ++file)
            {
                auto piece = m_board.at({chess::Rank{rank}, chess::File{file}});
                char p = ' ';
                if (piece.type() == chess::PieceType::NONE)
                    p = ' ';
                else if (piece.type() == chess::PieceType::BISHOP)
                    p = 'b';
                else if (piece.type() == chess::PieceType::KING)
                    p = 'k';
                else if (piece.type() == chess::PieceType::KNIGHT)
                    p = 'n';
                else if (piece.type() == chess::PieceType::PAWN)
                    p = 'p';
                else if (piece.type() == chess::PieceType::QUEEN)
                    p = 'q';
                else if (piece.type() == chess::PieceType::ROOK)
                    p = 'r';

                if (piece.color() == chess::Color::WHITE)
                    p = std::toupper(p);

                out += " ";
                out += p;
                out += " |";
            }
            out += "\n";
        }

        out += "+";
        for (int i = 0; i < 8; ++i)
        {
            out += "---+";
        }

        return out;
    }
};

// TODO: copy from board
struct tt
{
};


class timer
{
private:
    std::chrono::milliseconds m_target;
    bool m_is_stopped = false;
    bool m_forced_stopped = false;

public:
    void stop()
    {
        m_forced_stopped = true;
    }

    void unstop()
    {
        m_forced_stopped = false;
    }

    void start(int ms)
    {
        m_target = now() + std::chrono::milliseconds(ms);
        m_is_stopped = false;
        m_forced_stopped = false;
    }

    void add(int ms)
    {
        m_target = m_target + std::chrono::milliseconds(ms);
        m_is_stopped = false;
    }

    bool is_force_stopped() const
    {
        return m_forced_stopped;
    }

    bool is_stopped() const
    {
        return m_is_stopped || m_forced_stopped;
    }

    void check()
    {
        if (m_is_stopped || m_forced_stopped)
            return;

        auto current = now();
        if (current > m_target)
        {
            m_is_stopped = true;
        }
    }

    static std::chrono::milliseconds now()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch());
    }
};

namespace param
{
constexpr int32_t CHECKMATE = 9000000;
constexpr int32_t INF = 10000000;

// int exact_flag = 0;
// int alpha_flag = 1;
// int beta_flag = 2;

constexpr int MAX_DEPTH = 255;

constexpr int32_t BASE_SCORE = (1 << 30);

constexpr int32_t pv_move_score = 500;
constexpr int32_t killer_move_score = 480;
constexpr int32_t killer_move_score2 = 470;
constexpr int32_t end_move_score = 490;
}

struct search_result
{
    chess::Move best_move;
    uint8_t depth;
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
    std::chrono::milliseconds total_time;

    void display_delta(const engine_stats &old, const search_result &result) const
    {
        long delta = (total_time - old.total_time).count();
        uint32_t depth_nps = (nodes_searched - old.nodes_searched) / std::max(1L, delta);
        uint32_t nps = nodes_searched / total_time.count();

        printf(
            "[engine] depth %2d, nodes %10d, score %10s (%7d), nps %10d/%10d\n",
            result.depth,
            nodes_searched,
            result.get_score().c_str(),
            result.score,
            depth_nps,
            nps
        );
    }
};

// TODO: make engine
struct engine
{
    chess::Board m_position;
    timer m_timer;
    engine_stats m_stats;

    // must be set via methods
    explicit engine() = default;

    int32_t evaluate()
    {
        return 0;
    }

    int32_t qsearch()
    {
    }

    int32_t negamax(
        int32_t alpha, int32_t beta,
        uint8_t depth, uint8_t ply,
        std::vector<chess::Move> &pv_line
    )
    {
        m_stats.nodes_searched += 1;
        if (m_stats.nodes_searched % 2048 == 0)
            m_timer.check();

        if (m_timer.is_stopped())
            return 0;

        if (ply >= param::MAX_DEPTH)
            return evaluate();

        if (depth <= 0)
            return evaluate();

        std::vector<chess::Move> child_pv_line;

        chess::Movelist moves;
        chess::movegen::legalmoves(moves, m_position);

        int32_t best_score = std::numeric_limits<int32_t>::min();
        chess::Move best_move = chess::Move::NULL_MOVE;
        for (int i = 0; i < moves.size(); ++i)
        {
            const chess::Move &move = moves[i];
            m_position.makeMove(move);
            int32_t score = -negamax(-beta, -alpha, depth - 1, ply + 1, child_pv_line);
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
                break;
            }

            if (score > alpha)
            {
                alpha = score;
                pv_line.clear();
                pv_line.push_back(move);
                for (auto &m: child_pv_line)
                    pv_line.push_back(m);
            }

            child_pv_line.clear();
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


    search_result search(const chess::Board &reference, int ms, bool verbose = true)
    {
        m_position = reference;
        m_timer.start(ms);
        auto reference_time = timer::now();
        m_stats = engine_stats{0, timer::now() - reference_time};

        std::vector<chess::Move> pv_line{};
        int32_t alpha = -param::INF, beta = param::INF;
        uint8_t depth = 1;

        engine_stats last_stats = m_stats;

        search_result result{};
        while (depth < param::MAX_DEPTH)
        {
            int32_t score = negamax(alpha, beta, depth, 0, pv_line);
            // TODO: use this extra info
            if (m_timer.is_stopped())
                break;

            // TODO: asp window

            result.score = score;
            result.best_move = pv_line[0];
            result.depth = depth;


            // display info
            if (verbose)
            {
                m_stats.total_time = timer::now() - reference_time;
                m_stats.display_delta(last_stats, result);
                last_stats = m_stats;
            }

            depth += 1;
            pv_line.clear();
        }

        return result;
    }
};
