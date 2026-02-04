#pragma once

#include "../hpplib/chess.h"
#include "param.h"

#include <cmath>
#include <cstring>

struct table_entry_result
{
    bool hit;
    bool can_use;
    int32_t score;
    int32_t depth;
    chess::Move move;
    int32_t static_eval;
    bool is_pv;
    uint8_t flag;
};

class table_entry
{
  public:
    uint64_t m_hash = 0;
    int32_t m_score = param::VALUE_NONE;
    chess::Move m_best_move = chess::Move::NO_MOVE;
    int32_t m_depth = param::UNSEARCHED_DEPTH;
    int32_t m_static_eval = param::VALUE_NONE;
    bool m_is_pv = false;
    uint8_t m_flag = param::NO_FLAG;
    uint8_t m_age = 0;

    [[nodiscard]] table_entry_result get(uint64_t hash, int32_t ply, int32_t depth, int32_t alpha,
                                         int32_t beta) const
    {
        int32_t adj_score = param::VALUE_NONE;
        bool can_use = false;
        bool is_hit = false;
        if (m_hash == hash)
        {
            is_hit = true;

            if (m_depth >= depth)
            {
                if (param::IS_VALID(m_score))
                {
                    int32_t score = m_score;

                    // normalize for depth
                    if (score > param::CHECKMATE)
                        score -= ply;
                    if (score < -param::CHECKMATE)
                        score += ply;

                    if (m_flag == param::EXACT_FLAG)
                    {
                        adj_score = score;
                        can_use = true;
                    }
                    else if (m_flag == param::ALPHA_FLAG && score <= alpha)
                    {
                        adj_score = score;
                        can_use = true;
                    }
                    else if (m_flag == param::BETA_FLAG && score >= beta)
                    {
                        adj_score = score;
                        can_use = true;
                    }
                }
            }
        }

        return {.hit = is_hit,
                .can_use = can_use,
                .score = adj_score,
                .depth = m_depth,
                .move = m_best_move,
                .static_eval = m_static_eval,
                .is_pv = m_is_pv,
                .flag = m_flag};
    }

    void set(uint64_t hash, uint8_t flag, int32_t score, int32_t ply, int32_t depth,
             const chess::Move &best_move, int32_t static_eval, bool is_pv, uint8_t age)
    {
        m_hash = hash;
        m_depth = depth;
        m_best_move = best_move;
        m_flag = flag;
        m_static_eval = static_eval;
        m_is_pv = is_pv;

        // to absolute depth
        if (param::IS_VALID(score))
        {
            if (score > param::CHECKMATE)
                score += ply;
            if (score < -param::CHECKMATE)
                score -= ply;
        }

        m_score = score;
        m_age = age;
    }

    bool can_write(int32_t depth, uint8_t age) const
    {
        return depth >= (m_depth - (m_depth >= 2)) || age > m_age;
    }
};

class table
{
  public:
    table_entry *m_entries = nullptr;
    size_t m_size;
    int m_power;
    uint64_t m_mask;

    explicit table(size_t size_in_mb)
    {
        const size_t max_size = size_in_mb * 1024 * 1024 / sizeof(table_entry);
        m_power = std::floor(std::log2(max_size));
        m_size = 1ull << m_power;
        m_mask = m_size - 1;

        m_entries = static_cast<table_entry *>(std::malloc(m_size * sizeof(table_entry)));
        clear();
    }

    ~table()
    {
        if (m_entries != nullptr)
            std::free(m_entries);
    }

    void clear()
    {
        for (size_t i = 0; i < m_size; ++i)
        {
            m_entries[i].m_hash = 0;
            m_entries[i].m_depth = param::UNSEARCHED_DEPTH;
            m_entries[i].m_static_eval = param::VALUE_NONE;
            m_entries[i].m_score = param::VALUE_NONE;
            m_entries[i].m_flag = param::NO_FLAG;
            m_entries[i].m_is_pv = false;
            m_entries[i].m_best_move = chess::Move::NO_MOVE;
            m_entries[i].m_age = 0;
        }
    }

    table_entry &probe(const uint64_t hash)
    {
        return m_entries[hash & m_mask];
    }

    int16_t occupied() const
    {
        size_t count = 0;
        for (size_t i = 0; i < m_size; ++i)
        {
            if (m_entries[i].m_hash != 0)
                count++;
        }

        return static_cast<int16_t>(count * 1000 / m_size);
    }
};
