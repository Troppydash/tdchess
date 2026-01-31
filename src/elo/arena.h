#pragma once

#include <map>
#include <string>

#include "../hpplib/reader.h"
#include "agent.h"
#include "book.h"
#include "pentanomial.h"

struct arena_settings
{
    std::string name;
    int16_t movetime;
    int book_depth;
};

template <typename Result = gsprt_results> class arena
{
  private:
    arena_settings m_settings;
    openbook &m_book;
    std::vector<agent_settings> m_agents;
    std::vector<int> m_logical;
    Result m_results;

    struct match_input
    {
        agent_settings agent0;
        agent_settings agent1;
        int core = -1;
    };

    struct match_output
    {
        match_input input;
        pentanomial result;
    };

  public:
    explicit arena(arena_settings settings, openbook &book, const std::vector<agent_settings> &agents,
                   const std::vector<int> &logical)
        : m_settings(std::move(settings)), m_book(book), m_agents(agents), m_logical(logical), m_results(agents)
    {
    }

    int full_round(int repeats)
    {
        helper::thread_safe_queue<match_input> matches;
        for (size_t i = 0; i < m_agents.size(); ++i)
            for (size_t j = i + 1; j < m_agents.size(); ++j)
                for (int k = 0; k < repeats; ++k)
                    matches.push(match_input{m_agents[i], m_agents[j], -1});

        int jobs = matches.size();

        // allocate matches into threads
        helper::thread_safe_queue<match_output> outputs;
        std::vector<std::thread> threads;
        for (auto logical : m_logical)
        {
            threads.push_back(std::thread{[&, logical] {
                while (!matches.is_empty())
                {
                    match_input input = matches.pop();
                    input.core = logical;
                    match_output output = matchup(input);
                    outputs.push(output);
                }
            }});
        }

        // take results
        for (int i = 0; i < jobs; ++i)
        {
            auto output = outputs.pop();

            auto agent0_alias = output.input.agent0.m_alias;
            auto agent1_alias = output.input.agent1.m_alias;
            m_results.append(agent0_alias, agent1_alias, output.result);

            // use result
            std::cout << "[job " << i << "] " << agent0_alias << " vs " << agent1_alias << ": "
                      << output.result.display() << std::endl;
        }

        m_results.display();
        m_results.save(m_settings.name + "_elo.txt");

        // wait for thread to finish
        for (auto &t : threads)
            t.join();

        return jobs;
    }

    void loop(int repeats, int iterations)
    {
        int total = 0;
        for (size_t i = 0; total < iterations; i++)
        {
            std::cout << "[round " << i << "]" << std::endl;
            total += full_round(repeats);
        }
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
    int match(const agent_settings &agent0_settings, const agent_settings &agent1_settings,
              const std::vector<chess::Move> &initial_moves, const chess::Board &initial_position, int core)
    {
        std::vector<chess::Move> moves = initial_moves;
        chess::Board position = initial_position;
        chess::Color initial_side2move = position.sideToMove();

        agent agent0{agent0_settings};
        agent agent1{agent1_settings};

        int16_t ms = m_settings.movetime;

        for (int i = 0; i < 300; ++i)
        {
            auto [_, result] = position.isGameOver();
            chess::Color side2move = position.sideToMove();
            if (result != chess::GameResult::NONE)
            {
                if (result == chess::GameResult::DRAW)
                    return 2;

                return (side2move == initial_side2move) ? 1 : 0;
            }

            chess::Move move = chess::Move::NO_MOVE;
            if (side2move == initial_side2move)
            {
                move = agent0.search(moves, ms, param::MAX_DEPTH, core);
            }
            else
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
    match_output matchup(const match_input &input)
    {
        auto [moves, position] = m_book.generate_game(m_settings.book_depth);

        std::pair<double, double> scores{0, 0};

        int forward = match(input.agent0, input.agent1, moves, position, input.core);
        if (forward == 0)
            scores.first += 1;
        else if (forward == 1)
            scores.second += 1;
        else
        {
            scores.first += 0.5;
            scores.second += 0.5;
        }

        int backwards = match(input.agent1, input.agent0, moves, position, input.core);
        if (backwards == 0)
            scores.second += 1;
        else if (backwards == 1)
            scores.first += 1;
        else
        {
            scores.first += 0.5;
            scores.second += 0.5;
        }

        return {input, pentanomial::from_scores(scores)};
    }
};
