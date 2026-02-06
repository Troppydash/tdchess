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

constexpr uint8_t AGE_MASK = 0b00011111;
constexpr uint8_t PV_MASK = 0b00100000;
constexpr uint8_t FLAG_MASK = 0b11000000;

constexpr uint8_t GET_FLAG(uint8_t mask)
{
    return mask >> 6;
}

constexpr bool GET_PV(uint8_t mask)
{
    return mask & PV_MASK;
}

constexpr uint8_t GET_AGE(uint8_t mask)
{
    return mask & AGE_MASK;
}

constexpr uint8_t SET_FLAG(uint8_t flag)
{
    return flag << 6;
}

constexpr uint8_t SET_PV(bool is_pv)
{
    return static_cast<uint8_t>(is_pv) << 4;
}

constexpr uint8_t SET_AGE(uint8_t age)
{
    return age & AGE_MASK;
}



class table_entry
{
  public:
    uint64_t m_hash = 0;
    int32_t m_score = param::VALUE_NONE;
    int32_t m_depth = param::UNSEARCHED_DEPTH;
    int32_t m_static_eval = param::VALUE_NONE;
    uint8_t m_mask = 0;
    chess::Move m_best_move = chess::Move::NO_MOVE;

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

                    uint8_t flag = GET_FLAG(m_mask);
                    if (flag == param::EXACT_FLAG)
                    {
                        adj_score = score;
                        can_use = true;
                    }
                    else if (flag == param::ALPHA_FLAG && score <= alpha)
                    {
                        adj_score = score;
                        can_use = true;
                    }
                    else if (flag == param::BETA_FLAG && score >= beta)
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
                .is_pv = GET_PV(m_mask),
                .flag = GET_FLAG(m_mask)};
    }

    void set(uint64_t hash, uint8_t flag, int32_t score, int32_t ply, int32_t depth,
             const chess::Move &best_move, int32_t static_eval, bool is_pv, uint8_t age)
    {
        m_hash = hash;
        m_depth = depth;
        m_best_move = best_move;
        m_static_eval = static_eval;

        m_mask &= ~FLAG_MASK;
        m_mask |= SET_FLAG(flag);
        m_mask &= ~PV_MASK;
        m_mask |= SET_PV(is_pv);

        // to absolute depth
        if (param::IS_VALID(score))
        {
            if (score > param::CHECKMATE)
                score += ply;
            if (score < -param::CHECKMATE)
                score -= ply;
        }

        m_score = score;

        m_mask &= ~AGE_MASK;
        m_mask |= SET_AGE(age);
    }
};

constexpr int NUM_BUCKETS = 4;

struct alignas(128) bucket
{
    std::array<table_entry, NUM_BUCKETS> m_entries;

    void clear()
    {
        for (size_t i = 0; i < NUM_BUCKETS; ++i)
        {
            m_entries[i].m_hash = 0;
            m_entries[i].m_depth = param::UNSEARCHED_DEPTH;
            m_entries[i].m_static_eval = param::VALUE_NONE;
            m_entries[i].m_score = param::VALUE_NONE;
            m_entries[i].m_best_move = chess::Move::NO_MOVE;

            // zero mask, zero pv, one age, because we increase gen at search start
            m_entries[i].m_mask = 1;
        }
    }

    table_entry &probe(const uint64_t hash)
    {
        if (m_entries[0].m_hash == hash)
            return m_entries[0];

        for (int i = 1; i < NUM_BUCKETS; ++i)
        {
            if (m_entries[i].m_hash == hash)
            {
                std::swap(m_entries[i], m_entries[0]);
                break;
            }
        }

        return m_entries[0];
    }

    void store(uint64_t hash, uint8_t flag, int32_t score, int32_t ply, int32_t depth,
               const chess::Move &best_move, int32_t static_eval, bool is_pv, uint8_t age)
    {
        int best_slot = -1;
        int32_t worst_score = param::INF;

        for (int i = 0; i < NUM_BUCKETS; ++i)
        {
            auto &entry = m_entries[i];
            if (entry.m_hash == hash)
            {
                best_slot = i;
                break;
            }

            uint8_t entry_age = GET_AGE(entry.m_mask);
            uint8_t age_diff = (age - entry_age) & AGE_MASK;
            int32_t replacement_score = entry.m_depth - age_diff * 4;

            if (replacement_score < worst_score)
            {
                worst_score = replacement_score;
                best_slot = i;
            }
        }

        m_entries[best_slot].set(hash, flag, score, ply, depth, best_move, static_eval, is_pv, age);
    }

    size_t occ() const
    {
        size_t count = 0;
        for (size_t i = 0; i < NUM_BUCKETS; ++i)
        {
            count += m_entries[i].m_hash != 0;
        }
        return count;
    }
};

class table
{
  public:
    bucket *m_buckets = nullptr;
    size_t m_size;
    int m_power;
    uint64_t m_mask;
    uint8_t m_generation;

    explicit table(size_t size_in_mb)
    {
        const size_t max_size = size_in_mb * 1024 * 1024 / sizeof(bucket);
        m_power = std::floor(std::log2(max_size));
        m_size = 1ull << m_power;
        m_mask = m_size - 1;

        m_buckets = static_cast<bucket *>(std::malloc(m_size * sizeof(bucket)));
        clear();
    }

    ~table()
    {
        if (m_buckets != nullptr)
            std::free(m_buckets);
    }

    void clear()
    {
        m_generation = 0;

        for (size_t i = 0; i < m_size; ++i)
        {
            m_buckets[i].clear();
        }
    }

    void inc_generation()
    {
        m_generation++;
        m_generation &= AGE_MASK;
    }

    bucket &probe(const uint64_t hash)
    {
        __builtin_prefetch(&m_buckets[hash & m_mask]);
        return m_buckets[hash & m_mask];
    }

    int16_t occupied() const
    {
        size_t count = 0;
        for (size_t i = 0; i < m_size; ++i)
        {
            count += m_buckets[i].occ();
        }

        return static_cast<int16_t>(count * 1000 / (m_size * BUCKET_SIZE));
    }
};
