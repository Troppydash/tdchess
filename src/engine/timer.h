#pragma once

#include <chrono>

class timer
{
  private:
    std::chrono::milliseconds m_target{};
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

    void start(int64_t ms)
    {
        m_target = now() + std::chrono::milliseconds(ms);
        m_is_stopped = false;
        m_forced_stopped = false;
    }

    void add(int ms)
    {
        m_target = m_target + std::chrono::milliseconds(ms);
        m_is_stopped = false;
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