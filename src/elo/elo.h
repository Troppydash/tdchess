#pragma once

#include <map>
#include <queue>
#include <string>

#include "agent.h"
#include "book.h"
#include "../hpplib/reader.h"

template<typename T>
struct thread_safe_queue
{
    std::queue<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_is_done = false;

    void push(T value)
    { {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push(value);
        }
        m_cv.notify_one();
    }

    T pop()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [&] { return !m_queue.empty() || m_is_done; });

        if (m_queue.empty())
            throw std::runtime_error{"empty queue"};

        T value = std::move(m_queue.front());
        m_queue.pop();
        return value;
    }

    bool is_empty()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    size_t size()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }
};

struct pentanomial
{
    std::array<int, 5> result;

    static pentanomial from_scores(std::pair<double, double> scores)
    {
        int index = 0;
        if (scores == std::make_pair(2.0, 0.0))
            index = 0;
        else if (scores == std::make_pair(1.5, 0.5))
            index = 1;
        else if (scores == std::make_pair(1.0, 1.0))
            index = 2;
        else if (scores == std::make_pair(0.5, 1.5))
            index = 3;
        else if (scores == std::make_pair(0.0, 2.0))
            index = 4;
        else
            throw std::runtime_error{"impossible scores"};

        std::array<int, 5> result{0, 0, 0, 0, 0};
        result[index] += 1;
        return {result};
    }

    [[nodiscard]] std::string display() const
    {
        return std::format("[{}, {}, {}, {}, {}]", result[0], result[1], result[2], result[3], result[4]);
    }
};

class arena
{
private:
    std::string m_name;
    openbook &m_book;
    std::vector<agent_settings> m_agents;
    std::vector<int> m_logical;

    // std::map<int, double> m_wins;
    std::map<std::pair<std::string, std::string>, double> m_results;

    struct match_input
    {
        int agent0_index;
        int agent1_index;
        int core;
    };

    struct match_output
    {
        match_input input;
        pentanomial result;
    };

public:
    explicit arena(
        std::string name,
        openbook &book,
        std::vector<agent_settings> agents,
        const std::vector<int> &logical
    )
        : m_name(std::move(name)), m_book(book), m_agents(std::move(agents)), m_logical(logical)
    {
    }


    void full_round()
    {
        thread_safe_queue<match_input> matches;
        for (size_t i = 0; i < m_agents.size(); ++i)
            for (size_t j = i + 1; j < m_agents.size(); ++j)
                matches.push(match_input{static_cast<int>(i), static_cast<int>(j), -1});

        int jobs = matches.size();

        // allocate matches into threads
        thread_safe_queue<match_output> outputs;
        std::vector<std::thread> threads;
        for (auto logical: m_logical)
        {
            threads.push_back(std::thread{
                [&, logical]
                {
                    while (!matches.is_empty())
                    {
                        match_input input = matches.pop();
                        input.core = logical;
                        match_output output = matchup(input);
                        outputs.push(output);
                    }
                }
            });
        }

        // take results
        for (int i = 0; i < jobs; ++i)
        {
            auto output = outputs.pop();
            // use result
            std::cout << output.result.display() << std::endl;
        }

        // wait for thread to finish
        for (auto &t: threads)
            t.join();
    }

private:
    /**
     * Play a single match between two agents, return the result
     * @param agent0_settings
     * @param agent1_settings
     * @param initial_moves
     * @param initial_position
     * @param core
     * @return
     */
    int match(
        const agent_settings &agent0_settings,
        const agent_settings &agent1_settings,
        const std::vector<chess::Move> &initial_moves,
        const chess::Board &initial_position,
        int core
    )
    {
        std::vector<chess::Move> moves = initial_moves;
        chess::Board position = initial_position;
        chess::Color initial_side2move = position.sideToMove();

        agent agent0{agent0_settings};
        agent agent1{agent1_settings};

        int16_t ms = 1000;

        for (int i = 0; i < 200; ++i)
        {
            auto [_, result] = position.isGameOver();
            chess::Color side2move = position.sideToMove();
            if (result != chess::GameResult::NONE)
            {
                if (result == chess::GameResult::DRAW)
                    return 2;

                return (side2move == initial_side2move) ? 1 : 0;
            }


            std::cout << position << std::endl;

            chess::Move move;
            if (side2move == initial_side2move)
            {
                move = agent0.search(moves, ms, param::MAX_DEPTH, core);
            } else
            {
                move = agent1.search(moves, ms, param::MAX_DEPTH, core);
            }

            moves.push_back(move);
            position.makeMove(move);
        }

        return 2;
    }

    /**
     * Play a symmetrical matchup between two agents, returns the pent result
     * @param input
     * @return
     */
    match_output matchup(
        const match_input &input
    )
    {
        auto [moves, position] = m_book.generate_game(13);

        std::pair<double, double> scores{0, 0};

        int forward = match(m_agents[input.agent0_index], m_agents[input.agent1_index], moves, position, input.core);
        if (forward == 0)
            scores.first += 1;
        else if (forward == 1)
            scores.second += 1;
        else
        {
            scores.first += 0.5;
            scores.second += 0.5;
        }

        int backwards = match(m_agents[input.agent1_index], m_agents[input.agent0_index], moves, position, input.core);
        if (backwards == 0)
            scores.second += 1;
        else if (backwards == 1)
            scores.first += 1;
        else
        {
            scores.first += 0.5;
            scores.second += 0.5;
        }

        return {
            input,
            pentanomial::from_scores(scores)
        };
    }
};
