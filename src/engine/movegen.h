#pragma once

#include "heuristic.h"

enum class movegen_stage
{
    PV,
    PROMOTION,
    GOOD_CAPTURE,
    KILLER,
    BAD_CAPTURE,
    NORMAL
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

    chess::Board &m_position;
    history_heuristic &m_main_history;
    chess::Move m_pv_move;
    chess::Move m_prev_move;
    int32_t m_ply;

  public:
    explicit movegen(chess::Board &position, history_heuristic &main_history, chess::Move pv_move,
                     chess::Move prev_move, int32_t ply)
        : m_position(position), m_main_history(main_history), m_pv_move(pv_move),
          m_prev_move(prev_move), m_ply(ply)
    {
    }

    chess::Move next_move()
    {
        switch (m_stage)
        {
        case movegen_stage::PV: {
            m_stage = movegen_stage::PROMOTION;
            if (m_pv_move != chess::Move::NO_MOVE)
                return m_pv_move;
            return next_move();
        }
        case movegen_stage::PROMOTION: {
            // TODO:
            chess::movegen::legalmoves<chess::movegen::MoveGenType::CAPTURE>(m_moves, m_position);
            score_moves(m_moves);
        }
        case movegen_stage::GOOD_CAPTURE: {
        }
        case movegen_stage::KILLER: {
        }
        case movegen_stage::BAD_CAPTURE: {
        }
        case movegen_stage::NORMAL: {
        }
        }

        return chess::Move::NO_MOVE;
    }

    void score_moves(chess::Movelist &moves)
    {
    }

    void sort_moves(chess::Movelist &moves, int i)
    {
    }
};