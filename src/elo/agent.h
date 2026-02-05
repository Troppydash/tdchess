#pragma once

#include "../engine/engine.h"
#include "../hpplib/chess.h"

#include <boost/process.hpp>
#include <utility>
namespace bp = boost::process;

struct agent_settings
{
    std::string m_alias;
    std::string m_file;
    std::string m_nnue_file;
    std::string m_endgame_file;
    int m_tt_mb;
    bool m_verbose;

    explicit agent_settings(std::string alias, std::string m_file, std::string m_nnue_file,
                            std::string m_endgame_file, const int tt_mb, bool verbose = false)
        : m_alias(std::move(alias)), m_file(std::move(m_file)), m_nnue_file(std::move(m_nnue_file)),
          m_endgame_file(std::move(m_endgame_file)), m_tt_mb(tt_mb), m_verbose(verbose)
    {
    }
};

namespace pipe_helpers
{

inline bool read_line(const bp::pipe &out, std::string &buffer, std::string &line)
{
    int fd = out.native_source();

    char temp[4096];

    while (true)
    {
        auto pos = buffer.find('\n');
        if (pos != std::string::npos)
        {
            line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);

            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            return true;
        }

        ssize_t n = read(fd, temp, sizeof(temp));
        if (n == 0)
            return false;
        if (n < 0)
        {
            if (!(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            {
                perror("read_line");
                return false;
            }
        }

        if (n > 0)
        {
            buffer.append(temp, n);
        }
    }
}

inline bool write_line(const bp::pipe &in, const std::string &line)
{
    int fd = in.native_sink();

    const char *data = line.data();
    size_t remaining = line.size();

    while (remaining > 0)
    {
        ssize_t n = write(fd, data, remaining);
        if (n < 0)
        {
            if (!(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            {
                perror("write_line");
                return false;
            }
        }
        if (n > 0)
        {
            data += n;
            remaining -= n;
        }
    }

    return true;
}

inline void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

} // namespace pipe_helpers

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
    bp::pipe m_out;
    bp::pipe m_in;
    bp::child m_process;

  public:
    explicit agent(const agent_settings &settings)
        : m_settings(settings), m_verbose(settings.m_verbose)
    {
        m_process = bp::child{settings.m_file, bp::std_out > m_out, bp::std_in < m_in};

        pipe_helpers::set_nonblocking(m_in.native_sink());
        pipe_helpers::set_nonblocking(m_out.native_source());

        // read uci
        pipe_helpers::write_line(m_in, "uci\n");

        std::string line{};
        std::string buffer{};
        int index = 0;
        while (true)
        {
            pipe_helpers::read_line(m_out, buffer, line);

            if (index == 0)
                m_name = helper::string_split(line)[2] + " " + helper::string_split(line)[3];

            index++;

            if (m_verbose)
                std::cout << prefix() << line << std::endl;

            if (line == "uciok")
                break;
        }

        // set options
        pipe_helpers::write_line(m_in, "setoption name TTSizeMB value " +
                                           std::to_string(settings.m_tt_mb) + "\n");
        pipe_helpers::write_line(m_in,
                                 "setoption name NNUEPath value " + settings.m_nnue_file + "\n");
        pipe_helpers::write_line(m_in, "setoption name SyzygyPath value " +
                                           settings.m_endgame_file + "\n");
    }

    ~agent()
    {
        pipe_helpers::write_line(m_in, "quit\n");
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

    void new_game()
    {
        pipe_helpers::write_line(m_in, "ucinewgame\n");
    }

    chess::Move search(const std::vector<chess::Move> &moves, const search_param param,
                       const int core = -1)
    {
        // set core
        pipe_helpers::write_line(m_in,
                                 "setoption name CoreAff value " + std::to_string(core) + "\n");

        // load position
        chess::Board board;
        pipe_helpers::write_line(m_in, "position startpos moves");
        for (const auto &move : moves)
        {
            pipe_helpers::write_line(m_in, " " + chess::uci::moveToUci(move));
            board.makeMove(move);
        }
        pipe_helpers::write_line(m_in, "\n");

        // search
        std::string search_string =
            "go depth " + std::to_string(param.depth) + " movetime " +
            std::to_string(param.movetime) + " wtime " + std::to_string(param.wtime) + " btime " +
            std::to_string(param.btime) + " winc " + std::to_string(param.winc) + " binc " +
            std::to_string(param.binc) + "\n";
        pipe_helpers::write_line(m_in, search_string);

        std::string line{};
        std::string buffer{};
        while (true)
        {
            pipe_helpers::read_line(m_out, buffer, line);

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
