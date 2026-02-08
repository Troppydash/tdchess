#pragma once

#include "../engine/engine.h"
#include "../hpplib/chess.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

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

inline bool read_line(int fd, std::string &buffer, std::string &line)
{
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
        {
            if (!buffer.empty())
            {
                line = std::move(buffer);
                buffer.clear();
                return true;
            }
            return false;
        }
        if (n < 0)
        {
            if (errno == EINTR)
                continue;

            perror("read_line");
        }

        if (n > 0)
        {
            buffer.append(temp, n);
        }
    }
}

inline bool write_line(int fd, const std::string &line)
{
    const char *data = line.data();
    size_t remaining = line.size();

    while (remaining > 0)
    {
        ssize_t n = write(fd, data, remaining);

        if (n > 0)
        {
            data += n;
            remaining -= n;
            continue;
        }

        if (n == 0)
        {
            perror("write_line_empty");
            return false;
        }

        if (errno == EINTR)
            continue;

        perror("write_line");
        return false;
    }

    return true;
}

inline void set_nonblocking(int fd)
{
    // set nonblocking, ONLY ENABLE IF SPARE CPU SINCE IT REACHED 100% USAGE
    // int flags = fcntl(fd, F_GETFL, 0);
    // fcntl(fd, F_SETFL, flags | O_NONBLOCK);

#ifdef __linux__
    // set pipe buffer size high to prevent excessive blocking
    int buffer_size = 128 * 1024;
    int new_buffer = fcntl(fd, F_SETPIPE_SZ, buffer_size);
    if (new_buffer == -1)
    {
        perror("fcntl F_SETPIPE_SZ");
    }

    if (new_buffer < buffer_size)
    {
        std::cout << "[warning] tried setting " << buffer_size << "b, got only " << new_buffer
                  << "b\n";
    }
#else

#endif
}

inline pid_t spawn_process(const char *path, int stdin_fd, int stdout_fd)
{
    pid_t pid = fork();
    if (pid < 0)
    {
        // Fork failed
        perror("fork");
        return -1;
    }

    if (pid == 0)
    {
        // Child process

        // Redirect stdin
        if (dup2(stdin_fd, STDIN_FILENO) < 0)
        {
            perror("dup2 stdin");
            _exit(1);
        }

        // Redirect stdout
        if (dup2(stdout_fd, STDOUT_FILENO) < 0)
        {
            perror("dup2 stdout");
            _exit(1);
        }

        // Close fds that are no longer needed
        if (stdin_fd != STDIN_FILENO)
            close(stdin_fd);
        if (stdout_fd != STDOUT_FILENO)
            close(stdout_fd);

        // Execute the program
        execlp(path, path, static_cast<char *>(nullptr));

        // If execlp returns, it's an error
        perror("execlp");
        _exit(1);
    }

    return pid;
}

inline int wait_for_child(pid_t child_pid)
{
    int status = 0;
    pid_t ret;

    do
    {
        ret = waitpid(child_pid, &status, 0);
    } while (ret == -1 && errno == EINTR); // Retry if interrupted by signal

    if (ret == -1)
    {
        perror("waitpid");
        return -1;
    }

    if (WIFEXITED(status))
    {
        // Normal exit, return exit code
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status))
    {
        // Killed by signal, return negative signal number
        return -WTERMSIG(status);
    }

    return -1; // Should not normally reach here
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
    int m_out;
    int m_in;
    pid_t m_process;

  public:
    explicit agent(const agent_settings &settings)
        : m_settings(settings), m_verbose(settings.m_verbose)
    {
        int pipe_in[2];
        int pipe_out[2];
        int in_ok = pipe(pipe_in);
        if (in_ok == -1)
            perror("pipe_in failed");
        int out_ok = pipe(pipe_out);
        if (out_ok == -1)
            perror("pipe_out failed");

        m_process = pipe_helpers::spawn_process(settings.m_file.c_str(), pipe_in[0], pipe_out[1]);

        close(pipe_in[0]);  // close child stdin
        close(pipe_out[1]); // close child stdout

        m_in = pipe_in[1];
        m_out = pipe_out[0];

        pipe_helpers::set_nonblocking(m_in);
        pipe_helpers::set_nonblocking(m_out);

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

        close(m_in);
        close(m_out);

        int exit_code = pipe_helpers::wait_for_child(m_process);

        if (m_verbose)
        {
            std::cout << prefix() << "exited with " << exit_code << std::endl;
        }
    }

    [[nodiscard]] std::string get_name() const
    {
        return m_name;
    }

    void new_game(int core = -1) const
    {
        pipe_helpers::write_line(m_in, "ucinewgame\n");
        // set core
        pipe_helpers::write_line(m_in,
                                 "setoption name CoreAff value " + std::to_string(core) + "\n");
    }

    chess::Move search(const std::vector<chess::Move> &moves, const search_param param,
                       const int core = -1)
    {
        // load position
        chess::Board board;
        std::string position_string = "position startpos moves";
        for (const auto &move : moves)
        {
            position_string += " " + chess::uci::moveToUci(move);
            board.makeMove(move);
        }
        pipe_helpers::write_line(m_in, position_string + "\n");

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
