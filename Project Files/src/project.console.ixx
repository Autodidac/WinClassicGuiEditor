module;
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#endif

export module project.console;

import std;
import project.engine.config;

export namespace project::console
{
#if defined(_WIN32)
    inline void attach_raw()
    {
        if (::GetConsoleWindow())
            return;

        if (!::AllocConsole())
            return;

        FILE* f{};
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        freopen_s(&f, "CONIN$", "r", stdin);

        std::ios::sync_with_stdio(false);
        std::println("[Console] Attached.");
    }

    export inline void detach()
    {
        if (::GetConsoleWindow())
            ::FreeConsole();
    }
#else
    inline void attach_raw() {}
    export inline void detach() {}
#endif

    export inline void attach_if_enabled()
    {
        const auto cfg = project::engine::config::load();
#if defined(_WIN32)
        if (cfg.enable_console)
            attach_raw();
#else
        (void)cfg;
#endif
    }
}
