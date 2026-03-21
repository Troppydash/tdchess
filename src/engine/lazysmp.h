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
    volatile bool is_searching = false;
    volatile bool should_quit = false;

    // search info
    chess::Board s_board{};
    search_param s_param{};
    bool s_verbose{};

    // search result
    search_result s_result{};

    search_thread(int index, table *tt, endgame_table *endgame, nnue2::net *net)
        : index(index), nnue{net->clone()}, end{endgame->clone()}, eng{&end, &nnue, tt}
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
            std::unique_lock<std::mutex> lock{mutex};
            cv.wait(lock, [&] { return is_searching || should_quit; });
            if (should_quit)
                break;

            // do work
            s_param.is_main_thread = is_main_thread();
            s_result = eng.search(s_board, s_param, s_verbose && is_main_thread());
            eng.post_search_smp();

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

    void quit()
    {
        should_quit = true;
        if (is_searching)
        {
            // force stop
            eng.m_timer.stop();
        }

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
    std::vector<std::unique_ptr<search_thread>> search_threads;
    // std::vector<std::thread> threads;

    lazysmp(int num, nnue2::net *net, table *tt, endgame_table *endgame)
        : net(net), tt(tt), endgame(endgame)
    {
        // make threads
        for (int i = 0; i < num; ++i)
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
                    return (void *)0;
                },
                search_threads[i].get());

            pthread_detach(thread);
            pthread_attr_destroy(&attr);
        }
    }

    ~lazysmp()
    {
        int n = search_threads.size();
        for (int i = 0; i < n; ++i)
        {
            search_threads[i]->quit();
            // if (threads[i].joinable())
            //     threads[i].join();
        }
    }

    search_result search(const chess::Board &reference, search_param param, bool verbose = false)
    {
        tt->inc_generation();

        // 0 is main, rest is helper
        int n = search_threads.size();
        for (int i = 0; i < n; ++i)
        {
            search_threads[i]->start_search(reference, param, verbose);
        }

        for (int i = 0; i < n; ++i)
        {
            search_threads[i]->wait_search();
        }

        return search_threads[0]->s_result;
    }
};