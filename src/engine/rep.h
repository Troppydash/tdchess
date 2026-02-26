#pragma once
#include <algorithm>
#include <cstdint>

constexpr uint64_t REP_FILTER_SIZE = 1 << 15;
constexpr uint64_t REP_FILTER_SIZE_M1 = REP_FILTER_SIZE - 1;
inline int h1(uint64_t key)
{
    return key & REP_FILTER_SIZE_M1;
}

inline int h2(uint64_t key)
{
    return (key >> 16) & REP_FILTER_SIZE_M1;
}
class rep_filter
{
  private:
    uint64_t *filter;

  public:
    rep_filter()
    {
        filter = new uint64_t[REP_FILTER_SIZE];
        for (int i = 0; i < REP_FILTER_SIZE; ++i)
            filter[i] = 0;
    }

    ~rep_filter()
    {
        delete[] filter;
    }

  private:
    // returns if exists
    bool set(uint64_t key)
    {
        assert(key != 0);

        int slot = h1(key);
        while (filter[slot] != key)
        {
            std::swap(filter[slot], key);

            if (key == 0)
            {
                return false;
            }

            // Use the other slot
            slot = (slot == h1(key)) ? h2(key) : h1(key);
        }

        return true;
    }

    void unset(uint64_t key)
    {
        if (filter[h1(key)] == key)
            filter[h1(key)] = 0;
        else
        {
            assert(filter[h2(key)] == key);
            filter[h2(key)] = 0;
        }
    }

    bool lookup(uint64_t key, int ply, const chess::Board &board)
    {
        // check for one in search
        bool all_check = filter[h1(key)] == key || filter[h2(key)] == key;
        if (!all_check) [[likely]]
            return false;

        // full check
        auto &states = board.get_prev_state();
        int maxDist = std::min((int)states.size(), (int)board.halfMoveClock());
        bool hit = false;
        for (int i = 4; i <= maxDist; i += 2)
        {
            if (states[states.size() - i].hash == key)
            {
                if (ply >= i)
                    return true;
                if (hit)
                    return true;
                hit = true;
                i += 2;
            }
        }
        return false;
    }

  public:
    void prefetch(uint64_t key) const
    {
        __builtin_prefetch(filter + h1(key));
    }

    void add(const chess::Board &x)
    {
        set(x.hash());
    }

    bool check(const chess::Board &position, int ply)
    {
        return lookup(position.hash(), ply, position);
    }

    void remove(const chess::Board &x)
    {
        unset(x.hash());
    }

    void load(const chess::Board &position)
    {
        // clear at new hfm, so we have more space for the search
        for (int i = 0; i < REP_FILTER_SIZE; ++i)
            filter[i] = 0;

        const int maxDist = position.halfMoveClock();
        const auto &states = position.get_prev_state();
        for (int i = 1; i <= maxDist && i <= states.size(); ++i)
        {
            set(states[states.size() - i].hash);
        }
    }
};
