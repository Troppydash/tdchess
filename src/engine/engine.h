#pragma once
#include <chrono>
#include <memory>
#include <utility>

#include "chess.h"
#include "chess960.h"
#include "chessmap.h"
#include "cuckoo.h"
#include "endgame.h"
#include "features.h"
#include "legal.h"
#include "movegen.h"
#include "nnue2.h"
#include "param.h"
#include "rep.h"
#include "see.h"
#include "table.h"
#include "time_control.h"
#include "timer.h"
#include "util.h"
#include <iomanip>
#include <vector>

struct search_result
{
    std::vector<chess::Move> pv_line{};
    int32_t depth{0};
    int16_t score{0};

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

    long get_nps() const
    {
        return static_cast<long>(nodes_searched) * 1000 /
               std::max(static_cast<int64_t>(1), total_time.count());
    }

    void display_uci(const search_result &result) const
    {
        std::cout << "info depth " << result.depth << " seldepth " << sel_depth << " multipv 1"
                  << " score " << result.get_score_uci() << " nodes " << nodes_searched << " nps "
                  << get_nps() << " time " << total_time.count() << " hashfull " << tt_occupancy
                  << " pv";

        for (auto &m : result.pv_line)
        {
            std::cout << " " << chess::uci::moveToUci(m, global::chess_960);
        }
        std::cout << std::endl;
    }

    engine_stats append(const engine_stats &other) const
    {
        return engine_stats{.nodes_searched = nodes_searched + other.nodes_searched,
                            .tt_occupancy = std::max(tt_occupancy, other.tt_occupancy),
                            .sel_depth = std::max(sel_depth, other.sel_depth),
                            .total_time = std::max(total_time, other.total_time)};
    }
};

struct lmr_table
{
    int16_t lmr[64][64]{};

    explicit lmr_table()
    {
        // set lmr
        for (int depth = 1; depth < 64; ++depth)
            for (int move = 1; move < 64; ++move)
                lmr[depth][move] = lmr_quiet(depth, move);
    }

    static constexpr int lmr_quiet(int depth, int move)
    {
        return std::floor(0.99 + std::log(depth) * std::log(move) / 3.14);
    }

    [[nodiscard]] int lookup(bool is_quiet, int depth, int move) const
    {
        return lmr[std::min(63, depth)][std::min(63, move)];
    }
};

struct position_pawn_keys
{
    std::array<uint64_t, param::MAX_DEPTH> pawn_keys{};
    std::array<uint64_t[2], param::MAX_DEPTH> non_pawn_keys{};
    int head = 0;

    void initialize(const chess::Board &board)
    {
        head = 0;
        pawn_keys[head] = get_pawn_key(board);
        non_pawn_keys[head][0] = get_corrhist_key(board, chess::Color::WHITE);
        non_pawn_keys[head][1] = get_corrhist_key(board, chess::Color::BLACK);
    }

    void make_move(const chess::Board &board, chess::Move move)
    {
        head += 1;
        pawn_keys[head] = pawn_keys[head - 1];
        non_pawn_keys[head][0] = non_pawn_keys[head - 1][0];
        non_pawn_keys[head][1] = non_pawn_keys[head - 1][1];

        auto touch_piece = [&](chess::Piece piece, chess::Square sq) {
            if (piece.type() == chess::PieceType::PAWN)
                pawn_keys[head] ^= chess::Zobrist::piece(piece, sq);
            else
                non_pawn_keys[head][piece.color()] ^= chess::Zobrist::piece(piece, sq);
        };

        assert(move != chess::Move::NO_MOVE);
        switch (move.typeOf())
        {
        case chess::Move::NORMAL: {
            if (board.at(move.to()) != chess::Piece::NONE)
                touch_piece(board.at(move.to()), move.to());

            touch_piece(board.at(move.from()), move.from());
            touch_piece(board.at(move.from()), move.to());

            break;
        }
        case chess::Move::PROMOTION: {
            if (board.at(move.to()) != chess::Piece::NONE)
                touch_piece(board.at(move.to()), move.to());

            touch_piece(board.at(move.from()), move.from());
            touch_piece(chess::Piece{move.promotionType(), board.at(move.from()).color()},
                        move.to());
            break;
        }

        case chess::Move::ENPASSANT: {
            touch_piece(board.at(move.from()), move.from());
            touch_piece(board.at(move.from()), move.to());
            touch_piece(board.at(move.to().ep_square()), move.to().ep_square());
            break;
        }

        case chess::Move::CASTLING: {
            const bool king_side = move.to() > move.from();
            const chess::Square rook_to =
                chess::Square::castling_rook_square(king_side, board.sideToMove());
            const chess::Square king_to =
                chess::Square::castling_king_square(king_side, board.sideToMove());

            touch_piece(board.at(move.from()), move.from());
            touch_piece(board.at(move.to()), move.to());

            touch_piece(board.at(move.from()), king_to);
            touch_piece(board.at(move.to()), rook_to);
            break;
        }
        }
    }

    void unmake_move()
    {
        head -= 1;
    }

    uint64_t get_pawn_key() const
    {
        return pawn_keys[head];
    }

    uint64_t get_white_key() const
    {
        return non_pawn_keys[head][0];
    }

    uint64_t get_black_key() const
    {
        return non_pawn_keys[head][1];
    }

  private:
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
};

#define MOVEGEN_STRICT

struct search_stack
{
    int32_t ply = 0;
    int16_t static_eval = param::VALUE_NONE;
    chess::Move move = chess::Move::NO_MOVE;
    uint64_t key = 0;
    chess::Move excluded_move = chess::Move::NO_MOVE;
    bool in_check = false;
    bool tt_pv = false;
    bool tt_hit = false;
    bool is_cap = false;
    int move_count = 0;
    continuation_history *continuation = nullptr;
    continuation_correction_history *cont_corr = nullptr;
    std::array<std::array<chess::Move, param::QUIET_MOVES>, 2> quiet_moves{};
    std::array<std::array<chess::Move, param::QUIET_MOVES>, 2> capture_moves{};
    std::array<chess::Movelist, 2> moves{};
    int complex = 0;

    bool verify_null = false;

    // records the pv line
    std::array<chess::Move, param::MAX_DEPTH + 4> pv;
    int pv_length;

    void reset(heuristics &heuristics)
    {
        ply = 0;
        static_eval = param::VALUE_NONE;
        move = chess::Move::NO_MOVE;
        excluded_move = chess::Move::NO_MOVE;
        in_check = false;
        tt_pv = false;
        tt_hit = false;
        is_cap = false;
        move_count = 0;
        continuation =
            &(heuristics.continuation[0][0][static_cast<uint8_t>(chess::Piece::NONE)][0]);
        cont_corr = &(heuristics.cont_corr[static_cast<uint8_t>(chess::Piece::NONE)][0]);

        complex = 0;
        key = 0;

        verify_null = false;

        pv.fill(chess::Move::NO_MOVE);
        pv_length = 0;
    }

    void pv_init()
    {
        pv_length = 0;
    }

    void pv_update(chess::Move move, search_stack *ss_next)
    {
        pv[0] = move;
        for (int i = 0; i < ss_next->pv_length; ++i)
        {
            pv[1 + i] = ss_next->pv[i];
        }
        pv_length = ss_next->pv_length + 1;
    }

    std::vector<chess::Move> get_pv() const
    {
        std::vector<chess::Move> moves(pv_length);
        for (int i = 0; i < pv_length; ++i)
            moves[i] = pv[i];

        return moves;
    }
};

struct root_move_list
{
    struct root_move
    {
        chess::Move move;
        int average_score;
        int score;
    };

    std::array<root_move, chess::constants::MAX_MOVES> moves{};
    int size;

    void load(const chess::Board &position)
    {
        chess::Movelist tmp;
        chess::movegen::legalmoves(tmp, position);

        size = 0;
        for (auto m : tmp)
        {
            moves[size++] = {
                .move = m,
                .average_score = param::VALUE_NONE,
                .score = -param::INF,
            };
        }
    }

    bool is_singular() const
    {
        return size == 1;
    }

    root_move &get_by_move(chess::Move src)
    {
        for (auto &m : moves)
            if (m.move == src)
                return m;

        std::cout << "invalid move " << chess::uci::moveToUci(src, global::chess_960) << std::endl;
        exit(0);
    }

    const root_move &get_pv() const
    {
        return moves[0];
    }

    void sort()
    {
        // insertion sort
        for (int i = 1; i < size; ++i)
        {
            auto key = moves[i];
            int j = i - 1;
            while (j >= 0 && moves[j].score < key.score)
            {
                moves[j + 1] = moves[j];
                --j;
            }
            moves[j + 1] = key;
        }

        // std::cout << '\n';
        // for (int i = 0; i < std::min(4, size); ++i)
        // {
        //     auto m = moves[i];
        //     std::cout << chess::uci::moveToUci(m.move) << "/" << m.score << ", ";
        // }
        // std::cout << '\n';
    }

    void partial_sort()
    {
        const int depth_cost = 10;
        int best_index = 0;
        for (int i = 1; i < size; ++i)
        {
            if (moves[i].score > moves[best_index].score)
            {
                if (moves[best_index].score > -param::INF)
                {
                    moves[best_index].score -= depth_cost;
                }

                best_index = i;
            }
            else
            {
                if (moves[i].score > -param::INF)
                {
                    moves[i].score -= depth_cost;
                }
            }
        }

        std::swap(moves[0], moves[best_index]);
    }
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
    const lmr_table m_param;
    // tt
    table *m_table;
    // move ordering
    std::unique_ptr<heuristics> m_heuristics = std::make_unique<heuristics>();
    // search stack
    constexpr static int SEARCH_STACK_PREFIX = 10;
    search_stack *m_stack = nullptr;
    // endgame table ref
    endgame_table *m_endgame = nullptr;
    // nnue ref
    nnue2::net *m_nnue = nullptr;

    // repetition filter
    rep_filter m_filter{};

    // pawn keys
    position_pawn_keys m_keys{};

    // root move list
    root_move_list m_root_moves{};

    int16_t contempt_score[64]{};

    std::unique_ptr<chessmap::net> m_chessmap;

    // must be set via methods
    explicit engine(table *table) : engine(nullptr, nullptr, table)
    {
    }

    explicit engine(endgame_table *endgame, nnue2::net *nnue, table *table)
        : m_stats(), m_param{}, m_table(table), m_endgame(endgame), m_nnue(nnue)
    {
        // init tables
        if (m_nnue == nullptr)
        {
            std::cout << "no nnue\n";
            exit(0);
        }

        m_chessmap = std::make_unique<chessmap::net>();

        util::init();
        cuckoo::init();

        m_stack = new search_stack[param::MAX_DEPTH + SEARCH_STACK_PREFIX];
        post_search_smp();
        compute_contempt();
    }

    ~engine()
    {
        delete[] m_stack;
    }

    void compute_contempt()
    {
        for (int piece_count = 2; piece_count < 64; ++piece_count)
        {
            // increase this to draw more, decrease to draw less
            constexpr int cutoff = 24;
            if (piece_count <= cutoff)
                contempt_score[piece_count] = 0;
            else
            {
                int16_t MAX_CONTEMPT = global::contempt;
                contempt_score[piece_count] =
                    static_cast<int16_t>((MAX_CONTEMPT * (piece_count - cutoff)) / (32 - cutoff));
            }
        }
    }

    void post_search_smp()
    {
        // reset stack
        for (int i = SEARCH_STACK_PREFIX; i >= 0; --i)
        {
            m_stack[i].reset(*m_heuristics);
            m_stack[i].ply = 0;
        }

        for (int i = 0; i < param::MAX_DEPTH; ++i)
        {
            m_stack[i + SEARCH_STACK_PREFIX].reset(*m_heuristics);
            m_stack[i + SEARCH_STACK_PREFIX].ply = i;
        }

        // reset move ordering variables
        m_heuristics->begin();

        m_filter.clear();

        m_nnue->clear();
    }

    /**
     * Setup the engine for a new position in the same game
     */
    void begin()
    {
        // update stats
        auto reference_time = timer::now();
        m_stats = engine_stats{0, 0, 0, timer::now() - reference_time};

        // init nnue
        m_nnue->initialize(m_position);

        m_chessmap->initialize(m_position);

        m_filter.load(m_position);

        m_keys.initialize(m_position);

        m_root_moves.load(m_position);
    }

    [[nodiscard]] int16_t evaluate(search_stack *ss,
                                   chess::Move pv_move = chess::Move::NO_MOVE) const
    {
        // max is 100
        int phase = 2 * m_position.pieces(chess::PieceType::PAWN).count() +
                    3 * m_position.pieces(chess::PieceType::KNIGHT).count() +
                    3 * m_position.pieces(chess::PieceType::BISHOP).count() +
                    5 * m_position.pieces(chess::PieceType::ROOK).count() +
                    12 * m_position.pieces(chess::PieceType::QUEEN).count();

        int32_t score = m_nnue->evaluate(m_position);

        // tempo, acts like a contempt value, decrease to draw more, increase to win more against
        // weaker opponents
        // this breaks drawish positions
        int32_t tempo = 10;
        score += tempo;

        // taper to 70%
        score = score * (200 + phase) / 300;

        return std::clamp(score, -param::NNUE_MAX, (int)param::NNUE_MAX);
    }

    void make_move(const chess::Move &move, search_stack *ss)
    {
        ss->move = move;
        ss->key = m_position.hash();
        m_filter.add(ss->key);

        if (move == chess::Move::NO_MOVE)
        {
            ss->is_cap = false;

            ss->continuation =
                &(m_heuristics->continuation[0][0][static_cast<uint8_t>(chess::Piece::NONE)][0]);
            ss->cont_corr = &(m_heuristics->cont_corr[static_cast<uint8_t>(chess::Piece::NONE)][0]);
            m_position.makeNullMove();
        }
        else
        {
            m_keys.make_move(m_position, move);
            ss->is_cap = m_heuristics->is_capture(m_position, move);

            assert(m_position.at(move.from()) < 12);
            assert(move.to().index() < 64);
            ss->continuation = &(m_heuristics->continuation[ss->in_check][m_heuristics->is_capture(
                m_position, move)][m_position.at(move.from())][move.to().index()]);
            ss->cont_corr =
                &(m_heuristics->cont_corr[m_position.at(move.from())][move.to().index()]);

            m_nnue->make_move(m_position, move);
            m_chessmap->make_move(m_position, move);

            m_position.makeMove(move);
        }
    }

    void unmake_move(const chess::Move &move, search_stack *ss)
    {
        if (move == chess::Move::NO_MOVE)
        {
            m_position.unmakeNullMove();
        }
        else
        {
            m_position.unmakeMove(move);
            m_nnue->unmake_move();
            m_chessmap->unmake_move();
            m_keys.unmake_move();
        }

        m_filter.remove(ss->key);
    }

    template <bool is_pv_node>
    int16_t qsearch(int16_t alpha, int16_t beta, int depth, search_stack *ss)
    {
        assert(alpha < beta);

        const int32_t ply = ss->ply;
        ss->pv_init();
        m_stats.sel_depth = std::max(m_stats.sel_depth, ply + 1);

        m_stats.nodes_searched += 1;
        if ((m_stats.nodes_searched & 4095) == 0)
            m_timer.check();

        if (m_timer.is_stopped())
            return 0;

        if (ply >= param::MAX_DEPTH - 4)
        {
            if (m_position.inCheck())
                return get_contempt();

            return to_corrected_static_eval(evaluate(ss), ss).first;
        }

        // draw check
        if (m_position.isInsufficientMaterial() || m_filter.check(m_position, ply))
            return get_contempt();

        // 50 move limit
        if (m_position.isHalfMoveDraw())
        {
            auto [_, type] = m_position.getHalfMoveDrawType();
            if (type == chess::GameResult::DRAW)
                return get_contempt();

            return param::MATED_IN(ply);
        }

        alpha = std::max(alpha, param::MATED_IN(ply));
        beta = std::min(beta, param::MATE_IN(ply + 1));
        if (alpha >= beta)
            return alpha;

        if (alpha < get_contempt() && cuckoo::is_upcoming_rep(m_position, ply))
        {
            alpha = get_contempt();
            if (alpha >= beta)
                return alpha;
        }

        // [tt lookup]
        uint64_t raw_key = m_position.hash();
        uint64_t key = raw_key ^ util::ZOBRIST_50MR[m_position.halfMoveClock()];
        auto &bucket = m_table->probe(key);
        bool bucket_hit = false;
        auto [entry, entry_copy] = bucket.probe(key, bucket_hit, m_table->m_generation);
        auto tt_result = entry_copy.get(key, ply, param::QDEPTH, alpha, beta, bucket_hit);
        ss->tt_hit = tt_result.hit;
        tt_result.move = ss->tt_hit ? tt_result.move : chess::Move::NO_MOVE;
        if (!is_pv_node && tt_result.can_use)
        {
            return tt_result.score;
        }

        // [static evaluation]
        int16_t best_score = -param::INF;
        int16_t futility_base = -param::INF;
        int16_t unadjusted_static_eval = param::VALUE_NONE;
        ss->in_check = m_position.inCheck();
        if (ss->in_check)
        {
            best_score = futility_base = -param::INF;
        }
        else
        {
            if (ss->tt_hit)
            {
                unadjusted_static_eval = tt_result.static_eval;
                if (!param::IS_VALID(unadjusted_static_eval))
                    unadjusted_static_eval = evaluate(ss, tt_result.move);

                ss->static_eval = best_score =
                    to_corrected_static_eval(unadjusted_static_eval, ss).first;

                // use tt score to adjust static eval
                bool bound_hit =
                    tt_result.flag == param::EXACT_FLAG ||
                    (tt_result.flag == param::BETA_FLAG && tt_result.score > best_score) ||
                    (tt_result.flag == param::ALPHA_FLAG && tt_result.score < best_score);
                if (param::IS_VALID(tt_result.score) && !param::IS_DECISIVE(tt_result.score) &&
                    bound_hit)
                {
                    ss->static_eval = best_score = tt_result.score;
                }
            }
            else
            {
                unadjusted_static_eval = evaluate(ss, tt_result.move);
                ss->static_eval = best_score =
                    to_corrected_static_eval(unadjusted_static_eval, ss).first;

                bucket.store(key, param::NO_FLAG, best_score, ply, param::UNSEARCHED_DEPTH,
                             chess::Move::NO_MOVE, unadjusted_static_eval, false,
                             m_table->m_generation, entry);
            }

            if (best_score >= beta)
            {
                if (!param::IS_DECISIVE(best_score))
                {
                    best_score = (beta + best_score) / 2;
                }

                return best_score;
            }

            if (best_score > alpha)
                alpha = best_score;

            futility_base = ss->static_eval + features::QSEARCH_FUT_OFFSET;
        }

        int16_t score;
        chess::Move best_move = chess::Move::NO_MOVE;
        movegen gen{ss->moves[0],           m_position,
                    (*m_heuristics),        tt_result.move,
                    (ss - 1)->move,         ply,
                    param::QDEPTH,          m_keys.get_pawn_key(),
                    (ss - 1)->continuation, ss->in_check ? movegen_stage::EPV : movegen_stage::QPV};
        chess::Move move{};
        int move_count = 0;
        while ((move = gen.next_move()) != chess::Move::NO_MOVE)
        {
            move_count += 1;

            if (!param::IS_LOSS(best_score))
            {
                // [fut prune]
                if (m_heuristics->is_capture(m_position, move) && !ss->in_check &&
                    futility_base +
                            see::PIECE_VALUES[m_heuristics->get_capture(m_position, move)] <=
                        alpha &&
                    // we dont prune special moves
                    !see::test_ge(m_position, move, 0))
                {
                    best_score = std::max(
                        (int)best_score,
                        std::min(futility_base +
                                     see::PIECE_VALUES[m_heuristics->get_capture(m_position, move)],
                                 (int)param::NNUE_MAX));
                    continue;
                }

                if (!see::test_ge(m_position, move, features::QSEARCH_SEE_PRUNE))
                    continue;
            }

            m_table->prefetch(m_position.zobristAfter<false>(move) ^
                              util::ZOBRIST_50MR[m_position.halfMoveClock() + 1]);
            m_filter.prefetch(m_position.zobristAfter<false>(move));

            make_move(move, ss);
            score = -qsearch<is_pv_node>(-beta, -alpha, depth - 1, ss + 1);
            unmake_move(move, ss);

            if (m_timer.is_stopped())
                return 0;

            if (score > best_score)
            {
                best_score = score;

                if (score > alpha)
                {
                    best_move = move;

                    if (score >= beta)
                        break;

                    alpha = score;
                }
            }

            if (!param::IS_LOSS(best_score))
            {
                if (!ss->in_check && move_count >= (depth == 0 ? 5 : 3))
                    break;
                if (ss->in_check && !m_heuristics->is_capture(m_position, move))
                    break;
            }
        }

        int depth_stored = param::QDEPTH + ss->in_check;

        // [mate check]
        if (ss->in_check && move_count == 0)
        {
            best_score = param::MATED_IN(ply);
        }
        else if (!ss->in_check && move_count == 0 && gen.is_draw())
        {
            best_score = get_contempt();
        }
        // average out the best score
        else if (!param::IS_DECISIVE(best_score) && best_score > beta)
        {
            best_score = (beta + best_score) / 2;
        }

        uint8_t flag = best_score >= beta ? param::BETA_FLAG : param::ALPHA_FLAG;

        // assert(!m_timer.is_stopped());
        bucket.store(key, flag, best_score, ply, depth_stored, best_move, unadjusted_static_eval,
                     ss->tt_hit && ss->tt_pv, m_table->m_generation, entry);

        return best_score;
    }

    // avoid draws in midgame by giving a negative draw score
    int16_t get_contempt() const
    {
        // return 0;
        int piece_count = m_position.occ().count();
        return contempt_score[piece_count];
    }

    int history_malus(int depth) const
    {
        return std::min(features::HISTORY_MALUS_BASE + features::HISTORY_MALUS_MULT * depth,
                        features::MAX_HISTORY_UPDATE);
    }

    int history_bonus(int depth) const
    {
        return std::min(features::HISTORY_BASE + features::HISTORY_MULT * depth,
                        features::MAX_HISTORY_UPDATE);
    }

    template <bool is_pv_node>
    int16_t negamax(int16_t alpha, int16_t beta, int32_t depth, search_stack *ss, bool cut_node)
    {
        // constants
        const int32_t ply = ss->ply;
        ss->pv_init();

        m_stats.nodes_searched += 1;
        if ((m_stats.nodes_searched & 4095) == 0)
            m_timer.check();

        if (m_timer.is_stopped())
            return 0;

        if (ply >= param::MAX_DEPTH - 4)
        {
            if (m_position.inCheck())
                return get_contempt();

            return to_corrected_static_eval(evaluate(ss), ss).first;
        }

        const bool is_root = ply == 0 && is_pv_node;
        assert(!(ply == 0 && !is_pv_node));
        assert(!(is_pv_node && cut_node));
        // if (alpha >= beta)
        //     std::cout << alpha << ',' << beta << '\n';
        assert(alpha < beta);

        // [qsearch]
        if (depth <= 0)
        {
            m_stats.nodes_searched -= 1;
            return qsearch<is_pv_node>(alpha, beta, depth, ss);
        }

        // check draw
        // assert(!m_filter.check(m_position, ply) || m_position.isRepetition(1));
        if (!is_root && (m_position.isInsufficientMaterial() || m_filter.check(m_position, ply)))
            return get_contempt();

        // 50 move limit
        if (!is_root && m_position.isHalfMoveDraw())
        {
            auto [_, type] = m_position.getHalfMoveDrawType();
            if (type == chess::GameResult::DRAW)
                return get_contempt();

            return param::MATED_IN(ply);
        }

        // [mate distance pruning]
        if (!is_root)
        {
            alpha = std::max(alpha, param::MATED_IN(ply));
            beta = std::min(beta, param::MATE_IN(ply + 1));
            if (alpha >= beta)
                return alpha;
        }

        if (!is_root && alpha < get_contempt() && cuckoo::is_upcoming_rep(m_position, ply))
        {
            alpha = get_contempt();
            if (alpha >= beta)
                return alpha;
        }

        // [tt lookup]
        chess::Move &excluded_move = ss->excluded_move;
        bool has_excluded = excluded_move != chess::Move::NO_MOVE;
        uint64_t raw_key = m_position.hash();
        uint64_t key = raw_key ^ util::ZOBRIST_50MR[m_position.halfMoveClock()];
        auto &bucket = m_table->probe(key);
        bool bucket_hit = false;
        auto [entry, entry_copy] = bucket.probe(key, bucket_hit, m_table->m_generation);
        auto tt_result = entry_copy.get(key, ply, depth, alpha, beta, bucket_hit);
        ss->tt_hit = tt_result.hit;
        bool tt_pv = has_excluded ? ss->tt_pv : is_pv_node || (ss->tt_hit && tt_result.is_pv);
        ss->tt_pv = tt_pv;
        tt_result.move = ss->tt_hit && !has_excluded ? tt_result.move : chess::Move::NO_MOVE;

        if (is_root)
        {
            tt_result.move = m_root_moves.get_pv().move;
        }

        bool is_tt_capture = tt_result.move != chess::Move::NO_MOVE &&
                             m_heuristics->is_capture(m_position, tt_result.move);

        // [tt early return]
        if (!is_pv_node && tt_result.can_use && (cut_node == (tt_result.score >= beta)) &&
            !has_excluded && tt_result.depth >= depth + (tt_result.score >= beta))
        {
            // if early beta cutoff, prev move is shit so penalize it
            if (tt_result.score >= beta && (ss - 1)->move != chess::Move::NO_MOVE &&
                !(ss - 1)->is_cap)
            {
                int main_history_bonus = history_malus(depth);

                // [continuation history]
                auto piece = heuristics::get_prev_piece(m_position, (ss - 1)->move);
                update_continuation_history(ss - 1, piece, (ss - 1)->move.to(),
                                            -main_history_bonus);
            }

            // ignore tt for close to half move
            if (m_position.halfMoveClock() < 80)
                return tt_result.score;
        }

        // [tt evaluation fix]
        int16_t unadjusted_static_eval = param::VALUE_NONE;
        int16_t adjusted_static_eval = param::VALUE_NONE;
        int complexity = 0;
        ss->in_check = m_position.inCheck();
        if (ss->in_check)
        {
            ss->static_eval = adjusted_static_eval = param::VALUE_NONE;
        }
        else if (has_excluded)
        {
            unadjusted_static_eval = adjusted_static_eval = ss->static_eval;
            m_nnue->catchup(m_position);
        }
        else if (ss->tt_hit)
        {
            unadjusted_static_eval = tt_result.static_eval;
            if (!param::IS_VALID(unadjusted_static_eval))
                unadjusted_static_eval = evaluate(ss, tt_result.move);
            else if (is_pv_node)
                m_nnue->catchup(m_position);

            auto [corrected, c] = to_corrected_static_eval(unadjusted_static_eval, ss);
            ss->static_eval = adjusted_static_eval = corrected;
            complexity = std::abs(c);

            // use tt score to adjust static eval
            bool bound_hit =
                tt_result.flag == param::EXACT_FLAG ||
                (tt_result.flag == param::BETA_FLAG && tt_result.score > adjusted_static_eval) ||
                (tt_result.flag == param::ALPHA_FLAG && tt_result.score < adjusted_static_eval);
            if (param::IS_VALID(tt_result.score) && !param::IS_DECISIVE(tt_result.score) &&
                bound_hit)
            {
                adjusted_static_eval = tt_result.score;
            }
        }
        else
        {
            unadjusted_static_eval = evaluate(ss, tt_result.move);
            auto [corrected, c] = to_corrected_static_eval(unadjusted_static_eval, ss);
            ss->static_eval = adjusted_static_eval = corrected;
            complexity = std::abs(c);

            bucket.store(key, param::NO_FLAG, param::VALUE_NONE, ply, param::UNSEARCHED_DEPTH,
                         chess::Move::NO_MOVE, unadjusted_static_eval, ss->tt_pv,
                         m_table->m_generation, entry);
        }

        // [check syzygy endgame table]
        int16_t best_score = -param::INF;
        int16_t max_score = param::INF;
        if (m_endgame != nullptr && !has_excluded && !is_root && depth >= features::TB_HIT_DEPTH &&
            m_endgame->is_stored(m_position) && m_position.halfMoveClock() <= 30)
        {
            int16_t wdl = m_endgame->probe_wdl(m_position);
            int16_t tb_score = param::VALUE_SYZYGY - ply;
            int16_t score = wdl < -1 ? -tb_score : wdl > 1 ? tb_score : get_contempt();
            int8_t flag = wdl < -1  ? param::ALPHA_FLAG
                          : wdl > 1 ? param::BETA_FLAG
                                    : param::EXACT_FLAG;

            if (flag == param::EXACT_FLAG ||
                (flag == param::BETA_FLAG ? score >= beta : score <= alpha))
            {
                bucket.store(key, flag, score, ply, std::min(param::MAX_DEPTH, depth + 5),
                             chess::Move::NO_MOVE, unadjusted_static_eval, ss->tt_pv,
                             m_table->m_generation, entry);
                return score;
            }

            if (is_pv_node)
            {
                if (flag == param::BETA_FLAG)
                {
                    best_score = score;
                    alpha = std::max(alpha, score);
                }
                else
                {
                    max_score = score;
                }
            }
        }

        // improving flag
        bool improving = true;
        if (ss->in_check)
            improving = false;
        else if (param::IS_VALID((ss - 2)->static_eval) && param::IS_VALID(ss->static_eval))
        {
            improving = ss->static_eval > (ss - 2)->static_eval;
        }
        else if (param::IS_VALID((ss - 4)->static_eval) && param::IS_VALID(ss->static_eval))
        {
            improving = ss->static_eval > (ss - 4)->static_eval;
        }

        if (ss->in_check)
        {
            goto moves;
        }

        // if (!is_pv_node && is_tt_capture && !ss->tt_hit && depth < 7)
        // {
        //     adjusted_static_eval = ss->static_eval = qsearch<NonPV>(alpha, beta, 0, ss);
        // }

        // [razoring]
        if (!is_pv_node && param::IS_VALID(adjusted_static_eval) && !param::IS_DECISIVE(alpha) &&
            adjusted_static_eval <
                alpha - features::RAZOR_BASE - features::RAZOR_DEPTH_MULT * depth * depth)
        {
            return qsearch<NonPV>(alpha, beta, 0, ss);
        }

        // [static null move pruning]
        {
            // less strict if improving
            int margin =
                std::max(20, features::SNM_MARGIN * (depth - improving + complexity / 300));
            if (!is_pv_node && param::IS_VALID(adjusted_static_eval) &&
                adjusted_static_eval - margin >= beta && !param::IS_LOSS(beta) && depth <= 14 &&
                (tt_result.move == chess::Move::NO_MOVE || is_tt_capture) &&
                !param::IS_WIN(adjusted_static_eval))
            {
                return (beta + adjusted_static_eval) / 2;
            }
        }

        // [null move pruning]
        {
            const bool has_non_pawns = m_position.hasNonPawnMaterial(m_position.sideToMove());
            if (cut_node && !ss->verify_null && (ss - 1)->move != chess::Move::NO_MOVE &&
                has_non_pawns && param::IS_VALID(adjusted_static_eval) &&
                adjusted_static_eval >= beta && param::IS_VALID(ss->static_eval) &&
                ss->static_eval >= beta - 30 * depth + 200 - 50 * improving &&
                !param::IS_LOSS(beta) && !has_excluded)
            {
                m_table->prefetch(raw_key ^ chess::Zobrist::sideToMove() ^
                                  util::ZOBRIST_50MR[m_position.halfMoveClock()]);
                m_filter.prefetch(raw_key ^ chess::Zobrist::sideToMove());

                int32_t reduction = std::min((adjusted_static_eval - beta) / 300, 3) +
                                    features::NMP_REDUCTION_BASE +
                                    depth / features::NMP_REDUCTION_MULT + is_tt_capture;

                int reduced_depth = std::max(0, depth - reduction);

                // since nmp uses ss+1, we fake that this move is nothing
                make_move(chess::Move::NO_MOVE, ss);
                int16_t null_score =
                    -negamax<false>(-beta, -beta + 1, reduced_depth, ss + 1, false);
                unmake_move(chess::Move::NO_MOVE, ss);

                if (m_timer.is_stopped())
                    return 0;

                if (null_score >= beta)
                {
                    // if high depth, do confirm
                    if (depth > 16)
                    {
                        ss->verify_null = true;
                        int16_t verify_score =
                            negamax<false>(beta - 1, beta, reduced_depth, ss, cut_node);
                        ss->verify_null = false;

                        if (m_timer.is_stopped())
                            return 0;

                        if (verify_score >= beta)
                            return null_score;

                        adjusted_static_eval = verify_score;
                    }
                    else
                        return null_score;
                }
            }
        }

        // iir
        if ((is_pv_node || cut_node) && depth >= (2 + 2 * cut_node) &&
            tt_result.move == chess::Move::NO_MOVE)
            depth -= 1;

        // [prob cut]
        // the score of a lower depth is likely similar to a score of higher depth
        // goal is to convert beta to beta of a lower depth, and reject that
        // we do move gen and eval using this first
        {
            int probcut_beta = beta + features::PROBC_BETA_OFFSET - 100 * improving;
            if (!is_pv_node && depth >= features::PROBC_DEPTH && !param::IS_DECISIVE(beta) &&
                // also ignore when tt score is lower than expected beta
                !(tt_result.hit && param::IS_VALID(tt_result.score) &&
                  tt_result.score < probcut_beta && tt_result.depth >= depth - 3))
            {
                int16_t margin = std::clamp(probcut_beta - adjusted_static_eval, -param::NNUE_MAX,
                                            (int)param::NNUE_MAX);

                chess::Move tt = chess::Move::NO_MOVE;
                if (tt_result.move != chess::Move::NO_MOVE &&
                    legal::is_legal_full(m_position, tt_result.move) &&
                    see::test_ge(m_position, tt_result.move, margin))
                    tt = tt_result.move;

                movegen gen{ss->moves[has_excluded],
                            m_position,
                            *m_heuristics,
                            tt,
                            (ss - 1)->move,
                            ply,
                            depth,
                            margin,
                            m_keys.get_pawn_key(),
                            movegen_stage::PROBPV};

                int32_t probcut_depth =
                    std::clamp(depth - features::PROBC_DEPTH_REDUCTION, 0, depth);

                int move_count = 0;
                int32_t best_score = -param::INF;
                chess::Move move;
                while ((move = gen.next_move()) != chess::Move::NO_MOVE)
                {
                    if (move == excluded_move)
                        continue;

                    move_count += 1;

                    m_table->prefetch(m_position.zobristAfter<false>(move) ^
                                      util::ZOBRIST_50MR[m_position.halfMoveClock() + 1]);
                    m_filter.prefetch(m_position.zobristAfter<false>(move));

                    make_move(move, ss);

                    // check if move exceeds beta first
                    int16_t score = -qsearch<false>(-probcut_beta, -probcut_beta + 1, 0, ss + 1);
                    // full search if qsearch null window worked
                    if (score >= probcut_beta && probcut_depth > 0)
                    {
                        score = -negamax<false>(-probcut_beta, -probcut_beta + 1, probcut_depth,
                                                ss + 1, !cut_node);
                    }

                    unmake_move(move, ss);

                    if (m_timer.is_stopped())
                        return 0;

                    if (score > best_score)
                        best_score = score;

                    // check if can cut at lower depth
                    if (score >= probcut_beta)
                    {
                        bucket.store(key, param::BETA_FLAG, score, ply, probcut_depth + 1, move,
                                     unadjusted_static_eval, tt_pv, m_table->m_generation, entry);

                        return score - probcut_beta + beta;
                    }
                }

                // iir idea
                if (best_score < alpha - 400 - 200 * improving && move_count >= 3 &&
                    !param::IS_LOSS(alpha))
                {
                    depth -= 1;
                }
            }
        }

    moves:
        std::array<const continuation_history *, NUM_CONTINUATION> conthist{
            (ss - 1)->continuation, (ss - 2)->continuation, (ss - 3)->continuation,
            (ss - 4)->continuation, (ss - 5)->continuation, (ss - 6)->continuation,
        };

        movegen gen{ss->moves[has_excluded],
                    m_position,
                    *m_heuristics,
                    tt_result.move,
                    (ss - 1)->move,
                    ply,
                    depth,
                    m_keys.get_pawn_key(),
                    conthist,
                    m_nnue,
                    m_chessmap.get(),
                    m_table};

        chess::Move best_move = chess::Move::NO_MOVE;
        // track quiet/capture moves for malus
        int quiet_count = 0;
        int capture_count = 0;
        int move_count = 0;
        ss->move_count = move_count;
        if (!has_excluded)
            ss->complex = 0;

        chess::Move move;
        while ((move = gen.next_move()) != chess::Move::NO_MOVE)
        {
            int32_t new_depth;
            int32_t extension = 0;
            int16_t score = 0;
            int32_t history_score = 0;
            bool has_non_pawn = m_position.hasNonPawnMaterial(m_position.sideToMove());

            if (move == excluded_move)
                continue;

            move_count += 1;
            ss->move_count = move_count;

            bool is_capture = m_heuristics->is_capture(m_position, move);
            bool is_quiet = !is_capture;

            // [history calculation]
            history_score = move.score();

            // [low depth pruning]
            if (!is_root && has_non_pawn && !param::IS_LOSS(best_score))
            {
                // adjust the relevant depth
                int32_t lmr_depth = std::max(depth - m_param.lookup(is_quiet, depth, move_count) -
                                                 !improving + history_score / 10000,
                                             1);

                // [see pruning]
                int see_margin =
                    is_quiet ? features::SEE_QUIET_BASE +
                                   features::SEE_QUIET_DEPTH_MULT * lmr_depth * lmr_depth
                             : features::SEE_CAP_BASE + features::SEE_CAP_DEPTH_MULT * lmr_depth;
                if (is_capture)
                {
                    auto non_pawn_pieces_sqs =
                        m_position.us(m_position.sideToMove()) ^
                        m_position.pieces(chess::PieceType::KING, m_position.sideToMove()) ^
                        m_position.pieces(chess::PieceType::PAWN, m_position.sideToMove());
                    auto moved_sq = chess::Bitboard::fromSquare(move.from());
                    if (alpha >= get_contempt() || non_pawn_pieces_sqs != moved_sq)
                    {
                        if (!see::test_ge(m_position, move, -see_margin))
                            continue;
                    }
                }
                else
                {
                    if (!see::test_ge(m_position, move, -see_margin))
                        continue;
                }

                // [history pruning]
                if (is_quiet && history_score < -6000 * depth)
                {
                    gen.skip_quiet();
                    continue;
                }

                // [late move pruning], higher threshold if improving
                if (move_count >= (3 + depth * depth) / (2 - improving))
                {
                    // only skip quiets
                    gen.skip_quiet();
                }

                // [futility pruning]
                // prune quiet moves that doesn't raise alpha
                if (is_quiet && quiet_count > 1 && lmr_depth <= 12 && !ss->in_check &&
                    ss->static_eval + 150 + 150 * lmr_depth <= alpha)
                {
                    gen.skip_quiet();
                    continue;
                }

                // [capture futility pruning]
                // prune capture moves that doesn't raise alpha
                auto captured = m_heuristics->get_capture(m_position, move);
                if (is_capture && lmr_depth <= 14 && !ss->in_check &&
                    ss->static_eval + 200 + 200 * lmr_depth + see::PIECE_VALUES[captured] <= alpha)
                {
                    // gen.skip_quiet();
                    continue;
                }
            }

            m_table->prefetch(m_position.zobristAfter<false>(move) ^
                              util::ZOBRIST_50MR[m_position.halfMoveClock() + 1]);
            m_filter.prefetch(m_position.zobristAfter<false>(move));

            // [singular extension]
            if (!has_excluded && tt_result.move == move && !is_root && tt_result.hit &&
                param::IS_VALID(tt_result.score) && !param::IS_DECISIVE(tt_result.score) &&
                (tt_result.flag == param::EXACT_FLAG || tt_result.flag == param::BETA_FLAG) &&
                tt_result.depth >= depth - 3 && depth >= 5)
            {
                int32_t to_beat = tt_result.score - depth;
                ss->excluded_move = move;
                int16_t next_best_score =
                    negamax<false>(to_beat - 1, to_beat, (depth - 1) / 2, ss, cut_node);
                ss->excluded_move = chess::Move::NO_MOVE;

                if (m_timer.is_stopped())
                    return 0;

                if (next_best_score < to_beat)
                {
                    if (!is_pv_node && next_best_score < to_beat - 20)
                    {
                        if (is_quiet && next_best_score < to_beat - 200)
                            extension = 3;
                        else
                            extension = 2;
                    }
                    else
                        extension = 1;
                }
                else if (next_best_score >= beta)
                    return next_best_score;
                else if (tt_result.score >= beta)
                    // reduce if exact and tt score proved cutoff
                    extension = -3 + tt_pv;
                else if (cut_node)
                    extension = -2;
            }

            new_depth = depth - 1;
            new_depth = std::max(new_depth + extension, 0);

            make_move(move, ss);

            int bonus = 0;

            // [late move reduction]
            if (depth >= 2 && move_count > 1 + 2 * is_root)
            {
                int32_t reduction = m_param.lookup(is_quiet, depth, move_count);

                // check extension
                reduction -= ss->in_check;

                // reduce if not improving
                reduction += !improving;

                // high complexity extends
                reduction -= complexity / 200;

                // reduce if in cut node
                if (cut_node)
                    reduction += 2 - tt_pv;

                // extend if pv
                reduction -= tt_pv + is_pv_node;

                // reduce if tt capture
                reduction += is_tt_capture;

                // reduce/extend based on the history
                reduction -= history_score /
                             (is_quiet ? features::QUIET_LMR_DIV : features::CAPTURE_LMR_DIV);

                int32_t reduced_depth = std::clamp(new_depth - reduction, 1, new_depth + 1);
                score = -negamax<false>(-(alpha + 1), -alpha, reduced_depth, ss + 1, true);
                if (score > alpha && reduced_depth < new_depth)
                {
                    // depth extend if score is good, reduce if score is bad
                    new_depth += (score > best_score + features::RESEARCH_HIGH + new_depth * 2);
                    new_depth -= (score < best_score + features::RESEARCH_LOW);

                    if (reduced_depth < new_depth)
                        score = -negamax<false>(-(alpha + 1), -alpha, new_depth, ss + 1, !cut_node);

                    bonus = score <= alpha  ? -history_malus(new_depth)
                            : score >= beta ? history_bonus(new_depth)
                                            : 0;
                }
            }
            else if (!is_pv_node || move_count > 1)
            {
                score = -negamax<false>(-(alpha + 1), -alpha, new_depth, ss + 1, !cut_node);
            }

            if (is_pv_node && (move_count == 1 || score > alpha))
                score = -negamax<true>(-beta, -alpha, new_depth, ss + 1, false);

            // if (chess::uci::moveToUci(move) == "g2f3" && is_root)
            //     std::cout << score << "," << new_depth << std::endl;
            unmake_move(move, ss);

            if (m_timer.is_stopped())
                return 0;

            if (bonus != 0)
                update_continuation_history(ss, m_position.at(move.from()), move.to(), bonus);

            if (is_root)
            {
                root_move_list::root_move &root = m_root_moves.get_by_move(move);
                root.average_score =
                    !param::IS_VALID(root.average_score) ? score : (score + root.average_score) / 2;

                if (score >= alpha)
                {
                    root.score = score;
                }
                else
                {
                    // faillow cannot order
                    root.score = -param::INF;
                }
            }

            if (score > best_score)
            {
                best_score = score;

                if (score > alpha)
                {
                    best_move = move;
                    if (is_pv_node)
                        ss->pv_update(best_move, (ss + 1));

                    if (score >= beta)
                    {
                        break;
                    }

                    alpha = score;
                }
            }

            // malus save
            if (!m_heuristics->is_capture(m_position, move))
            {
                if (quiet_count < param::QUIET_MOVES)
                    ss->quiet_moves[has_excluded][quiet_count++] = move;
            }
            else
            {
                if (capture_count < param::QUIET_MOVES)
                    ss->capture_moves[has_excluded][capture_count++] = move;
            }
        }

        if (is_pv_node)
            best_score = std::min(best_score, max_score);

        // checkmate or draw
        if (move_count == 0)
        {
            if (has_excluded)
                best_score = alpha;
            else if (ss->in_check)
                best_score = param::MATED_IN(ply);
            else
                best_score = get_contempt();
        }
        // beta cutoff
        else if (best_score >= beta)
        {
            // [main history update]
            int adjusted_depth = depth + (best_score > beta + 150);
            int main_history_bonus =
                std::min(features::HISTORY_MULT * adjusted_depth + features::HISTORY_BASE,
                         features::MAX_HISTORY_UPDATE);
            int main_history_malus = std::min(features::HISTORY_MALUS_MULT * adjusted_depth +
                                                  features::HISTORY_MALUS_BASE,
                                              features::MAX_HISTORY_UPDATE);
            if (!m_heuristics->is_capture(m_position, best_move))
            {
                // don't store if early cutoff at low depth
                if (depth > features::HISTORY_EARLY_CUTOFF || quiet_count > 0)
                {
                    // [main history]
                    m_heuristics->update_main_history(m_position, best_move, ply,
                                                      m_keys.get_pawn_key(), main_history_bonus);
                }

                // [continuation history]
                update_continuation_history(ss, m_position.at(best_move.from()), best_move.to(),
                                            main_history_bonus);
                // [killer moves update]
                m_heuristics->store_killer(best_move, ply, param::IS_WIN(best_score));

                // [countermove]
                auto prev_move = (ss - 1)->move;
                if (prev_move != chess::Move::NO_MOVE)
                {
                    m_heuristics->counter[heuristics::get_prev_piece(m_position, prev_move)]
                                         [prev_move.to().index()] = best_move;
                }

                // malus apply
                for (int j = 0; j < quiet_count; ++j)
                {
                    m_heuristics->update_main_history(m_position, ss->quiet_moves[has_excluded][j],
                                                      ply, m_keys.get_pawn_key(),
                                                      -main_history_malus);

                    update_continuation_history(
                        ss, m_position.at(ss->quiet_moves[has_excluded][j].from()),
                        ss->quiet_moves[has_excluded][j].to(), -main_history_malus);
                }
            }
            else
            {
                m_heuristics->update_capture_history(m_position, best_move, main_history_bonus);
            }

            // malus apply
            for (int j = 0; j < capture_count; ++j)
            {
                m_heuristics->update_capture_history(m_position, ss->capture_moves[has_excluded][j],
                                                     -main_history_malus);
            }
        }

        // if no good move found, last move good so add this one too
        if (best_score <= alpha)
            ss->tt_pv = tt_pv = tt_pv || (ss - 1)->tt_pv;

        const uint8_t flag = best_score >= beta                                ? param::BETA_FLAG
                             : is_pv_node && best_move != chess::Move::NO_MOVE ? param::EXACT_FLAG
                                                                               : param::ALPHA_FLAG;

        // assert(!m_timer.is_stopped());
        if (!has_excluded)
        {
            bucket.store(key, flag, best_score, ply, depth, best_move, unadjusted_static_eval,
                         tt_pv, m_table->m_generation, entry);
        }

        // update correction history
        if (!has_excluded && param::IS_VALID(ss->static_eval) && !ss->in_check &&
            !(best_move != chess::Move::NO_MOVE &&
              m_heuristics->is_capture(m_position, best_move)) &&
            (best_score > ss->static_eval) == (best_move != chess::Move::NO_MOVE))
        {
            int bonus = std::clamp((best_score - ss->static_eval) * depth / 8,
                                   -CORRECTION_LIMIT / 4, CORRECTION_LIMIT / 4);
            m_heuristics->update_corr_hist_score(m_position, m_keys.get_pawn_key(),
                                                 m_keys.get_white_key(), m_keys.get_black_key(),
                                                 bonus);

            auto prev_move = (ss - 1)->move;
            if (prev_move != chess::Move::NO_MOVE)
            {
                auto piece = heuristics::get_prev_piece(m_position, prev_move);
                if ((ss - 2)->move != chess::Move::NO_MOVE)
                    (*(ss - 2)->cont_corr)[piece][prev_move.to().index()].add_bonus(bonus);
            }
        }

        return best_score;
    }

    std::pair<int16_t, int32_t> to_corrected_static_eval(int32_t static_eval,
                                                         search_stack *ss) const
    {
        static_eval = (static_eval * (200 - (int32_t)m_position.halfMoveClock())) / 200;

        int32_t value = 30 *
                        m_heuristics
                            ->correction_history[m_position.sideToMove()]
                                                [m_keys.get_pawn_key() & NON_PAWN_SIZE_M1]
                            .get_value() /
                        512;

        value +=
            35 *
            m_heuristics
                ->white_corrhist[m_position.sideToMove()][m_keys.get_white_key() & NON_PAWN_SIZE_M1]
                .get_value() /
            512;
        value +=
            35 *
            m_heuristics
                ->black_corrhist[m_position.sideToMove()][m_keys.get_black_key() & NON_PAWN_SIZE_M1]
                .get_value() /
            512;

        auto prev_move = (ss - 1)->move;
        if (prev_move != chess::Move::NO_MOVE)
        {
            auto piece = heuristics::get_prev_piece(m_position, prev_move);
            value += 24 * (*(ss - 2)->cont_corr)[piece][prev_move.to().index()].get_value() / 512;
        }

        value = std::clamp(value, -4096, 4096);
        int scaled_value = (value * (200 - (int32_t)m_position.halfMoveClock())) / 200;
        static_eval += scaled_value;
        return {std::clamp((int)static_eval, -param::NNUE_MAX, (int)param::NNUE_MAX), scaled_value};
    }

    void update_continuation_history(search_stack *ss, chess::Piece piece, chess::Square to,
                                     int bonus) const
    {
        constexpr std::array<int, NUM_CONTINUATION> weights{1100, 800, 100, 400, 70, 300};
        for (int i = 1; i <= NUM_CONTINUATION; ++i)
        {
            assert((ss - i)->continuation != nullptr);
            if ((ss - i)->move != chess::Move::NO_MOVE)
            {
                if (ss->in_check && i > 2)
                    break;

                (*(ss - i)->continuation)[piece][to.index()].add_bonus(bonus * weights[i - 1] /
                                                                       1024);
            }
        }
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
        cout << "nps: " << nodes * 1000 / std::max(static_cast<int64_t>(1), ms) << endl;
        std::cout.imbue(original);
    }

    search_result search(const chess::Board &reference, search_param param, bool verbose = false)
    {
        // timer info first
        const auto control = param.time_control(reference.fullMoveNumber(), reference.sideToMove());
        if (param.is_main_thread && verbose)
            std::cout << "info maxtime " << control.time << " opttime " << control.opt_time
                      << std::endl;

        m_timer.start(control.time, control.opt_time);

        auto reference_time = timer::now();
        m_position = reference;

        begin();

        search_result result{};

        if (m_endgame != nullptr && m_endgame->is_stored(m_position))
        {
            // only dtm when on main
            if (param.is_main_thread)
            {
                // root search
                auto probe = m_endgame->probe_dtm(m_position, m_timer);
                result.pv_line = probe.first;
                result.depth = result.pv_line.size();
                result.score = probe.second;

                if (verbose)
                {
                    m_stats.display_uci(result);
                }
            }
            else
            {
                result.depth = 0;
                result.score = 0;
            }

            return result;
        }

        search_stack *root_ss = &m_stack[SEARCH_STACK_PREFIX];
        chess::Move last_move = chess::Move::NO_MOVE;
        int last_score = 0;
        int complexity = 0;
        int error = 0;
        for (int32_t depth = 1; depth <= std::min(param::MAX_DEPTH - 4, control.depth); depth += 1)
        {
            const auto &pv = m_root_moves.get_pv();

            // check if just one move
            if (m_root_moves.is_singular())
            {
                result.pv_line = {pv.move};
                result.depth = 1;
                result.score = 0;
                break;
            }

            // scale window by score, larger scores warrants higher window
            constexpr int MOD = 8;
            int average_score = 0;
            if (param::IS_VALID(pv.average_score))
                average_score = pv.average_score;

            int window = 10 + (param.thread_index + MOD - param.main_thread_index) % MOD +
                         average_score * average_score / 12000;
            assert(window > 0);

            int alpha = -param::INF;
            int beta = param::INF;

            if (depth >= 2)
            {
                alpha = std::max(-param::INF, pv.score - window);
                beta = std::min((int)param::INF, pv.score + window);
                assert(alpha < beta);
            }

            int32_t score = 0;
            int fail_highs = 0;
            while (true)
            {
                // if (alpha >= beta)
                //     std::cout << alpha << "," << beta << "\n";
                assert(alpha < beta);
                int adjusted_depth = std::max(1, depth - fail_highs);
                score = negamax<true>(alpha, beta, adjusted_depth, root_ss, false);
                m_root_moves.sort();

                if (m_timer.is_stopped())
                    break;

                if (score <= alpha)
                {
                    beta = alpha + 1;
                    alpha = std::max(-param::INF, score - window);
                    // std::cout << window << "\n";
                    // std::cout << score << "," << alpha << "," << beta << "\n";

                    fail_highs = 0;
                }
                else if (score >= beta)
                {
                    alpha = beta - 1;
                    beta = std::min((int)param::INF, score + window);

                    if (score < 2000)
                        fail_highs += 1;
                }
                else
                {
                    // only update score on exact scores
                    result.score = score;
                    result.depth = depth;
                    break;
                }

                if (window < 20000)
                    window += window / 2;
            }

            // update lines always, since root moves are updated only when timer ok
            if (!root_ss->get_pv().empty())
                result.pv_line = root_ss->get_pv();

            // exit if max time exceeded
            if (m_timer.is_stopped())
                break;

            // optimum time check, after asp window re-search
            if (param.is_main_thread && !result.pv_line.empty() && m_timer.is_opt_time_stop())
                break;

            // display info
            m_stats.total_time = timer::now() - reference_time;
            m_stats.tt_occupancy = m_table->occupied();
            if (param.is_main_thread && verbose)
            {
                m_stats.display_uci(result);
            }
        }

        // final log
        m_stats.total_time = timer::now() - reference_time;
        m_stats.tt_occupancy = m_table->occupied();
        if (param.is_main_thread && verbose)
        {
            m_stats.display_uci(result);
        }

        return result;
    }

    void ponderhit(const chess::Board &reference, search_param param, bool verbose)
    {
        const auto control = param.time_control(reference.fullMoveNumber(), reference.sideToMove());
        if (verbose)
            std::cout << "info ponderhit new searchtime " << control.time << std::endl;

        auto already_spent = m_stats.total_time;
        int64_t max_time = control.time;

        // handles when opp takes a long time to explore and we are at high depth
        if (already_spent.count() > control.opt_time)
        {
            // we either use double the opt_time, or a bit higher than currently spent
            max_time =
                std::min(max_time, std::max(already_spent.count() + 100, control.opt_time * 2));
        }

        m_timer.start(max_time, control.opt_time);
    }
};
