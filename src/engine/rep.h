#pragma once
#include <algorithm>
#include <cstdint>

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

struct bucket_map2
{
    struct alignas(32) bucket
    {
        uint64_t items[3]{};
        bool reps[3]{};

        void clear()
        {
            std::memset(items, 0, sizeof(items));
            std::memset(reps, 0, sizeof(reps));
        }
    };

    bucket *buckets;
    int size;

    bucket_map2()
    {
        size = CURRENT_BUCKET_SIZE;
        buckets = (bucket *)std::aligned_alloc(1 << 14, size * sizeof(bucket));
    }

    ~bucket_map2()
    {
        std::free(buckets);
    }

    void set(uint64_t key)
    {
        auto &bucket = buckets[key & CURRENT_BUCKET_MASK];
        for (int i = 0; i < 3; ++i)
        {
            if (bucket.items[i] == key)
            {
                bucket.reps[i] = true;
                return;
            }
        }

        for (int i = 0; i < 3; ++i)
        {
            if (bucket.items[i] == 0)
            {
                bucket.items[i] = key;
                bucket.reps[i] = false;
                return;
            }
        }

        assert(false);
    }

    int lookup(uint64_t key) const
    {
        auto &bucket = buckets[key & CURRENT_BUCKET_MASK];
        for (int i = 0; i < 3; ++i)
        {
            if (bucket.items[i] == key && bucket.reps[i])
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
    bucket_map2 history{};
    bucket_map current{};

  public:
    void prefetch(uint64_t key) const
    {
        current.prefetch(key);
        history.prefetch(key);
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
        if (c)
            return true;

        c = history.lookup(board.hash());
        if (c)
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
