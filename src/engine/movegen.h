#pragma once

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
    QGOOD_CAPTURE,
    QBAD_CAPTURE,

    PROBPV,
    PROB_CAPTURE_INIT,
    PROB_GOOD_CAPTURE,

    DONE
};

class movegen
{
  private:
    int m_stage;
    chess::Movelist m_moves{};
    int m_bad_capture_end{0};
    int m_bad_quiet_end{0};
    int m_move_index{0};

    const chess::Board &m_position;
    const heuristics &m_heuristics;
    chess::Move m_pv_move;
    chess::Move m_prev_move;
    int32_t m_ply;

    const continuation_history *m_continuation1 = nullptr;
    const continuation_history *m_continuation2 = nullptr;

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
                     movegen_stage stage = movegen_stage::PV)
        : m_stage{static_cast<int>(stage)}, m_position(position), m_heuristics(heuristics),
          m_pv_move(pv_move), m_prev_move(prev_move), m_ply(ply), m_continuation1(continuation1),
          m_continuation2(continuation2)
    {
    }

    chess::Move next_move()
    {
        while (true)
        {
            switch (static_cast<movegen_stage>(m_stage))
            {
            case movegen_stage::PV:
            case movegen_stage::QPV:
            case movegen_stage::PROBPV: {
                m_stage++;
                if (m_pv_move != chess::Move::NO_MOVE)
                    return m_pv_move;

                [[fallthrough]];
            }

            case movegen_stage::CAPTURE_INIT:
            case movegen_stage::QCAPTURE_INIT:
            case movegen_stage::PROB_CAPTURE_INIT: {
                // generate
                chess::movegen::legalmoves<chess::movegen::MoveGenType::CAPTURE>(m_moves,
                                                                                 m_position);

                // score, update bad captures
                m_bad_capture_end = 0;
                for (auto &move : m_moves)
                {
                    if (move == m_pv_move)
                    {
                        move.setScore(-31000);
                        continue;
                    }

                    auto captured = move.typeOf() == chess::Move::ENPASSANT
                                        ? chess::PieceType::PAWN
                                        : m_position.at(move.to()).type();
                    int16_t mvv = see::PIECE_VALUES[captured];

                    // capture history
                    int16_t capture_score = m_heuristics
                                                .capture_history[m_position.at(move.from())]
                                                                [move.to().index()][captured]
                                                .get_value();

                    int16_t score = mvv + capture_score;
                    move.setScore(score);

                    if (!see::test_ge(m_position, move, -100))
                    {
                        std::swap(m_moves[m_bad_capture_end], move);
                        m_bad_capture_end++;
                    }
                }

                m_move_index = m_bad_capture_end;
                m_stage++;
                break;
            }

            case movegen_stage::GOOD_CAPTURE: {
                m_move_index = pick_move(m_moves, m_move_index, m_moves.size());
                if (m_move_index < m_moves.size())
                    return m_moves[m_move_index++];

                m_stage++;
                [[fallthrough]];
            }
            case movegen_stage::QUIET_INIT: {
                // m_moves = [bad captures, quiets]
                m_moves.set_size(m_bad_capture_end);
                chess::movegen::legalmoves_no_clear<chess::movegen::MoveGenType::QUIET>(m_moves,
                                                                                        m_position);

                // chess::Move counter = chess::Move::NO_MOVE;
                // if (m_prev_move != chess::Move::NO_MOVE &&
                //     m_position.at(m_prev_move.to()) != chess::Piece::NONE)
                // {
                //     counter =
                //         m_heuristics
                //             .counter[m_position.at(m_prev_move.to())][m_prev_move.to().index()];
                // }

                // m_moves = [bad captures, bad_quiet, good_quiet]
                m_bad_quiet_end = m_bad_capture_end;
                for (int i = m_bad_capture_end; i < m_moves.size(); ++i)
                {
                    chess::Move &move = m_moves[i];
                    if (move == m_pv_move)
                    {
                        move.setScore(-31000);
                        continue;
                    }

                    // killer move
                    bool found = false;
                    for (int i = 0; i < param::NUMBER_KILLERS; ++i)
                    {
                        const auto &entry = m_heuristics.killers[m_ply][i];
                        if (move == entry.first)
                        {
                            // mate killers first
                            move.setScore(30000 + 100 * entry.second - i);
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
                                 .get_value() /
                             2;

                    // low ply
                    if (m_ply < LOW_PLY)
                    {
                        score += m_heuristics
                                     .low_ply_history[m_position.sideToMove()][m_ply]
                                                     [move.from().index()][move.to().index()]
                                     .get_value() /
                                 2;
                    }

                    // continuation
                    if (m_continuation1 != nullptr)
                        score += (*m_continuation1)[m_position.at(move.from())][move.to().index()]
                                     .get_value() /
                                 2;

                    if (m_continuation2 != nullptr)
                        score += (*m_continuation2)[m_position.at(move.from())][move.to().index()]
                                     .get_value() /
                                 6;

                    score = std::clamp(score, -32000, 32000);

                    move.setScore(score);

                    if (score < -500)
                    {
                        std::swap(m_moves[m_bad_quiet_end], m_moves[i]);
                        m_bad_quiet_end++;
                    }
                }

                m_move_index = m_bad_quiet_end;
                m_stage++;
                [[fallthrough]];
            }
            case movegen_stage::GOOD_QUIET: {
                m_move_index = pick_move(m_moves, m_move_index, m_moves.size());
                if (m_move_index < m_moves.size())
                    return m_moves[m_move_index++];

                m_move_index = 0;
                m_stage++;
                [[fallthrough]];
            }
            case movegen_stage::BAD_CAPTURE: {
                m_move_index = pick_move(m_moves, m_move_index, m_bad_capture_end);
                if (m_move_index < m_bad_capture_end)
                    return m_moves[m_move_index++];

                m_move_index = m_bad_capture_end;
                m_stage++;
                [[fallthrough]];
            }
            case movegen_stage::BAD_QUIET: {
                m_move_index = pick_move(m_moves, m_move_index, m_bad_quiet_end);
                if (m_move_index < m_bad_quiet_end)
                    return m_moves[m_move_index++];

                m_stage = static_cast<int>(movegen_stage::DONE);
                break;
            }

            case movegen_stage::QGOOD_CAPTURE: {
                m_move_index = pick_move(m_moves, m_move_index, m_moves.size());
                if (m_move_index < m_moves.size())
                    return m_moves[m_move_index++];

                m_move_index = 0;
                m_stage++;
                [[fallthrough]];
            }
            case movegen_stage::QBAD_CAPTURE: {
                m_move_index = pick_move(m_moves, m_move_index, m_bad_capture_end);
                if (m_move_index < m_bad_capture_end)
                    return m_moves[m_move_index++];

                m_stage = static_cast<int>(movegen_stage::DONE);
                break;
            }

            case movegen_stage::PROB_GOOD_CAPTURE: {
                m_move_index = pick_move(m_moves, m_move_index, m_moves.size());
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

    int pick_move(chess::Movelist &moves, int start, int end)
    {
        for (int i = start; i < end; ++i)
        {
            sort_moves(moves, i, end);
            if (moves[i] != m_pv_move)
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