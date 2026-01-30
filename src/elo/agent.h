#pragma once

#include "../engine/engine.h"
#include "../hpplib/chess.h"

#include <boost/process.hpp>
#include <utility>
namespace bp = boost::process;

struct agent_settings
{
    std::string m_file;
    std::string m_nnue_file;
    std::string m_endgame_file;
    int m_tt_mb;
    bool m_verbose;

    explicit agent_settings(std::string m_file, std::string m_nnue_file, std::string m_endgame_file,
                            const int tt_mb, bool verbose = false)
        : m_file(std::move(m_file)),
          m_nnue_file(std::move(m_nnue_file)),
          m_endgame_file(std::move(m_endgame_file)),
          m_tt_mb(tt_mb),
          m_verbose(verbose)
    {
    }
};

/**
 * UCI agent wrapper
 */
class agent
{
private:
    agent_settings m_settings;
    bool m_verbose;

    std::string m_name;

    // process info
    bp::ipstream m_out;
    bp::opstream m_in;
    bp::child m_process;

public:
    explicit agent(const agent_settings &settings)
        : m_settings(settings), m_verbose(settings.m_verbose)
    {
        m_process = bp::child{settings.m_file, bp::std_out > m_out, bp::std_in < m_in};

        // read uci
        m_in << "uci" << std::endl;
        std::string line;
        int index = 0;
        while (std::getline(m_out, line))
        {
            if (index == 0)
                m_name = helper::string_split(line)[2] + " " + helper::string_split(line)[3];

            index++;

            if (m_verbose)
                std::cout << prefix() << line << std::endl;


            if (line == "uciok")
                break;
        }

        // set options
        m_in << "setoption name TTSizeMB value " << settings.m_tt_mb << std::endl;
        m_in << "setoption name NNUEPath value " << settings.m_nnue_file << std::endl;
        m_in << "setoption name SyzygyPath value " << settings.m_endgame_file << std::endl;
    }

    ~agent()
    {
        m_in << "quit" << std::endl;
        m_in.close();
        m_process.wait();

        if (m_verbose)
        {
            int result = m_process.exit_code();
            std::cout << prefix() << "exited with " << result << std::endl;
        }
    }

    [[nodiscard]] std::string get_name() const
    {
        return m_name;
    }

    chess::Move search(
        const std::vector<chess::Move> &moves,
        const int16_t ms,
        const int16_t max_depth,
        const int core = -1
    )
    {
        // set core
        m_in << "setoption name CoreAff value " << core << std::endl;

        // load position
        chess::Board board;
        m_in << "position startpos moves";
        for (const auto &move: moves)
        {
            m_in << " " << chess::uci::moveToUci(move);
            board.makeMove(move);
        }
        m_in << std::endl;


        // search
        m_in << "go depth " << max_depth << " movetime " << ms << std::endl;

        std::string line{};
        while (std::getline(m_out, line))
        {
            if (m_verbose)
                std::cout << prefix() << line << std::endl;

            if (line.starts_with("bestmove"))
            {
                // get move
                return chess::uci::uciToMove(board, helper::string_split(line)[1]);
            }
        }

        return chess::Move::NO_MOVE;
    }

private:
    [[nodiscard]] std::string prefix() const
    {
        return "[" + m_name + "] ";
    }
};
