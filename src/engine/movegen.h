#pragma once

#include "chessmap.h"
#include "features.h"
#include "heuristic.h"
#include "legal.h"
#include "nnue2.h"
#include "param.h"
#include "see.h"
#include "table.h"
#include <cstdint>

enum class movegen_stage
{
    // negamax flow
    PV,
    CAPTURE_INIT,
    GOOD_CAPTURE,
    KILLER,
    QUIET_INIT,
    GOOD_QUIET,
    BAD_CAPTURE,
    BAD_QUIET,

    // qsearch normal flow
    QPV,
    QCAPTURE_INIT,
    QCAPTURE,

    // qsearch evasion flow
    EPV,
    ECAPTURE_INIT,
    ECAPTURE,
    EQUIET_INIT,
    EQUIET,

    // probcut flow
    PROBPV,
    PROB_CAPTURE_INIT,
    PROB_GOOD_CAPTURE,

    DONE
};

constexpr int16_t IGNORE_SCORE = std::numeric_limits<int16_t>::min();
constexpr int STATIC_SORT_TOP_N = 12;

class movegen
{
  private:
    int m_stage;
    // stack for regular: [bad capture, (bad quiet, good quiet)/good capture]
    chess::Movelist &m_moves;
    int m_bad_capture_end{0};
    int m_move_index{0};
    int m_capture_end{0};

    chess::Board &m_position;
    const heuristics &m_heuristics;
    chess::Move m_pv_move;
    int32_t m_ply;
    int32_t m_depth;
    chess::Move m_prev_move;

    std::array<const continuation_history *, NUM_CONTINUATION> m_continuations{nullptr};

    int16_t m_prob_margin;
    bool m_skip_quiet = false;
    chess::Move m_killer[param::NUMBER_KILLERS]{chess::Move::NO_MOVE};

    chess::movegen::precompute m_precompute{};
    uint64_t m_pawn_key{};

    chess::Bitboard threats{};
    chess::Bitboard pawn_threats{};
    chess::PieceType threat_piece{};

    // for nnue based move ordering
    nnue2::net *nnue = nullptr;
    chessmap::net *chessmap = nullptr;
    table *tt = nullptr;

  public:
    // probcut
    explicit movegen(chess::Movelist &moves, chess::Board &position, const heuristics &heuristics,
                     chess::Move pv_move, chess::Move prev_move, int32_t ply, int depth,
                     int16_t margin, uint64_t pawn_key, movegen_stage stage = movegen_stage::PV)
        : m_stage{static_cast<int>(stage)}, m_moves{moves}, m_position(position),
          m_heuristics(heuristics), m_pv_move(pv_move), m_ply(ply), m_depth(depth),
          m_prev_move{prev_move}, m_prob_margin{margin}, m_pawn_key{pawn_key}
    {
    }

    // negamax main
    explicit movegen(
        chess::Movelist &moves, chess::Board &position, const heuristics &heuristics,
        chess::Move pv_move, chess::Move prev_move, int32_t ply, int depth, uint64_t pawn_key,
        const std::array<const continuation_history *, NUM_CONTINUATION> &continuations,
        nnue2::net *nnue, chessmap::net *chessmap, table *tt,
        movegen_stage stage = movegen_stage::PV)
        : m_stage{static_cast<int>(stage)}, m_moves{moves}, m_position(position),
          m_heuristics(heuristics), m_pv_move(pv_move), m_ply(ply), m_depth(depth),
          m_prev_move{prev_move}, m_continuations{continuations}, m_pawn_key(pawn_key), nnue(nnue),
          chessmap(chessmap), tt(tt)
    {
        assert(stage == movegen_stage::PV);
    }

    // qsearch
    explicit movegen(chess::Movelist &moves, chess::Board &position, const heuristics &heuristics,
                     chess::Move pv_move, chess::Move prev_move, int32_t ply, int depth,
                     uint64_t pawn_key, const continuation_history *continuation1,
                     movegen_stage stage = movegen_stage::PV)
        : m_stage{static_cast<int>(stage)}, m_moves{moves}, m_position(position),
          m_heuristics(heuristics), m_pv_move(pv_move), m_ply(ply), m_depth(depth),
          m_prev_move{prev_move}, m_pawn_key(pawn_key)
    {
        m_continuations[0] = continuation1;
    }

    // computes the (current perspective) static evaluation assuming we've made [move]
    int16_t evaluate_after_move(chess::Move move)
    {
        uint64_t hash = m_position.zobristAfter(move);
        auto &bucket = tt->probe(hash);
        bool bucket_hit = false;
        auto [_, result] = bucket.probe(hash, bucket_hit, tt->m_generation);
        if (bucket_hit)
        {
            // use exact results
            if (param::IS_VALID(result.m_score) &&
                result.m_depth + param::DEPTH_OFFSET >= 0)
            {
                return -result.m_score;
            }

            if (param::IS_VALID(result.m_static_eval))
                return -result.m_static_eval;
        }

        return param::VALUE_NONE;
    }

    void skip_quiet()
    {
        m_skip_quiet = true;
    }

    chess::Move get_counter() const
    {
        if (m_prev_move != chess::Move::NO_MOVE)
        {
            auto counter = m_heuristics.counter[heuristics::get_prev_piece(m_position, m_prev_move)]
                                               [m_prev_move.to().index()];
            return counter;
        }

        return chess::Move::NO_MOVE;
    }

    chess::Move next_move()
    {
        while (true)
        {
            switch (static_cast<movegen_stage>(m_stage))
            {
                // pv find
            case movegen_stage::PV:
            case movegen_stage::QPV:
            case movegen_stage::EPV:
            case movegen_stage::PROBPV: {
                m_stage++;
                if (m_pv_move != chess::Move::NO_MOVE &&
                    legal::is_legal_full(m_position, m_pv_move))
                {
                    m_pv_move.setScore(10000);
                    return m_pv_move;
                }

                break;
            }

                // generate all capture moves and score them
            case movegen_stage::CAPTURE_INIT:
            case movegen_stage::QCAPTURE_INIT:
            case movegen_stage::PROB_CAPTURE_INIT:
            case movegen_stage::ECAPTURE_INIT: {
                // direct capture generation
                m_precompute = chess::movegen::legalmoves_precompute(m_position);
                m_moves.clear();
                chess::movegen::legalmoves_capture(m_moves, m_position, m_precompute);

                // score
                for (int i = 0;; ++i)
                {
                    if (i >= m_moves.size())
                        break;

                    auto &move = m_moves[i];
                    if (move == m_pv_move)
                    {
                        std::swap(move, m_moves.back());
                        m_moves.decr();
                        i--;
                        continue;
                    }

                    assert(m_heuristics.is_capture(m_position, move));

                    auto captured = m_heuristics.get_capture(m_position, move);
                    int mvv = see::PIECE_VALUES[captured] * features::CAPTURE_MVV_SCALE;

                    // capture history
                    int capture_score = m_heuristics
                                            .capture_history[m_position.at(move.from())]
                                                            [move.to().index()][captured]
                                            .get_value();

                    int32_t score = mvv + capture_score;
                    score = std::clamp(score, -32000, 32000);
                    move.setScore(score);
                }

                m_capture_end = m_moves.size();
                sort_moves(m_moves, 0, m_capture_end);

                m_bad_capture_end = 0;
                m_move_index = 0;
                m_stage++;
                break;
            }

                // iterate good captures, sorting into bad captures
            case movegen_stage::GOOD_CAPTURE: {
                // see check, incr bad_capture_end
                m_move_index = pick_move(m_moves, m_move_index, m_capture_end, [&](auto &move) {
                    assert(m_heuristics.is_capture(m_position, move));
                    if (!see::test_ge(m_position, move,
                                      -move.score() / features::GOOD_CAPTURE_SEE_DIV))
                    {
                        std::swap(m_moves[m_bad_capture_end], move);
                        m_bad_capture_end++;
                        return false;
                    }
                    return true;
                });
                if (m_move_index < m_capture_end)
                    return m_moves[m_move_index++];

                // note here that bad_capture_end should point to end of bad captures
                m_stage++;
                m_move_index = 0;
                break;
            }

            case movegen_stage::KILLER: {
                if (!m_skip_quiet)
                {
                    while (m_move_index < param::NUMBER_KILLERS)
                    {
                        auto killer = m_heuristics.killers[m_ply][m_move_index].first;
                        m_killer[m_move_index++] = killer;
                        if (killer != chess::Move::NO_MOVE && killer != m_pv_move &&
                            legal::is_legal_full(m_position, killer) &&
                            !m_heuristics.is_capture(m_position, killer))
                        {
                            killer.setScore(10000);
                            return killer;
                        }
                    }
                }

                m_stage++;
                break;
            }

                // generating all quiet moves and scoring them
            case movegen_stage::QUIET_INIT: {
                if (!m_skip_quiet)
                {
                    bool use_threat = m_depth < 14;
                    if (use_threat)
                        generate_threat();

                    chess::movegen::legalmoves_quiet(m_moves, m_position, m_precompute);
                    auto counter = get_counter();

                    int static_start = m_capture_end;
                    int static_end = std::min(m_moves.size(), m_capture_end + STATIC_SORT_TOP_N);
                    bool will_static_sort = static_end - static_start > 1 && m_depth < 8;

                    // chessmap limit
                    const int CHESSMAP_DEPTH_LIMIT = 10;
                    bool use_chessmap = m_depth < CHESSMAP_DEPTH_LIMIT;
                    if (use_chessmap)
                        chessmap->catchup(m_position);

                    for (int i = m_capture_end;; ++i)
                    {
                        if (i >= m_moves.size())
                            break;

                        chess::Move &move = m_moves[i];
                        if (move == m_pv_move)
                        {
                            std::swap(move, m_moves.back());
                            m_moves.decr();
                            i -= 1;
                            continue;
                        }

                        if (move.typeOf() == chess::Move::PROMOTION &&
                            move.promotionType() == chess::PieceType::QUEEN)
                        {
                            std::swap(move, m_moves.back());
                            m_moves.decr();
                            i -= 1;
                            continue;
                        }

                        assert(!m_heuristics.is_capture(m_position, move));
                        // killer move
                        if (move == m_killer[0] || move == m_killer[1])
                        {
                            std::swap(move, m_moves.back());
                            m_moves.decr();
                            i -= 1;
                            continue;
                        }

                        int32_t score = 0;

                        // normal
                        score += m_heuristics
                                     .main_history[m_position.sideToMove()][move.from().index()]
                                                  [move.to().index()]
                                     .get_value();

                        // low ply
                        if (m_ply < LOW_PLY)
                        {
                            score += features::QUIET_LOW_PLY_SCALE *
                                     m_heuristics
                                         .low_ply[m_position.sideToMove()][m_ply]
                                                 [move.from().index()][move.to().index()]
                                         .get_value() /
                                     (1 + m_ply);
                        }

                        // pawn history
                        score += m_heuristics
                                     .pawn[m_pawn_key & PAWN_STRUCTURE_SIZE_M1]
                                          [m_position.at(move.from())][move.to().index()]
                                     .get_value();

                        // continuation
                        for (int i = 0; i < NUM_CONTINUATION; ++i)
                        {
                            if (m_continuations[i] != nullptr)
                                score += (*m_continuations[i])[m_position.at(move.from())]
                                                              [move.to().index()]
                                                                  .get_value() /
                                         2;
                        }

                        if (move == counter)
                            score += 10000;

                        // threat
                        if (use_threat)
                        {
                            auto piece = m_position.at(move.from()).type();
                            if (threat_piece != chess::PieceType::NONE && piece > threat_piece &&
                                (chess::Bitboard::fromSquare(move.from()) & threats) &&
                                !(chess::Bitboard::fromSquare(move.to()) & threats))
                            {
                                int additional = (see::ATTACKED_PIECE_VALUES[piece] -
                                                  see::ATTACKED_PIECE_VALUES[threat_piece]) /
                                                     16 +
                                                 100;

                                assert(additional > 0);
                                score += additional;
                            }
                        }

                        if (use_chessmap)
                        {
                            score += chessmap->evaluate_cached(m_position, move) / (1 + m_depth);
                        }

                        score = std::clamp(score, -32000, 32000);
                        move.setScore(score);

                        if (will_static_sort)
                            tt->prefetch(m_position.zobristAfter(move));
                    }

                    sort_moves(m_moves, m_capture_end, m_moves.size(), -4000 * m_depth);

                    // static nnue ordering
                    if (will_static_sort)
                    {
                        std::array<int, STATIC_SORT_TOP_N> static_scores{};
                        int static_scores_index = 0;
                        int32_t baseline = param::INF;

                        for (int i = static_start; i < static_end; ++i)
                        {
                            auto &move = m_moves[i];
                            int32_t score = evaluate_after_move(move);
                            static_scores[static_scores_index++] = score;

                            if (param::IS_VALID(score))
                                baseline = std::min(baseline, score);
                        }

                        if (baseline != param::INF)
                        {
                            static_scores_index = 0;
                            for (int i = static_start; i < static_end; ++i)
                            {
                                int32_t static_score = static_scores[static_scores_index++];
                                if (param::IS_VALID(static_score))
                                {
                                    auto &move = m_moves[i];
                                    int32_t adjusted_score = std::clamp(
                                        move.score() + (static_score - baseline), -32000, 32000);
                                    move.setScore(adjusted_score);
                                }
                            }

                            sort_moves(m_moves, static_start, static_end);
                        }
                    }
                }

                m_move_index = m_capture_end;
                m_stage++;
                break;
            }
                // iterating through quiet moves, sorting into bad quiets
            case movegen_stage::GOOD_QUIET: {
                if (m_skip_quiet)
                {
                    m_move_index = 0;
                    m_stage++;
                    break;
                }

                m_move_index = pick_move(m_moves, m_move_index, m_moves.size(), [](auto &m) {
                    return m.score() >= features::QUIET_BAD_THRESHOLD;
                });
                if (m_move_index < m_moves.size())
                    return m_moves[m_move_index++];

                m_move_index = 0;
                m_stage++;
                break;
            }
                // iterating through bad capture moves
            case movegen_stage::BAD_CAPTURE: {
                m_move_index = pick_move(m_moves, m_move_index, m_bad_capture_end,
                                         [](auto &) { return true; });
                if (m_move_index < m_bad_capture_end)
                    return m_moves[m_move_index++];

                assert(m_capture_end >= m_bad_capture_end);
                m_move_index = m_capture_end;
                m_stage++;
                break;
            }
                // iterating through bad quiet moves
            case movegen_stage::BAD_QUIET: {
                if (m_skip_quiet)
                {
                    m_stage = static_cast<int>(movegen_stage::DONE);
                    break;
                }

                m_move_index = pick_move(m_moves, m_move_index, m_moves.size(), [](auto &m) {
                    return m.score() < features::QUIET_BAD_THRESHOLD;
                });
                if (m_move_index < m_moves.size())
                    return m_moves[m_move_index++];

                m_stage = static_cast<int>(movegen_stage::DONE);
                break;
            }

                // explore all captures
            case movegen_stage::QCAPTURE: {
                m_move_index =
                    pick_move(m_moves, m_move_index, m_moves.size(), [](auto &) { return true; });
                if (m_move_index < m_moves.size())
                    return m_moves[m_move_index++];

                m_stage = static_cast<int>(movegen_stage::DONE);
                break;
            }

            case movegen_stage::ECAPTURE: {
                m_move_index =
                    pick_move(m_moves, m_move_index, m_moves.size(), [](auto &) { return true; });
                if (m_move_index < m_moves.size())
                    return m_moves[m_move_index++];

                m_stage++;
                break;
            }

            case movegen_stage::EQUIET_INIT: {
                chess::movegen::legalmoves_quiet(m_moves, m_position, m_precompute);
                auto counter = get_counter();

                for (int i = m_capture_end;; ++i)
                {
                    if (i >= m_moves.size())
                        break;

                    chess::Move &move = m_moves[i];
                    if (move == m_pv_move)
                    {
                        std::swap(move, m_moves.back());
                        m_moves.decr();
                        i -= 1;
                        continue;
                    }

                    if (move.typeOf() == chess::Move::PROMOTION &&
                        move.promotionType() == chess::PieceType::QUEEN)
                    {
                        std::swap(move, m_moves.back());
                        m_moves.decr();
                        i -= 1;
                        continue;
                    }

                    assert(!m_heuristics.is_capture(m_position, move));

                    if (move == m_heuristics.killers[m_ply][0].first)
                    {
                        move.setScore(32500);
                        continue;
                    }

                    if (move == m_heuristics.killers[m_ply][1].first)
                    {
                        move.setScore(32000);
                        continue;
                    }

                    int32_t score = 0;

                    // normal
                    score += m_heuristics
                                 .main_history[m_position.sideToMove()][move.from().index()]
                                              [move.to().index()]
                                 .get_value();

                    // low ply
                    if (m_ply < LOW_PLY)
                    {
                        score += features::QUIET_LOW_PLY_SCALE *
                                 m_heuristics
                                     .low_ply[m_position.sideToMove()][m_ply][move.from().index()]
                                             [move.to().index()]
                                     .get_value() /
                                 (1 + m_ply);
                    }

                    // pawn history
                    score += m_heuristics
                                 .pawn[m_pawn_key & PAWN_STRUCTURE_SIZE_M1]
                                      [m_position.at(move.from())][move.to().index()]
                                 .get_value();

                    // continuation
                    for (int i = 0; i < NUM_CONTINUATION; ++i)
                    {
                        if (m_continuations[i] != nullptr)
                            score +=
                                (*m_continuations[i])[m_position.at(move.from())][move.to().index()]
                                    .get_value() /
                                2;
                    }

                    if (move == counter)
                        score += 10000;

                    score = std::clamp(score, -32000, 32000);
                    move.setScore(score);
                }

                sort_moves(m_moves, m_capture_end, m_moves.size());
                m_move_index = m_capture_end;
                m_stage++;
                break;
            }

            case movegen_stage::EQUIET: {
                m_move_index =
                    pick_move(m_moves, m_move_index, m_moves.size(), [](auto &) { return true; });
                if (m_move_index < m_moves.size())
                    return m_moves[m_move_index++];

                m_stage = static_cast<int>(movegen_stage::DONE);
                break;
            }

                // explore only see captures
            case movegen_stage::PROB_GOOD_CAPTURE: {
                m_move_index = pick_move(m_moves, m_move_index, m_moves.size(), [&](auto &move) {
                    return see::test_ge(m_position, move, m_prob_margin);
                });
                if (m_move_index < m_moves.size())
                    return m_moves[m_move_index++];

                m_stage = static_cast<int>(movegen_stage::DONE);
                break;
            }

            case movegen_stage::DONE:
                return chess::Move::NO_MOVE;
            }
        }
    }

    void generate_threat()
    {
        threat_piece = chess::PieceType::NONE;
        threats = 0;

        if (m_prev_move != chess::Move::NO_MOVE)
        {
            threat_piece = heuristics::get_prev_piece_threat(m_position, m_prev_move).type();
            if (threat_piece == chess::PieceType::QUEEN)
                threats = chess::attacks::queen(m_prev_move.to(), m_position.occ());
            else if (threat_piece == chess::PieceType::ROOK)
                threats = chess::attacks::rook(m_prev_move.to(), m_position.occ());
            else if (threat_piece == chess::PieceType::BISHOP)
                threats = chess::attacks::bishop(m_prev_move.to(), m_position.occ());
            else if (threat_piece == chess::PieceType::KNIGHT)
                threats = chess::attacks::knight(m_prev_move.to());
            else if (threat_piece == chess::PieceType::PAWN)
                threats = chess::attacks::pawn(m_position.sideToMove() ^ 1, m_prev_move.to()) &
                          m_position.occ();
        }

        // pawn_threats = 0;
        // auto pawns = m_position.pieces(chess::PieceType::PAWN, m_position.sideToMove() ^ 1);
        // while (pawns)
        // {
        //     auto sq = pawns.pop();
        //     pawn_threats |= chess::attacks::pawn(m_position.sideToMove() ^ 1, sq);
        // }
    }

    bool is_draw()
    {
        assert(m_moves.empty());
        chess::movegen::legalmoves_quiet(m_moves, m_position, m_precompute);
        return m_moves.empty();
    }

    template <typename Pred>
    int pick_move(chess::Movelist &moves, const int start, const int end, Pred filter)
    {
        for (int i = start; i < end; ++i)
        {
            // ignore specific moves
            if (!filter(moves[i]))
                continue;

            return i;
        }

        return end;
    }

    static void sort_moves(chess::Movelist &moves, int start, int end,
                           int limit = std::numeric_limits<int16_t>::min())
    {
        for (int i = start + 1; i < end; ++i)
        {
            if (moves[i].score() >= limit)
            {
                chess::Move temp = moves[i];
                int j = i - 1;
                while (j >= start && moves[j].score() < temp.score())
                {
                    moves[j + 1] = moves[j];
                    j--;
                }
                moves[j + 1] = temp;
            }
        }
    }
};
