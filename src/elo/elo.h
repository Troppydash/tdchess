#pragma once
#include <map>
#include <utility>
#include <vector>

#include "agent.h"
#include "pentanomial.h"
#include "stats.h"

#include <fstream>

class elo_results
{
  private:
    std::vector<agent_settings> m_agents;
    std::map<std::pair<std::string, std::string>, double> m_results;

  public:
    // TODO: write elo algorithm
};

class gsprt_results
{
  private:
    std::vector<agent_settings> m_agents;
    pentanomial m_results;

  public:
    explicit gsprt_results(const std::vector<agent_settings> &agents) : m_agents(agents)
    {
    }

    void save(const std::string &file) const
    {
        std::ofstream ofs{file};
        ofs << m_results;
    }

    void load(const std::string &file)
    {
        std::ifstream ifs{file};
        ifs >> m_results;
    }

    void append(const std::string &p0, const std::string &p1, const pentanomial &result)
    {
        m_results = m_results + result;
    }

    void display() const
    {
        std::cout << "[gsprt]" << std::endl;
        std::cout << "baseline " << m_agents[1].m_alias << " against latest " << m_agents[0].m_alias << std::endl;
        std::cout << "pentanomial " << m_results.display() << std::endl;
        std::cout << "iter " << m_results.total() << std::endl;

        sprt s{"logistic", 0.05, 0.05, 0, 20};
        s.set_state(m_results);
        s.analytics(0.05);
    }
};