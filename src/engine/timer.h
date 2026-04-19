#pragma once

#include <atomic>
#include <chrono>

class timer
{
  private:
    // start time
    std::atomic<std::chrono::milliseconds> m_start{};

    // max time
    std::atomic<std::chrono::milliseconds> m_target{};

    // optimal time
    std::atomic<std::chrono::milliseconds> m_opt_time{};
    std::atomic<std::chrono::milliseconds> m_opt_time_delta{};

    std::atomic<bool> m_is_stopped = false;
    std::atomic<bool> m_forced_stopped = false;

  public:
    void stop()
    {
        m_forced_stopped.store(true, std::memory_order_relaxed);
    }

    void unstop()
    {
        m_forced_stopped.store(false, std::memory_order_relaxed);
    }

    bool is_opt_time_stop() const
    {
        return now() >= m_opt_time.load(std::memory_order_relaxed);
    }

    void start(int64_t ms, int64_t opt_ms)
    {
        m_start.store(now(), std::memory_order_relaxed);
        m_target = m_start.load(std::memory_order_relaxed) + std::chrono::milliseconds(ms);

        m_opt_time = m_start.load(std::memory_order_relaxed) + std::chrono::milliseconds(opt_ms);
        m_opt_time_delta = std::chrono::milliseconds(opt_ms);

        m_is_stopped.store(false, std::memory_order_relaxed);
        m_forced_stopped.store(false, std::memory_order_relaxed);
    }

    bool is_force_stopped() const
    {
        return m_forced_stopped.load(std::memory_order_relaxed);
    }

    bool is_stopped() const
    {
        return m_is_stopped.load(std::memory_order_relaxed) ||
               m_forced_stopped.load(std::memory_order_relaxed);
    }

    void check()
    {
        if (is_stopped())
            return;

        auto current = now();
        if (current >= m_target.load(std::memory_order_relaxed))
        {
            m_is_stopped.store(true, std::memory_order_relaxed);
        }
    }

    static std::chrono::milliseconds now()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
    }

    void set_mult_optimal(double mult)
    {
        if (is_stopped())
            return;
        
        auto new_opt_time =
            m_start.load(std::memory_order_relaxed) +
            std::chrono::milliseconds(
                (long long)(mult * m_opt_time_delta.load(std::memory_order_relaxed).count()));
        m_opt_time = new_opt_time;
    }
};
