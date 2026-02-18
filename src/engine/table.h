#pragma once

#include "../hpplib/chess.h"
#include "param.h"

#include <cmath>
#include <cstring>

struct table_entry_result
{
    bool hit;
    bool can_use;
    int16_t score;
    int16_t depth;
    chess::Move move;
    int16_t static_eval;
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
    return static_cast<uint8_t>(is_pv) << 5;
}

constexpr uint8_t SET_AGE(uint8_t age)
{
    return age & AGE_MASK;
}

constexpr bool MATCHES(uint64_t hash, uint32_t partial)
{
    return (hash >> 32) == partial;
}

class table_entry
{
  public:
    uint32_t m_hash;
    int16_t m_depth;
    int16_t m_score;
    int16_t m_static_eval;
    uint8_t m_mask = 1;
    chess::Move m_best_move;

    [[nodiscard]] table_entry_result get(uint64_t hash, int32_t ply, int32_t depth, int16_t alpha,
                                         int16_t beta, bool bucket_hit) const
    {
        int16_t adj_score = param::VALUE_NONE;
        bool can_use = false;
        bool is_hit = false;
        if (bucket_hit)
        {
            is_hit = true;

            if (m_depth >= depth)
            {
                if (param::IS_VALID(m_score))
                {
                    int16_t score = m_score;

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

    void set(uint64_t hash, uint8_t flag, int16_t score, int32_t ply, int32_t depth,
             const chess::Move &best_move, int16_t static_eval, bool is_pv, uint8_t age)
    {
        if (best_move != chess::Move::NO_MOVE || !MATCHES(hash, m_hash))
            m_best_move = best_move;

        uint8_t age_diff = (age - GET_AGE(m_mask)) & AGE_MASK;
        if (flag == param::EXACT_FLAG || !MATCHES(hash, m_hash) ||
            depth + 4 + 2 * is_pv > m_depth || age_diff >= 1)
        {
            m_hash = hash >> 32;
            m_depth = depth;
            m_static_eval = static_eval;

            // to absolute depth
            if (param::IS_VALID(score))
            {
                if (score > param::CHECKMATE)
                    score += ply;
                if (score < -param::CHECKMATE)
                    score -= ply;
            }

            m_score = score;

            m_mask = SET_AGE(age) | SET_FLAG(flag) | SET_PV(is_pv);
        }

        // m_hash = hash >> 32;
        // m_depth = depth;
        // assert(depth <= 255);
        // m_best_move = best_move;
        // m_static_eval = static_eval;
        //
        // m_mask &= ~FLAG_MASK;
        // m_mask |= SET_FLAG(flag);
        // m_mask &= ~PV_MASK;
        // m_mask |= SET_PV(is_pv);
        //
        // // to absolute depth
        // if (param::IS_VALID(score))
        // {
        //     if (score > param::CHECKMATE)
        //         score += ply;
        //     if (score < -param::CHECKMATE)
        //         score -= ply;
        // }
        //
        // m_score = score;
        //
        // m_mask &= ~AGE_MASK;
        // m_mask |= SET_AGE(age);
    }
};

constexpr int NUM_BUCKETS = 4;

struct alignas(64) bucket
{
    std::array<table_entry, NUM_BUCKETS> m_entries;

    void clear()
    {
        for (size_t i = 0; i < NUM_BUCKETS; ++i)
        {
            m_entries[i].m_hash = 0;
            m_entries[i].m_depth = param::UNINIT_DEPTH;
            m_entries[i].m_static_eval = param::VALUE_NONE;
            m_entries[i].m_score = param::VALUE_NONE;
            m_entries[i].m_best_move = chess::Move::NO_MOVE;

            // zero mask, zero pv, one age, because we increase gen at search start
            m_entries[i].m_mask = 1;
        }
    }

    table_entry &probe(const uint64_t hash, bool &bucket_hit)
    {
        const uint32_t key = hash >> 32;
        for (int i = 0; i < NUM_BUCKETS; ++i)
        {
            if (key == m_entries[i].m_hash)
            {
                bucket_hit = m_entries[i].m_depth > param::UNINIT_DEPTH;
                return m_entries[i];
            }
        }

        // int best_slot = -1;
        // int32_t worst_score = std::numeric_limits<int32_t>::max();
        // uint32_t key = hash >> 32;
        // for (int i = 0; i < NUM_BUCKETS; ++i)
        // {
        //     const auto &entry = m_entries[i];
        //     if (key == entry.m_hash && entry.m_depth > param::UNINIT_DEPTH)
        //     {
        //         best_slot = i;
        //         break;
        //     }
        //
        //     uint8_t entry_age = GET_AGE(entry.m_mask);
        //     uint8_t age_diff = (age - entry_age) & AGE_MASK;
        //     int32_t replacement_score = entry.m_depth - age_diff * 8;
        //
        //     if (replacement_score < worst_score)
        //     {
        //         worst_score = replacement_score;
        //         best_slot = i;
        //     }
        // }


        bucket_hit = false;
        return m_entries[0];
    }

    void store(uint64_t hash, uint8_t flag, int16_t score, int32_t ply, int32_t depth,
               const chess::Move &best_move, int16_t static_eval, bool is_pv, uint8_t age)
    {
        int best_slot = -1;
        int32_t worst_score = std::numeric_limits<int32_t>::max();
        uint32_t key = hash >> 32;
        for (int i = 0; i < NUM_BUCKETS; ++i)
        {
            const auto &entry = m_entries[i];
            if (key == entry.m_hash && entry.m_depth > param::UNINIT_DEPTH)
            {
                best_slot = i;
                break;
            }

            uint8_t entry_age = GET_AGE(entry.m_mask);
            uint8_t age_diff = (age - entry_age) & AGE_MASK;
            int32_t replacement_score = entry.m_depth - age_diff * 8;

            if (replacement_score < worst_score)
            {
                worst_score = replacement_score;
                best_slot = i;
            }
        }

        m_entries[best_slot].set(hash, flag, score, ply, depth, best_move, static_eval, is_pv, age);
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

        m_buckets = static_cast<bucket *>(std::aligned_alloc(64, m_size * sizeof(bucket)));
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
        return m_buckets[hash & m_mask];
    }

    int16_t occupied() const
    {
        size_t count = 0;
        for (int i = 0; i < 1000; ++i)
        {
            for (int j = 0; j < NUM_BUCKETS; ++j)
            {
                // only care about ages in the current gen
                count += (m_buckets[i].m_entries[j].m_depth != param::UNINIT_DEPTH &&
                          GET_AGE(m_buckets[i].m_entries[j].m_mask) == m_generation);
            }
        }

        return count / NUM_BUCKETS;
    }
};
