module;

#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#define _WIN32_IE    0x0A00

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#pragma comment(lib, "Comctl32.lib")

export module almond.security.ui;

import std;
import almond.security.memory;

namespace almond::security::ui
{
    using almond::security::memory::SectionInfo;
    using almond::security::memory::CodeCave;
    using almond::security::memory::RwxRegion;
    using almond::security::memory::ThreadInfo;
    using almond::security::memory::ImportSnapshot;
    using almond::security::memory::IatEntry;
    using almond::security::memory::TextIntegritySnapshot;

    namespace detail
    {
        constexpr wchar_t kMainClass[] = L"AlmondSecurityInspector";
        constexpr wchar_t kHexClass[] = L"AlmondHexView";

        constexpr int   kTabSections = 0;
        constexpr int   kTabCaves = 1;
        constexpr int   kTabRwx = 2;
        constexpr int   kTabThreads = 3;
        constexpr int   kTabImports = 4;
        constexpr int   kTabCount = 5;

        constexpr UINT_PTR kTimerId = 1;
        constexpr UINT      kTimerMillis = 1000;
        constexpr std::size_t kHexMaxDump = 512; // bytes

        constexpr int ID_AUTOREFRESH = 2001;
        constexpr int ID_CTX_COPY = 1001;
        constexpr int ID_CTX_DUMP = 1002;
        constexpr int ID_CTX_HEX = 1003;

        struct SortState
        {
            int  column{ 0 };
            bool ascending{ true };
        };

        struct UiState
        {
            HFONT font{};
            HWND  hwnd_main{};
            HWND  hwnd_tab{};
            HWND  hwnd_list{};
            HWND  hwnd_detail{};
            HWND  hwnd_auto{};       // auto-refresh checkbox
            HWND  hwnd_tooltip{};    // tooltip control

            int   current_tab{ 0 };
            int   context_row{ -1 };

            SortState sort[kTabCount]{};

            // Live data
            std::vector<SectionInfo> sections;
            std::vector<CodeCave>    caves;
            std::vector<RwxRegion>   rwx_regions;
            std::vector<ThreadInfo>  threads;

            // Baseline imports (for IAT tamper)
            ImportSnapshot           imports_baseline{};

            // .text integrity/diff
            bool                     text_hash_ok{ false };
            bool                     text_disk_equal{ false };
            std::size_t              text_mismatch_offset{ static_cast<std::size_t>(-1) };
            TextIntegritySnapshot    text_snap{};
        };

        struct HexViewState
        {
            HWND          hwnd{};
            std::uintptr_t base{};
            std::size_t    size{};
        };

        export inline UiState g_state{};
        inline HexViewState   g_hex{};
        export inline thread_local std::wstring g_tooltipText;

        // ----------------------------------------------------
        // Forward declarations
        // ----------------------------------------------------
        LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
        LRESULT CALLBACK HexWndProc(HWND, UINT, WPARAM, LPARAM);

        void CreateChildControls(HWND);
        void OnSize(HWND, int, int);
        void RefreshInitialTab();
        void BuildColumnsForTab(int tab);
        void FillRowsForTab(int tab);
        void RebuildData();
        void RefillCurrentTab();
        void SortTab(int tab, int column);
        void SortTab_NoToggle(int tab);   // used by auto-refresh
        void UpdateGlobalStatus();
        void UpdateDetailForSelection();
        void UpdateDetailForItem(int tab, int row);

        void OpenHexViewForItem(int tab, int row);
        void ShowContextMenuForItem(int tab, int row, POINT screenPt);
        void CopyAddressForItem(int tab, int row);
        void DumpRegionForItem(int tab, int row);
        bool GetRegionForItem(int tab, int row, std::uintptr_t& base, std::size_t& size);

        bool IsImportRowTampered(int row);
        void InitBaseline();
        void InitTimer(HWND);
        void StopTimer(HWND);

        // ----------------------------------------------------
        // Helpers
        // ----------------------------------------------------
        inline std::wstring to_wstring(const std::string& s)
        {
            return { s.begin(), s.end() };
        }

        inline void SetListViewStyle(HWND lv)
        {
            DWORD style = LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL;
            SetWindowLongPtrW(lv, GWL_STYLE,
                (GetWindowLongPtrW(lv, GWL_STYLE) & ~LVS_TYPEMASK) | style);

            ListView_SetExtendedListViewStyle(
                lv,
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_HEADERDRAGDROP);
        }

        inline void AddColumn(HWND lv, int index, int width, const wchar_t* text)
        {
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.cx = width;
            col.pszText = const_cast<wchar_t*>(text);
            col.iSubItem = index;
            SendMessageW(lv, LVM_INSERTCOLUMNW, static_cast<WPARAM>(index), (LPARAM)&col);
        }

        inline void AddItem(HWND lv, int row, int col, const std::wstring& text)
        {
            if (col == 0)
            {
                LVITEMW item{};
                item.mask = LVIF_TEXT;
                item.iItem = row;
                item.iSubItem = 0;
                item.pszText = const_cast<wchar_t*>(text.c_str());
                SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&item);
            }
            else
            {
                LVITEMW sub{};
                sub.mask = LVIF_TEXT;
                sub.iItem = row;
                sub.iSubItem = col;
                sub.pszText = const_cast<wchar_t*>(text.c_str());
                SendMessageW(lv, LVM_SETITEMW, 0, (LPARAM)&sub);
            }
        }

        inline void SetDetailText(const std::wstring& text)
        {
            if (g_state.hwnd_detail)
                SetWindowTextW(g_state.hwnd_detail, text.c_str());
        }

        inline COLORREF MakeColor(BYTE r, BYTE g, BYTE b)
        {
            return RGB(r, g, b);
        }

        // ----------------------------------------------------
        // Baseline init and data refresh
        // ----------------------------------------------------
        void InitBaseline()
        {
            using namespace almond::security::memory;

            if (auto imp = snapshot_imports())
                g_state.imports_baseline = *imp;

            if (auto snap = snapshot_text())
            {
                g_state.text_snap = *snap;
                g_state.text_hash_ok = verify_text(*snap);
            }
            else
            {
                g_state.text_hash_ok = false;
            }

            if (auto diff = diff_text_with_disk())
            {
                g_state.text_disk_equal = diff->valid && diff->all_equal;
                g_state.text_mismatch_offset = diff->valid && !diff->all_equal
                    ? diff->first_mismatch_offset
                    : static_cast<std::size_t>(-1);
            }
            else
            {
                g_state.text_disk_equal = false;
                g_state.text_mismatch_offset = static_cast<std::size_t>(-1);
            }
        }

        void RebuildData()
        {
            using namespace almond::security::memory;

            g_state.sections = enumerate_sections();
            g_state.caves = find_code_caves_in_text(48);
            g_state.rwx_regions = scan_rwx_regions();
            g_state.threads = enumerate_threads_current_process();

            if (g_state.text_snap.base && g_state.text_snap.size)
                g_state.text_hash_ok = verify_text(g_state.text_snap);
        }

        // ----------------------------------------------------
        // UI entry point
        // ----------------------------------------------------
        export void RunSecurityInspector()
        {
            InitBaseline();
            RebuildData();

            INITCOMMONCONTROLSEX icex{};
            icex.dwSize = sizeof(icex);
            icex.dwICC = ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_WIN95_CLASSES;
            InitCommonControlsEx(&icex);

            HINSTANCE hinst = GetModuleHandleW(nullptr);

            // Main window
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = MainWndProc;
            wc.hInstance = hinst;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wc.lpszClassName = kMainClass;
            RegisterClassExW(&wc);

            // Hex view window
            WNDCLASSEXW wcHex{};
            wcHex.cbSize = sizeof(wcHex);
            wcHex.lpfnWndProc = HexWndProc;
            wcHex.hInstance = hinst;
            wcHex.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wcHex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wcHex.lpszClassName = kHexClass;
            RegisterClassExW(&wcHex);

            g_state.hwnd_main = CreateWindowExW(
                0,
                kMainClass,
                L"AlmondSecurity Inspector",
                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                CW_USEDEFAULT, CW_USEDEFAULT,
                1100, 700,
                nullptr,
                nullptr,
                hinst,
                nullptr);

            g_state.font = CreateFontW(
                -16, 0, 0, 0, FW_NORMAL,
                FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                FF_DONTCARE, L"Consolas");

            MSG msg{};
            while (GetMessageW(&msg, nullptr, 0, 0))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            if (g_state.font)
            {
                DeleteObject(g_state.font);
                g_state.font = nullptr;
            }
        }

        // ----------------------------------------------------
        // Main window proc
        // ----------------------------------------------------
        LRESULT CALLBACK MainWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        {
            switch (msg)
            {
            case WM_CREATE:
                CreateChildControls(h);
                InitTimer(h);
                return 0;

            case WM_SIZE:
                OnSize(h, LOWORD(lp), HIWORD(lp));
                return 0;

            case WM_TIMER:
                if (wp == kTimerId)
                {
                    RebuildData();
                    SortTab_NoToggle(g_state.current_tab);
                    RefillCurrentTab();
                    UpdateGlobalStatus();
                }
                return 0;

            case WM_NOTIFY:
            {
                auto* hdr = reinterpret_cast<LPNMHDR>(lp);

                // Tab change
                if (hdr->hwndFrom == g_state.hwnd_tab && hdr->code == TCN_SELCHANGE)
                {
                    int tab = TabCtrl_GetCurSel(g_state.hwnd_tab);
                    if (tab >= 0 && tab < kTabCount)
                    {
                        g_state.current_tab = tab;
                        RefreshInitialTab();
                        UpdateGlobalStatus();
                    }
                    return 0;
                }

                // ListView notifications
                if (hdr->hwndFrom == g_state.hwnd_list)
                {
                    if (hdr->code == LVN_COLUMNCLICK)
                    {
                        auto* lvh = reinterpret_cast<LPNMLISTVIEW>(lp);
                        int col = lvh->iSubItem;
                        SortTab(g_state.current_tab, col);
                        RefillCurrentTab();
                        return 0;
                    }
                    else if (hdr->code == LVN_ITEMCHANGED)
                    {
                        auto* lvn = reinterpret_cast<LPNMLISTVIEW>(lp);
                        if ((lvn->uChanged & LVIF_STATE) &&
                            (lvn->uNewState & LVIS_SELECTED))
                        {
                            UpdateDetailForSelection();
                        }
                        return 0;
                    }
                    else if (hdr->code == NM_DBLCLK)
                    {
                        auto* nm = reinterpret_cast<LPNMITEMACTIVATE>(lp);
                        if (nm->iItem >= 0)
                            OpenHexViewForItem(g_state.current_tab, nm->iItem);
                        return 0;
                    }
                    else if (hdr->code == NM_CUSTOMDRAW)
                    {
                        auto* cd = reinterpret_cast<LPNMLVCUSTOMDRAW>(lp);
                        switch (cd->nmcd.dwDrawStage)
                        {
                        case CDDS_PREPAINT:
                            return CDRF_NOTIFYITEMDRAW;

                        case CDDS_ITEMPREPAINT:
                        {
                            int row = static_cast<int>(cd->nmcd.dwItemSpec);
                            COLORREF text = GetSysColor(COLOR_WINDOWTEXT);
                            COLORREF bk = GetSysColor(COLOR_WINDOW);

                            if (g_state.current_tab == kTabRwx)
                            {
                                bk = MakeColor(80, 0, 0);
                                text = MakeColor(255, 255, 255);
                            }
                            else if (g_state.current_tab == kTabCaves)
                            {
                                if (row >= 0 &&
                                    static_cast<std::size_t>(row) < g_state.caves.size() &&
                                    g_state.caves[row].length >= 256)
                                {
                                    bk = MakeColor(60, 40, 0);
                                    text = MakeColor(255, 255, 0);
                                }
                            }
                            else if (g_state.current_tab == kTabImports)
                            {
                                if (IsImportRowTampered(row))
                                {
                                    bk = MakeColor(100, 0, 0);
                                    text = MakeColor(255, 255, 255);
                                }
                            }

                            cd->clrText = text;
                            cd->clrTextBk = bk;
                            return CDRF_DODEFAULT;
                        }
                        }

                        return CDRF_DODEFAULT;
                    }
                }

                // Tooltip text (self-explaining UI)
                if (hdr->hwndFrom == g_state.hwnd_tooltip &&
                    (hdr->code == TTN_GETDISPINFOW || hdr->code == TTN_GETDISPINFOA))
                {
                    auto* di = reinterpret_cast<LPNMTTDISPINFOW>(lp);

                    POINT pt{};
                    GetCursorPos(&pt);
                    ScreenToClient(g_state.hwnd_list, &pt);

                    LVHITTESTINFO hti{};
                    hti.pt = pt;
                    int idx = static_cast<int>(
                        SendMessageW(g_state.hwnd_list, LVM_HITTEST, 0, (LPARAM)&hti));

                    g_tooltipText.clear();

                    if (idx < 0)
                    {
                        // General help per tab
                        switch (g_state.current_tab)
                        {
                        case kTabSections:
                            g_tooltipText = L"PE sections in this module (code, data, resources). "
                                L".text\" is executable code; changes here often mean hooks or injected code.";
                            break;
                        case kTabCaves:
                            g_tooltipText = L"Code caves: long runs of filler bytes (0x00/0x90/0xCC). "
                                L"Often used as scratch space for injected shellcode.";
                            break;
                        case kTabRwx:
                            g_tooltipText = L"RWX regions: memory that is both writable and executable. "
                                L"Legitimate code rarely needs RWX; treat these as high-risk.";
                            break;
                        case kTabThreads:
                            g_tooltipText = L"Threads in this process. Unexpected extra threads can "
                                L"indicate injected code running in the background.";
                            break;
                        case kTabImports:
                            g_tooltipText = L"Import table entries (IAT). If a function target changes at runtime, "
                                L"someone is hooking or redirecting calls.";
                            break;
                        default:
                            g_tooltipText = L"Security details for this view.";
                            break;
                        }
                    }
                    else
                    {
                        // Row-specific hints
                        switch (g_state.current_tab)
                        {
                        case kTabSections:
                            if (static_cast<std::size_t>(idx) < g_state.sections.size())
                            {
                                auto& s = g_state.sections[idx];
                                if (s.name == ".text")
                                {
                                    g_tooltipText =
                                        L".text: executable code section. Tampering here usually "
                                        L"means inline hooks or injected code. Compare with disk image "
                                        L"and keep it RX, not RWX.";
                                }
                                else
                                {
                                    g_tooltipText =
                                        L"Data/resource section. Less critical than .text, but changes "
                                        L"can still be suspicious if not expected.";
                                }
                            }
                            break;

                        case kTabCaves:
                            if (static_cast<std::size_t>(idx) < g_state.caves.size())
                            {
                                auto& c = g_state.caves[idx];
                                if (c.length >= 256)
                                {
                                    g_tooltipText =
                                        L"Large code cave: enough space for non-trivial shellcode. "
                                        L"If you don’t allocate this yourself, you should treat it as "
                                        L"a potential injection target.";
                                }
                                else
                                {
                                    g_tooltipText =
                                        L"Small code cave: filler bytes in .text. Usually normal, "
                                        L"but can still be used by very tiny hooks.";
                                }
                            }
                            break;

                        case kTabRwx:
                            if (static_cast<std::size_t>(idx) < g_state.rwx_regions.size())
                            {
                                g_tooltipText =
                                    L"RWX region: writable + executable. This is how JITs and shellcode "
                                    L"normally run. To harden: use RW then RX, or guard these regions carefully.";
                            }
                            break;

                        case kTabThreads:
                            if (static_cast<std::size_t>(idx) < g_state.threads.size())
                            {
                                g_tooltipText =
                                    L"Process thread. If you see threads you didn't create, they may belong "
                                    L"to injected code or debuggers.";
                            }
                            break;

                        case kTabImports:
                            if (static_cast<std::size_t>(idx) < g_state.imports_baseline.entries.size())
                            {
                                bool tampered = IsImportRowTampered(idx);
                                if (tampered)
                                {
                                    g_tooltipText =
                                        L"Tampered IAT entry: this function now points somewhere else. "
                                        L"Typical of API hooks, trampolines, or injected DLLs.";
                                }
                                else
                                {
                                    g_tooltipText =
                                        L"IAT entry: imported function. If this address ever changes, "
                                        L"you've got a hook or redirection.";
                                }
                            }
                            break;
                        }
                    }

                    di->lpszText = g_tooltipText.empty()
                        ? const_cast<wchar_t*>(L"Security information.")
                        : g_tooltipText.data();

                    return 0;
                }

                return 0;
            }

            case WM_CONTEXTMENU:
            {
                HWND from = (HWND)wp;
                if (from == g_state.hwnd_list)
                {
                    POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                    if (pt.x == -1 && pt.y == -1)
                    {
                        int sel = static_cast<int>(
                            SendMessageW(g_state.hwnd_list, LVM_GETNEXTITEM,
                                static_cast<WPARAM>(-1), LVNI_FOCUSED));
                        if (sel >= 0)
                        {
                            RECT rc{};
                            SendMessageW(g_state.hwnd_list, LVM_GETITEMRECT,
                                static_cast<WPARAM>(sel), (LPARAM)&rc);
                            pt.x = rc.left;
                            pt.y = rc.bottom;
                            ClientToScreen(g_state.hwnd_list, &pt);
                        }
                    }

                    POINT client = pt;
                    ScreenToClient(g_state.hwnd_list, &client);
                    LVHITTESTINFO hti{};
                    hti.pt = client;
                    int idx = static_cast<int>(
                        SendMessageW(g_state.hwnd_list, LVM_HITTEST, 0, (LPARAM)&hti));
                    if (idx >= 0)
                    {
                        g_state.context_row = idx;
                        ShowContextMenuForItem(g_state.current_tab, idx, pt);
                    }
                    return 0;
                }
                break;
            }

            case WM_COMMAND:
            {
                int id = LOWORD(wp);
                if (id == ID_AUTOREFRESH)
                {
                    BOOL checked = (SendMessageW(g_state.hwnd_auto, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    if (checked)
                        InitTimer(h);
                    else
                        StopTimer(h);
                    return 0;
                }

                if (id == ID_CTX_COPY)
                {
                    if (g_state.context_row >= 0)
                        CopyAddressForItem(g_state.current_tab, g_state.context_row);
                    return 0;
                }
                if (id == ID_CTX_DUMP)
                {
                    if (g_state.context_row >= 0)
                        DumpRegionForItem(g_state.current_tab, g_state.context_row);
                    return 0;
                }
                if (id == ID_CTX_HEX)
                {
                    if (g_state.context_row >= 0)
                        OpenHexViewForItem(g_state.current_tab, g_state.context_row);
                    return 0;
                }
                break;
            }

            case WM_DESTROY:
                StopTimer(h);
                PostQuitMessage(0);
                return 0;
            }

            return DefWindowProcW(h, msg, wp, lp);
        }

        // ----------------------------------------------------
        // Hex view window proc
        // ----------------------------------------------------
        LRESULT CALLBACK HexWndProc(HWND h, UINT msg, WPARAM, LPARAM lp)
        {
            switch (msg)
            {
            case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC dc = BeginPaint(h, &ps);

                RECT rc{};
                GetClientRect(h, &rc);

                HFONT font = CreateFontW(
                    -16, 0, 0, 0, FW_NORMAL,
                    FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                    FF_DONTCARE, L"Consolas");
                HFONT old = (HFONT)SelectObject(dc, font);

                if (!g_hex.base || !g_hex.size)
                {
                    std::wstring msgL = L"No region selected.";
                    TextOutW(dc, 10, 10, msgL.c_str(), (int)msgL.size());
                }
                else
                {
                    auto* p = reinterpret_cast<const std::uint8_t*>(g_hex.base);
                    std::size_t n = std::min<std::size_t>(g_hex.size, kHexMaxDump);

                    int y = 10;
                    for (std::size_t offset = 0; offset < n; offset += 16)
                    {
                        std::wstring line = std::format(L"{:08X}: ", (unsigned)offset);
                        std::wstring ascii;

                        for (std::size_t i = 0; i < 16; ++i)
                        {
                            if (offset + i < n)
                            {
                                auto b = p[offset + i];
                                line += std::format(L"{:02X} ", b);
                                ascii += (b >= 32 && b < 127)
                                    ? (wchar_t)b
                                    : L'.';
                            }
                            else
                            {
                                line += L"   ";
                                ascii += L' ';
                            }
                        }

                        line += L" ";
                        line += ascii;

                        TextOutW(dc, 10, y, line.c_str(), (int)line.size());
                        y += 20;
                    }
                }

                SelectObject(dc, old);
                DeleteObject(font);
                EndPaint(h, &ps);
                return 0;
            }

            case WM_CLOSE:
                ShowWindow(h, SW_HIDE);
                return 0;
            }

            return DefWindowProcW(h, msg, 0, lp);
        }

        // ----------------------------------------------------
        // Child controls / layout
        // ----------------------------------------------------
        void CreateChildControls(HWND parent)
        {
            RECT rc{};
            GetClientRect(parent, &rc);

            HINSTANCE hinst = GetModuleHandleW(nullptr);

            g_state.hwnd_tab = CreateWindowExW(
                0, WC_TABCONTROLW, L"",
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                0, 0,
                rc.right - rc.left,
                28,
                parent,
                nullptr,
                hinst,
                nullptr);

            const wchar_t* labels[kTabCount] = {
                L"Sections",
                L"Code Caves",
                L"RWX Regions",
                L"Threads",
                L"Imports"
            };

            for (int i = 0; i < kTabCount; ++i)
            {
                TCITEMW item{};
                item.mask = TCIF_TEXT;
                item.pszText = const_cast<wchar_t*>(labels[i]);
                TabCtrl_InsertItem(g_state.hwnd_tab, i, &item);
            }

            // Auto-refresh checkbox (top-right)
            g_state.hwnd_auto = CreateWindowExW(
                0, L"BUTTON", L"Auto refresh",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                rc.right - 150, 4, 140, 20,
                parent, (HMENU)(INT)ID_AUTOREFRESH, hinst, nullptr);
            SendMessageW(g_state.hwnd_auto, BM_SETCHECK, BST_CHECKED, 0);

            // List + detail layout
            const int tab_height = 28;
            const int detail_height = (rc.bottom - tab_height) / 3;
            const int list_height = (rc.bottom - tab_height) - detail_height;

            g_state.hwnd_list = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                WC_LISTVIEWW,
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT,
                0, tab_height,
                rc.right - rc.left,
                list_height,
                parent,
                nullptr,
                hinst,
                nullptr);

            g_state.hwnd_detail = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                L"",
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
                ES_AUTOVSCROLL | WS_VSCROLL,
                0, tab_height + list_height,
                rc.right - rc.left,
                detail_height,
                parent,
                nullptr,
                hinst,
                nullptr);

            SetListViewStyle(g_state.hwnd_list);

            SendMessageW(g_state.hwnd_list, WM_SETFONT, (WPARAM)g_state.font, TRUE);
            SendMessageW(g_state.hwnd_tab, WM_SETFONT, (WPARAM)g_state.font, TRUE);
            SendMessageW(g_state.hwnd_detail, WM_SETFONT, (WPARAM)g_state.font, TRUE);
            SendMessageW(g_state.hwnd_auto, WM_SETFONT, (WPARAM)g_state.font, TRUE);

            // Tooltip over list view (one tool, dynamic text)
            g_state.hwnd_tooltip = CreateWindowExW(
                0, TOOLTIPS_CLASS, nullptr,
                WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                CW_USEDEFAULT, CW_USEDEFAULT,
                CW_USEDEFAULT, CW_USEDEFAULT,
                parent, nullptr, hinst, nullptr);

            TOOLINFOW ti{};
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd = g_state.hwnd_list;
            ti.uId = (UINT_PTR)g_state.hwnd_list;
            ti.lpszText = const_cast<wchar_t*>(L"");
            SendMessageW(g_state.hwnd_tooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);

            g_state.current_tab = 0;
            TabCtrl_SetCurSel(g_state.hwnd_tab, 0);
            RefreshInitialTab();
            UpdateGlobalStatus();
        }

        void OnSize(HWND, int width, int height)
        {
            if (!g_state.hwnd_tab || !g_state.hwnd_list || !g_state.hwnd_detail)
                return;

            const int tab_height = 28;
            const int detail_height = (height - tab_height) / 3;
            const int list_height = (height - tab_height) - detail_height;

            MoveWindow(g_state.hwnd_tab, 0, 0, width, tab_height, TRUE);
            MoveWindow(g_state.hwnd_auto, width - 150, 4, 140, 20, TRUE);
            MoveWindow(g_state.hwnd_list, 0, tab_height, width, list_height, TRUE);
            MoveWindow(g_state.hwnd_detail, 0, tab_height + list_height, width, detail_height, TRUE);
        }

        void InitTimer(HWND h)
        {
            SetTimer(h, kTimerId, kTimerMillis, nullptr);
        }

        void StopTimer(HWND h)
        {
            KillTimer(h, kTimerId);
        }

        // ----------------------------------------------------
        // Tab refresh / columns / rows
        // ----------------------------------------------------
        void RefreshInitialTab()
        {
            SendMessageW(g_state.hwnd_list, LVM_DELETEALLITEMS, 0, 0);
            while (SendMessageW(g_state.hwnd_list, LVM_DELETECOLUMN, 0, 0))
            {
            }

            BuildColumnsForTab(g_state.current_tab);
            FillRowsForTab(g_state.current_tab);
        }

        void RefillCurrentTab()
        {
            SendMessageW(g_state.hwnd_list, LVM_DELETEALLITEMS, 0, 0);
            FillRowsForTab(g_state.current_tab);
        }

        void BuildColumnsForTab(int tab)
        {
            switch (tab)
            {
            case kTabSections:
                AddColumn(g_state.hwnd_list, 0, 120, L"Name");
                AddColumn(g_state.hwnd_list, 1, 180, L"Base");
                AddColumn(g_state.hwnd_list, 2, 120, L"Size");
                AddColumn(g_state.hwnd_list, 3, 80, L"Prot");
                AddColumn(g_state.hwnd_list, 4, 120, L"Chars");
                break;

            case kTabCaves:
                AddColumn(g_state.hwnd_list, 0, 180, L"Address");
                AddColumn(g_state.hwnd_list, 1, 100, L"Length");
                AddColumn(g_state.hwnd_list, 2, 80, L"Filler");
                break;

            case kTabRwx:
                AddColumn(g_state.hwnd_list, 0, 180, L"Base");
                AddColumn(g_state.hwnd_list, 1, 120, L"Size");
                AddColumn(g_state.hwnd_list, 2, 80, L"Prot");
                break;

            case kTabThreads:
                AddColumn(g_state.hwnd_list, 0, 100, L"TID");
                AddColumn(g_state.hwnd_list, 1, 100, L"PID");
                break;

            case kTabImports:
                AddColumn(g_state.hwnd_list, 0, 160, L"DLL");
                AddColumn(g_state.hwnd_list, 1, 220, L"Function");
                AddColumn(g_state.hwnd_list, 2, 180, L"Slot");
                AddColumn(g_state.hwnd_list, 3, 200, L"Target");
                break;
            }
        }

        void FillRowsForTab(int tab)
        {
            HWND lv = g_state.hwnd_list;

            switch (tab)
            {
            case kTabSections:
            {
                int row = 0;
                for (auto& s : g_state.sections)
                {
                    std::wstring name = to_wstring(s.name);
                    std::wstring base = std::format(L"{:#016x}", s.base);
                    std::wstring size = std::format(L"{}", s.size);
                    std::wstring prot = L"";
                    std::wstring chars = std::format(L"{:#08x}", s.characteristics);

                    AddItem(lv, row, 0, name);
                    AddItem(lv, row, 1, base);
                    AddItem(lv, row, 2, size);
                    AddItem(lv, row, 3, prot);
                    AddItem(lv, row, 4, chars);
                    ++row;
                }
            }
            break;

            case kTabCaves:
            {
                int row = 0;
                for (auto& c : g_state.caves)
                {
                    std::wstring addr = std::format(L"{:#016x}", c.address);
                    std::wstring length = std::format(L"{}", c.length);
                    std::wstring filler = std::format(L"0x{:02x}", c.filler);

                    AddItem(lv, row, 0, addr);
                    AddItem(lv, row, 1, length);
                    AddItem(lv, row, 2, filler);
                    ++row;
                }
            }
            break;

            case kTabRwx:
            {
                int row = 0;
                for (auto& r : g_state.rwx_regions)
                {
                    std::wstring base = std::format(L"{:#016x}", r.base);
                    std::wstring size = std::format(L"{}", r.size);
                    std::wstring prot = L"RWX";

                    AddItem(lv, row, 0, base);
                    AddItem(lv, row, 1, size);
                    AddItem(lv, row, 2, prot);
                    ++row;
                }
            }
            break;

            case kTabThreads:
            {
                int row = 0;
                for (auto& t : g_state.threads)
                {
                    std::wstring tid = std::format(L"{}", t.thread_id);
                    std::wstring pid = std::format(L"{}", t.owner_process_id);
                    AddItem(lv, row, 0, tid);
                    AddItem(lv, row, 1, pid);
                    ++row;
                }
            }
            break;

            case kTabImports:
            {
                int row = 0;
                for (auto& e : g_state.imports_baseline.entries)
                {
                    std::wstring dll = to_wstring(e.dll_name);
                    std::wstring func = to_wstring(e.function_name);
                    std::wstring slot = std::format(L"{:#016x}",
                        (std::uint64_t)(reinterpret_cast<std::uintptr_t>(e.slot_addr)));

                    std::uintptr_t current = e.slot_addr ? *e.slot_addr : 0;
                    std::wstring tgt = std::format(L"{:#016x}", (std::uint64_t)current);

                    AddItem(lv, row, 0, dll);
                    AddItem(lv, row, 1, func);
                    AddItem(lv, row, 2, slot);
                    AddItem(lv, row, 3, tgt);
                    ++row;
                }
            }
            break;
            }
        }

        // ----------------------------------------------------
        // Sorting
        // ----------------------------------------------------
        void SortSections(int column, bool asc)
        {
            auto& v = g_state.sections;
            std::sort(v.begin(), v.end(),
                [=](const SectionInfo& a, const SectionInfo& b)
                {
                    switch (column)
                    {
                    case 0: return asc ? (a.name < b.name) : (a.name > b.name);
                    case 1: return asc ? (a.base < b.base) : (a.base > b.base);
                    case 2: return asc ? (a.size < b.size) : (a.size > b.size);
                    case 4: return asc ? (a.characteristics < b.characteristics)
                        : (a.characteristics > b.characteristics);
                    default: return asc ? (a.name < b.name) : (a.name > b.name);
                    }
                });
        }

        void SortCaves(int column, bool asc)
        {
            auto& v = g_state.caves;
            std::sort(v.begin(), v.end(),
                [=](const CodeCave& a, const CodeCave& b)
                {
                    switch (column)
                    {
                    case 0: return asc ? (a.address < b.address) : (a.address > b.address);
                    case 1: return asc ? (a.length < b.length) : (a.length > b.length);
                    case 2: return asc ? (a.filler < b.filler) : (a.filler > b.filler);
                    default: return asc ? (a.address < b.address) : (a.address > b.address);
                    }
                });
        }

        void SortRwx(int column, bool asc)
        {
            auto& v = g_state.rwx_regions;
            std::sort(v.begin(), v.end(),
                [=](const RwxRegion& a, const RwxRegion& b)
                {
                    switch (column)
                    {
                    case 0: return asc ? (a.base < b.base) : (a.base > b.base);
                    case 1: return asc ? (a.size < b.size) : (a.size > b.size);
                    default: return asc ? (a.base < b.base) : (a.base > b.base);
                    }
                });
        }

        void SortThreads(int column, bool asc)
        {
            auto& v = g_state.threads;
            std::sort(v.begin(), v.end(),
                [=](const ThreadInfo& a, const ThreadInfo& b)
                {
                    switch (column)
                    {
                    case 0: return asc ? (a.thread_id < b.thread_id) : (a.thread_id > b.thread_id);
                    case 1: return asc ? (a.owner_process_id < b.owner_process_id)
                        : (a.owner_process_id > b.owner_process_id);
                    default: return asc ? (a.thread_id < b.thread_id) : (a.thread_id > b.thread_id);
                    }
                });
        }

        void SortImports(int column, bool asc)
        {
            auto& v = g_state.imports_baseline.entries;
            std::sort(v.begin(), v.end(),
                [=](const IatEntry& a, const IatEntry& b)
                {
                    switch (column)
                    {
                    case 0: return asc ? (a.dll_name < b.dll_name) : (a.dll_name > b.dll_name);
                    case 1: return asc ? (a.function_name < b.function_name)
                        : (a.function_name > b.function_name);
                    case 2:
                        return asc ? ((std::uintptr_t)a.slot_addr < (std::uintptr_t)b.slot_addr)
                            : ((std::uintptr_t)a.slot_addr > (std::uintptr_t)b.slot_addr);
                    case 3:
                        return asc ? (a.original_target < b.original_target)
                            : (a.original_target > b.original_target);
                    default: return asc ? (a.dll_name < b.dll_name) : (a.dll_name > b.dll_name);
                    }
                });
        }

        void SortTab(int tab, int column)
        {
            auto& state = g_state.sort[tab];

            if (state.column == column)
                state.ascending = !state.ascending;
            else
            {
                state.column = column;
                state.ascending = true;
            }

            switch (tab)
            {
            case kTabSections: SortSections(column, state.ascending); break;
            case kTabCaves:    SortCaves(column, state.ascending);    break;
            case kTabRwx:      SortRwx(column, state.ascending);      break;
            case kTabThreads:  SortThreads(column, state.ascending);  break;
            case kTabImports:  SortImports(column, state.ascending);  break;
            }
        }

        // used by auto-refresh: do NOT toggle direction
        void SortTab_NoToggle(int tab)
        {
            auto& state = g_state.sort[tab];
            int column = state.column;

            switch (tab)
            {
            case kTabSections: SortSections(column, state.ascending); break;
            case kTabCaves:    SortCaves(column, state.ascending);    break;
            case kTabRwx:      SortRwx(column, state.ascending);      break;
            case kTabThreads:  SortThreads(column, state.ascending);  break;
            case kTabImports:  SortImports(column, state.ascending);  break;
            }
        }

        // ----------------------------------------------------
        // Global status / detail panel
        // ----------------------------------------------------
        void UpdateGlobalStatus()
        {
            using namespace almond::security::memory;

            std::size_t rwx_count = g_state.rwx_regions.size();
            std::size_t caves_big = 0;
            for (auto& c : g_state.caves)
                if (c.length >= 256)
                    ++caves_big;

            auto changed = detect_iat_changes(g_state.imports_baseline);
            std::size_t iat_tampered = changed.size();

            std::wstring text = std::format(
                L"[Security Summary]\r\n"
                L"Text hash OK (in-memory): {}\r\n"
                L"Text matches disk image:  {}\r\n"
                L"First .text mismatch:     {}\r\n"
                L"RWX regions:              {}\r\n"
                L"Large code caves (>=256): {}\r\n"
                L"IAT tampered entries:     {}\r\n"
                L"\r\n[Hardening hints]\r\n"
                L"- Keep code pages RX only; avoid RWX except for controlled JITs.\r\n"
                L"- Treat mismatched .text as potential inline hooks or patches.\r\n"
                L"- Large code caves and extra threads are prime injection spots.\r\n"
                L"- IAT changes almost always mean hooks: log and verify callers.\r\n",
                g_state.text_hash_ok ? L"true" : L"false",
                g_state.text_disk_equal ? L"true" : L"false",
                g_state.text_mismatch_offset == static_cast<std::size_t>(-1)
                ? L"(none)" : std::format(L"{:#x}", g_state.text_mismatch_offset),
                rwx_count,
                caves_big,
                iat_tampered);

            SetDetailText(text);
        }

        void UpdateDetailForSelection()
        {
            int sel = static_cast<int>(
                SendMessageW(g_state.hwnd_list, LVM_GETNEXTITEM,
                    static_cast<WPARAM>(-1), LVNI_SELECTED));
            if (sel < 0)
            {
                UpdateGlobalStatus();
                return;
            }
            UpdateDetailForItem(g_state.current_tab, sel);
        }

        void UpdateDetailForItem(int tab, int row)
        {
            std::wstring text;

            switch (tab)
            {
            case kTabSections:
                if (row >= 0 && static_cast<std::size_t>(row) < g_state.sections.size())
                {
                    auto& s = g_state.sections[row];
                    text += std::format(
                        L"[Section]\r\n"
                        L"Name: {}\r\nBase: {:#016x}\r\nSize: {}\r\nChars: {:#08x}\r\n",
                        to_wstring(s.name), s.base, s.size, s.characteristics);

                    if (s.name == ".text")
                    {
                        text += std::format(
                            L"\r\n[.text Integrity]\r\n"
                            L"Hash OK (in-memory): {}\r\n"
                            L"Disk match:          {}\r\n"
                            L"First mismatch:      {}\r\n"
                            L"\r\n[Hardening suggestion]\r\n"
                            L"- Ensure no code writes directly into .text.\r\n"
                            L"- Turn on CFG / DEP / ASLR where possible.\r\n"
                            L"- Use sealed, signed binaries for release builds.\r\n",
                            g_state.text_hash_ok ? L"true" : L"false",
                            g_state.text_disk_equal ? L"true" : L"false",
                            g_state.text_mismatch_offset == static_cast<std::size_t>(-1)
                            ? L"(none)" : std::format(L"{:#x}", g_state.text_mismatch_offset));
                    }
                }
                break;

            case kTabCaves:
                if (row >= 0 && static_cast<std::size_t>(row) < g_state.caves.size())
                {
                    auto& c = g_state.caves[row];
                    text += std::format(
                        L"[Code Cave]\r\n"
                        L"Address: {:#016x}\r\nLength: {}\r\nFiller: 0x{:02x}\r\n",
                        c.address, c.length, c.filler);

                    if (c.length >= 256)
                    {
                        text +=
                            L"\r\n[Risk]\r\n"
                            L"- This is large enough to host real shellcode.\r\n"
                            L"\r\n[Hardening suggestion]\r\n"
                            L"- Avoid leaving big unused gaps in .text.\r\n"
                            L"- Consider filling with non-trivial patterns or "
                            L"using guard pages where appropriate.\r\n";
                    }
                    else
                    {
                        text +=
                            L"\r\n[Note]\r\n"
                            L"- Small caves are usually compiler/linker artifacts.\r\n"
                            L"- Still keep an eye if something later writes here.\r\n";
                    }
                }
                break;

            case kTabRwx:
                if (row >= 0 && static_cast<std::size_t>(row) < g_state.rwx_regions.size())
                {
                    auto& r = g_state.rwx_regions[row];
                    text += std::format(
                        L"[RWX Region]\r\n"
                        L"Base: {:#016x}\r\nSize: {}\r\nProtect: RWX\r\n"
                        L"\r\n[Risk]\r\n"
                        L"- Writable + executable is the classic shellcode target.\r\n"
                        L"\r\n[Hardening suggestion]\r\n"
                        L"- Use RW then RX (no RWX) when setting up JIT buffers.\r\n"
                        L"- Log and restrict where RWX appears in your engine.\r\n",
                        r.base, r.size);
                }
                break;

            case kTabThreads:
                if (row >= 0 && static_cast<std::size_t>(row) < g_state.threads.size())
                {
                    auto& t = g_state.threads[row];
                    text += std::format(
                        L"[Thread]\r\n"
                        L"TID: {}\r\nPID: {}\r\n"
                        L"\r\n[Hardening suggestion]\r\n"
                        L"- In your engine, track all threads you create.\r\n"
                        L"- If you see extra TIDs at runtime, inspect their start "
                        L"addresses and stacks.\r\n",
                        t.thread_id, t.owner_process_id);
                }
                break;

            case kTabImports:
                if (row >= 0 && static_cast<std::size_t>(row) < g_state.imports_baseline.entries.size())
                {
                    auto& e = g_state.imports_baseline.entries[row];
                    std::uintptr_t current = e.slot_addr ? *e.slot_addr : 0;
                    bool tampered = (current != e.original_target);

                    text += std::format(
                        L"[Import]\r\n"
                        L"DLL: {}\r\nFunction: {}\r\nSlot: {:#016x}\r\n"
                        L"Current target: {:#016x}\r\nTampered: {}\r\n",
                        to_wstring(e.dll_name),
                        to_wstring(e.function_name),
                        (std::uint64_t)(reinterpret_cast<std::uintptr_t>(e.slot_addr)),
                        (std::uint64_t)current,
                        tampered ? L"true" : L"false");

                    if (tampered)
                    {
                        text +=
                            L"\r\n[Risk]\r\n"
                            L"- This function is hooked or redirected.\r\n"
                            L"\r\n[Hardening suggestion]\r\n"
                            L"- Monitor and log IAT changes.\r\n"
                            L"- Consider late-binding critical APIs manually "
                            L"and validating function pointers.\r\n";
                    }
                    else
                    {
                        text +=
                            L"\r\n[Note]\r\n"
                            L"- Currently matches the baseline IAT target.\r\n"
                            L"- Use this as a reference when scanning for hooks.\r\n";
                    }
                }
                break;
            }

            if (text.empty())
                UpdateGlobalStatus();
            else
                SetDetailText(text);
        }

        // ----------------------------------------------------
        // IAT tamper check helper
        // ----------------------------------------------------
        bool IsImportRowTampered(int row)
        {
            if (row < 0 || static_cast<std::size_t>(row) >= g_state.imports_baseline.entries.size())
                return false;

            auto& e = g_state.imports_baseline.entries[row];
            if (!e.slot_addr)
                return false;

            std::uintptr_t current = *e.slot_addr;
            return current != e.original_target;
        }

        // ----------------------------------------------------
        // Hex view / context menu / region handling
        // ----------------------------------------------------
        bool GetRegionForItem(int tab, int row, std::uintptr_t& base, std::size_t& size)
        {
            base = 0;
            size = 0;

            switch (tab)
            {
            case kTabSections:
                if (row >= 0 && static_cast<std::size_t>(row) < g_state.sections.size())
                {
                    auto& s = g_state.sections[row];
                    base = s.base;
                    size = s.size;
                    return true;
                }
                break;

            case kTabCaves:
                if (row >= 0 && static_cast<std::size_t>(row) < g_state.caves.size())
                {
                    auto& c = g_state.caves[row];
                    base = c.address;
                    size = c.length;
                    return true;
                }
                break;

            case kTabRwx:
                if (row >= 0 && static_cast<std::size_t>(row) < g_state.rwx_regions.size())
                {
                    auto& r = g_state.rwx_regions[row];
                    base = r.base;
                    size = r.size;
                    return true;
                }
                break;

            case kTabImports:
                if (row >= 0 && static_cast<std::size_t>(row) < g_state.imports_baseline.entries.size())
                {
                    auto& e = g_state.imports_baseline.entries[row];
                    if (e.slot_addr)
                    {
                        base = *e.slot_addr;
                        size = 256; // arbitrary view length around function entry
                        return true;
                    }
                }
                break;
            }

            return false;
        }

        void OpenHexViewForItem(int tab, int row)
        {
            std::uintptr_t base{};
            std::size_t    size{};

            if (!GetRegionForItem(tab, row, base, size) || !base || !size)
                return;

            g_hex.base = base;
            g_hex.size = size;

            if (!g_hex.hwnd)
            {
                HINSTANCE hinst = GetModuleHandleW(nullptr);
                g_hex.hwnd = CreateWindowExW(
                    0,
                    kHexClass,
                    L"Hex View",
                    WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                    CW_USEDEFAULT, CW_USEDEFAULT,
                    800, 500,
                    nullptr,
                    nullptr,
                    hinst,
                    nullptr);
            }
            else
            {
                ShowWindow(g_hex.hwnd, SW_SHOWNORMAL);
                SetForegroundWindow(g_hex.hwnd);
            }

            InvalidateRect(g_hex.hwnd, nullptr, TRUE);
        }

        void ShowContextMenuForItem(int tab, int row, POINT screenPt)
        {
            (void)tab;
            (void)row;

            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, ID_CTX_COPY, L"Copy address");
            AppendMenuW(menu, MF_STRING, ID_CTX_DUMP, L"Dump region to file");
            AppendMenuW(menu, MF_STRING, ID_CTX_HEX, L"Open in Hex View");

            TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                screenPt.x, screenPt.y, 0, g_state.hwnd_main, nullptr);

            DestroyMenu(menu);
        }

        void CopyAddressForItem(int tab, int row)
        {
            std::uintptr_t base{};
            std::size_t    size{};
            if (!GetRegionForItem(tab, row, base, size) || !base)
                return;

            std::wstring text = std::format(L"{:#016x}", base);

            if (!OpenClipboard(g_state.hwnd_main))
                return;
            EmptyClipboard();

            const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (!hMem)
            {
                CloseClipboard();
                return;
            }

            void* ptr = GlobalLock(hMem);
            std::memcpy(ptr, text.c_str(), bytes);
            GlobalUnlock(hMem);

            SetClipboardData(CF_UNICODETEXT, hMem);
            CloseClipboard();
        }

        void DumpRegionForItem(int tab, int row)
        {
            std::uintptr_t base{};
            std::size_t    size{};
            if (!GetRegionForItem(tab, row, base, size) || !base || !size)
                return;

            std::size_t to_dump = std::min<std::size_t>(size, 1U << 20); // cap at 1MB

            wchar_t filename[MAX_PATH]{};
            const wchar_t* tabName = L"region";

            switch (tab)
            {
            case kTabSections: tabName = L"section"; break;
            case kTabCaves:    tabName = L"cave";    break;
            case kTabRwx:      tabName = L"rwx";     break;
            case kTabImports:  tabName = L"import";  break;
            }

            std::swprintf(filename, MAX_PATH, L"dump_%s_%016llX.bin",
                tabName, (unsigned long long)base);

            HANDLE hFile = CreateFileW(
                filename,
                GENERIC_WRITE,
                0,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);

            if (hFile == INVALID_HANDLE_VALUE)
                return;

            DWORD written = 0;
            auto* p = reinterpret_cast<const std::uint8_t*>(base);
            WriteFile(hFile, p, (DWORD)to_dump, &written, nullptr);
            CloseHandle(hFile);
        }

    } // namespace detail

    export using detail::RunSecurityInspector;
}
