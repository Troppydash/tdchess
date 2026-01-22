#pragma once

#include "chess.h"
#include "param.h"

struct table_entry_result
{
    int32_t score;
    bool hit;
    chess::Move move;
};

class table_entry
{
public:
    uint64_t m_hash = 0;
    int32_t m_score = 0;
    chess::Move m_best_move = chess::Move::NULL_MOVE;
    int16_t m_depth = 0;
    uint8_t m_flag = 0;

    [[nodiscard]] table_entry_result get(uint64_t hash, uint8_t ply, uint8_t depth, int32_t alpha, int32_t beta) const
    {
        int32_t adj_score = 0;
        bool should_use = false;
        chess::Move best_move = chess::Move::NULL_MOVE;

        if (m_hash == hash)
        {
            best_move = m_best_move;
            adj_score = m_score;

            if (m_depth >= depth)
            {
                int score = m_score;

                // normalize for depth
                if (score > param::CHECKMATE)
                    score -= ply;
                if (score < -param::CHECKMATE)
                    score += ply;

                if (m_flag == param::EXACT_FLAG)
                {
                    adj_score = score;
                    should_use = true;
                } else if (m_flag == param::ALPHA_FLAG && score <= alpha)
                {
                    adj_score = alpha;
                    should_use = true;
                } else if (m_flag == param::BETA_FLAG && score >= beta)
                {
                    adj_score = beta;
                    should_use = true;
                }
            }
        }

        return {adj_score, should_use, best_move};
    }

    void set(uint64_t hash, int32_t score, const chess::Move &best_move, uint8_t ply, uint8_t depth, uint8_t flag)
    {
        m_hash = hash;
        m_depth = depth;
        m_best_move = best_move;
        m_flag = flag;

        // to absolute depth
        if (score > param::CHECKMATE)
            score += ply;
        if (score < -param::CHECKMATE)
            score -= ply;

        m_score = score;
    }
};

class table
{
public:
    std::vector<table_entry> m_entries;
    size_t m_size;

    explicit table(size_t size_in_mb)
    {
        // TODO: should prob make this a power of 2
        m_size = size_in_mb * 1024 * 1024 / sizeof(table_entry);

        for (size_t i = 0; i < m_size; ++i)
            m_entries.push_back(table_entry{});
    }

    table_entry &probe(uint64_t hash)
    {
        uint64_t index = hash % m_size;
        return m_entries[index];
    }

    int16_t occupied() const
    {
        size_t count = 0;
        for (const auto &entry: m_entries)
        {
            if (entry.m_hash > 0)
                count++;
        }

        return static_cast<int16_t>(count * 100 / m_entries.size());
    }
};
