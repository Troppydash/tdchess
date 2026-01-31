#pragma once

#include <condition_variable>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

namespace helper
{
constexpr int16_t clamp(int16_t x, int16_t lower, int16_t upper)
{
    if (x < lower)
        return lower;
    if (x > upper)
        return upper;

    return x;
}

inline std::vector<std::string> string_split(std::string const &input)
{
    std::stringstream ss(input);

    std::vector<std::string> words((std::istream_iterator<std::string>(ss)), std::istream_iterator<std::string>());

    return words;
}

template <typename T> struct thread_safe_queue
{
    std::queue<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_is_done = false;

    void push(T value)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push(value);
        }
        m_cv.notify_one();
    }

    T pop()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [&] { return !m_queue.empty() || m_is_done; });

        if (m_queue.empty())
            throw std::runtime_error{"empty queue"};

        T value = std::move(m_queue.front());
        m_queue.pop();
        return value;
    }

    bool is_empty()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    size_t size()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }
};
} // namespace helper
