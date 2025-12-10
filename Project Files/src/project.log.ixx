module;
export module project.log;

import <mutex>;
import <string>;
import <string_view>;
import <format>;
import <chrono>;
import <cstdio>;
import std;

namespace project::log
{
    // ---------------------------------------------------------
    // Minimal log level enum
    // ---------------------------------------------------------
    enum class Level
    {
        Info,
        Warn,
        Error,
        Fatal
    };

    inline std::mutex g_log_mutex;

    // ---------------------------------------------------------
    // Internal function — NOT exported
    // ---------------------------------------------------------
    template <typename... Args>
    void log_internal(Level lvl, std::string_view fmt, Args&&... args)
    {
        std::scoped_lock lock(g_log_mutex);

        // timestamp
        namespace chr = std::chrono;
        const auto now = chr::duration_cast<chr::milliseconds>(
            chr::system_clock::now().time_since_epoch()).count();

        const char* tag =
            lvl == Level::Info ? "INFO" :
            lvl == Level::Warn ? "WARN" :
            lvl == Level::Error ? "ERROR" : "FATAL";

        // vformat
        std::string msg = std::vformat(fmt, std::make_format_args(args...));

        std::print("[{} ms] [{}] {}\n", now, tag, msg);

        if (lvl == Level::Fatal)
            std::fflush(stdout);
    }

} // namespace project::log


// -------------------------------------------------------------
// EXPORTED API — THIS MUST BE OUTSIDE THE INTERNAL NAMESPACE
// -------------------------------------------------------------
export namespace project::log
{
    template <typename... Args>
    inline void info(std::string_view fmt, Args&&... args)
    {
        log_internal(Level::Info, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void warn(std::string_view fmt, Args&&... args)
    {
        log_internal(Level::Warn, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void error(std::string_view fmt, Args&&... args)
    {
        log_internal(Level::Error, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void fatal(std::string_view fmt, Args&&... args)
    {
        log_internal(Level::Fatal, fmt, std::forward<Args>(args)...);
    }
}
