#pragma once

#include "../helper.h"
#include "../version.h"
#include "chess960.h"
#include "lazysmp.h"
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

    std::cout << "bad range\n";
    exit(0);
}

inline int64_t parse_i64(std::string_view s)
{
    int64_t out = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    if (ec == std::errc() && ptr == s.data() + s.size())
    {
        return out;
    }

    std::cout << "bad range\n";
    exit(0);
}

class uci_handler
{
  private:
    chess::Board m_position;
    endgame_table *m_endgame_table = nullptr;
    nnue2::net *m_nnue = nullptr;
    int m_thread_aff = -1;
    int64_t m_move_overhead = 10;
    search_param m_param{};
    int m_num_threads = 1;

    std::unique_ptr<lazysmp> m_engine;
    table *m_tt;
    std::thread m_engine_thread;

  public:
    explicit uci_handler()
    {
        m_tt = new table{128};
        m_nnue = new nnue2::net{};
        m_nnue->incbin_load();
        reload_engine();
    }

    ~uci_handler()
    {
        delete m_endgame_table;
        delete m_nnue;
        delete m_tt;
    }

    void reload_engine()
    {
        m_engine = std::make_unique<lazysmp>(m_num_threads, m_nnue, m_tt, m_endgame_table);
    }

    void loop(const std::string &variant)
    {
        std::ios::sync_with_stdio(false);
        std::cout << std::unitbuf; // auto-flush after each output

        if (variant == "bench")
        {
            // depth x
            search_param param{};
            param.depth = 24;
            chess::Board position{};
            search_param temp_param = param;
            m_engine->search(position, temp_param, false);

            std::cout << m_engine->get_stats().nodes_searched << " nodes "
                      << m_engine->get_stats().get_nps() << " nps" << std::endl;
            return;
        }

        if (variant == "pgo")
        {
            std::vector<std::string> positions{};

            std::ifstream file{"./pgo/STS1-STS15_LAN_v3.epd"};
            if (!file.is_open())
                exit(0);

            std::string line;
            while (std::getline(file, line))
            {
                int index = line.find("bm");
                positions.push_back(line.substr(0, index - 1));
            }
            std::cout << "loaded " << positions.size() << " positions\n";

            for (size_t i = 0; i < positions.size(); i += 10)
            {
                search_param param{};
                param.movetime = 1000;
                chess::Board position{positions[i]};

                m_engine = std::make_unique<lazysmp>(3, m_nnue, m_tt, m_endgame_table);
                m_engine->search(position, param, true);
                m_tt->clear();
            }

            return;
        }

        std::cout << "TDchess made by Troppydash\n";
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
                std::cout << "option name EVALFILE type string default <empty>\n";
                std::cout << "option name Hash type spin default 128 min 8 max 16384\n";
                std::cout << "option name Threads type spin default 1 min 1 max " << total_threads
                          << "\n";
                std::cout << "option name CoreAff type spin default -1 min -1 max "
                          << total_threads - 1 << "\n";
                std::cout << "option name MoveOverhead type spin default 10 min 0 max 2000\n";
                std::cout << "option name UCI_Chess960 type check default false\n";
                std::cout << "option name DrawContempt type spin default 0 min -100 max 100\n";

#ifdef TDCHESS_TUNE
                auto &features = tunable_features_list();
                for (auto &f : features)
                {
                    if (!f.is_active())
                        continue;

                    std::cout << "option name " << "tune_" + f.name << " type spin default "
                              << f.value << " min " << f.min << " max " << f.max << "\n";
                }

#endif

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
                        reload_engine();
                    }
                }
                else if (parts[2] == "EVALFILE")
                {
                    delete m_nnue;
                    m_nnue = new nnue2::net{};
                    if (!m_nnue->load_network(parts[4]))
                    {
                        delete m_nnue;
                        std::cout << "info cannot load nnue\n";
                    }
                    else
                    {
                        reload_engine();
                    }
                }
                else if (parts[2] == "Hash")
                {
                    size_t tt_size = parse_i32(parts[4]);
                    delete m_tt;
                    m_tt = new table{tt_size};
                    reload_engine();
                }
                else if (parts[2] == "CoreAff")
                {
                    m_thread_aff = parse_i32(parts[4]);
                }
                else if (parts[2] == "MoveOverhead")
                {
                    m_move_overhead = parse_i64(parts[4]);
                }
                else if (parts[2] == "Threads")
                {
                    m_num_threads = parse_i64(parts[4]);
                    reload_engine();
                }
                else if (parts[2] == "UCI_Chess960")
                {
                    global::chess_960 = parts[4] == "true";
                }
                else if (parts[2] == "DrawContempt")
                {
                    global::contempt = parse_i32(parts[4]);
                }
                else
                {

#ifdef TDCHESS_TUNE
                    if (parts[2].starts_with("tune_"))
                    {
                        auto &features = tunable_features_list();
                        // set value
                        for (auto &f : features)
                        {
                            if (f.name == parts[2])
                            {
                                f.value = parse_i64(parts[4]);
                                f.apply();
                                break;
                            }
                        }
                        goto end;
                    }
#endif
                    std::cout << "warning unknown option\n";

#ifdef TDCHESS_TUNE
                end:
#endif
                }
            }
            else if (lead == "position")
            {
                stop_task();

                size_t moves = 2;
                if (parts[1] == "fen")
                {
                    std::string fen = parts[2] + " " + parts[3] + " " + parts[4] + " " + parts[5] +
                                      " " + parts[6] + " " + parts[7];
                    // std::string fen = std::format("{} {} {} {} {} {}", parts[2], parts[3],
                    // parts[4], parts[5], parts[6], parts[7]);
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

                m_position.set960(global::chess_960);
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
                
                // also reset nnue
                m_nnue->clear();

                // reload lazysmp just in case
                reload_engine();
            }
            else if (lead == "isready")
            {
                std::cout << "readyok\n";
                std::cout << std::flush;
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
                    else if (parts[i] == "ponder")
                    {
                        m_param.ponder = true;
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
                m_param.ponder = false;
                m_engine->ponderhit(m_position, m_param, true);
            }
            else if (lead == "perft")
            {
                exit(0);

                // depth x
                // int32_t max_depth = 4;
                //
                // for (size_t i = 1; i < parts.size(); i++)
                // {
                //     if (i >= parts.size())
                //         break;
                //
                //     if (parts[i] == "depth")
                //     {
                //         max_depth = parse_i32(parts[i + 1]);
                //         i += 1;
                //     }
                // }
                //
                // start_perft(max_depth);
            }
            else if (lead == "bench")
            {
                exit(0);

                // depth x
                // search_param param{};
                // for (size_t i = 1; i < parts.size(); i++)
                // {
                //     if (i >= parts.size())
                //         break;
                //
                //     if (parts[i] == "depth")
                //     {
                //         param.depth = parse_i32(parts[i + 1]);
                //         i += 1;
                //     }
                // }
                //
                // start_bench(param);
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
        m_engine->stop();

        if (m_engine_thread.joinable())
            m_engine_thread.join();
    }

    void start_search()
    {
        chess::Board position = m_position;
        start_task([&, position]() {
            auto result = m_engine->search(position, m_param, true);

            if (result.pv_line.empty())
            {
                std::cout << "empty pv line, crashing\n";
                std::cout << result.score << std::endl;
                std::cout << result.depth << std::endl;
                std::cout << std::flush;
                exit(0);
            }

            // display results
            std::cout << "bestmove " << chess::uci::moveToUci(result.pv_line[0], global::chess_960);
            if (result.pv_line.size() >= 2)
            {
                std::cout << " ponder "
                          << chess::uci::moveToUci(result.pv_line[1], global::chess_960);
            }
            std::cout << std::endl;
            std::cout << std::flush;
        });
    }

    // void start_perft(const int32_t depth)
    // {
    //     chess::Board position = m_position;
    //     start_task([&, depth, position]() { m_engine->perft(position, depth); });
    // }
    //
    // void start_bench(search_param param)
    // {
    //     chess::Board position = m_position;
    //     start_task([&, param, position]() {
    //         search_param temp_param = param;
    //         m_engine->search(position, temp_param, true);
    //
    //         std::cout << m_engine->m_stats.nodes_searched << " nodes "
    //                   << m_engine->m_stats.get_nps() << " nps" << std::endl;
    //
    //         m_engine->post_search();
    //     });
    // }
};
