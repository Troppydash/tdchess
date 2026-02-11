#pragma once

#include "../helper.h"
#include "../version.h"
#include <thread>

inline void pin_thread_to_processor(int logical_processor)
{
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(logical_processor, &cpuset);

    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#else
#endif
}

inline int32_t parse_i32(std::string_view s)
{
    int32_t out = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    if (ec == std::errc() && ptr == s.data() + s.size())
    {
        return out;
    }

    throw std::runtime_error{"bad range"};
}

inline int64_t parse_i64(std::string_view s)
{
    int64_t out = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    if (ec == std::errc() && ptr == s.data() + s.size())
    {
        return out;
    }

    throw std::runtime_error{"bad range"};
}

class uci_handler
{
  private:
    chess::Board m_position;
    endgame_table *m_endgame_table = nullptr;
    nnue *m_nnue = nullptr;
    int m_thread_aff = -1;
    int64_t m_move_overhead = 75;
    search_param m_param{};

    std::unique_ptr<engine> m_engine;
    table *m_tt;
    std::thread m_engine_thread;

  public:
    explicit uci_handler()
    {
        m_tt = new table{128};
        m_engine = std::make_unique<engine>(m_endgame_table, m_nnue, m_tt);
    };

    ~uci_handler()
    {
        delete m_endgame_table;
        delete m_nnue;
        delete m_tt;
    }

    void loop()
    {
        std::ios::sync_with_stdio(false);

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
                std::cout << "option name TTSizeMB type spin default 128 min 8 max 4096\n";
                std::cout << "option name CoreAff type spin default -1 min -1 max "
                          << total_threads - 1 << "\n";
                std::cout << "option name MoveOverhead type spin default 75 min 0 max 2000\n";
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
                        std::cout << "info cannot load endgame table\n";
                    }
                    else
                    {
                        m_engine->m_endgame = m_endgame_table;
                    }
                }
                else if (parts[2] == "NNUEPath")
                {
                    delete m_nnue;
                    m_nnue = new nnue{};
                    if (!m_nnue->load_network(parts[4]))
                    {
                        delete m_nnue;
                        std::cout << "info cannot load nnue\n";
                    }
                    else
                    {
                        m_engine->m_nnue = m_nnue;
                    }
                }
                else if (parts[2] == "TTSizeMB")
                {
                    size_t tt_size = parse_i32(parts[4]);
                    delete m_tt;
                    m_tt = new table{tt_size};
                    m_engine = std::make_unique<engine>(m_endgame_table, m_nnue, m_tt);
                }
                else if (parts[2] == "CoreAff")
                {
                    m_thread_aff = parse_i32(parts[4]);
                }
                else if (parts[2] == "MoveOverhead")
                {
                    m_move_overhead = parse_i64(parts[4]);
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
                    std::string fen = std::format("{} {} {} {} {} {}", parts[2], parts[3], parts[4],
                                                  parts[5], parts[6], parts[7]);
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
                stop_task();

                // to reset time calculations
                m_param.reset();

                // to reset tt to empty
                m_tt->clear();

                // reset engine
                m_engine = std::make_unique<engine>(m_endgame_table, m_nnue, m_tt);
            }
            else if (lead == "isready")
            {
                std::cout << "readyok\n";
            }
            else if (lead == "go")
            {
                m_param.clear_some();

                for (size_t i = 1; i < parts.size(); i++)
                {
                    if (i >= parts.size())
                        break;

                    if (parts[i] == "infinite")
                        continue;
                    else if (parts[i] == "depth")
                    {
                        m_param.depth = parse_i32(parts[i + 1]);
                        i += 1;
                    }
                    else if (parts[i] == "movetime")
                    {
                        m_param.movetime = parse_i64(parts[i + 1]);
                        i += 1;
                    }
                    else if (parts[i] == "wtime")
                    {
                        m_param.wtime = parse_i64(parts[i + 1]);
                        i += 1;
                    }
                    else if (parts[i] == "btime")
                    {
                        m_param.btime = parse_i64(parts[i + 1]);
                        i += 1;
                    }
                    else if (parts[i] == "winc")
                    {
                        m_param.winc = parse_i64(parts[i + 1]);
                        i += 1;
                    }
                    else if (parts[i] == "binc")
                    {
                        m_param.binc = parse_i64(parts[i + 1]);
                        i += 1;
                    }
                }

                m_param.move_overhead = m_move_overhead;
                start_search();
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
                int32_t max_depth = 4;

                for (size_t i = 1; i < parts.size(); i++)
                {
                    if (i >= parts.size())
                        break;

                    if (parts[i] == "depth")
                    {
                        max_depth = parse_i32(parts[i + 1]);
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
                        param.depth = parse_i32(parts[i + 1]);
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
        m_engine_thread = std::thread{[this, task]() {
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

    void start_search()
    {
        chess::Board position = m_position;
        start_task([&, position]() {
            auto result = m_engine->search(position, m_param, true, true);

            // display results
            std::cout << "bestmove " << chess::uci::moveToUci(result.pv_line[0]);
            if (result.pv_line.size() >= 2)
            {
                std::cout << " ponder " << chess::uci::moveToUci(result.pv_line[1]);
            }
            std::cout << std::endl;
        });
    }

    void start_perft(const int32_t depth)
    {
        chess::Board position = m_position;
        start_task([&, depth, position]() { m_engine->perft(position, depth); });
    }

    void start_bench(search_param param)
    {
        chess::Board position = m_position;
        start_task([&, param, position]() {
            search_param temp_param = param;
            m_engine->search(position, temp_param, true, true);

            std::cout << "info finalnps " << m_engine->m_stats.get_nps() << std::endl;
        });
    }
};
