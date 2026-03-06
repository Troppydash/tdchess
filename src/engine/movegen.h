#pragma once

#include "features.h"
#include "heuristic.h"
#include "see.h"
#include "sorting.h"

enum class movegen_stage
{
    PV,
    CAPTURE_INIT,
    GOOD_CAPTURE,
    QUIET_INIT,
    GOOD_QUIET,
    BAD_CAPTURE,
    BAD_QUIET,

    QPV,
    QCAPTURE_INIT,
    QCAPTURE,

    EPV,
    EINIT,
    EMOVES,

    PROBPV,
    PROB_CAPTURE_INIT,
    PROB_GOOD_CAPTURE,

    DONE
};

constexpr int16_t IGNORE_SCORE = std::numeric_limits<int16_t>::min();

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

    chess::movegen::precompute m_precompute{};

  public:
    explicit movegen(chess::Movelist &moves, chess::Board &position, const heuristics &heuristics,
                     chess::Move pv_move, chess::Move prev_move, int32_t ply, int depth,
                     movegen_stage stage = movegen_stage::PV)
        : m_moves{moves}, m_stage{static_cast<int>(stage)}, m_position(position),
          m_heuristics(heuristics), m_pv_move(pv_move), m_prev_move{prev_move}, m_ply(ply),
          m_depth(depth)
    {
    }

    explicit movegen(chess::Movelist &moves, chess::Board &position, const heuristics &heuristics,
                     chess::Move pv_move, chess::Move prev_move, int32_t ply, int depth,
                     int16_t margin, movegen_stage stage = movegen_stage::PV)
        : m_moves{moves}, m_stage{static_cast<int>(stage)}, m_position(position),
          m_heuristics(heuristics), m_pv_move(pv_move), m_prev_move{prev_move}, m_ply(ply),
          m_prob_margin{margin}, m_depth(depth)
    {
    }

    explicit movegen(
        chess::Movelist &moves, chess::Board &position, const heuristics &heuristics,
        chess::Move pv_move, chess::Move prev_move, int32_t ply, int depth,
        const std::array<const continuation_history *, NUM_CONTINUATION> &continuations,
        movegen_stage stage = movegen_stage::PV)
        : m_moves{moves}, m_stage{static_cast<int>(stage)}, m_position(position),
          m_heuristics(heuristics), m_pv_move(pv_move), m_prev_move{prev_move}, m_ply(ply),
          m_continuations{continuations}, m_depth(depth)
    {
    }

    explicit movegen(chess::Movelist &moves, chess::Board &position, const heuristics &heuristics,
                     chess::Move pv_move, chess::Move prev_move, int32_t ply, int depth,
                     const continuation_history *continuation1,
                     movegen_stage stage = movegen_stage::PV)
        : m_moves{moves}, m_stage{static_cast<int>(stage)}, m_position(position),
          m_heuristics(heuristics), m_pv_move(pv_move), m_prev_move{prev_move}, m_ply(ply),
          m_depth(depth)
    {
        m_continuations[0] = continuation1;
    }

    void skip_quiet()
    {
        m_skip_quiet = true;
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
                if (m_pv_move != chess::Move::NO_MOVE)
                    return m_pv_move;

                break;
            }

                // generate all capture moves and score them
            case movegen_stage::CAPTURE_INIT:
            case movegen_stage::QCAPTURE_INIT:
            case movegen_stage::PROB_CAPTURE_INIT: {
                // direct capture generation
                m_precompute = chess::movegen::legalmoves_precompute(m_position);
                m_moves.clear();
                chess::movegen::legalmoves_capture(m_moves, m_position, m_precompute);
                m_capture_end = m_moves.size();

                // score
                for (int i = 0; i < m_capture_end; ++i)
                {
                    auto &move = m_moves[i];
                    if (move == m_pv_move)
                    {
                        move.setScore(IGNORE_SCORE);
                        continue;
                    }

                    assert(m_heuristics.is_capture(m_position, move));

                    auto captured = m_heuristics.get_capture(m_position, move);
                    int16_t mvv = see::PIECE_VALUES[captured] * features::CAPTURE_MVV_SCALE;

                    // capture history
                    int16_t capture_score = m_heuristics
                                                .capture_history[m_position.at(move.from())]
                                                                [move.to().index()][captured]
                                                .get_value();

                    int32_t score = mvv + capture_score;
                    score = std::clamp(score, -32000, 32000);
                    move.setScore(score);
                }

                sort_moves(m_moves, 0, m_capture_end);

                m_bad_capture_end = 0;
                m_move_index = 0;
                m_stage++;
                break;
            }

                // generate all evasion moves
            case movegen_stage::EINIT: {
                // don't use the sep movegen since all moves are generated
                chess::movegen::legalmoves(m_moves, m_position);

                // score
                uint64_t pawn_key = m_heuristics.get_pawn_key(m_position);
                for (auto &move : m_moves)
                {
                    if (move == m_pv_move)
                    {
                        move.setScore(IGNORE_SCORE);
                        continue;
                    }

                    if (m_heuristics.is_capture(m_position, move))
                    {
                        int32_t score =
                            features::CAPTURE_MVV_SCALE *
                                see::PIECE_VALUES[m_heuristics.get_capture(m_position, move)] +
                            m_heuristics
                                .capture_history[m_position.at(move.from())][move.to().index()]
                                                [m_heuristics.get_capture(m_position, move)]
                                .get_value();

                        score *= 2;

                        // baseline
                        score += 20000;
                        score = std::clamp(score, -32000, 32000);
                        move.setScore(score);
                    }
                    else
                    {
                        int32_t score = m_heuristics
                                            .main_history[m_position.sideToMove()]
                                                         [move.from().index()][move.to().index()]
                                            .get_value();

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
                                     .pawn[pawn_key & PAWN_STRUCTURE_SIZE_M1]
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
                        //
                        // score += m_heuristics
                        //     .king[m_position.sideToMove()][m_heuristics.get_king_bucket(m_position)][move.from().index()]
                        //          [move.to().index()]
                        //     .get_value();

                        score = std::clamp(score, -32000, 32000);
                        move.setScore(score);
                    }
                }

                sort_moves(m_moves, 0, m_moves.size());

                m_stage++;
                m_move_index = 0;
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
                break;
            }

                // generating all quiet moves and scoring them
            case movegen_stage::QUIET_INIT: {
                if (!m_skip_quiet)
                {
                    chess::movegen::legalmoves_quiet(m_moves, m_position, m_precompute);

                    uint64_t pawn_key = m_heuristics.get_pawn_key(m_position);
                    for (int i = m_capture_end; i < m_moves.size(); ++i)
                    {
                        chess::Move &move = m_moves[i];
                        if (move == m_pv_move)
                        {
                            move.setScore(IGNORE_SCORE);
                            continue;
                        }

                        if (move.typeOf() == chess::Move::PROMOTION && (move.promotionType() == chess::PieceType::QUEEN || move.promotionType() == chess::PieceType::KNIGHT))
                        {
                            move.setScore(IGNORE_SCORE);
                            continue;
                        }

                        // killer move
                        if (m_heuristics.killers[m_ply][0].first == move)
                        {
                            move.setScore(32500);
                            continue;
                        }
                        // std::cout << m_position << std::endl;
                        // std::cout << chess::uci::moveToUci(move) << std::endl;

                        assert(!m_heuristics.is_capture(m_position, move));

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
                                     .pawn[pawn_key & PAWN_STRUCTURE_SIZE_M1]
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

                        score = std::clamp(score, -32000, 32000);
                        move.setScore(score);
                    }

                    sort_moves(m_moves, m_capture_end, m_moves.size(), -4000 * m_depth);
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

            case movegen_stage::EMOVES: {
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

    // TODO: try sorting here using selection sort
    template <typename Pred>
    int pick_move(chess::Movelist &moves, const int start, const int end, Pred filter)
    {
        for (int i = start; i < end; ++i)
        {
            // ignore specific moves
            if (moves[i].score() == IGNORE_SCORE || !filter(moves[i]))
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