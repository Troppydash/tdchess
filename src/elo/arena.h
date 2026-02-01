#pragma once

#include <map>
#include <string>

#include "../hpplib/reader.h"
#include "agent.h"
#include "book.h"
#include "elo.h"
#include "pentanomial.h"

struct arena_settings
{
    std::string name;
    int book_depth;

    int basetime;
    int increment;

    bool verbose = false;
};

class arena_clock
{

  private:
    int m_time;
    int m_incr;
    std::chrono::microseconds m_ref;

  public:
    explicit arena_clock(int time, int incr) : m_time(time), m_incr(incr)
    {
    }

    void start()
    {
        m_ref = now();
    }

    bool stop()
    {
        int diff = std::chrono::duration_cast<std::chrono::milliseconds>(now() - m_ref).count();
        m_time -= diff;
        if (m_time < 0)
            return true;

        m_time += m_incr;
        return false;
    }

    int get_time() const
    {
        return m_time;
    }

    int get_incr() const
    {
        return m_incr;
    }

    static std::chrono::microseconds now()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch());
    }
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
    explicit arena(arena_settings settings, openbook &book,
                   const std::vector<agent_settings> &agents, const std::vector<int> &logical)
        : m_settings(std::move(settings)), m_book(book), m_agents(agents), m_logical(logical),
          m_results(agents)
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
    const int AGENT0 = 0;
    const int AGENT1 = 1;
    const int DRAW = 2;

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
              const std::vector<chess::Move> &initial_moves, const chess::Board &initial_position,
              int core)
    {
        std::vector<chess::Move> moves = initial_moves;
        chess::Board position = initial_position;
        chess::Color agent0_side2move = position.sideToMove();

        agent agent0{agent0_settings};
        agent agent1{agent1_settings};

        arena_clock agent0_clock{m_settings.basetime, m_settings.increment};
        arena_clock agent1_clock{m_settings.basetime, m_settings.increment};

        for (int i = 0; i < 300; ++i)
        {
            auto [_, result] = position.isGameOver();
            chess::Color side2move = position.sideToMove();
            if (result != chess::GameResult::NONE)
            {
                if (result == chess::GameResult::DRAW)
                    return DRAW;

                return (side2move == agent0_side2move) ? AGENT1 : AGENT0;
            }

            if (m_settings.verbose)
            {
                std::cout << position << std::endl;
                std::cout << position.getFen() << std::endl;
            }

            search_param param;
            if (agent0_side2move == chess::Color::WHITE)
            {
                param = search_param{agent0_clock.get_time(), agent1_clock.get_time(),
                                     agent0_clock.get_incr(), agent1_clock.get_incr()};
            }
            else
            {
                param = search_param{agent1_clock.get_time(), agent0_clock.get_time(),
                                     agent1_clock.get_incr(), agent0_clock.get_incr()};
            }

            chess::Move move = chess::Move::NO_MOVE;
            if (side2move == agent0_side2move)
            {
                agent0_clock.start();
                move = agent0.search(moves, param, core);
                bool timeout = agent0_clock.stop();
                if (timeout)
                {
                    return AGENT1;
                }
            }
            else
            {
                agent1_clock.start();
                move = agent1.search(moves, param, core);
                bool timeout = agent1_clock.stop();
                if (timeout)
                {
                    return AGENT0;
                }
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
