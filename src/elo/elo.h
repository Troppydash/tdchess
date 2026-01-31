#pragma once
#include <map>
#include <utility>
#include <vector>

#include "agent.h"
#include "pentanomial.h"

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

    void append(const std::string &p0, const std::string &p1, pentanomial result)
    {
        if (p0 != m_agents[0].m_alias || p1 != m_agents[1].m_alias)
        {
            std::cerr << "warning, incorrect chisq format" << std::endl;
            return;
        }

        m_results = m_results + result;
    }

    double gsqrt_test(double elo0, double elo1) const
    {
        double llr = 0;
        // for (size_t i = 0; i)
    }

    void display() const
    {
        std::cout << m_agents[0].m_alias << " vs " << m_agents[1].m_alias << std::endl;
        std::cout << m_results.display() << std::endl;

        // compute pentanomial results
    }
};