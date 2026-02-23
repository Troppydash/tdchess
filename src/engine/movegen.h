#pragma once

#include "features.h"
#include "heuristic.h"
#include "see.h"

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

constexpr int16_t IGNORE_SCORE = -32300;

class movegen
{
  private:
    int m_stage;
    // stack for regular: [bad capture, (bad quiet, good quiet)/good capture]
    chess::Movelist m_moves{};
    int m_bad_capture_end{0};
    int m_bad_quiet_end{0};
    int m_move_index{0};

    chess::Board &m_position;
    const heuristics &m_heuristics;
    chess::Move m_pv_move;
    chess::Move m_prev_move;
    int32_t m_ply;

    const continuation_history *m_continuation1 = nullptr;
    const continuation_history *m_continuation2 = nullptr;
    const continuation_history *m_continuation3 = nullptr;
    const continuation_history *m_continuation4 = nullptr;

    bool m_skip_quiet = false;

  public:
    explicit movegen(chess::Board &position, const heuristics &heuristics, chess::Move pv_move,
                     chess::Move prev_move, int32_t ply, movegen_stage stage = movegen_stage::PV)
        : m_stage{static_cast<int>(stage)}, m_position(position), m_heuristics(heuristics),
          m_pv_move(pv_move), m_prev_move(prev_move), m_ply(ply)
    {
    }

    explicit movegen(chess::Board &position, const heuristics &heuristics, chess::Move pv_move,
                     chess::Move prev_move, int32_t ply, const continuation_history *continuation1,
                     const continuation_history *continuation2,
                     const continuation_history *continuation3,
                     const continuation_history *continuation4,
                     movegen_stage stage = movegen_stage::PV)
        : m_stage{static_cast<int>(stage)}, m_position(position), m_heuristics(heuristics),
          m_pv_move(pv_move), m_prev_move(prev_move), m_ply(ply), m_continuation1(continuation1),
          m_continuation2(continuation2), m_continuation3(continuation3),
          m_continuation4(continuation4)
    {
    }

    explicit movegen(chess::Board &position, const heuristics &heuristics, chess::Move pv_move,
                     chess::Move prev_move, int32_t ply, const continuation_history *continuation1,
                     movegen_stage stage = movegen_stage::PV)
        : m_stage{static_cast<int>(stage)}, m_position(position), m_heuristics(heuristics),
          m_pv_move(pv_move), m_prev_move(prev_move), m_ply(ply), m_continuation1(continuation1)
    {
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
                // generate
                chess::movegen::legalmoves<chess::movegen::MoveGenType::CAPTURE>(m_moves,
                                                                                 m_position);

                // generate quiet queen promotions
                chess::movegen::legal_promote_moves<chess::movegen::MoveGenType::QUIET>(m_moves,
                                                                                        m_position);

                // score
                for (auto &move : m_moves)
                {
                    if (move == m_pv_move)
                    {
                        move.setScore(IGNORE_SCORE);
                        continue;
                    }

                    auto captured = m_heuristics.get_capture(m_position, move);
                    int16_t mvv = see::PIECE_VALUES[captured] * features::CAPTURE_MVV_SCALE;

                    // capture history
                    int16_t capture_score = m_heuristics
                                                .capture_history[m_position.at(move.from())]
                                                                [move.to().index()][captured]
                                                .get_value();

                    int32_t score = mvv + capture_score;

                    if (move.typeOf() == chess::Move::PROMOTION)
                        score += 10000;

                    score = std::clamp(score, -32000, 32000);
                    move.setScore(score);
                }

                m_bad_capture_end = 0;
                m_move_index = 0;
                m_stage++;
                break;
            }

                // generate all evasion moves
            case movegen_stage::EINIT: {
                chess::movegen::legalmoves(m_moves, m_position);

                // score
                for (auto &move : m_moves)
                {
                    if (move == m_pv_move)
                    {
                        move.setScore(IGNORE_SCORE);
                        continue;
                    }

                    if (m_heuristics.is_capture(m_position, move))
                    {
                        move.setScore(
                            see::PIECE_VALUES[m_heuristics.get_capture(m_position, move)] +
                            (32000 - see::QUEEN_VALUE));
                    }
                    else
                    {
                        int32_t score = m_heuristics
                                            .main_history[m_position.sideToMove()]
                                                         [move.from().index()][move.to().index()]
                                            .get_value();

                        if (m_continuation1 != nullptr)
                            score +=
                                (*m_continuation1)[m_position.at(move.from())][move.to().index()]
                                    .get_value() /
                                2;

                        score = std::clamp(score, -30000, 30000);
                        move.setScore(score);
                    }
                }

                m_stage++;
                m_move_index = 0;
                break;
            }

                // iterate good captures, sorting into bad captures
            case movegen_stage::GOOD_CAPTURE: {
                // see check, incr bad_capture_end
                m_move_index = pick_move(m_moves, m_move_index, m_moves.size(), [&](auto &move) {
                    if (!see::test_ge(m_position, move,
                                      -move.score() / features::GOOD_CAPTURE_SEE_DIV))
                    {
                        std::swap(m_moves[m_bad_capture_end], move);
                        m_bad_capture_end++;
                        return false;
                    }
                    return true;
                });
                if (m_move_index < m_moves.size())
                    return m_moves[m_move_index++];

                // note here that bad_capture_end should point to end of bad captures
                m_stage++;
                break;
            }

                // generating all quiet moves and scoring them
            case movegen_stage::QUIET_INIT: {
                // m_moves = [bad captures, quiets]
                m_moves.set_size(m_bad_capture_end);
                chess::movegen::legalmoves_no_clear<chess::movegen::MoveGenType::QUIET>(m_moves,
                                                                                        m_position);

                // m_moves = [bad captures, quiets]
                m_bad_quiet_end = m_bad_capture_end;
                uint64_t pawn_key = m_heuristics.get_pawn_key(m_position);
                for (int i = m_bad_capture_end; i < m_moves.size(); ++i)
                {
                    chess::Move &move = m_moves[i];
                    if (move == m_pv_move)
                    {
                        move.setScore(IGNORE_SCORE);
                        continue;
                    }

                    if (move.typeOf() == chess::Move::PROMOTION &&
                        (move.promotionType() == chess::PieceType::QUEEN ||
                         move.promotionType() == chess::PieceType::KNIGHT))
                    {
                        move.setScore(IGNORE_SCORE);
                        continue;
                    }

                    // killer move
                    bool found = false;
                    for (int j = 0; j < param::NUMBER_KILLERS; ++j)
                    {
                        const auto &entry = m_heuristics.killers[m_ply][j];
                        if (move == entry.first)
                        {
                            // mate killers first
                            move.setScore(32100 + entry.second - j * 100);
                            found = true;
                            break;
                        }
                    }
                    if (found)
                        continue;

                    // normal
                    int32_t score = 0;
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
                                 .pawn[pawn_key & PAWN_STRUCTURE_SIZE_M1]
                                      [m_position.at(move.from())][move.to().index()]
                                 .get_value();

                    // continuation
                    if (m_continuation1 != nullptr)
                        score += (*m_continuation1)[m_position.at(move.from())][move.to().index()]
                                     .get_value() /
                                 2;

                    if (m_continuation2 != nullptr)
                        score += (*m_continuation2)[m_position.at(move.from())][move.to().index()]
                                     .get_value() /
                                 2;

                    if (m_continuation3 != nullptr)
                        score += (*m_continuation3)[m_position.at(move.from())][move.to().index()]
                                     .get_value() /
                                 2;

                    if (m_continuation4 != nullptr)
                        score += (*m_continuation4)[m_position.at(move.from())][move.to().index()]
                                     .get_value() /
                                 2;

                    // penalty for weak promotion
                    if (move.typeOf() == chess::Move::PROMOTION)
                        score -= 1000;

                    score = std::clamp(score, -31000, 31000);
                    move.setScore(score);

                    if (score < features::QUIET_BAD_THRESHOLD)
                    {
                        std::swap(m_moves[m_bad_quiet_end], m_moves[i]);
                        m_bad_quiet_end++;
                    }
                }

                m_move_index = m_bad_quiet_end;
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

                m_move_index =
                    pick_move(m_moves, m_move_index, m_moves.size(), [](auto &_m) { return true; });
                if (m_move_index < m_moves.size())
                    return m_moves[m_move_index++];

                m_move_index = 0;
                m_stage++;
                break;
            }
                // iterating through bad capture moves
            case movegen_stage::BAD_CAPTURE: {
                m_move_index = pick_move(m_moves, m_move_index, m_bad_capture_end,
                                         [](auto &_m) { return true; });
                if (m_move_index < m_bad_capture_end)
                    return m_moves[m_move_index++];

                m_move_index = m_bad_capture_end;
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

                m_move_index = pick_move(m_moves, m_move_index, m_bad_quiet_end,
                                         [](auto &_m) { return true; });
                if (m_move_index < m_bad_quiet_end)
                    return m_moves[m_move_index++];

                m_stage = static_cast<int>(movegen_stage::DONE);
                break;
            }

                // explore all good captures
            case movegen_stage::QCAPTURE: {
                m_move_index =
                    pick_move(m_moves, m_move_index, m_moves.size(), [](auto &_m) { return true; });
                if (m_move_index < m_moves.size())
                    return m_moves[m_move_index++];

                m_stage = static_cast<int>(movegen_stage::DONE);
                break;
            }

            case movegen_stage::EMOVES: {
                m_move_index =
                    pick_move(m_moves, m_move_index, m_moves.size(), [](auto &_m) { return true; });
                if (m_move_index < m_moves.size())
                    return m_moves[m_move_index++];

                m_stage = static_cast<int>(movegen_stage::DONE);
                break;
            }

                // explore only see captures
            case movegen_stage::PROB_GOOD_CAPTURE: {
                m_move_index = pick_move(m_moves, m_move_index, m_moves.size(), [&](auto &move) {
                    return see::test_ge(m_position, move,
                                        -move.score() / features::PROB_GOOD_CAPTURE_SEE_DIV);
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

    template <typename Pred>
    int pick_move(chess::Movelist &moves, const int start, const int end, Pred filter)
    {
        for (int i = start; i < end; ++i)
        {
            sort_moves(moves, i, end);

            // ignore specific moves
            if (moves[i].score() == IGNORE_SCORE || !filter(moves[i]))
                continue;

            return i;
        }

        return end;
    }

    void sort_moves(chess::Movelist &moves, int i, int end = -1)
    {
        if (end == -1)
            end = moves.size();

        int highest_score = moves[i].score();
        int highest_index = i;
        for (int j = i + 1; j < end; ++j)
        {
            if (moves[j].score() > highest_score)
            {
                highest_score = moves[j].score();
                highest_index = j;
            }
        }

        if (i != highest_index)
            std::swap(moves[i], moves[highest_index]);
    }
};