module;
export module project.timing;

import <chrono>;
import <string>;
import <string_view>;
import std;

import project.log;

export namespace project::timing
{
    using clock = std::chrono::steady_clock;
    using nanosec = std::chrono::nanoseconds;
    using microsec = std::chrono::microseconds;

    class ScopedTimer
    {
    public:
        explicit ScopedTimer(std::string_view name) noexcept
            : m_name(name)
            , m_start(clock::now())
        {
        }

        ~ScopedTimer()
        {
            namespace ch = std::chrono;

            //
            // MSVC INTELLISENSE FIX:
            // ----------------------------------------------------
            // Instead of:
            //
            //     duration_cast<...>(end - m_start)
            //
            // We FIRST capture the raw tick difference as a 64-bit integer,
            // then manually wrap it in a well-known duration type.
            //
            // This eliminates all ratio-based template deduction, which is
            // where IntelliSense explodes.
            //
            const clock::time_point end = clock::now();

            // Convert to raw nanoseconds WITHOUT duration_cast
            const long long raw_ns =
                (end.time_since_epoch().count() - m_start.time_since_epoch().count());

            // Wrap into stable chrono durations
            const nanosec  ns(raw_ns);
            const microsec us = ch::duration_cast<microsec>(ns);

            const long long micros = us.count();

            project::log::info(
                "ScopedTimer '{}' finished in {} us",
                std::string(m_name),
                micros
            );
        }

    private:
        std::string_view  m_name;
        clock::time_point m_start;
    };
}
