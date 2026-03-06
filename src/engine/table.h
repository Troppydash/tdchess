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

constexpr int MAX_AGE = 1 << 5;
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

using BUCKET_HASH = uint16_t;

constexpr bool MATCHES(uint64_t hash, BUCKET_HASH partial)
{
    return BUCKET_HASH(hash) == partial;
}

class table_entry
{
  public:
    BUCKET_HASH m_hash;
    int16_t m_score;
    int16_t m_static_eval;
    uint16_t m_best_move;
    int8_t m_depth;
    uint8_t m_mask;

    [[nodiscard]] table_entry_result get(uint64_t, int32_t ply, int32_t depth, int16_t alpha,
                                         int16_t beta, bool bucket_hit) const
    {

        if (bucket_hit)
        {
            int16_t adj_score = param::VALUE_NONE;
            bool can_use = false;

            if (param::IS_VALID(m_score))
            {
                adj_score = m_score;

                // normalize for depth
                if (adj_score > param::CHECKMATE)
                    adj_score -= ply;
                if (adj_score < -param::CHECKMATE)
                    adj_score += ply;
            }

            // note that if depth exceeds and hit, def valid
            if ((m_depth + param::DEPTH_OFFSET) >= depth && param::IS_VALID(m_score))
            {
                uint8_t flag = GET_FLAG(m_mask);
                if (flag == param::EXACT_FLAG)
                    can_use = true;
                else if (flag == param::ALPHA_FLAG && adj_score <= alpha)
                    can_use = true;
                else if (flag == param::BETA_FLAG && adj_score >= beta)
                    can_use = true;
            }

            return {.hit = true,
                    .can_use = can_use,
                    .score = adj_score,
                    .depth = int16_t(m_depth + param::DEPTH_OFFSET),
                    .move = m_best_move,
                    .static_eval = m_static_eval,
                    .is_pv = GET_PV(m_mask),
                    .flag = GET_FLAG(m_mask)};
        }

        return {.hit = false,
                .can_use = false,
                .score = param::VALUE_NONE,
                .depth = param::UNINIT_DEPTH,
                .move = chess::Move::NO_MOVE,
                .static_eval = param::VALUE_NONE,
                .is_pv = false,
                .flag = param::NO_FLAG};
    }

    void set(uint64_t hash, uint8_t flag, int16_t score, int32_t ply, int32_t depth,
             const chess::Move &best_move, int16_t static_eval, bool is_pv, uint8_t age)
    {
        if (best_move != chess::Move::NO_MOVE || !MATCHES(hash, m_hash))
        {
            // will fuck up if hash collision
            // hence we need to check best move
            m_best_move = best_move.move();
        }

        int age_diff = (MAX_AGE + age - GET_AGE(m_mask)) & AGE_MASK;
        if (flag == param::EXACT_FLAG || !MATCHES(hash, m_hash) ||
            depth + 4 + 2 * is_pv > (m_depth + param::DEPTH_OFFSET) || age_diff >= 1)
        {
            m_hash = BUCKET_HASH(hash);
            m_depth = int8_t(depth - param::DEPTH_OFFSET);
            m_static_eval = static_eval;

            // to absolute depth
            if (param::IS_VALID(score))
            {
                if (score > param::CHECKMATE)
                    score += ply;
                else if (score < -param::CHECKMATE)
                    score -= ply;
            }

            m_score = score;
            m_mask = SET_AGE(age) | SET_FLAG(flag) | SET_PV(is_pv);
        }
    }
};

constexpr int NUM_BUCKETS = 3;
struct alignas(32) bucket
{
    table_entry m_entries[NUM_BUCKETS];

    void clear()
    {
        for (size_t i = 0; i < NUM_BUCKETS; ++i)
        {
            m_entries[i].m_hash = 0;
            m_entries[i].m_depth = param::UNINIT_DEPTH - param::DEPTH_OFFSET;
            m_entries[i].m_static_eval = param::VALUE_NONE;
            m_entries[i].m_score = param::VALUE_NONE;
            m_entries[i].m_best_move = chess::Move::NO_MOVE;

            // zero mask, zero pv, zero age
            m_entries[i].m_mask = 0;
        }
    }

    table_entry &probe(const uint64_t hash, bool &bucket_hit, uint8_t age)
    {
        const BUCKET_HASH key = hash;
        for (int i = 0; i < NUM_BUCKETS; ++i)
        {
            if (key == m_entries[i].m_hash)
            {
                bucket_hit = !(m_entries[i].m_score == 0 && m_entries[i].m_mask == 0);
                return m_entries[i];
            }
        }

        int best_slot = 0;
        int worst_score = m_entries[0].m_depth + param::DEPTH_OFFSET -
                          ((MAX_AGE + age - GET_AGE(m_entries[0].m_mask)) & AGE_MASK) * 8;

        for (int i = 1; i < NUM_BUCKETS; ++i)
        {
            const auto &entry = m_entries[i];
            int age_diff = (MAX_AGE + age - GET_AGE(entry.m_mask)) & AGE_MASK;
            int replacement_score = (entry.m_depth + param::DEPTH_OFFSET) - age_diff * 8;

            if (replacement_score < worst_score)
            {
                worst_score = replacement_score;
                best_slot = i;
            }
        }

        bucket_hit = false;
        return m_entries[best_slot];
    }

    void store(uint64_t hash, uint8_t flag, int16_t score, int32_t ply, int32_t depth,
               const chess::Move &best_move, int16_t static_eval, bool is_pv, uint8_t age,
               table_entry &entry)
    {
        entry.set(hash, flag, score, ply, depth, best_move, static_eval, is_pv, age);
    }
};

class table
{
  public:
    bucket *m_buckets = nullptr;
    size_t m_size;
    uint8_t m_generation;

    explicit table(size_t size_in_mb)
    {
        size_t bytes = size_in_mb * 1024 * 1024;
        m_size = bytes / sizeof(bucket);

        constexpr size_t alignment = 1 << 14;
        m_buckets = static_cast<bucket *>(std::aligned_alloc(alignment, bytes));
        assert(m_buckets != nullptr);
        clear();
    }

    ~table()
    {
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

    bucket &probe(const uint64_t hash) const
    {
        using uint128 = unsigned __int128;
        uint64_t index = (uint128(hash) * uint128(m_size)) >> 64;
        return m_buckets[index];
    }

    int16_t occupied() const
    {
        size_t count = 0;
        for (int i = 0; i < 1000; ++i)
        {
            for (int j = 0; j < NUM_BUCKETS; ++j)
            {
                // only care about ages in the current gen
                count += ((m_buckets[i].m_entries[j].m_depth + param::DEPTH_OFFSET) !=
                              param::UNINIT_DEPTH &&
                          GET_AGE(m_buckets[i].m_entries[j].m_mask) == m_generation);
            }
        }

        return count / NUM_BUCKETS;
    }

    void prefetch(const uint64_t key) const
    {
        __builtin_prefetch(&probe(key));
    }
};
