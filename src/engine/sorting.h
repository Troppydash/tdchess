#pragma once

#include "../hpplib/chess.h"

// Source - https://stackoverflow.com/a/36205084
// Posted by Vectorized, modified by community. See post 'Timeline' for change history
// Retrieved 2026-02-26, License - CC BY-SA 4.0

/**
 * A Functor class to create a sort for fixed sized arrays/containers with a
 * compile time generated Bose-Nelson sorting network.
 * \tparam NumElements  The number of elements in the array or container to sort.
 * \tparam T            The element type.
 * \tparam Compare      A comparator functor class that returns true if lhs < rhs.
 */
template <unsigned NumElements, class Compare = void> class StaticSort
{
    template <class A, class C> struct Swap
    {
        template <class T> inline void s(T &v0, T &v1)
        {
            T t = Compare()(v0, v1) ? v0 : v1; // Min
            v1 = Compare()(v0, v1) ? v1 : v0;  // Max
            v0 = t;
        }

        inline Swap(A &a, const int &i0, const int &i1)
        {
            s(a[i0], a[i1]);
        }
    };

    template <class A> struct Swap<A, void>
    {
        template <class T> inline void s(T &v0, T &v1)
        {
            // Explicitly code out the Min and Max to nudge the compiler
            // to generate branchless code.
            T t = v0 < v1 ? v0 : v1; // Min
            v1 = v0 < v1 ? v1 : v0;  // Max
            v0 = t;
        }

        inline Swap(A &a, const int &i0, const int &i1)
        {
            s(a[i0], a[i1]);
        }
    };

    template <class A, class C, int I, int J, int X, int Y> struct PB
    {
        inline PB(A &a)
        {
            enum
            {
                L = X >> 1,
                M = (X & 1 ? Y : Y + 1) >> 1,
                IAddL = I + L,
                XSubL = X - L
            };
            PB<A, C, I, J, L, M> p0(a);
            PB<A, C, IAddL, J + M, XSubL, Y - M> p1(a);
            PB<A, C, IAddL, J, XSubL, M> p2(a);
        }
    };

    template <class A, class C, int I, int J> struct PB<A, C, I, J, 1, 1>
    {
        inline PB(A &a)
        {
            Swap<A, C> s(a, I - 1, J - 1);
        }
    };

    template <class A, class C, int I, int J> struct PB<A, C, I, J, 1, 2>
    {
        inline PB(A &a)
        {
            Swap<A, C> s0(a, I - 1, J);
            Swap<A, C> s1(a, I - 1, J - 1);
        }
    };

    template <class A, class C, int I, int J> struct PB<A, C, I, J, 2, 1>
    {
        inline PB(A &a)
        {
            Swap<A, C> s0(a, I - 1, J - 1);
            Swap<A, C> s1(a, I, J - 1);
        }
    };

    template <class A, class C, int I, int M, bool Stop = false> struct PS
    {
        inline PS(A &a)
        {
            enum
            {
                L = M >> 1,
                IAddL = I + L,
                MSubL = M - L
            };
            PS<A, C, I, L, (L <= 1)> ps0(a);
            PS<A, C, IAddL, MSubL, (MSubL <= 1)> ps1(a);
            PB<A, C, I, IAddL, L, MSubL> pb(a);
        }
    };

    template <class A, class C, int I, int M> struct PS<A, C, I, M, true>
    {
        inline PS(A &a)
        {
        }
    };

  public:
    /**
     * Sorts the array/container arr.
     * \param  arr  The array/container to be sorted.
     */
    template <class Container> inline void operator()(Container &arr) const
    {
        PS<Container, Compare, 1, NumElements, (NumElements <= 1)> ps(arr);
    };

    /**
     * Sorts the array arr.
     * \param  arr  The array to be sorted.
     */
    template <class T> inline void operator()(T *arr) const
    {
        PS<T *, Compare, 1, NumElements, (NumElements <= 1)> ps(arr);
    };
};

struct MoveComparator
{
    bool operator()(const chess::Move &a, const chess::Move &b) const
    {
        return a.score() > b.score();
    }
};

template <std::size_t N> void sort_executor(chess::Move *moves)
{
    StaticSort<N, MoveComparator> sort;
    sort(moves);
}

template <std::size_t... Is>
void dispatch_sort(std::size_t length, chess::Move *moves, std::index_sequence<Is...>)
{
    using SortFunc = void (*)(chess::Move *);
    static constexpr std::array<SortFunc, sizeof...(Is)> table = {&sort_executor<Is>...};

    if (length < table.size())
    {
        table[length](moves);
    }
}

template <std::size_t Limit>
inline void static_sort(std::size_t length, chess::Move *moves)
{
    if (length >= Limit)
    {
        std::sort(moves, moves + length,
                  [](const chess::Move &a, const chess::Move &b) { return a.score() > b.score(); });
        return;
    }

    dispatch_sort(length, moves, std::make_index_sequence<Limit>{});
}