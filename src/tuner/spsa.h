#pragma once

#include "../elo/arena.h"
#include "../elo/book.h"

#include <algorithm>
#include <string>
#include <vector>

#include "../engine/features.h"

struct spsa
{
    static void display_features(const std::vector<tunable_feature> &features)
    {
        for (auto &f : features)
        {
            // std::cout << f.name << " = " << f.value << " (" << f.get() << ")" << " + " << f.delta
            //           << " [" << f.min << ", " << f.max << "]" << std::endl;
            std::cout << "TUNE(" << f.name << ", " << f.get() << ", " << f.delta << ", " << f.min << ", " << f.max << ");\n";
        }
    }

    static void apply(const std::vector<tunable_feature> &theta_plus)
    {
        for (auto &f : theta_plus)
            f.apply();
    }

    constexpr static int DRAW = 2;
    constexpr static int NEW = 0;
    constexpr static int BASELINE = 1;

    static int matchup(std::vector<chess::Move> moves, chess::Board position,
                       const std::vector<tunable_feature> &new_theta,
                       const std::vector<tunable_feature> &baseline_theta, int incr)
    {
        std::vector<int> scores{};

        std::unique_ptr<table> engine_new_table = std::make_unique<table>(512);
        std::unique_ptr<endgame_table> engine_new_endgame_table = std::make_unique<endgame_table>();
        engine_new_endgame_table->load_file("../syzygy");
        std::unique_ptr<nnue> engine_new_nnue = std::make_unique<nnue>();
        engine_new_nnue->load_network("../nets/2026-02-08-1800-370.bin");
        engine engine_new{engine_new_endgame_table.get(), engine_new_nnue.get(),
                          engine_new_table.get()};

        std::unique_ptr<table> engine_table = std::make_unique<table>(512);
        std::unique_ptr<endgame_table> engine_endgame_table = std::make_unique<endgame_table>();
        engine_endgame_table->load_file("../syzygy");
        std::unique_ptr<nnue> engine_nnue = std::make_unique<nnue>();
        engine_nnue->load_network("../nets/2026-02-08-1800-370.bin");
        engine engine{engine_endgame_table.get(), engine_nnue.get(), engine_table.get()};

        arena_clock engine_new_clock{1000, 100};
        arena_clock engine_clock{1000, 100};

        search_param engine_new_param{};
        search_param engine_param{};

        chess::Color new_side2move = position.sideToMove();

        // new goes first
        while (true)
        {
            auto [_, result] = position.isGameOver();
            chess::Color side2move = position.sideToMove();
            if (result != chess::GameResult::NONE)
            {
                if (result == chess::GameResult::DRAW)
                    return DRAW;

                return (side2move == new_side2move) ? BASELINE : NEW;
            }

            if (new_side2move == chess::Color::WHITE)
            {
                engine_new_param.wtime = engine_new_clock.get_time();
                engine_new_param.winc = engine_new_clock.get_incr();
                engine_new_param.btime = engine_clock.get_time();
                engine_new_param.binc = engine_clock.get_incr();

                engine_param.wtime = engine_new_clock.get_time();
                engine_param.winc = engine_new_clock.get_incr();
                engine_param.btime = engine_clock.get_time();
                engine_param.binc = engine_clock.get_incr();
            }
            else
            {
                engine_param.wtime = engine_clock.get_time();
                engine_param.winc = engine_clock.get_incr();
                engine_param.btime = engine_new_clock.get_time();
                engine_param.binc = engine_new_clock.get_incr();

                engine_new_param.wtime = engine_clock.get_time();
                engine_new_param.winc = engine_clock.get_incr();
                engine_new_param.btime = engine_new_clock.get_time();
                engine_new_param.binc = engine_new_clock.get_incr();
            }

            search_result search;

            if (side2move == new_side2move)
            {
                apply(new_theta);
                engine_new_clock.start();
                search = engine_new.search(position, engine_new_param, false);
                bool timeout = engine_new_clock.stop();
                if (timeout)
                {
                    std::cout << "[warning] timeout\n";
                    return BASELINE;
                }
            }
            else
            {
                apply(baseline_theta);
                engine_clock.start();
                search = engine.search(position, engine_param, false);
                bool timeout = engine_clock.stop();
                if (timeout)
                {
                    std::cout << "[warning] timeout\n";
                    return NEW;
                }
            }

            if (search.pv_line.empty())
            {
                std::cout << "empty pvline\n";
                std::terminate();
            }
            moves.push_back(search.pv_line[0]);
            position.makeMove(search.pv_line[0]);

            // always from white's perspective
            scores.push_back(side2move == chess::Color::WHITE ? search.score : -search.score);

            // check win
            int win_value = 1500;
            int win_number = 3;
            if (scores.size() >= win_number)
            {
                bool sign = std::signbit(scores[scores.size() - 1]);
                bool ok = true;
                for (int i = 0; i < win_number; ++i)
                {
                    int score = scores[scores.size() - 1 - i];
                    if (std::signbit(score) != sign || std::abs(score) < win_value)
                    {
                        ok = false;
                        break;
                    }
                }

                if (ok)
                {
                    // white win
                    if (sign > 0)
                    {
                        return new_side2move == chess::Color::WHITE ? NEW : BASELINE;
                    }

                    return new_side2move == chess::Color::BLACK ? NEW : BASELINE;
                }
            }

            // check draw
            int draw_value = 30;
            int draw_number = 6;
            if (moves.size() > 34)
            {
                bool ok = true;
                for (int i = 0; i < draw_number; ++i)
                {
                    int score = scores[scores.size() - 1 - i];
                    if (std::abs(score) > draw_value)
                    {
                        ok = false;
                        break;
                    }
                }

                if (ok)
                {
                    return DRAW;
                }
            }
        }
    }

    static double match(openbook &book, const std::vector<tunable_feature> &theta_plus,
                        const std::vector<tunable_feature> &theta_minus)
    {
        double score = 0;

        int k = 4;
        for (int j = 0; j < k; ++j)
        {
            std::cout << "game " << j << std::endl;
            auto [moves, position] = book.generate_game(12);

            int result = matchup(moves, position, theta_plus, theta_minus, 1);
            score += (result == DRAW) ? 0 : (result == NEW) ? 1 : -1;

            result = matchup(moves, position, theta_minus, theta_plus, -1);
            score += (result == DRAW) ? 0 : (result == NEW) ? -1 : 1;
        }

        return score / (2 * k);
    }

    static void loop()
    {
        // startup

        auto &all = tunable_features_list();
        std::cout << "[startup] registered " << all.size() << " features\n";

        std::vector<std::string> tuned_features{
            "HISTORY_MULT",
            "HISTORY_BASE",
            "HISTORY_MALUS_MULT",
            "HISTORY_MALUS_BASE",
        };

        std::vector<tunable_feature> features = all;
        // for (auto &name : tuned_features)
        // {
        //     features.push_back(*std::find_if(all.begin(), all.end(),
        //                                      [&](tunable_feature &f) { return f.name == name;
        //                                      }));
        // }

        std::cout << "[startup] tuning " << features.size() << " features\n";
        display_features(features);

        // spsa
        srand(42);
        openbook book{"../book/baron30.bin"};
        double alpha = 0.602;
        double gamma = 0.101;
        int n = 25;
        double A = 0.1 * n;
        double a_init = 0.01;
        double c_init = 0.1;

        std::vector<tunable_feature> theta = features;

        for (int k = 0; k < n; ++k)
        {
            std::cout << "[round " << k << "]\n";

            double scale = a_init * std::pow(1 + A, alpha);
            double ak = scale / std::pow(k + 1 + A, alpha);
            double c_scale = c_init * std::pow(1, gamma);
            double ck = c_scale / std::pow(k + 1, gamma);

            std::vector<double> delta(features.size());
            for (int i = 0; i < delta.size(); ++i)
                delta[i] = (rand() / ((double)RAND_MAX) > 0.5) ? 1 : -1;

            std::vector<tunable_feature> theta_plus(features.size());
            std::vector<tunable_feature> theta_minus(features.size());
            for (int i = 0; i < delta.size(); ++i)
            {
                theta_plus[i] = theta[i].add(ck * delta[i]);
                theta_minus[i] = theta[i].add(-ck * delta[i]);
            }

            std::cout << "theta+\n";
            display_features(theta_plus);
            std::cout << "theta-\n";
            display_features(theta_minus);

            double result = match(book, theta_plus, theta_minus);
            for (int i = 0; i < theta.size(); ++i)
            {
                theta[i] = theta[i].add(ak * result / (ck * delta[i]));
            }


            std::cout << "result " << result << "\n";

            // if (result != 0)
            // {
            std::cout << "current\n";
            display_features(theta);
            // }
        }
    }
};