module;

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#pragma comment(lib, "Comctl32.lib")
#endif

export module project.entry.win;

#if defined(_WIN32)
import <vector>;
import <string>;
import <string_view>;
import <chrono>;
import <thread>;
import <algorithm>;
import std;

import project.entry;
import project.log;
import project.ui.ids;
import project.ui.layout;

using namespace project::ui::launcher;
using project::ui::layout::LauncherLayout;
using project::ui::layout::compute_launcher_layout;
using project::ui::layout::clamp_scale;

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------
namespace
{
    bool  g_launch_engine = false;
    float g_dpi_scale = 1.0f;   // relative to 96 DPI

    float GetWindowDpiScale(HWND hwnd)
    {
        using GetDpiForWindow_t = UINT(WINAPI*)(HWND);

        static GetDpiForWindow_t pGetDpiForWindow =
            reinterpret_cast<GetDpiForWindow_t>(
                ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));

        UINT dpi = 96;
        if (pGetDpiForWindow)
            dpi = pGetDpiForWindow(hwnd);

        return clamp_scale(static_cast<float>(dpi) / 96.0f);
    }

    void CenterWindowOnScreen(HWND hwnd)
    {
        RECT rc{};
        ::GetWindowRect(hwnd, &rc);

        int win_w = rc.right - rc.left;
        int win_h = rc.bottom - rc.top;

        POINT pt{};
        ::GetCursorPos(&pt);

        HMONITOR hmon = ::MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

        MONITORINFO mi{ sizeof(mi) };
        ::GetMonitorInfoW(hmon, &mi);

        int mon_w = mi.rcWork.right - mi.rcWork.left;
        int mon_h = mi.rcWork.bottom - mi.rcWork.top;

        int x = mi.rcWork.left + (mon_w - win_w) / 2;
        int y = mi.rcWork.top + (mon_h - win_h) / 2;

        ::SetWindowPos(
            hwnd,
            nullptr,
            x, y,
            0, 0,
            SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER
        );
    }

    void ApplyLauncherLayout(HWND hwnd)
    {
        RECT rc{};
        if (!::GetClientRect(hwnd, &rc))
            return;

        const int client_w = rc.right - rc.left;
        const int client_h = rc.bottom - rc.top;

        LauncherLayout L = compute_launcher_layout(client_w, client_h, g_dpi_scale);

        auto mv = [hwnd](int id, const project::ui::layout::Rect& r)
            {
                if (HWND h = ::GetDlgItem(hwnd, id))
                {
                    ::MoveWindow(h, r.x, r.y, r.w, r.h, TRUE);
                }
            };

        mv(static_cast<int>(TITLE.value), L.title);
        mv(static_cast<int>(SUBTITLE.value), L.subtitle);

        mv(static_cast<int>(BTN_START.value), L.btn_start);
        mv(static_cast<int>(BTN_EDITOR.value), L.btn_editor);
        mv(static_cast<int>(BTN_SETTINGS.value), L.btn_settings);
        mv(static_cast<int>(BTN_EXIT.value), L.btn_exit);

        mv(static_cast<int>(GROUP_BASIC.value), L.group_basic);
        mv(static_cast<int>(GROUP_LISTS.value), L.group_lists);

        mv(static_cast<int>(CHK_DEMO.value), L.chk_demo);
        mv(static_cast<int>(RADIO_1.value), L.radio1);
        mv(static_cast<int>(RADIO_2.value), L.radio2);
        mv(static_cast<int>(RADIO_3.value), L.radio3);
        mv(static_cast<int>(EDIT_SINGLE.value), L.edit_single);
        mv(static_cast<int>(EDIT_MULTI.value), L.edit_multi);

        mv(static_cast<int>(LISTBOX.value), L.listbox);

        // --- SPECIAL-CASE COMBO: preserve dropdown height ---
        if (HWND hCombo = ::GetDlgItem(hwnd, static_cast<int>(COMBO.value)))
        {
            // Visible (closed) height: let Windows tell us item height
            LRESULT item_h = ::SendMessageW(hCombo, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0);
            if (item_h <= 0)
            {
                // Fallback if style/theme reports weird value
                item_h = static_cast<LRESULT>(std::lround(20.0f * g_dpi_scale));
            }

            const int border_h = ::GetSystemMetrics(SM_CYEDGE) * 2;
            const int visible_h = static_cast<int>(item_h) + border_h;

            ::MoveWindow(
                hCombo,
                L.combo.x,
                L.combo.y,
                L.combo.w,
                visible_h,
                TRUE
            );
        }

        mv(static_cast<int>(PROGRESS.value), L.progress);
        mv(static_cast<int>(SLIDER.value), L.slider);
        mv(static_cast<int>(SLIDER_VALUE.value), L.slider_value);

        mv(static_cast<int>(STATUS.value), L.status);
    }

    void UpdateSliderValue(HWND hwnd)
    {
        if (HWND hSlider = ::GetDlgItem(hwnd, static_cast<int>(SLIDER.value)))
        {
            const LRESULT pos = ::SendMessageW(hSlider, TBM_GETPOS, 0, 0);
            wchar_t buf[64]{};
            std::swprintf(buf, std::size(buf), L"Slider value: %ld",
                static_cast<long>(pos));

            if (HWND hVal = ::GetDlgItem(hwnd, static_cast<int>(SLIDER_VALUE.value)))
                ::SetWindowTextW(hVal, buf);
        }
    }

    void FillListControls(HWND hwnd)
    {
        if (HWND hList = ::GetDlgItem(hwnd, static_cast<int>(LISTBOX.value)))
        {
            ::SendMessageW(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Item One"));
            ::SendMessageW(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Item Two"));
            ::SendMessageW(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Item Three"));
        }

        if (HWND hCombo = ::GetDlgItem(hwnd, static_cast<int>(COMBO.value)))
        {
            ::SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Option A"));
            ::SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Option B"));
            ::SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Option C"));
            ::SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
        }
    }

    void InitSliderAndProgress(HWND hwnd)
    {
        if (HWND hProgress = ::GetDlgItem(hwnd, static_cast<int>(PROGRESS.value)))
        {
            ::SendMessageW(hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            ::SendMessageW(hProgress, PBM_SETPOS, 25, 0);
        }

        if (HWND hSlider = ::GetDlgItem(hwnd, static_cast<int>(SLIDER.value)))
        {
            ::SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            ::SendMessageW(hSlider, TBM_SETPOS, TRUE, 25);
        }

        UpdateSliderValue(hwnd);
    }

    LRESULT CALLBACK AlmondWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_COMMAND:
        {
            const int id = LOWORD(wp);
            switch (id)
            {
            case static_cast<int>(BTN_START.value):
                g_launch_engine = true;
                ::DestroyWindow(hwnd);
                return 0;

            case static_cast<int>(BTN_EDITOR.value):
            {
                if (HWND hStatus = ::GetDlgItem(hwnd, static_cast<int>(STATUS.value)))
                {
                    ::SetWindowTextW(
                        hStatus,
                        L"Editor launch not wired yet (placeholder)."
                    );
                }
                return 0;
            }

            case static_cast<int>(BTN_SETTINGS.value):
            {
                if (HWND hStatus = ::GetDlgItem(hwnd, static_cast<int>(STATUS.value)))
                {
                    ::SetWindowTextW(
                        hStatus,
                        L"Settings panel not implemented yet."
                    );
                }
                return 0;
            }

            case static_cast<int>(BTN_EXIT.value):
                g_launch_engine = false;
                ::DestroyWindow(hwnd);
                return 0;
            }
            break;
        }

        case WM_HSCROLL:
        {
            if ((HWND)lp == ::GetDlgItem(hwnd, static_cast<int>(SLIDER.value)))
            {
                UpdateSliderValue(hwnd);

                if (HWND hProgress = ::GetDlgItem(hwnd, static_cast<int>(PROGRESS.value)))
                {
                    const LRESULT pos = ::SendMessageW((HWND)lp, TBM_GETPOS, 0, 0);
                    ::SendMessageW(hProgress, PBM_SETPOS, pos, 0);
                }
                return 0;
            }
            break;
        }

        case WM_SIZE:
            ApplyLauncherLayout(hwnd);
            return 0;

        case WM_DPICHANGED:
        {
            const UINT new_dpi = HIWORD(wp);
            g_dpi_scale = clamp_scale(static_cast<float>(new_dpi) / 96.0f);

            RECT* suggested = reinterpret_cast<RECT*>(lp);

            ::SetWindowPos(
                hwnd,
                nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);

            ApplyLauncherLayout(hwnd);
            return 0;
        }

        case WM_GETMINMAXINFO:
        {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            const int base_min_w = 480;
            const int base_min_h = 260;

            const int min_w = static_cast<int>(base_min_w * g_dpi_scale);
            const int min_h = static_cast<int>(base_min_h * g_dpi_scale);

            mmi->ptMinTrackSize.x = min_w;
            mmi->ptMinTrackSize.y = min_h;
            return 0;
        }

        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        }

        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }

    void CreateLauncherControls(HWND hwnd, HINSTANCE hInst)
    {
        // Title
        ::CreateWindowExW(
            0, L"STATIC", L"AlmondEngine Demo",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)TITLE.value, hInst, nullptr
        );

        // Subtitle
        ::CreateWindowExW(
            0, L"STATIC", L"Win32 control showcase + engine launcher template",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)SUBTITLE.value, hInst, nullptr
        );

        // Top buttons
        ::CreateWindowExW(
            0, L"BUTTON", L"&Start Engine Demo",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)BTN_START.value, hInst, nullptr
        );

        ::CreateWindowExW(
            0, L"BUTTON", L"Open &Editor (stub)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)BTN_EDITOR.value, hInst, nullptr
        );

        ::CreateWindowExW(
            0, L"BUTTON", L"&Settings (stub)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)BTN_SETTINGS.value, hInst, nullptr
        );

        ::CreateWindowExW(
            0, L"BUTTON", L"E&xit",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)BTN_EXIT.value, hInst, nullptr
        );

        // Group boxes
        ::CreateWindowExW(
            0, L"BUTTON", L"Basic Widgets",
            WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)GROUP_BASIC.value, hInst, nullptr
        );

        ::CreateWindowExW(
            0, L"BUTTON", L"Lists / Combo",
            WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)GROUP_LISTS.value, hInst, nullptr
        );

        // Basic controls
        ::CreateWindowExW(
            0, L"BUTTON", L"&Demo CheckBox",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)CHK_DEMO.value, hInst, nullptr
        );

        ::CreateWindowExW(
            0, L"BUTTON", L"Radio &One",
            WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)RADIO_1.value, hInst, nullptr
        );

        ::CreateWindowExW(
            0, L"BUTTON", L"Radio &Two",
            WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)RADIO_2.value, hInst, nullptr
        );

        ::CreateWindowExW(
            0, L"BUTTON", L"Radio &Three",
            WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)RADIO_3.value, hInst, nullptr
        );

        ::CreateWindowExW(
            0, L"EDIT", L"Single-line edit",
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)EDIT_SINGLE.value, hInst, nullptr
        );

        ::CreateWindowExW(
            0, L"EDIT", L"Multi-line edit\r\n(Win32 demo)",
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOVSCROLL | ES_MULTILINE | ES_WANTRETURN,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)EDIT_MULTI.value, hInst, nullptr
        );

        // Lists
        ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_VISIBLE | WS_CHILD | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)LISTBOX.value, hInst, nullptr
        );

        // Combo – give it a real dropdown height at creation
        constexpr int kComboDropHeight = 200;

        ::CreateWindowExW(
            0, L"COMBOBOX", nullptr,
            WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0,
            160,               // dummy width, overridden by layout
            kComboDropHeight,  // dropdown list height
            hwnd, (HMENU)(UINT_PTR)COMBO.value, hInst, nullptr
        );

        // Progress + slider + label
        ::CreateWindowExW(
            0, PROGRESS_CLASSW, nullptr,
            WS_VISIBLE | WS_CHILD,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)PROGRESS.value, hInst, nullptr
        );

        ::CreateWindowExW(
            0, TRACKBAR_CLASSW, nullptr,
            WS_VISIBLE | WS_CHILD | TBS_AUTOTICKS,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)SLIDER.value, hInst, nullptr
        );

        ::CreateWindowExW(
            0, L"STATIC", L"Slider value: 0",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)SLIDER_VALUE.value, hInst, nullptr
        );

        // Status line
        ::CreateWindowExW(
            0, L"STATIC", L"Ready.",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            0, 0, 0, 0,
            hwnd, (HMENU)(UINT_PTR)STATUS.value, hInst, nullptr
        );

        FillListControls(hwnd);
        InitSliderAndProgress(hwnd);
        ApplyLauncherLayout(hwnd);
    }
} // namespace

// -----------------------------------------------------------------------------
// WinMain – always GUI + optional console via run_engine_demo
// -----------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    using project::log::info;
    using project::log::fatal;
    using project::entry::mark_from_winmain;

    mark_from_winmain();

    info("WinMain entry (AlmondEngine Demo GUI)");

    INITCOMMONCONTROLSEX icc{
        .dwSize = sizeof(INITCOMMONCONTROLSEX),
        .dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES
    };
    ::InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = AlmondWndProc,
        .cbClsExtra = 0,
        .cbWndExtra = 0,
        .hInstance = hInst,
        .hIcon = nullptr,
        .hCursor = ::LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = (HBRUSH)(COLOR_WINDOW + 1),
        .lpszMenuName = nullptr,
        .lpszClassName = L"AlmondEngineDemoWindow",
        .hIconSm = nullptr
    };

    if (!::RegisterClassExW(&wc))
    {
        fatal("RegisterClassExW failed ({})",
            static_cast<unsigned long>(::GetLastError()));
        return -1;
    }

    const int base_w = 800;
    const int base_h = 520;

    HWND hwnd = ::CreateWindowExW(
        0,
        L"AlmondEngineDemoWindow",
        L"AlmondEngine – Win32 Demo & Launcher Template",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        base_w, base_h,
        nullptr,
        nullptr,
        hInst,
        nullptr
    );

    if (!hwnd)
    {
        fatal("CreateWindowExW failed ({})",
            static_cast<unsigned long>(::GetLastError()));
        return -2;
    }

    g_dpi_scale = GetWindowDpiScale(hwnd);
    CenterWindowOnScreen(hwnd);
    CreateLauncherControls(hwnd, hInst);

    ::ShowWindow(hwnd, SW_SHOW);
    ::UpdateWindow(hwnd);

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    info("Demo window closed. g_launch_engine = {}",
        g_launch_engine ? "true" : "false");

    if (!g_launch_engine)
    {
        info("User chose not to start engine demo; exiting from WinMain.");
        return 0;
    }

    // Convert command line args → UTF-8 → run_engine_demo()
    int argc = 0;
    LPWSTR* wargv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);

    std::vector<std::string> utf8;
    utf8.reserve(argc);

    for (int i = 0; i < argc; ++i)
    {
        std::wstring_view w = wargv[i];
        std::string s;
        s.resize(::WideCharToMultiByte(
            CP_UTF8, 0,
            w.data(), static_cast<int>(w.size()),
            nullptr, 0,
            nullptr, nullptr));

        ::WideCharToMultiByte(
            CP_UTF8, 0,
            w.data(), static_cast<int>(w.size()),
            s.data(), static_cast<int>(s.size()),
            nullptr, nullptr);

        utf8.push_back(std::move(s));
    }

    ::LocalFree(wargv);

    std::vector<std::string_view> sv;
    sv.reserve(utf8.size());
    for (auto& s : utf8)
        sv.emplace_back(s);

    info("Calling engine demo from WinMain with {} arg(s)", sv.size());

    int rc = project::entry::run_engine_demo(
        std::span<std::string_view>{ sv.data(), sv.size() });

    info("Engine demo returned code {}", rc);
    return rc;
}
#endif
