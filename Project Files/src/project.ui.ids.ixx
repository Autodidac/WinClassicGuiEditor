module;
export module project.ui.ids;

import <cstdint>;
import <atomic>;
import <string_view>;
import <unordered_map>;
import <mutex>;
import std;

namespace project::ui
{
    // Public-facing ID type
    export struct ID
    {
        std::uint32_t value{};

        constexpr operator std::uint32_t() const noexcept { return value; }
        constexpr bool operator==(const ID& other) const noexcept { return value == other.value; }
        constexpr bool operator!=(const ID& other) const noexcept { return value != other.value; }
    };

    // Hash support
    export struct IDHash
    {
        std::size_t operator()(const ID& id) const noexcept
        {
            return static_cast<std::size_t>(id.value);
        }
    };

    // Runtime generator — thread-safe, atomic, monotonic
    inline std::atomic_uint32_t g_next{ 10'000 }; // reserve low range for static IDs

    export inline ID generate()
    {
        return ID{ g_next.fetch_add(1, std::memory_order_relaxed) };
    }

    // Compile-time generator for stable blocks
    export template<std::uint32_t Base, std::uint32_t Offset>
        consteval ID make_id()
    {
        static_assert(Base >= 1000);
        static_assert(Offset < 1000);
        return ID{ Base + Offset };
    }

#if !defined(NDEBUG)
    export inline std::mutex g_reg_mutex;
    export inline std::unordered_map<ID, std::string, IDHash> g_registry;

    export inline void register_name(ID id, std::string_view name)
    {
        std::scoped_lock lock(g_reg_mutex);
        g_registry[id] = std::string(name);
    }

    export inline std::string lookup_name(ID id)
    {
        std::scoped_lock lock(g_reg_mutex);
        if (auto it = g_registry.find(id); it != g_registry.end())
            return it->second;
        return "<unnamed>";
    }
#else
    export inline void register_name(ID, std::string_view) {}
    export inline std::string lookup_name(ID) { return {}; }
#endif

    // -------------------------------------------------------------------------
    // Stable ID blocks for subsystems
    // -------------------------------------------------------------------------
    export namespace launcher
    {
        inline constexpr ID TITLE = make_id<1000, 1>();
        inline constexpr ID SUBTITLE = make_id<1000, 2>();
        inline constexpr ID BTN_START = make_id<1000, 3>();
        inline constexpr ID BTN_EDITOR = make_id<1000, 4>();
        inline constexpr ID BTN_SETTINGS = make_id<1000, 5>();
        inline constexpr ID BTN_EXIT = make_id<1000, 6>();
        inline constexpr ID STATUS = make_id<1000, 7>();

        inline constexpr ID GROUP_BASIC = make_id<1000, 20>();
        inline constexpr ID GROUP_LISTS = make_id<1000, 21>();

        inline constexpr ID CHK_DEMO = make_id<1000, 30>();
        inline constexpr ID RADIO_1 = make_id<1000, 31>();
        inline constexpr ID RADIO_2 = make_id<1000, 32>();
        inline constexpr ID RADIO_3 = make_id<1000, 33>();
        inline constexpr ID EDIT_SINGLE = make_id<1000, 34>();
        inline constexpr ID EDIT_MULTI = make_id<1000, 35>();

        inline constexpr ID LISTBOX = make_id<1000, 40>();
        inline constexpr ID COMBO = make_id<1000, 41>();

        inline constexpr ID PROGRESS = make_id<1000, 50>();
        inline constexpr ID SLIDER = make_id<1000, 51>();
        inline constexpr ID SLIDER_VALUE = make_id<1000, 52>();
    }

    // Reserved blocks for later
    export namespace editor
    {
        inline constexpr ID BTN_NEW = make_id<2000, 1>();
        inline constexpr ID BTN_OPEN = make_id<2000, 2>();
        inline constexpr ID BTN_SAVE = make_id<2000, 3>();
        inline constexpr ID STATUS = make_id<2000, 4>();
    }

    export namespace capture
    {
        inline constexpr ID BTN_CAPTURE = make_id<3000, 1>();
        inline constexpr ID BTN_TRAIN = make_id<3000, 2>();
        inline constexpr ID BTN_LOAD = make_id<3000, 3>();
        inline constexpr ID STATUS = make_id<3000, 4>();
    }
}
