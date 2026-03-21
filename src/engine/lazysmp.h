#pragma once

#include "engine.h"
#include <thread>

// interface is
// search()
// ponderhit() (ignore for now)

struct search_thread
{
    // underlying
    engine eng;
    nnue2::net nnue;
    endgame_table end;
    int index;

    // multithreading
    std::condition_variable cv{};
    std::mutex mutex{};
    std::atomic<bool> is_searching = false;
    std::atomic<bool> should_quit = false;

    // search info
    chess::Board s_board{};
    search_param s_param{};
    bool s_verbose{};

    // search result
    search_result s_result{};

    search_thread(int index, table *tt, endgame_table *endgame, nnue2::net *net)
        : eng{&end, &nnue, tt}, nnue{net->clone()}, end{endgame->clone()}, index(index)
    {
    }

    bool is_main_thread()
    {
        return index == 0;
    }

    void loop()
    {
        while (true)
        {
            eng.post_search_smp();

            std::unique_lock<std::mutex> lock{mutex};
            cv.wait(lock, [&] { return is_searching || should_quit; });
            if (should_quit)
                break;

            // do work
            s_param.is_main_thread = is_main_thread();
            s_result = eng.search(s_board, s_param, s_verbose && is_main_thread());

            is_searching = false;
            cv.notify_all();
        }
    }

    void start_search(const chess::Board &reference, search_param param, bool verbose = false)
    {
        assert(!is_searching);

        s_board = reference;
        s_param = param;
        s_verbose = verbose;

        is_searching = true;
        cv.notify_all();
    }

    void wait_search()
    {
        std::unique_lock<std::mutex> lock{mutex};
        cv.wait(lock, [&] { return !is_searching; });
    }

    void stop()
    {
        if (is_searching)
        {
            eng.m_timer.stop();
        }
    }

    void quit()
    {
        should_quit = true;
        stop();
        cv.notify_all();
    }

    ~search_thread()
    {
        if (!should_quit)
            quit();
    }
};

struct lazysmp
{
    nnue2::net *net = nullptr;
    table *tt = nullptr;
    endgame_table *endgame = nullptr;

    // thread stuff
    int num_threads = 1;
    std::vector<std::unique_ptr<search_thread>> search_threads;
    std::vector<pthread_t> threads;

    lazysmp(int num, nnue2::net *net, table *tt, endgame_table *endgame)
        : net(net), tt(tt), endgame(endgame), num_threads{num}
    {
        if (num_threads == 0)
            exit(0);

        // make threads
        for (int i = 0; i < num_threads; ++i)
        {
            search_threads.push_back(std::make_unique<search_thread>(i, tt, endgame, net));

            pthread_t thread;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            size_t stack_size = 8 * 1024 * 1024;
            pthread_attr_setstacksize(&attr, stack_size);
            pthread_create(
                &thread, &attr,
                [](void *t) {
                    static_cast<search_thread *>(t)->loop();
                    return static_cast<void *>(nullptr);
                },
                search_threads[i].get());

            pthread_attr_destroy(&attr);
            threads.push_back(thread);
        }
    }

    ~lazysmp()
    {
        quit();

        for (int i = 0; i < num_threads; ++i)
        {
            pthread_join(threads[i], nullptr);
        }
    }

    search_result search(const chess::Board &reference, search_param param, bool verbose = false)
    {
        tt->inc_generation();

        // 0 is main, rest is helper
        if (verbose)
            std::cout << "info lazysmp with " << num_threads << " threads\n";

        for (int i = 0; i < num_threads; ++i)
        {
            search_threads[i]->start_search(reference, param, verbose);
        }

        for (int i = 0; i < num_threads; ++i)
        {
            search_threads[i]->wait_search();
        }

        auto result = search_threads[0]->s_result;
        if (verbose)
        {
            engine_stats stats = get_stats(0);
            for (int i = 1; i < num_threads; ++i)
                stats = stats.append(get_stats(i));

            std::cout << "info lazysmp ";
            stats.display_uci(result);
        }

        return result;
    }

    void quit()
    {
        for (int i = 0; i < num_threads; ++i)
        {
            search_threads[i]->quit();
        }
    }

    void stop()
    {
        for (int i = 0; i < num_threads; ++i)
        {
            search_threads[i]->stop();
        }
    }

    engine_stats get_stats(int index = 0) const
    {
        return search_threads[index]->eng.m_stats;
    }
};