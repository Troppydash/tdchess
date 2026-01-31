#pragma once

#include "../helper.h"
#include "../version.h"
#include <thread>

inline void pin_thread_to_processor(int logical_processor)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(logical_processor, &cpuset);

    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}


class uci_handler
{
  private:
    chess::Board m_position;
    endgame_table *m_endgame_table = nullptr;
    nnue *m_nnue = nullptr;
    int m_tt_size = 128;
    int m_thread_aff = -1;

    engine *m_engine = nullptr;
    std::thread m_engine_thread;

  public:
    explicit uci_handler() = default;

    ~uci_handler()
    {
        delete m_engine;
        delete m_endgame_table;
        delete m_nnue;
    }

    void loop()
    {
        std::string buffer{};
        while (true)
        {
            std::getline(std::cin, buffer);

            auto parts = helper::string_split(buffer);
            if (parts.empty())
            {
                std::cout << "empty input, continuing\n";
                continue;
            }

            std::string &lead = parts[0];

            if (lead == "quit")
            {
                break;
            }
            else if (lead == "uci")
            {
                // get threads
                int total_threads = std::thread::hardware_concurrency();
                std::string version{version_txt, version_txt + version_txt_len};

                std::cout << "id name TDchess " << version << "\n";
                std::cout << "id author troppydash\n";
                std::cout << "option name SyzygyPath type string default <empty>\n";
                std::cout << "option name NNUEPath type string default <empty>\n";
                std::cout << "option name TTSizeMB type spin default 256 min 8 max 4096\n";
                std::cout << "option name CoreAff type spin default -1 min -1 max " << total_threads - 1 << "\n";
                std::cout << "uciok\n";
            }
            else if (lead == "setoption")
            {
                if (parts[2] == "SyzygyPath")
                {
                    delete m_endgame_table;
                    m_endgame_table = new endgame_table{};
                    if (!m_endgame_table->load_file(parts[4]))
                    {
                        delete m_endgame_table;
                    }
                }
                else if (parts[2] == "NNUEPath")
                {
                    delete m_nnue;
                    m_nnue = new nnue{};
                    if (!m_nnue->load_network(parts[4]))
                    {
                        delete m_nnue;
                    }
                }
                else if (parts[2] == "TTSizeMB")
                {
                    m_tt_size = atoi(parts[4].c_str());
                }
                else if (parts[2] == "CoreAff")
                {
                    m_thread_aff = atoi(parts[4].c_str());
                }
                else
                {
                    std::cout << "warning unknown option\n";
                }
            }
            else if (lead == "position")
            {
                size_t moves = 2;
                if (parts[1] == "fen")
                {
                    std::string fen =
                        std::format("{} {} {} {} {} {}", parts[2], parts[3], parts[4], parts[5], parts[6], parts[7]);
                    m_position = chess::Board::fromFen(fen);
                    moves = 8;
                }
                else if (parts[1] == "startpos")
                {
                    m_position = chess::Board{};
                }
                else
                {
                    std::cout << "warning unknown position type\n";
                }

                if (moves < parts.size() && parts[moves] == "moves")
                {
                    for (size_t move = moves + 1; move < parts.size(); ++move)
                    {
                        const auto m = chess::uci::uciToMove(m_position, parts[move]);
                        m_position.makeMove(m);
                    }
                }
            }
            else if (lead == "ucinewgame")
            {
                // ignore
            }
            else if (lead == "isready")
            {
                std::cout << "readyok\n";
            }
            else if (lead == "go")
            {
                search_param param{};

                for (size_t i = 1; i < parts.size(); i++)
                {
                    if (i >= parts.size())
                        break;

                    if (parts[i] == "infinite")
                        continue;
                    else if (parts[i] == "depth")
                    {
                        param.depth = std::atoi(parts[i + 1].c_str());
                        i += 1;
                    }
                    else if (parts[i] == "movetime")
                    {
                        param.movetime = std::atoi(parts[i + 1].c_str());
                        i += 1;
                    }
                    else if (parts[i] == "wtime")
                    {
                        param.wtime = std::atoi(parts[i + 1].c_str());
                        i += 1;
                    }
                    else if (parts[i] == "btime")
                    {
                        param.btime = std::atoi(parts[i + 1].c_str());
                        i += 1;
                    }
                    else if (parts[i] == "winc")
                    {
                        param.winc = std::atoi(parts[i + 1].c_str());
                        i += 1;
                    }
                    else if (parts[i] == "binc")
                    {
                        param.binc = std::atoi(parts[i + 1].c_str());
                        i += 1;
                    }
                }

                start_search(param);
            }
            else if (lead == "stop")
            {
                stop_task();
            }
            else if (lead == "ponderhit")
            {
                // ignore
            }
            else if (lead == "perft")
            {
                // depth x
                int16_t max_depth = 4;

                for (size_t i = 1; i < parts.size(); i++)
                {
                    if (i >= parts.size())
                        break;

                    if (parts[i] == "depth")
                    {
                        max_depth = std::atoi(parts[i + 1].c_str());
                        i += 1;
                    }
                }

                start_perft(max_depth);
            }
            else if (lead == "bench")
            {
                // depth x
                search_param param{};
                for (size_t i = 1; i < parts.size(); i++)
                {
                    if (i >= parts.size())
                        break;

                    if (parts[i] == "depth")
                    {
                        param.depth = std::atoi(parts[i + 1].c_str());
                        i += 1;
                    }
                }

                start_bench(param);
            }
            else
            {
                std::cout << "warning unknown command\n";
            }
        }

        stop_task();
    }

  private:
    void start_task(const std::function<void()> &task)
    {
        stop_task();
        m_engine_thread = std::thread{[=, this]() {
            if (m_thread_aff != -1)
            {
                pin_thread_to_processor(m_thread_aff);
            }

            task();
        }};
    }

    void stop_task()
    {
        // end current thread
        if (m_engine != nullptr)
            m_engine->m_timer.stop();

        if (m_engine_thread.joinable())
            m_engine_thread.join();
    }

    void start_search(search_param param)
    {
        start_task([&]() {
            delete m_engine;
            m_engine = new engine{m_endgame_table, m_nnue, m_tt_size};

            auto result = m_engine->search(m_position, param, true, true);

            // display results
            std::cout << "bestmove " << chess::uci::moveToUci(result.pv_line[0]);
            if (result.pv_line.size() >= 2)
            {
                std::cout << " ponder " << chess::uci::moveToUci(result.pv_line[1]);
            }
            std::cout << std::endl;
        });
    }

    void start_perft(const int16_t depth)
    {
        start_task([&]() {
            delete m_engine;
            m_engine = new engine{m_endgame_table, m_nnue, m_tt_size};
            m_engine->perft(m_position, depth);
        });
    }

    void start_bench(search_param param)
    {
        start_task([&]() {
            delete m_engine;
            m_engine = new engine{m_endgame_table, m_nnue, m_tt_size};
            m_engine->search(m_position, param, true, true);

            std::cout << "info finalnps " << m_engine->m_stats.get_nps() << std::endl;
        });
    }
};
