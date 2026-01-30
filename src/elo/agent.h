#pragma once

#include "../engine/engine.h"
#include "../hpplib/chess.h"

#include <boost/process.hpp>
#include <utility>
namespace bp = boost::process;

/**
 * UCI agent wrapper
 */
class agent
{
private:
    std::string m_file;
    std::string m_nnue_file;
    std::string m_endgame_file;
    int m_tt_mb;

    std::string m_name;

    // process info
    bp::ipstream m_out;
    bp::opstream m_in;
    bp::child m_process;

public:
    explicit agent(std::string m_file, std::string m_nnue_file, std::string m_endgame_file,
                   const int tt_mb)
        : m_file(std::move(m_file)),
          m_nnue_file(std::move(m_nnue_file)),
          m_endgame_file(std::move(m_endgame_file)),
          m_tt_mb(tt_mb)
    {
    }

    void initialize(bool verbose)
    {
        m_process = bp::child{m_file, bp::std_out > m_out, bp::std_in < m_in};

        // read uci
        m_in << "uci" << std::endl;
        std::string line;
        int index = 0;
        while (std::getline(m_out, line))
        {
            if (index == 0)
                m_name = helper::string_split(line)[2] + " " + helper::string_split(line)[3];

            index++;

            if (verbose)
                std::cout << prefix() << line << std::endl;


            if (line == "uciok")
                break;
        }

        // set options
        m_in << "setoption name TTSizeMB value " << m_tt_mb << std::endl;
        m_in << "setoption name NNUEPath value " << m_nnue_file << std::endl;
        m_in << "setoption name SyzygyPath value " << m_endgame_file << std::endl;
    }

    ~agent()
    {
        m_in << "quit" << std::endl;
        m_in.close();
        m_process.wait();
        int result = m_process.exit_code();
        std::cout << prefix() << "exited with " << result << std::endl;
    }

    chess::Move search(
        const std::vector<chess::Move> &moves,
        const int16_t ms,
        const int16_t max_depth,
        const int core = -1,
        const bool verbose = false
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
            if (verbose)
                std::cout << prefix() << line << std::endl;

            if (line.starts_with("bestmove"))
            {
                // get move
                return chess::uci::uciToMove(board, helper::string_split(line)[1]);
            }
        }
    }

private:
    [[nodiscard]] std::string prefix() const
    {
        return "[" + m_name + "] ";
    }
};
