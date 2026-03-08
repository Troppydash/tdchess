#pragma once

#include <chrono>

class timer
{
  private:
    std::chrono::milliseconds m_target{};
    std::chrono::milliseconds m_start{};
    std::chrono::milliseconds m_opt_time{};
    bool m_is_stopped = false;
    bool m_forced_stopped = false;

  public:
    void stop()
    {
        m_forced_stopped = true;
    }

    void unstop()
    {
        m_forced_stopped = false;
    }

    bool is_opt_time_stop() const
    {
        return now() >= m_opt_time;
    }

    void start(int64_t ms, int64_t opt_ms)
    {
        m_start = now();
        m_target = m_start + std::chrono::milliseconds(ms);
        m_opt_time = m_start + std::chrono::milliseconds(opt_ms);
        m_is_stopped = false;
        m_forced_stopped = false;
    }

    bool is_delta(int64_t ms) const
    {
        return now() - m_start >= std::chrono::milliseconds(ms);
    }

    void add(int ms)
    {
        m_target = m_target + std::chrono::milliseconds(ms);
        m_is_stopped = false;
    }

    long delta()
    {
        return (now() - m_start).count();
    }

    bool is_force_stopped() const
    {
        return m_forced_stopped;
    }

    bool is_stopped() const
    {
        return m_is_stopped || m_forced_stopped;
    }

    void check()
    {
        if (m_is_stopped || m_forced_stopped)
            return;

        auto current = now();
        if (current >= m_target)
        {
            m_is_stopped = true;
        }
    }

    static std::chrono::milliseconds now()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
    }
};