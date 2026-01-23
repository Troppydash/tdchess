#pragma once
#include <c++/12/thread>


std::vector<std::string> string_split(std::string const &input)
{
    std::stringstream ss(input);

    std::vector<std::string> words(
        (std::istream_iterator<std::string>(ss)),
        std::istream_iterator<std::string>()
    );

    return words;
}

struct uci_handler
{
    chess::Board m_position;

    std::unique_ptr<engine> m_engine;
    std::thread m_engine_thread;

    explicit uci_handler()
        : m_engine(std::make_unique<engine>(engine{}))
    {
    }

    void start_search(uint8_t depth, int ms)
    {
        stop_search();

        m_engine_thread = std::thread{
            [&]()
            {
                m_engine = std::make_unique<engine>(engine{});
                auto result = m_engine->search(m_position, depth, ms, true, true);

                // display results
                std::cout << "bestmove " << chess::uci::moveToUci(result.pv_line[0]);
                if (result.pv_line.size() >= 2)
                {
                    std::cout << " ponder " << chess::uci::moveToUci(result.pv_line[1]);
                }
                std::cout << std::endl;
            }
        };
    }

    void stop_search()
    {
        // end current thread
        m_engine->m_timer.stop();
        if (m_engine_thread.joinable())
            m_engine_thread.join();
    }

    void loop()
    {
        std::string buffer{};
        while (true)
        {
            std::getline(std::cin, buffer);

            auto parts = string_split(buffer);
            if (parts.empty())
            {
                std::cout << "empty input, continuing\n";
                continue;
            }

            std::string &lead = parts[0];

            if (lead == "quit")
            {
                break;
            } else if (lead == "uci")
            {
                std::cout << "id name Tdchess 1.0.1\n";
                std::cout << "id author troppydash\n";
                std::cout << "uciok\n";
            } else if (lead == "position")
            {
                size_t moves = 2;
                if (parts[1] == "fen")
                {
                    std::string fen = parts[2] + parts[3] + parts[4] + parts[5] + parts[6] + parts[7];
                    m_position = chess::Board::fromFen(fen);
                    moves = 8;
                } else if (parts[1] == "startpos")
                {
                    m_position = chess::Board{};
                }

                if (moves < parts.size() && parts[moves] == "moves")
                {
                    for (size_t move = moves + 1; move < parts.size(); ++move)
                    {
                        const auto m = chess::uci::uciToMove(m_position, parts[move]);
                        m_position.makeMove(m);
                    }
                }
            } else if (lead == "ucinewgame")
            {
                // ignore
            } else if (lead == "isready")
            {
                std::cout << "readyok\n";
            } else if (lead == "go")
            {
                uint8_t max_depth = param::MAX_DEPTH;
                int ms = 1000000;

                for (size_t i = 1; i < parts.size(); i++)
                {
                    if (i >= parts.size())
                        break;

                    if (parts[i] == "infinite")
                        continue;
                    else if (parts[i] == "depth")
                    {
                        max_depth = std::atoi(parts[i + 1].c_str());
                        i += 1;
                    } else if (parts[i] == "movetime")
                    {
                        ms = std::atoi(parts[i + 1].c_str());
                        i += 1;
                    } else
                    {
                        // ignore
                    }
                }

                start_search(max_depth, ms);
            } else if (lead == "stop")
            {
                stop_search();
            } else if (lead == "ponderhit")
            {
                // ignore
            }
        }

        stop_search();
    }
};
