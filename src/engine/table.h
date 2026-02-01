#pragma once

#include "../hpplib/chess.h"
#include "param.h"

#include <cmath>

enum Bound : uint8_t
{
    BOUND_NONE ,
    BOUND_UPPER,
    BOUND_LOWER,
    BOUND_EXACT = BOUND_UPPER | BOUND_LOWER
};

constexpr int DEPTH_UNSEARCHED = -1;

struct table_entry_result
{
    // the minimax score
    int32_t score;
    // the static evaluation
    int32_t eval;
    chess::Move move;
    bool is_pv;
    int depth;
    Bound bound;
};

class table_entry
{
  public:
    uint64_t m_hash = 0;
    // the minimax score
    int32_t m_score = param::VALUE_NONE;
    // the static evaluation
    int32_t m_eval = param::VALUE_NONE;
    chess::Move m_move = chess::Move::NO_MOVE;
    bool m_is_pv = false;
    int m_depth = DEPTH_UNSEARCHED;
    Bound m_bound = BOUND_NONE;

    [[nodiscard]] std::pair<bool, table_entry_result> get(uint64_t hash, int ply) const
    {
        table_entry_result adjusted{.score = param::VALUE_NONE,
                                    .eval = m_eval,
                                    .move = chess::Move::NO_MOVE,
                                    .is_pv = m_is_pv,
                                    .depth = m_depth,
                                    .bound = m_bound};

        bool tt_hit = false;
        if (m_hash == hash)
        {
            adjusted.move = m_move;
            adjusted.score = m_score;

            tt_hit = true;
        }

        // correct mate scores
        if (tt_hit && adjusted.score != param::VALUE_NONE)
        {
            // normalize for depth
            if (adjusted.score > param::CHECKMATE)
                adjusted.score -= ply;
            if (adjusted.score < -param::CHECKMATE)
                adjusted.score += ply;
        }

        return {tt_hit, adjusted};
    }

    void write(uint64_t hash, int32_t score, int32_t eval, chess::Move move, bool is_pv, int depth,
               Bound bound, int ply)
    {
        if (depth >= m_depth)
        {
            m_hash = hash;
            m_eval = eval;
            m_move = move;
            m_is_pv = is_pv;
            m_depth = depth;
            m_bound = bound;

            if (score != param::VALUE_NONE)
            {
                if (score > param::CHECKMATE)
                    score += ply;
                if (score < -param::CHECKMATE)
                    score -= ply;
                m_score = score;
            }
            else
                m_score = score;
        }
    }
};

class table
{
  private:
    std::vector<table_entry> m_entries;
    size_t m_size;
    int m_power;
    uint64_t m_mask;

  public:
    explicit table(size_t size_in_mb)
    {
        const size_t max_size = size_in_mb * 1024 * 1024 / sizeof(table_entry);
        m_power = std::floor(std::log2(max_size));
        m_size = 1ull << m_power;
        m_mask = m_size - 1;

        m_entries.resize(m_size);
    }

    table_entry &probe(const uint64_t hash)
    {
        return m_entries[hash & m_mask];
    }

    int16_t occupied() const
    {
        size_t count = 0;
        for (const auto &entry : m_entries)
        {
            if (entry.m_hash > 0)
                count++;
        }

        return static_cast<int16_t>(count * 1000 / m_entries.size());
    }
};
