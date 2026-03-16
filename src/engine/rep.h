#pragma once
#include <algorithm>
#include <cstdint>

constexpr uint64_t REP_FILTER_SIZE = 1 << 15;
constexpr uint64_t REP_FILTER_SIZE_M1 = REP_FILTER_SIZE - 1;
constexpr size_t h1(uint64_t key)
{
    return key & REP_FILTER_SIZE_M1;
}

constexpr size_t h2(uint64_t key)
{
    size_t value = (key >> 32) & REP_FILTER_SIZE_M1;
    if (value == h1(key))
        return (value + 1) & REP_FILTER_SIZE_M1;
    return value;
}

template <int L> struct cuckoo_table
{
    std::pair<uint64_t, uint8_t> *filter;
    cuckoo_table()
    {
        filter = new std::pair<uint64_t, uint8_t>[L];
        for (int i = 0; i < L; ++i)
        {
            filter[i].first = 0;
            filter[i].second = 0;
        }
    }

    ~cuckoo_table()
    {
        delete[] filter;
    }

    // returns if exists
    void set(uint64_t key)
    {
        assert(key != 0);
        assert(h1(key) != h2(key));

        // first check that it is in the table
        if (filter[h1(key)].first == key)
        {
            filter[h1(key)].second++;
            return;
        }

        if (filter[h2(key)].first == key)
        {
            filter[h2(key)].second++;
            return;
        }

        // insert
        std::pair<uint64_t, uint8_t> insert = {key, 1};
        size_t slot = h1(insert.first);
        int kicks = 0;
        while (true)
        {
            // ignore if too many kicks
            // TODO: technically this is not correct since we miss a rep
            if (kicks >= 32)
            {
                break;
            }

            std::swap(filter[slot], insert);

            // inserted
            if (insert.first == 0)
                break;

            // Use the other slot
            slot = (slot == h1(insert.first)) ? h2(insert.first) : h1(insert.first);
            kicks += 1;
        }
    }

    void unset(uint64_t key)
    {
        assert(key != 0);

        auto *ref = &filter[h1(key)];
        if (ref->first != key)
        {
            ref = &filter[h2(key)];
            // skip if not exist
            if (ref->first != key)
                return;
        }

        assert(ref->second > 0);
        if (ref->second == 1)
        {
            ref->first = 0;
            ref->second = 0;
        }
        else
            ref->second -= 1;
    }

    int lookup(uint64_t key) const
    {
        assert(key != 0);
        if (filter[h1(key)].first == key)
            return filter[h1(key)].second;

        if (filter[h2(key)].first == key)
            return filter[h2(key)].second;

        return 0;
    }

    void clear()
    {
        for (size_t i = 0; i < L; ++i)
        {
            filter[i].first = 0;
            filter[i].second = 0;
        }
    }
};

constexpr int CURRENT_BUCKET_SIZE = 1 << 15;
constexpr int CURRENT_BUCKET_MASK = CURRENT_BUCKET_SIZE - 1;

struct bucket_map
{
    struct alignas(32) bucket
    {
        uint64_t items[4]{};

        void clear()
        {
            std::memset(items, 0, sizeof(items));
        }
    };

    bucket *buckets;
    int size;

    bucket_map()
    {
        size = CURRENT_BUCKET_SIZE;
        buckets = (bucket *)std::aligned_alloc(1 << 14, size * sizeof(bucket));
    }

    ~bucket_map()
    {
        std::free(buckets);
    }

    void set(uint64_t key)
    {
        auto &bucket = buckets[key & CURRENT_BUCKET_MASK];
        for (int i = 0; i < 4; ++i)
        {
            if (bucket.items[i] == 0)
            {
                bucket.items[i] = key;
                return;
            }
        }

        assert(false);
    }

    void unset(uint64_t key)
    {
        auto &bucket = buckets[key & CURRENT_BUCKET_MASK];
        for (int i = 0; i < 4; ++i)
        {
            if (bucket.items[i] == key)
            {
                bucket.items[i] = 0;
                return;
            }
        }

        assert(false);
    }

    int lookup(uint64_t key) const
    {
        auto &bucket = buckets[key & CURRENT_BUCKET_MASK];
        for (int i = 0; i < 4; ++i)
        {
            if (bucket.items[i] == key)
                return 1;
        }

        return 0;
    }

    void clear()
    {
        for (size_t i = 0; i < size; ++i)
        {
            buckets[i].clear();
        }
    }

    void prefetch(uint64_t key) const
    {
        return __builtin_prefetch(&buckets[key & CURRENT_BUCKET_MASK]);
    }
};

class rep_filter
{
    cuckoo_table<REP_FILTER_SIZE> history{};
    bucket_map current{};

  public:
    void prefetch(uint64_t key) const
    {
        current.prefetch(key);
        __builtin_prefetch(history.filter + h1(key));
    }

    void add(const chess::Board &x)
    {
        assert(current.lookup(x.hash()) == 0);
        current.set(x.hash());
    }

    void remove(const chess::Board &x)
    {
        current.unset(x.hash());
        assert(current.lookup(x.hash()) == 0);
    }

    bool check(const chess::Board &board, int ply) const
    {
        // backup for misses
        // if (ply >= 40)
        // {
        //     const auto &states = board.get_prev_state();
        //     int maxDist = std::min((int)states.size(), (int)board.halfMoveClock());
        //     bool hit = false;
        //     for (int i = 4; i <= maxDist; i += 2)
        //     {
        //         if (states[states.size() - i].hash == board.hash())
        //         {
        //             if (ply >= i)
        //                 return true;
        //             if (hit)
        //                 return true;
        //             hit = true;
        //             i += 2;
        //         }
        //     }
        //     return false;
        // }

        int c = current.lookup(board.hash());
        if (c >= 1)
            return true;

        c = history.lookup(board.hash());
        if (c >= 2)
            return true;

        return false;
    }

    void load(const chess::Board &position)
    {
        const auto &states = position.get_prev_state();
        const int maxDist = std::min((int)position.halfMoveClock(), (int)states.size());
        for (int i = 1; i <= maxDist; ++i)
        {
            history.set(states[states.size() - i].hash);
        }
    }

    void clear()
    {
        current.clear();
        history.clear();
    }
};
