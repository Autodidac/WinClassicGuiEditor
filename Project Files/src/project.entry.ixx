module;
export module project.entry;

import <vector>;
import <string_view>;
import <span>;
import <thread>;
import <chrono>;
import std;

import project.log;
import project.timing;
import project.console;

export namespace project::entry::detail
{
    inline bool g_from_winmain = false;
}

export namespace project::entry
{
    using clock = std::chrono::steady_clock;

    export void mark_from_winmain() noexcept
    {
        detail::g_from_winmain = true;
    }

    export bool launched_from_winmain() noexcept
    {
        return detail::g_from_winmain;
    }

    // ------------------------------------------------------------------------
    // Engine demo – correct integration with timing module.
    // ------------------------------------------------------------------------
    export int run_engine_demo(std::span<std::string_view> args)
    {
        using namespace std::chrono_literals;
        using project::log::info;
        using project::timing::ScopedTimer;

        project::console::attach_if_enabled();

        ScopedTimer scope{ "run_engine_demo" };

        info("Engine demo starting…");
        info("Args: {}", args.size());
        for (auto a : args)
            info("  arg: {}", a);

        constexpr int frame_count = 5;

        // Stable time anchor
        auto last = clock::now();

        for (int i = 0; i < frame_count; ++i)
        {
            ScopedTimer frameScope{ "demo_frame" };

            auto now = clock::now();
            auto diff = now - last;
            last = now;

            // Use correct float seconds conversion
            const float dt =
                std::chrono::duration<float>(diff).count();

            info("Engine demo frame {} / {} | dt = {:.3f} sec",
                i + 1, frame_count, dt);

            std::this_thread::sleep_for(120ms);
        }

        info("Engine demo complete.");
        return 0;
    }

    // ------------------------------------------------------------------------
    // Platform entry
    // ------------------------------------------------------------------------
#if defined(_WIN32)

    export int main(int, char**)
    {
        return 0; // WinMain is the real entry
    }

#else

    export int main(int argc, char** argv)
    {
        using project::log::info;
        using project::timing::ScopedTimer;

        ScopedTimer scope{ "main" };

        std::vector<std::string_view> args;
        args.reserve(argc);
        for (int i = 0; i < argc; ++i)
            args.emplace_back(argv[i]);

        info("main() invoked directly (non-Windows console mode)");

        return run_engine_demo(
            std::span<std::string_view>{ args.data(), args.size() });
    }
#endif
}
