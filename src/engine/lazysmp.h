#pragma once

#include "engine.h"
#include <map>
#include <thread>

struct lazysmp
{
    struct search_thread
    {
        // underlying
        engine *eng;
        nnue2::net *nnue;
        endgame_table *end;
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

        lazysmp *parent = nullptr;

        search_thread(int index, lazysmp *parent, table *tt, endgame_table *endgame,
                      nnue2::net *net)
            : nnue{new nnue2::net{net->clone()}},
              end{endgame != nullptr ? new endgame_table{endgame->clone()} : nullptr}, index(index),
              parent{parent}
        {
            eng = new engine{end, nnue, tt};
        }

        bool is_main_thread() const
        {
            return index == parent->main_thread_index;
        }

        void loop()
        {
            while (true)
            {
                eng->post_search_smp();

                std::unique_lock<std::mutex> lock{mutex};
                cv.wait(lock, [&] { return is_searching || should_quit; });
                if (should_quit)
                    break;

                // do work
                s_param.is_main_thread = is_main_thread();
                s_param.thread_index = index;
                s_param.main_thread_index = parent->main_thread_index;
                s_result = eng->search(s_board, s_param, s_verbose);

                // stop other threads if main thread exits
                if (is_main_thread())
                {
                    parent->stop();
                }

                is_searching = false;
                cv.notify_all();
            }
        }

        void start_search(const chess::Board &reference, search_param param, bool verbose = false)
        {
            assert(!is_searching);

            mutex.lock();
            s_board = reference;
            s_param = param;
            s_verbose = verbose;

            is_searching = true;
            mutex.unlock();
            cv.notify_all();
        }

        void ponderhit(const chess::Board &reference, search_param param, bool verbose = false)
        {
            // ignore setting s_* since we never exit search
            param.is_main_thread = is_main_thread();
            eng->ponderhit(reference, param, verbose && is_main_thread());
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
                eng->m_timer.stop();
            }
        }

        void quit()
        {
            mutex.lock();
            should_quit = true;
            stop();
            mutex.unlock();
            cv.notify_all();
        }

        ~search_thread()
        {
            if (!should_quit)
                quit();

            delete eng;
            delete end;
            delete nnue;
        }
    };

    nnue2::net *net = nullptr;
    table *tt = nullptr;
    endgame_table *endgame = nullptr;

    // thread stuff
    int num_threads = 1;
    std::vector<std::unique_ptr<search_thread>> search_threads;
    std::vector<pthread_t> threads;
    int main_thread_index = 0;

    lazysmp(int num, nnue2::net *net, table *tt, endgame_table *endgame)
        : net(net), tt(tt), endgame(endgame), num_threads{num}
    {
        if (num_threads == 0)
            exit(0);

        // make threads
        for (int i = 0; i < num_threads; ++i)
        {
            search_threads.push_back(std::make_unique<search_thread>(i, this, tt, endgame, net));

            pthread_t thread;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            size_t stack_size = 4 * 1024 * 1024;
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
        if (verbose && num_threads > 1)
            std::cout << "info lazysmp with " << num_threads << " threads\n";

        for (int i = 0; i < num_threads; ++i)
        {
            search_threads[i]->start_search(reference, param, verbose);
        }

        for (int i = 0; i < num_threads; ++i)
        {
            search_threads[i]->wait_search();
        }

        // thread voting
        // note threads with zero depth are always ignored since no pv
        int best_thread = main_thread_index;
        if (num_threads > 1)
        {
            std::map<uint16_t, long long> votes{};

            int min_score = param::INF;
            for (int i = 0; i < num_threads; ++i)
            {
                if (search_threads[i]->s_result.depth > 0)
                    min_score = std::min(min_score, (int)search_threads[i]->s_result.score);
            }

            const int MULT = 50;
            for (int i = 0; i < num_threads; ++i)
            {
                const auto &result = search_threads[i]->s_result;
                if (result.depth > 0)
                {
                    assert(!result.pv_line.empty());
                    votes[result.pv_line[0].move()] +=
                        (result.score - min_score + MULT) * (result.depth);
                }
            }

            for (int i = 0; i < num_threads; ++i)
            {
                if (best_thread == i)
                    continue;

                const auto &result = search_threads[i]->s_result;
                if (result.depth > 0)
                {
                    assert(!result.pv_line.empty());

                    int current_score = result.score;
                    int best_score = search_threads[best_thread]->s_result.score;

                    long long current_vote = votes[result.pv_line[0].move()];
                    long long best_vote =
                        votes[search_threads[best_thread]->s_result.pv_line[0].move()];

                    long long current_mult = result.depth * (MULT + result.score - min_score);
                    long long best_mult =
                        search_threads[best_thread]->s_result.depth *
                        (MULT + search_threads[best_thread]->s_result.score - min_score);

                    if (std::abs(best_score) >= param::CHECKMATE)
                    {
                        if (current_score > best_score)
                            best_thread = i;
                    }
                    else if (current_score >= param::CHECKMATE)
                    {
                        best_thread = i;
                    }
                    else if (current_score > -param::CHECKMATE &&
                             (current_vote > best_vote ||
                              (current_vote == best_vote && current_score > best_score)))
                    {
                        best_thread = i;
                    }
                    // else will be [best_score] not decisive, but current score is neg checkmate
                    // idk why we also don't update but whatever
                }
            }
        }
        main_thread_index = best_thread;

        auto result = search_threads[main_thread_index]->s_result;
        if (verbose && num_threads > 1)
        {
            engine_stats stats = get_stats(0);
            for (int i = 1; i < num_threads; ++i)
                stats = stats.append(get_stats(i));

            std::cout << "info lazysmp " << main_thread_index << " ";
            stats.display_uci(result);
        }

        return result;
    }

    void ponderhit(const chess::Board &reference, search_param param, bool verbose = false)
    {
        for (int i = 0; i < num_threads; ++i)
        {
            search_threads[i]->ponderhit(reference, param, verbose);
        }
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
        return search_threads[index]->eng->m_stats;
    }
};
