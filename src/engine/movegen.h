#pragma once

#include "heuristic.h"
#include "see.h"

enum class movegen_stage
{
    PV = 0,
    GOOD_CAPTURE = 1,
    PROMOTION = 2,
    KILLER = 3,
    BAD_CAPTURE = 4,
    NORMAL = 5,
    DONE = 6
};

class movegen
{
  private:
    movegen_stage m_stage{movegen_stage::PV};
    chess::Movelist m_moves{};
    int m_move_index{0};

    chess::Movelist m_promotions{};
    chess::Movelist m_good_captures{};
    chess::Movelist m_bad_captures{};
    chess::Movelist m_killers{};
    chess::Movelist m_normal{};
    chess::Movelist m_bad_promotion{};

    chess::Board &m_position;
    heuristics &m_heuristics;
    chess::Move m_pv_move;
    chess::Move m_prev_move;
    int32_t m_ply;

    std::array<std::array<int16_t, 6>, 7> m_mvv_lva;

    // whether to skip non-captures
    bool m_only_good;

  public:
    explicit movegen(chess::Board &position, heuristics &heuristics, chess::Move pv_move,
                     chess::Move prev_move, int32_t ply, bool only_good = false)
        : m_position(position), m_heuristics(heuristics), m_pv_move(pv_move),
          m_prev_move(prev_move), m_ply(ply), m_only_good(only_good)
    {

        // set mvv_lva
        m_mvv_lva = {
            std::array<int16_t, 6>{15, 14, 13, 12, 11, 10}, // victim Pawn
            {25, 24, 23, 22, 21, 20},                       // victim Knight
            {35, 34, 33, 32, 31, 30},                       // victim Bishop
            {45, 44, 43, 42, 41, 40},                       // victim Rook
            {55, 54, 53, 52, 51, 50},                       // victim Queen
            {0, 0, 0, 0, 0, 0},                             // victim King
            {0, 0, 0, 0, 0, 0},                             // No piece
        };
    }

    movegen_stage get_stage() const
    {
        return m_stage;
    }

    chess::Move next_move()
    {
        while (true)
        {
            switch (m_stage)
            {
            case movegen_stage::PV: {
                m_stage = movegen_stage::GOOD_CAPTURE;
                if (m_pv_move != chess::Move::NO_MOVE)
                    return m_pv_move;
                break;
            }
            case movegen_stage::GOOD_CAPTURE: {
                if (!m_good_captures.empty())
                {
                    // get move
                    if (m_move_index < m_good_captures.size())
                    {
                        sort_moves(m_good_captures, m_move_index);
                        return m_good_captures[m_move_index++];
                    }

                    m_stage = movegen_stage::PROMOTION;
                    break;
                }

                chess::movegen::legalmoves<chess::movegen::MoveGenType::CAPTURE>(m_moves,
                                                                                 m_position);
                // move captures to good_captures or bad_captures
                for (auto &move : m_moves)
                {
                    if (move == m_pv_move)
                        continue;

                    auto captured = move.typeOf() == chess::Move::ENPASSANT
                                        ? chess::PieceType::PAWN
                                        : m_position.at(move.to()).type();
                    auto attacker = m_position.at(move.from()).type();
                    int16_t mvv_lva = m_mvv_lva[captured][attacker];

                    // capture history
                    int16_t capture_score = m_heuristics
                                                .capture_history[m_position.at(move.from())]
                                                                [move.to().index()][captured]
                                                .get_value();

                    int16_t combined_rank = mvv_lva + capture_score;
                    int16_t score = static_cast<int16_t>(combined_rank);
                    move.setScore(score);

                    if (see::test_ge(m_position, move, 0))
                    {
                        m_good_captures.add(move);
                    }
                    else
                    {
                        m_bad_captures.add(move);
                    }
                }

                m_move_index = 0;
                if (m_good_captures.empty())
                {
                    m_stage = movegen_stage::PROMOTION;
                }
                break;
            }

            case movegen_stage::PROMOTION: {
                if (m_only_good)
                {
                    m_stage = movegen_stage::BAD_CAPTURE;
                    break;
                }

                if (!m_promotions.empty())
                {
                    // get move
                    if (m_move_index < m_promotions.size())
                    {
                        sort_moves(m_promotions, m_move_index);
                        return m_promotions[m_move_index++];
                    }

                    m_stage = movegen_stage::KILLER;
                    break;
                }

                // try to generate promotions, note all the capture promotions are in Captures
                chess::movegen::legal_promote_moves<chess::movegen::MoveGenType::QUIET>(m_moves,
                                                                                        m_position);
                // move promotions to m_promotions or m_bad_captures
                for (auto &move : m_moves)
                {
                    if (move == m_pv_move)
                        continue;

                    assert(move.typeOf() == chess::Move::PROMOTION);
                    int16_t score = see::PIECE_VALUES[move.promotionType()];
                    move.setScore(score);

                    if ((move.promotionType() == chess::PieceType::QUEEN ||
                         move.promotionType() == chess::PieceType::KNIGHT))
                        m_promotions.add(move);
                    else
                        m_bad_promotion.add(move);
                }

                m_move_index = 0;
                if (m_promotions.empty())
                    m_stage = movegen_stage::KILLER;
                break;
            }
            case movegen_stage::KILLER: {
                if (!m_killers.empty())
                {
                    if (m_move_index < m_killers.size())
                    {
                        sort_moves(m_killers, m_move_index);
                        return m_killers[m_move_index++];
                    }

                    // bad capture needs 0 index
                    m_move_index = 0;
                    m_stage = movegen_stage::BAD_CAPTURE;
                    break;
                }

                // generate killers and normal
                chess::movegen::legalmoves<chess::movegen::MoveGenType::QUIET>(m_moves, m_position);
                // convert to m_killers and m_normal
                for (auto &move : m_moves)
                {
                    if (move == m_pv_move)
                        continue;

                    // ignore promotions
                    if (move.typeOf() == chess::Move::PROMOTION)
                        continue;

                    // killers
                    bool found = false;
                    for (size_t i = 0; i < param::NUMBER_KILLERS; ++i)
                    {
                        const auto &entry = m_heuristics.killers[m_ply][i];
                        if (move == entry.first)
                        {
                            // mate killers first
                            move.setScore(100 * entry.second - i);
                            m_killers.add(move);
                            found = true;
                            break;
                        }
                    }
                    if (found)
                        continue;

                    // normal
                    int16_t score = 0;
                    if (m_prev_move != chess::Move::NO_MOVE)
                    {
                        const auto &counter =
                            m_heuristics
                                .counter[m_position.sideToMove()][m_prev_move.from().index()]
                                        [m_prev_move.to().index()];
                        if (move == counter)
                            score += param::COUNTER_BONUS;
                    }

                    score += m_heuristics
                                 .main_history[m_position.sideToMove()][move.from().index()]
                                              [move.to().index()]
                                 .get_value();

                    move.setScore(score);
                    m_normal.add(move);
                }

                // also add bad promotion to normals
                for (auto &move : m_bad_promotion)
                    m_normal.add(move);

                m_move_index = 0;
                if (m_killers.empty())
                    m_stage = movegen_stage::BAD_CAPTURE;
                break;
            }
            case movegen_stage::BAD_CAPTURE: {
                if (m_move_index < m_bad_captures.size())
                {
                    sort_moves(m_bad_captures, m_move_index);
                    return m_bad_captures[m_move_index++];
                }

                // normal requires zero index
                m_move_index = 0;
                m_stage = movegen_stage::NORMAL;
                break;
            }
            case movegen_stage::NORMAL: {
                if (m_only_good)
                {
                    m_stage = movegen_stage::DONE;
                    break;
                }

                // get move
                if (m_move_index < m_normal.size())
                {
                    sort_moves(m_normal, m_move_index);
                    return m_normal[m_move_index++];
                }

                m_stage = movegen_stage::DONE;
                break;
            }

            case movegen_stage::DONE:
                return chess::Move::NO_MOVE;
            }
        }
    }

    void sort_moves(chess::Movelist &moves, int i)
    {
        int highest_score = moves[i].score();
        int highest_index = i;
        for (int j = i + 1; j < moves.size(); ++j)
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