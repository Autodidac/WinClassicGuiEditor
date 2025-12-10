module;
export module project.engine.config;

export namespace project::engine::config
{
    struct EngineConfig
    {
        bool enable_console{};
        // Extend later: vsync, fullscreen, renderer, etc.
    };

    export inline EngineConfig load()
    {
        EngineConfig cfg{};

#if defined(ALMOND_ENABLE_CONSOLE)
        cfg.enable_console = true;
#else
        cfg.enable_console = false;
#endif

        return cfg;
    }
}
