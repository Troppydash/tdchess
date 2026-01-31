#pragma once
#include <map>
#include <vector>

#include "agent.h"
#include "pentanomial.h"


class elo
{
private:
    std::vector<agent_settings> m_agents;
    std::map<std::pair<std::string, std::string>, double> m_results;

public:
    // TODO: write elo algorithm
};


class chisq
{
private:
    std::vector<agent_settings> m_agents;
    pentanomial m_results;
};