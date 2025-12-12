module;
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <fstream>

#ifndef TPM_RC_ANCHOR
#define TPM_RC_ANCHOR 0x0000
#endif

#pragma comment(lib, "Comctl32.lib")

export module win32_ui_editor;

import std;
import win32_ui_editor.model;
import win32_ui_editor.importparser;

namespace wui = win32_ui_editor::model;
namespace wimp = win32_ui_editor::importparser;

using std::wstring;
using std::vector;
using std::to_wstring;

// -----------------------------------------------------------------------------
// GLOBAL STATE
// -----------------------------------------------------------------------------

namespace editor
{
    struct ZOrderNodeData
    {
        int  controlIndex{ -1 };
        int  tabPage{ -1 };
        bool isPageNode{ false };
    };
    HINSTANCE g_hInst{};
    HWND      g_hMain{};
    HWND      g_hDesign{};
    HWND      g_hPropPanel{};
    HWND      g_hTabPageLabel{};
    HWND      g_hTabPageCombo{};
    HWND      g_hHierarchyTree{};
    HWND      g_hReparentBtn{};
    HWND      g_hZOrderTree{};
    HWND      g_hZBringFront{};
    HWND      g_hZSendBack{};
    HWND      g_hZForward{};
    HWND      g_hZBackward{};
    HWND      g_hToolbar{};
    HMENU     g_hInsertMenu{};

    int       g_zListTop{ 0 };
    int       g_treeTop{ 0 };
    bool      g_inZTreeUpdate{ false };
    bool      g_inTreeUpdate{ false };

    constexpr int kDesignMargin = 8;
    constexpr int kPropPanelWidth = 320;

    // Property panel controls
    HWND g_hPropEdits[6]{}; // X,Y,W,H,Text,ID
    HWND g_hStyleEdit{};    // style expression textbox
    HWND g_hStyleChkChild{};
    HWND g_hStyleChkVisible{};
    HWND g_hStyleChkTabstop{};
    HWND g_hStyleChkBorder{};

    bool g_inStyleUpdate{ false };
    bool g_inTabPageUpdate{ false };

    // Model
    vector<wui::ControlDef> g_controls;
    int   g_selectedIndex{ -1 };

    // Runtime HWNDs per control index
    vector<HWND> g_hwndControls;
    std::unordered_map<int, std::vector<HWND>> g_tabPageContainers;
    std::vector<HTREEITEM> g_treeItems;
    std::vector<HTREEITEM> g_zTreeItems;
    std::deque<ZOrderNodeData> g_zTreeNodes;

    // Subclassing map for live controls
    std::unordered_map<HWND, WNDPROC> g_originalProcs;
    WNDPROC g_originalDesignProc{};
    WNDPROC g_originalZTreeProc{};

    enum class DragHandle
    {
        None,
        Move,
        Left,
        Right,
        Top,
        Bottom,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
    };

    struct DragState
    {
        bool       active{ false };
        DragHandle handle{ DragHandle::None };
        POINT      startPt{};      // in design surface coords
        RECT       startRect{};    // control rect in design coords
        RECT       previewRect{};  // live preview rect during drag
    };

    DragState g_drag{};

    struct ParentPickResult
    {
        int parentIndex{ -1 };
        int tabPageId{ -1 };
    };


    struct CreationState
    {
        bool pending{ false };
        bool drawing{ false };
        wui::ControlType type{};
        ParentPickResult parentChoice{};
        POINT startPt{};
        RECT  previewRect{};
    };

    CreationState g_create{};
    bool g_drawToCreateMode{ true };

    constexpr int kHandleSize = 6;
    constexpr int kGridSize = 4;
    constexpr DWORD kPropPanelDebounceMs = 60;
    DWORD g_lastPropRefreshTick{ 0 };

    auto& CurrentControls() noexcept { return g_controls; }

    // Forward decl
    struct ParentInfo
    {
        HWND hwnd{};
        RECT rect{};
    };

    void RefreshPropertyPanel();
    void RebuildRuntimeControls();
    void RebuildHierarchyTree();
    std::wstring HierarchyLabelForControl(const wui::ControlDef& c, int idx);
    const std::vector<std::wstring>& TabPagesFor(const wui::ControlDef& tab);
    void RebuildZOrderTree();
    void SyncZOrderSelection();
    void SyncHierarchySelection();
    void UpdateZOrderButtons();
    void RestackAndRefreshSelection();
    void ApplyZOrderToWindows();
    void EndDrag();
    bool BeginCreateDrag(const POINT& designPt);
    bool UpdateCreateDrag(const POINT& designPt);
    void EndCreateDrag();
    ParentInfo GetParentInfoFor(const wui::ControlDef& c);
    HWND EnsureControlCreated(int index);
    void EnsureTabPageContainers(int tabIndex);
    RECT TabPageRectInDesignCoords(const wui::ControlDef& tabDef, HWND hTab);
    void ShowArrangeContextMenu(POINT screenPt);
    int NormalizedTabPage(int tabIndex, int requested);
    LRESULT CALLBACK ZOrderTreeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
}

using namespace editor;

// -----------------------------------------------------------------------------
// UTILS
// -----------------------------------------------------------------------------

namespace
{
    wstring get_window_text_w(HWND h)
    {
        const int len = GetWindowTextLengthW(h);
        if (len <= 0) return {};
        wstring s(static_cast<size_t>(len), L'\0');
        GetWindowTextW(h, s.data(), len + 1);
        return s;
    }

    void set_window_text_w(HWND h, const wstring& s)
    {
        SetWindowTextW(h, s.c_str());
    }

    POINT PhysicalScreenToLogical(HWND hwnd, POINT screenPt)
    {
        POINT logicalPt = screenPt;
        if (!hwnd)
            return logicalPt;

        if (!PhysicalToLogicalPointForPerMonitorDPI(hwnd, &logicalPt))
        {
            const UINT dpi = GetDpiForWindow(hwnd);
            if (dpi)
            {
                logicalPt.x = MulDiv(screenPt.x, USER_DEFAULT_SCREEN_DPI, dpi);
                logicalPt.y = MulDiv(screenPt.y, USER_DEFAULT_SCREEN_DPI, dpi);
            }
        }

        return logicalPt;
    }

    void set_edit_int(HWND h, int value)
    {
        set_window_text_w(h, to_wstring(value));
    }

    int parse_int_or(const wstring& s, int fallback)
    {
        try
        {
            if (s.empty()) return fallback;
            return std::stoi(s);
        }
        catch (...)
        {
            return fallback;
        }
    }

    // Style string helpers for C: checkboxes + custom text
    bool style_contains_flag(const wstring& expr, const wchar_t* flag)
    {
        return expr.find(flag) != wstring::npos;
    }

    void style_add_flag(wstring& expr, const wchar_t* flag)
    {
        if (style_contains_flag(expr, flag))
            return;

        if (expr.empty())
            expr = flag;
        else
            expr += L" | " + wstring(flag);
    }

    void style_remove_flag(wstring& expr, const wchar_t* flag)
    {
        if (expr.empty())
            return;

        wstring f = flag;

        auto erase_sub = [&](const wstring& what)
            {
                auto pos = expr.find(what);
                if (pos != wstring::npos)
                    expr.erase(pos, what.size());
            };

        erase_sub(L" | " + f);
        erase_sub(f + L" | ");
        erase_sub(f);

        // Clean leftover "||"
        for (;;)
        {
            auto pos = expr.find(L"||");
            if (pos == wstring::npos)
                break;
            expr.erase(pos, 1);
        }

        // Trim spaces and stray '|'
        while (!expr.empty() && (expr.front() == L' ' || expr.front() == L'|'))
            expr.erase(expr.begin());
        while (!expr.empty() && (expr.back() == L' ' || expr.back() == L'|'))
            expr.pop_back();
    }

    void sync_style_checkboxes_from_expr(const wstring& expr)
    {
        if (!g_hStyleChkChild) return;
        g_inStyleUpdate = true;

        auto set_chk = [](HWND h, bool on)
            {
                if (h)
                    SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
            };

        set_chk(g_hStyleChkChild, style_contains_flag(expr, L"WS_CHILD"));
        set_chk(g_hStyleChkVisible, style_contains_flag(expr, L"WS_VISIBLE"));
        set_chk(g_hStyleChkTabstop, style_contains_flag(expr, L"WS_TABSTOP"));
        set_chk(g_hStyleChkBorder, style_contains_flag(expr, L"WS_BORDER"));

        g_inStyleUpdate = false;
    }

    void apply_style_checkbox_change(HWND chk, const wchar_t* flag)
    {
        if (g_selectedIndex < 0 ||
            g_selectedIndex >= static_cast<int>(CurrentControls().size()))
            return;

        auto& c = CurrentControls()[g_selectedIndex];

        const auto checked = (SendMessageW(chk, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (checked)
            style_add_flag(c.styleExpr, flag);
        else
            style_remove_flag(c.styleExpr, flag);

        set_window_text_w(g_hStyleEdit, c.styleExpr);
        sync_style_checkboxes_from_expr(c.styleExpr);
    }

    void SetSelectedIndex(int idx)
    {
        if (idx < -1 || idx >= static_cast<int>(CurrentControls().size()))
            return;

        if (g_drag.active)
            EndDrag();

        g_selectedIndex = idx;
        InvalidateRect(g_hDesign, nullptr, TRUE);
        RefreshPropertyPanel();
        RebuildZOrderTree();
        SyncZOrderSelection();
        SyncHierarchySelection();
        UpdateZOrderButtons();
    }

    int FindControlIndexFromHwnd(HWND hwnd)
    {
        for (int i = 0; i < static_cast<int>(g_hwndControls.size()); ++i)
        {
            if (g_hwndControls[i] == hwnd)
                return i;
        }
        return -1;
    }

    POINT ScreenToDesign(POINT pt)
    {
        MapWindowPoints(HWND_DESKTOP, g_hDesign, &pt, 1);
        return pt;
    }

    POINT ClientToDesign(HWND from, POINT pt)
    {
        MapWindowPoints(from, g_hDesign, &pt, 1);
        return pt;
    }

    RECT SelectedRect()
    {
        RECT rc{};
        if (g_selectedIndex >= 0 && g_selectedIndex < static_cast<int>(CurrentControls().size()))
        {
            if (g_drag.active)
                rc = g_drag.previewRect;
            else
                rc = CurrentControls()[g_selectedIndex].rect;
        }
        return rc;
    }

    int SnapToGrid(int value)
    {
        return ((value + (kGridSize / 2)) / kGridSize) * kGridSize;
    }

    RECT ClampToDesignSurface(const RECT& rc)
    {
        RECT bounds{};
        GetClientRect(g_hDesign, &bounds);

        RECT out = rc;
        const int minW = 4;
        const int minH = 4;

        if (out.right - out.left < minW)
            out.right = out.left + minW;
        if (out.bottom - out.top < minH)
            out.bottom = out.top + minH;

        if (out.left < bounds.left)
        {
            int delta = bounds.left - out.left;
            out.left += delta;
            out.right += delta;
        }
        if (out.top < bounds.top)
        {
            int delta = bounds.top - out.top;
            out.top += delta;
            out.bottom += delta;
        }

        if (out.right > bounds.right)
        {
            int delta = out.right - bounds.right;
            out.right -= delta;
            out.left -= delta;
        }
        if (out.bottom > bounds.bottom)
        {
            int delta = out.bottom - bounds.bottom;
            out.bottom -= delta;
            out.top -= delta;
        }

        return out;
    }

    void RefreshPropertyPanelDebounced(bool force = false)
    {
        const DWORD tick = GetTickCount();
        if (force || tick - g_lastPropRefreshTick >= kPropPanelDebounceMs)
        {
            g_lastPropRefreshTick = tick;
            RefreshPropertyPanel();
        }
    }

    std::array<RECT, 8> BuildHandleRects(const RECT& rc)
    {
        const int hs = kHandleSize;
        const int midX = rc.left + (rc.right - rc.left) / 2;
        const int midY = rc.top + (rc.bottom - rc.top) / 2;

        return std::array<RECT, 8>{
            RECT{ rc.left - hs, rc.top - hs, rc.left + hs, rc.top + hs },                     // TL
            RECT{ midX - hs, rc.top - hs, midX + hs, rc.top + hs },                           // T
            RECT{ rc.right - hs, rc.top - hs, rc.right + hs, rc.top + hs },                  // TR
            RECT{ rc.left - hs, midY - hs, rc.left + hs, midY + hs },                        // L
            RECT{ rc.right - hs, midY - hs, rc.right + hs, midY + hs },                      // R
            RECT{ rc.left - hs, rc.bottom - hs, rc.left + hs, rc.bottom + hs },              // BL
            RECT{ midX - hs, rc.bottom - hs, midX + hs, rc.bottom + hs },                    // B
            RECT{ rc.right - hs, rc.bottom - hs, rc.right + hs, rc.bottom + hs },            // BR
        };
    }

    DragHandle HitTestHandles(const POINT& pt, const RECT& rc)
    {
        auto handles = BuildHandleRects(rc);
        if (PtInRect(&handles[0], pt)) return DragHandle::TopLeft;
        if (PtInRect(&handles[1], pt)) return DragHandle::Top;
        if (PtInRect(&handles[2], pt)) return DragHandle::TopRight;
        if (PtInRect(&handles[3], pt)) return DragHandle::Left;
        if (PtInRect(&handles[4], pt)) return DragHandle::Right;
        if (PtInRect(&handles[5], pt)) return DragHandle::BottomLeft;
        if (PtInRect(&handles[6], pt)) return DragHandle::Bottom;
        if (PtInRect(&handles[7], pt)) return DragHandle::BottomRight;
        return DragHandle::None;
    }

    bool IsOnActiveTabPage(int index)
    {
        if (index < 0 || index >= static_cast<int>(CurrentControls().size()))
            return false;

        int child = index;
        int parent = CurrentControls()[child].parentIndex;

        while (parent >= 0 && parent < static_cast<int>(CurrentControls().size()))
        {
            const auto& parentCtrl = CurrentControls()[parent];
            if (parentCtrl.type == wui::ControlType::Tab)
            {
                const auto& childCtrl = CurrentControls()[child];
                int pageId = childCtrl.tabPageId < 0 ? 0 : childCtrl.tabPageId;

                HWND hTab = (parent < static_cast<int>(g_hwndControls.size()))
                    ? g_hwndControls[parent]
                    : nullptr;

                int sel = hTab ? TabCtrl_GetCurSel(hTab) : 0;
                if (sel < 0)
                    sel = 0;

                if (pageId != sel)
                    return false;
            }

            child = parent;
            parent = parentCtrl.parentIndex;
        }

        return true;
    }

    int HitTestTopmostControl(const POINT& designPt)
    {
        for (int i = static_cast<int>(CurrentControls().size()) - 1; i >= 0; --i)
        {
            const auto& c = CurrentControls()[i];
            if (!PtInRect(&c.rect, designPt))
                continue;

            if (!IsOnActiveTabPage(i))
                continue;

            return i;
        }

        return -1;
    }

    void ApplyRectToControl(int index, const RECT& rc)
    {
        if (index < 0 || index >= static_cast<int>(CurrentControls().size()))
            return;

        auto& c = CurrentControls()[index];
        c.rect = rc;

        if (index >= 0 && index < static_cast<int>(g_hwndControls.size()))
        {
            HWND h = g_hwndControls[index];
            if (h)
            {
                ParentInfo pinfo = GetParentInfoFor(c);
                const int w = c.rect.right - c.rect.left;
                const int hgt = c.rect.bottom - c.rect.top;
                const int relX = c.rect.left - pinfo.rect.left;
                const int relY = c.rect.top - pinfo.rect.top;
                MoveWindow(h, relX, relY, w, hgt, TRUE);

                if (c.type == wui::ControlType::Tab)
                    EnsureTabPageContainers(index);
            }
        }
    }

    void RedrawDesignOverlay()
    {
        if (!g_hDesign)
            return;
        InvalidateRect(g_hDesign, nullptr, TRUE);
        UpdateWindow(g_hDesign);
    }

    bool BeginDrag(const POINT& designPt)
    {
        if (g_selectedIndex < 0 || g_selectedIndex >= static_cast<int>(CurrentControls().size()))
            return false;

        RECT rc = SelectedRect();
        DragHandle h = HitTestHandles(designPt, rc);
        if (h == DragHandle::None && PtInRect(&rc, designPt))
            h = DragHandle::Move;

        if (h == DragHandle::None)
            return false;

        g_drag.active = true;
        g_drag.handle = h;
        g_drag.startPt = designPt;
        g_drag.startRect = rc;
        g_drag.previewRect = rc;

        SetCapture(g_hDesign);
        return true;
    }

    void UpdateDrag(const POINT& designPt)
    {
        if (!g_drag.active || g_selectedIndex < 0 || g_selectedIndex >= static_cast<int>(CurrentControls().size()))
            return;

        RECT rc = g_drag.startRect;
        const int dx = designPt.x - g_drag.startPt.x;
        const int dy = designPt.y - g_drag.startPt.y;

        const int origW = rc.right - rc.left;
        const int origH = rc.bottom - rc.top;

        switch (g_drag.handle)
        {
        case DragHandle::Move:
            OffsetRect(&rc, dx, dy);
            rc.left = SnapToGrid(rc.left);
            rc.top = SnapToGrid(rc.top);
            rc.right = rc.left + origW;
            rc.bottom = rc.top + origH;
            break;
        case DragHandle::Left:
            rc.left += dx;
            break;
        case DragHandle::Right:
            rc.right += dx;
            break;
        case DragHandle::Top:
            rc.top += dy;
            break;
        case DragHandle::Bottom:
            rc.bottom += dy;
            break;
        case DragHandle::TopLeft:
            rc.left += dx; rc.top += dy; break;
        case DragHandle::TopRight:
            rc.right += dx; rc.top += dy; break;
        case DragHandle::BottomLeft:
            rc.left += dx; rc.bottom += dy; break;
        case DragHandle::BottomRight:
            rc.right += dx; rc.bottom += dy; break;
        default:
            break;
        }

        if (g_drag.handle != DragHandle::Move)
        {
            rc.left = SnapToGrid(rc.left);
            rc.top = SnapToGrid(rc.top);
            rc.right = SnapToGrid(rc.right);
            rc.bottom = SnapToGrid(rc.bottom);
        }

        rc = ClampToDesignSurface(rc);
        g_drag.previewRect = rc;
        RedrawDesignOverlay();
    }

    void EndDrag()
    {
        if (!g_drag.active)
            return;

        const RECT finalRect = g_drag.previewRect;
        g_drag = {};
        ReleaseCapture();
        ApplyRectToControl(g_selectedIndex, finalRect);
        RebuildRuntimeControls();
        RefreshPropertyPanelDebounced(true);
        RedrawDesignOverlay();
    }

    RECT ModelRectToClient(const RECT& rc)
    {
        // Model rectangles are stored in design-surface coordinates already, but this helper
        // keeps the transformation explicit and future-proofs against scrollable surfaces.
        RECT out = rc;
        return out;
    }

    void DrawSelectionOverlay(HDC hdc)
    {
        if (!g_hDesign || !hdc || g_selectedIndex < 0 || g_selectedIndex >= static_cast<int>(CurrentControls().size()))
            return;

        const auto& c = CurrentControls()[g_selectedIndex];
        RECT rc = ModelRectToClient(SelectedRect());
        RECT client{};
        GetClientRect(g_hDesign, &client);
        if (!IntersectRect(&rc, &rc, &client))
            return;

        const COLORREF accent = RGB(0, 120, 215);
        const int oldBk = SetBkMode(hdc, TRANSPARENT);
        const int originalRop = SetROP2(hdc, R2_NOTXORPEN);

        HPEN dashPen = CreatePen(PS_DOT, 1, accent);
        HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
        HPEN oldPen = (HPEN)SelectObject(hdc, dashPen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, nullBrush);

        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);

        const int handleRop = SetROP2(hdc, R2_COPYPEN);
        HPEN solidPen = CreatePen(PS_SOLID, 1, accent);
        HBRUSH handleBrush = CreateSolidBrush(accent);
        SelectObject(hdc, solidPen);
        SelectObject(hdc, handleBrush);
        for (const auto& handleRc : BuildHandleRects(rc))
        {
            Rectangle(hdc, handleRc.left, handleRc.top, handleRc.right, handleRc.bottom);
        }
        SetROP2(hdc, handleRop);

        // Alignment crosshairs
        SelectObject(hdc, dashPen);
        SetROP2(hdc, R2_NOTXORPEN);
        const int midX = rc.left + (rc.right - rc.left) / 2;
        const int midY = rc.top + (rc.bottom - rc.top) / 2;
        MoveToEx(hdc, midX, rc.top, nullptr); LineTo(hdc, midX, rc.bottom);
        MoveToEx(hdc, rc.left, midY, nullptr); LineTo(hdc, rc.right, midY);

        // Label with control type to aid focus identification
        std::wstring label = wui::ControlTypeLabel(c.type);
        if (!c.text.empty())
            label += L" | " + c.text;

        SIZE textSize{};
        GetTextExtentPoint32W(hdc, label.c_str(), static_cast<int>(label.size()), &textSize);
        int textX = rc.left;
        int textY = rc.top - textSize.cy - 2;
        if (textY < client.top)
            textY = rc.top + 2;

        RECT textBg{ textX - 2, textY - 1, textX + textSize.cx + 2, textY + textSize.cy + 1 };
        FillRect(hdc, &textBg, (HBRUSH)GetStockObject(WHITE_BRUSH));
        SetTextColor(hdc, accent);
        TextOutW(hdc, textX, textY, label.c_str(), static_cast<int>(label.size()));

        // Restore GDI objects
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        SetBkMode(hdc, oldBk);
        SetROP2(hdc, originalRop);
        DeleteObject(dashPen);
        DeleteObject(solidPen);
        DeleteObject(handleBrush);
    }

    void DrawCreationOverlay(HDC hdc)
    {
        if (!g_hDesign || !hdc || !g_create.drawing)
            return;

        RECT rc = ModelRectToClient(g_create.previewRect);
        RECT client{};
        GetClientRect(g_hDesign, &client);
        if (!IntersectRect(&rc, &rc, &client))
            return;

        const COLORREF accent = RGB(0, 160, 80);
        const int originalRop = SetROP2(hdc, R2_NOTXORPEN);
        HPEN dashPen = CreatePen(PS_DOT, 1, accent);
        HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
        HPEN oldPen = (HPEN)SelectObject(hdc, dashPen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, nullBrush);

        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);

        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        SetROP2(hdc, originalRop);
        DeleteObject(dashPen);
    }
}

// -----------------------------------------------------------------------------
// Z-ORDER LIST PANEL HELPERS
// -----------------------------------------------------------------------------

namespace
{
    bool IsContainerControl(const wui::ControlDef& c)
    {
        return c.isContainer || wui::is_container_type(c.type);
    }

    int NormalizedPageForChild(int parentIndex, int tabPageId)
    {
        if (parentIndex < 0 || parentIndex >= static_cast<int>(CurrentControls().size()))
            return -1;

        if (CurrentControls()[parentIndex].type != wui::ControlType::Tab)
            return -1;

        return NormalizedTabPage(parentIndex, tabPageId);
    }

    std::vector<int> CollectZOrderSiblings(int idx)
    {
        if (idx < 0 || idx >= static_cast<int>(CurrentControls().size()))
            return {};

        const int parentIndex = CurrentControls()[idx].parentIndex;
        const int page = NormalizedPageForChild(parentIndex, CurrentControls()[idx].tabPageId);

        std::vector<int> siblings;
        siblings.reserve(CurrentControls().size());

        for (int i = 0; i < static_cast<int>(CurrentControls().size()); ++i)
        {
            if (CurrentControls()[i].parentIndex != parentIndex)
                continue;

            if (page >= 0)
            {
                if (NormalizedPageForChild(parentIndex, CurrentControls()[i].tabPageId) != page)
                    continue;
            }

            siblings.push_back(i);
        }

        return siblings;
    }

    int IndexInSiblings(int idx, const std::vector<int>& siblings)
    {
        for (size_t i = 0; i < siblings.size(); ++i)
        {
            if (siblings[i] == idx)
                return static_cast<int>(i);
        }
        return -1;
    }

    wstring ZOrderLabelForControl(const wui::ControlDef& c, int idx)
    {
        wstring label = std::format(L"{}: {}", idx, wui::ControlTypeLabel(c.type));
        if (!c.text.empty())
            label += std::format(L" - {}", c.text);
        label += std::format(L" (ID {})", c.id);
        return label;
    }

    void UpdateZOrderButtons()
    {
        auto set_enabled = [](HWND h, bool on)
            {
                if (h) EnableWindow(h, on);
            };

        const int idx = g_selectedIndex;
        const bool hasSel = (idx >= 0 && idx < static_cast<int>(CurrentControls().size()));

        int parentIdx = -1;
        if (hasSel)
            parentIdx = CurrentControls()[idx].parentIndex;

        auto siblings = CollectZOrderSiblings(idx);
        const int pos = hasSel ? IndexInSiblings(idx, siblings) : -1;
        const int last = static_cast<int>(siblings.size()) - 1;

        set_enabled(g_hZBringFront, hasSel && pos >= 0 && pos < last);
        set_enabled(g_hZForward, hasSel && pos >= 0 && pos < last);
        set_enabled(g_hZSendBack, hasSel && pos > 0);
        set_enabled(g_hZBackward, hasSel && pos > 0);
    }

    void SyncZOrderSelection()
    {
        if (!g_hZOrderTree)
            return;

        g_inZTreeUpdate = true;
        if (g_zTreeItems.size() < CurrentControls().size())
            g_zTreeItems.resize(CurrentControls().size(), nullptr);

        HTREEITEM target = nullptr;
        if (g_selectedIndex >= 0 && g_selectedIndex < static_cast<int>(g_zTreeItems.size()))
            target = g_zTreeItems[g_selectedIndex];

        TreeView_SelectItem(g_hZOrderTree, target);
        g_inZTreeUpdate = false;

        UpdateZOrderButtons();
    }

    void RestackAndRefreshSelection()
    {
        ApplyZOrderToWindows();
        RedrawDesignOverlay();
        SyncZOrderSelection();
        SyncHierarchySelection();
    }

    void RebuildZOrderTree()
    {
        if (!g_hZOrderTree)
            return;

        g_inZTreeUpdate = true;
        TreeView_DeleteAllItems(g_hZOrderTree);
        g_zTreeItems.assign(CurrentControls().size(), nullptr);
        g_zTreeNodes.clear();

        const int count = static_cast<int>(CurrentControls().size());
        std::vector<std::vector<int>> children(count);
        std::unordered_map<int, std::map<int, std::vector<int>>> tabPageChildren;
        std::vector<int> roots;

        for (int i = 0; i < count; ++i)
        {
            const int parent = CurrentControls()[i].parentIndex;
            if (parent >= 0 && parent < count)
            {
                if (CurrentControls()[parent].type == wui::ControlType::Tab)
                {
                    int page = NormalizedPageForChild(parent, CurrentControls()[i].tabPageId);
                    if (page < 0) page = 0;
                    tabPageChildren[parent][page].push_back(i);
                }
                else
                {
                    children[parent].push_back(i);
                }
            }
            else
            {
                roots.push_back(i);
            }
        }

        auto addNodeData = [&](int controlIdx, int tabPage, bool isPage)
            {
                g_zTreeNodes.push_back(ZOrderNodeData{ controlIdx, tabPage, isPage });
                return reinterpret_cast<LPARAM>(&g_zTreeNodes.back());
            };

        auto insertControl = [&](auto&& self, int idx, HTREEITEM hParent) -> void
            {
                TVINSERTSTRUCTW tvis{};
                tvis.hParent = hParent ? hParent : TVI_ROOT;
                tvis.hInsertAfter = TVI_LAST;
                wstring label = ZOrderLabelForControl(CurrentControls()[idx], idx);
                tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
                tvis.item.pszText = label.data();
                tvis.item.lParam = addNodeData(idx, -1, false);
                HTREEITEM hItem = TreeView_InsertItem(g_hZOrderTree, &tvis);
                g_zTreeItems[idx] = hItem;

                if (CurrentControls()[idx].type == wui::ControlType::Tab)
                {
                    auto tabIt = tabPageChildren.find(idx);
                    if (tabIt != tabPageChildren.end())
                    {
                        const auto& pages = TabPagesFor(CurrentControls()[idx]);
                        const size_t pageCount = std::max<size_t>(1, pages.size());
                        for (size_t pi = 0; pi < pageCount; ++pi)
                        {
                            auto childrenIt = tabIt->second.find(static_cast<int>(pi));
                            if (childrenIt == tabIt->second.end())
                                continue;

                            const wstring pageName = (pi < pages.size()) ? pages[pi] : std::format(L"Page {}", pi + 1);
                            wstring pageLabel = std::format(L"Page {}: {}", pi, pageName);
                            TVINSERTSTRUCTW pageTvis{};
                            pageTvis.hParent = hItem;
                            pageTvis.hInsertAfter = TVI_LAST;
                            pageTvis.item.mask = TVIF_TEXT | TVIF_PARAM;
                            pageTvis.item.pszText = pageLabel.data();
                            pageTvis.item.lParam = addNodeData(idx, static_cast<int>(pi), true);
                            HTREEITEM hPage = TreeView_InsertItem(g_hZOrderTree, &pageTvis);

                            for (int child : childrenIt->second)
                                self(self, child, hPage);

                            TreeView_Expand(g_hZOrderTree, hPage, TVE_EXPAND);
                        }
                    }
                }
                else
                {
                    for (int child : children[idx])
                        self(self, child, hItem);
                }

                if (IsContainerControl(CurrentControls()[idx]) && hItem)
                    TreeView_Expand(g_hZOrderTree, hItem, TVE_EXPAND);
            };

        for (int root : roots)
            insertControl(insertControl, root, nullptr);

        g_inZTreeUpdate = false;
        SyncZOrderSelection();
    }
}

// Replace this:
// for (int root : roots)
//     insertControl(insertControl, root, nullptr);

// With this workaround for MSVC's E3133 lambda recursion bug:
//struct InsertControlHelper
//{
//    static void insert(
//        const std::function<void(int, HTREEITEM)>& self,
//        int idx, HTREEITEM hParent)
//    {
//        TVINSERTSTRUCTW tvis{};
//        tvis.hParent = hParent ? hParent : TVI_ROOT;
//        tvis.hInsertAfter = TVI_LAST;
//        wstring label = ZOrderLabelForControl(CurrentControls()[idx], idx);
//        tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
//        tvis.item.pszText = label.data();
//        tvis.item.lParam = addNodeData(idx, -1, false);
//        HTREEITEM hItem = TreeView_InsertItem(g_hZOrderTree, &tvis);
//        g_zTreeItems[idx] = hItem;
//
//        if (CurrentControls()[idx].type == wui::ControlType::Tab)
//        {
//            auto tabIt = tabPageChildren.find(idx);
//            if (tabIt != tabPageChildren.end())
//            {
//                const auto& pages = TabPagesFor(CurrentControls()[idx]);
//                const size_t pageCount = std::max<size_t>(1, pages.size());
//                for (size_t pi = 0; pi < pageCount; ++pi)
//                {
//                    auto childrenIt = tabIt->second.find(static_cast<int>(pi));
//                    if (childrenIt == tabIt->second.end())
//                        continue;
//
//                    const wstring pageName = (pi < pages.size()) ? pages[pi] : std::format(L"Page {}", pi + 1);
//                    wstring pageLabel = std::format(L"Page {}: {}", pi, pageName);
//                    TVINSERTSTRUCTW pageTvis{};
//                    pageTvis.hParent = hItem;
//                    pageTvis.hInsertAfter = TVI_LAST;
//                    pageTvis.item.mask = TVIF_TEXT | TVIF_PARAM;
//                    pageTvis.item.pszText = pageLabel.data();
//                    pageTvis.item.lParam = addNodeData(idx, static_cast<int>(pi), true);
//                    HTREEITEM hPage = TreeView_InsertItem(g_hZOrderTree, &pageTvis);
//
//                    for (int child : childrenIt->second)
//                        self(child, hPage);
//
//                    TreeView_Expand(g_hZOrderTree, hPage, TVE_EXPAND);
//                }
//            }
//        }
//        else
//        {
//            for (int child : children[idx])
//                self(child, hItem);
//        }
//
//        if (IsContainerControl(CurrentControls()[idx]) && hItem)
//            TreeView_Expand(g_hZOrderTree, hItem, TVE_EXPAND);
//    }
//};
//
//std::function<void(int, HTREEITEM)> insertControlFn;
//insertControlFn = [&](int idx, HTREEITEM hParent)
//    {
//        InsertControlHelper::insert(insertControlFn, idx, hParent);
//    };
//
//for (int root : roots)
//insertControlFn(root, nullptr);
//
//g_inZTreeUpdate = false;
//SyncZOrderSelection();
//    }
//}


// -----------------------------------------------------------------------------
// HIERARCHY TREE
// -----------------------------------------------------------------------------

namespace
{
    wstring HierarchyLabelForControl(const wui::ControlDef& c, int idx)
    {
        wstring label = std::format(L"{}: {}", idx, wui::ControlTypeLabel(c.type));
        if (!c.text.empty())
            label += std::format(L" - {}", c.text);
        return label;
    }

    void SyncHierarchySelection()
    {
        if (!g_hHierarchyTree)
            return;

        if (g_treeItems.size() < CurrentControls().size())
            g_treeItems.resize(CurrentControls().size(), nullptr);

        g_inTreeUpdate = true;
        HTREEITEM target = nullptr;
        if (g_selectedIndex >= 0 && g_selectedIndex < static_cast<int>(g_treeItems.size()))
            target = g_treeItems[g_selectedIndex];

        TreeView_SelectItem(g_hHierarchyTree, target);
        g_inTreeUpdate = false;
    }

    void RebuildHierarchyTree()
    {
        if (!g_hHierarchyTree)
            return;

        g_inTreeUpdate = true;
        TreeView_DeleteAllItems(g_hHierarchyTree);
        g_treeItems.assign(CurrentControls().size(), nullptr);

        const int count = static_cast<int>(CurrentControls().size());
        std::vector<std::vector<int>> children(count);
        std::vector<int> roots;

        for (int i = 0; i < count; ++i)
        {
            int parent = CurrentControls()[i].parentIndex;
            if (parent >= 0 && parent < count)
                children[parent].push_back(i);
            else
                roots.push_back(i);
        }

        auto insertNode = [&](auto&& self, int idx, HTREEITEM hParent) -> void
            {
                TVINSERTSTRUCTW tvis{};
                tvis.hParent = hParent ? hParent : TVI_ROOT;
                tvis.hInsertAfter = TVI_LAST;
                wstring label = HierarchyLabelForControl(CurrentControls()[idx], idx);
                tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
                tvis.item.pszText = label.data();
                tvis.item.lParam = idx;
                HTREEITEM hItem = TreeView_InsertItem(g_hHierarchyTree, &tvis);
                g_treeItems[idx] = hItem;

                if (IsContainerControl(CurrentControls()[idx]) && hItem)
                    TreeView_Expand(g_hHierarchyTree, hItem, TVE_EXPAND);

                for (int child : children[idx])
                    self(self, child, hItem);
            };

        for (int root : roots)
            insertNode(insertNode, root, nullptr);

        g_inTreeUpdate = false;
        SyncHierarchySelection();
    }
}

// -----------------------------------------------------------------------------
// PROPERTY PANEL
// -----------------------------------------------------------------------------

namespace
{
    constexpr int IDC_ZORDER_TREE = 300;
    constexpr int IDC_ZORDER_BRING_FRONT = 301;
    constexpr int IDC_ZORDER_SEND_BACK = 302;
    constexpr int IDC_ZORDER_FORWARD = 303;
    constexpr int IDC_ZORDER_BACKWARD = 304;
    constexpr int IDC_REPARENT_BTN = 305;

    enum class PropIndex
    {
        X = 0,
        Y,
        W,
        H,
        Text,
        ID
    };

    void RefreshStylePanel(const wui::ControlDef& c)
    {
        if (!g_hStyleEdit) return;

        wstring style = c.styleExpr.empty()
            ? wui::default_style_expr(c.type)
            : c.styleExpr;

        g_inStyleUpdate = true;
        set_window_text_w(g_hStyleEdit, style);
        g_inStyleUpdate = false;

        sync_style_checkboxes_from_expr(style);
    }

    const vector<wstring>& TabPagesFor(const wui::ControlDef& tab)
    {
        static const vector<wstring> kDefaultPages{ L"Page 1", L"Page 2" };
        return tab.tabPages.empty() ? kDefaultPages : tab.tabPages;
    }

    void RefreshTabPageSelector(const wui::ControlDef& c)
    {
        if (!g_hTabPageCombo || !g_hTabPageLabel)
            return;

        bool hasTabParent = false;
        const wui::ControlDef* parent = nullptr;
        if (c.parentIndex >= 0 && c.parentIndex < static_cast<int>(CurrentControls().size()))
        {
            parent = &CurrentControls()[c.parentIndex];
            hasTabParent = parent->type == wui::ControlType::Tab;
        }

        ShowWindow(g_hTabPageCombo, hasTabParent ? SW_SHOW : SW_HIDE);
        ShowWindow(g_hTabPageLabel, hasTabParent ? SW_SHOW : SW_HIDE);

        if (!hasTabParent)
            return;

        const auto& pages = TabPagesFor(*parent);
        g_inTabPageUpdate = true;
        SendMessageW(g_hTabPageCombo, CB_RESETCONTENT, 0, 0);
        for (const auto& p : pages)
            SendMessageW(g_hTabPageCombo, CB_ADDSTRING, 0, (LPARAM)p.c_str());

        int sel = c.tabPageId;
        if (sel < 0 || sel >= static_cast<int>(pages.size()))
            sel = 0;
        SendMessageW(g_hTabPageCombo, CB_SETCURSEL, sel, 0);
        g_inTabPageUpdate = false;
    }

    void RefreshPropertyPanel()
    {
        if (!g_hPropPanel)
            return;

        if (g_selectedIndex < 0 ||
            g_selectedIndex >= static_cast<int>(CurrentControls().size()))
        {
            for (HWND hEdit : g_hPropEdits)
            {
                if (hEdit)
                    set_window_text_w(hEdit, L"");
            }
            if (g_hStyleEdit)
                set_window_text_w(g_hStyleEdit, L"");
            sync_style_checkboxes_from_expr(L"");
            ShowWindow(g_hTabPageCombo, SW_HIDE);
            ShowWindow(g_hTabPageLabel, SW_HIDE);
            return;
        }

        const auto& c = CurrentControls()[g_selectedIndex];

        const int x = c.rect.left;
        const int y = c.rect.top;
        const int w = c.rect.right - c.rect.left;
        const int h = c.rect.bottom - c.rect.top;

        set_edit_int(g_hPropEdits[(int)PropIndex::X], x);
        set_edit_int(g_hPropEdits[(int)PropIndex::Y], y);
        set_edit_int(g_hPropEdits[(int)PropIndex::W], w);
        set_edit_int(g_hPropEdits[(int)PropIndex::H], h);

        if (g_hPropEdits[(int)PropIndex::Text])
            set_window_text_w(g_hPropEdits[(int)PropIndex::Text], c.text);

        set_edit_int(g_hPropEdits[(int)PropIndex::ID], c.id);

        RefreshStylePanel(c);
        RefreshTabPageSelector(c);
    }

    void ApplyPropertyChange(PropIndex idx)
    {
        if (g_selectedIndex < 0 ||
            g_selectedIndex >= static_cast<int>(CurrentControls().size()))
            return;

        auto& c = CurrentControls()[g_selectedIndex];

        const int curX = c.rect.left;
        const int curY = c.rect.top;
        const int curW = c.rect.right - c.rect.left;
        const int curH = c.rect.bottom - c.rect.top;

        auto read_int = [&](PropIndex p, int current)
            {
                const int i = (int)p;
                if (!g_hPropEdits[i])
                    return current;
                return parse_int_or(get_window_text_w(g_hPropEdits[i]), current);
            };

        const int newX = (idx == PropIndex::X) ? read_int(PropIndex::X, curX) : curX;
        const int newY = (idx == PropIndex::Y) ? read_int(PropIndex::Y, curY) : curY;
        const int newW = (idx == PropIndex::W) ? read_int(PropIndex::W, curW) : curW;
        const int newH = (idx == PropIndex::H) ? read_int(PropIndex::H, curH) : curH;

        c.rect.left = newX;
        c.rect.top = newY;
        c.rect.right = newX + std::max(newW, 4);
        c.rect.bottom = newY + std::max(newH, 4);

        if (idx == PropIndex::Text)
        {
            c.text = get_window_text_w(g_hPropEdits[(int)PropIndex::Text]);
        }
        else if (idx == PropIndex::ID)
        {
            c.id = read_int(PropIndex::ID, c.id);
        }

        // Live update HWND if it exists
        if (g_selectedIndex >= 0 &&
            g_selectedIndex < static_cast<int>(g_hwndControls.size()))
        {
            HWND h = g_hwndControls[g_selectedIndex];
            if (h)
            {
                ParentInfo pinfo = GetParentInfoFor(c);
                const int w = c.rect.right - c.rect.left;
                const int hgt = c.rect.bottom - c.rect.top;
                const int relX = c.rect.left - pinfo.rect.left;
                const int relY = c.rect.top - pinfo.rect.top;
                MoveWindow(h, relX, relY, w, hgt, TRUE);

                if (idx == PropIndex::Text)
                    set_window_text_w(h, c.text);

                if (c.type == wui::ControlType::Tab)
                    EnsureTabPageContainers(g_selectedIndex);
            }
        }

        if (idx == PropIndex::Text || idx == PropIndex::ID)
            RebuildZOrderTree();

        RedrawDesignOverlay();
    }
}

// -----------------------------------------------------------------------------
// LIVE CONTROL SUBCLASSING (selection, basic behavior)
// -----------------------------------------------------------------------------

namespace
{
    LRESULT CALLBACK ControlSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        auto it = g_originalProcs.find(hwnd);
        WNDPROC orig = (it != g_originalProcs.end()) ? it->second : DefWindowProcW;

        switch (msg)
        {
        case WM_LBUTTONDOWN:
        {
            // Map HWND back to control index
            for (int i = 0; i < (int)g_hwndControls.size(); ++i)
            {
                if (g_hwndControls[i] == hwnd)
                {
                    POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                    POINT designPt = ClientToDesign(hwnd, pt);
                    if (BeginCreateDrag(designPt))
                        return 0;
                    SetSelectedIndex(i);
                    if (BeginDrag(designPt))
                        return 0;
                    break;
                }
            }
            break;
        }

        case WM_MOUSEMOVE:
        {
            if (g_create.drawing)
            {
                POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                POINT designPt = ClientToDesign(hwnd, pt);
                UpdateCreateDrag(designPt);
                return 0;
            }
            else if (g_drag.active)
            {
                POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                POINT designPt = ClientToDesign(hwnd, pt);
                UpdateDrag(designPt);
                return 0;
            }
            break;
        }

        case WM_LBUTTONUP:
        {
            if (g_create.drawing)
            {
                EndCreateDrag();
                return 0;
            }
            if (g_drag.active)
            {
                EndDrag();
                return 0;
            }
            break;
        }

        case WM_CONTEXTMENU:
        {
            int idx = FindControlIndexFromHwnd(hwnd);
            if (idx >= 0)
                SetSelectedIndex(idx);

            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (pt.x == -1 && pt.y == -1)
            {
                GetCursorPos(&pt);
            }
            else
            {
                ClientToScreen(hwnd, &pt);
            }

            pt = PhysicalScreenToLogical(g_hDesign, pt);

            ShowArrangeContextMenu(pt);
            return 0;
        }

        case WM_MOVE:
        case WM_SIZE:
        case WM_WINDOWPOSCHANGED:
        case WM_SHOWWINDOW:
            RedrawDesignOverlay();
            break;

        case WM_CLOSE:
            // Embedded children should not close in editor
            return 0;
        }

        return CallWindowProcW(orig, hwnd, msg, wp, lp);
    }

    void SubclassControl(HWND h)
    {
        if (!h) return;
        if (g_originalProcs.contains(h))
            return;

        WNDPROC orig = (WNDPROC)GetWindowLongPtrW(h, GWLP_WNDPROC);
        if (!orig) return;

        g_originalProcs[h] = orig;
        SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)ControlSubclassProc);
    }
}

// -----------------------------------------------------------------------------
// RUNTIME CONTROL CREATION
// -----------------------------------------------------------------------------

namespace
{
    HWND GetTabPageContainer(int tabIndex, int pageIndex);
    void UpdateTabPageVisibility(int tabIndex);

    ParentInfo GetParentInfoFor(const wui::ControlDef& c)
    {
        ParentInfo info{};
        info.hwnd = g_hDesign;
        GetClientRect(g_hDesign, &info.rect);

        if (c.parentIndex >= 0 &&
            c.parentIndex < static_cast<int>(CurrentControls().size()))
        {
            const auto& parentDef = CurrentControls()[c.parentIndex];
            info.hwnd = EnsureControlCreated(c.parentIndex);
            info.rect = parentDef.rect;

            if (parentDef.type == wui::ControlType::Tab)
            {
                RECT pageRc = TabPageRectInDesignCoords(parentDef, info.hwnd);
                info.rect = pageRc;

                const int pageIndex = c.tabPageId < 0 ? 0 : c.tabPageId;
                info.hwnd = GetTabPageContainer(c.parentIndex, pageIndex);
            }
        }

        return info;
    }

    DWORD BuildStyleFlags(const wui::ControlDef& c)
    {
        // For preview we always ensure WS_CHILD | WS_VISIBLE;
        // we OR some common flags if they appear in styleExpr.
        DWORD style = WS_CHILD | WS_VISIBLE;

        const wstring& expr = c.styleExpr;

        if (style_contains_flag(expr, L"WS_TABSTOP"))
            style |= WS_TABSTOP;
        if (style_contains_flag(expr, L"ES_AUTOHSCROLL"))
            style |= ES_AUTOHSCROLL;
        if (style_contains_flag(expr, L"WS_BORDER"))
            style |= WS_BORDER;
        if (style_contains_flag(expr, L"LBS_NOTIFY"))
            style |= LBS_NOTIFY;
        if (style_contains_flag(expr, L"BS_GROUPBOX"))
            style |= BS_GROUPBOX;
        if (style_contains_flag(expr, L"BS_AUTOCHECKBOX"))
            style |= BS_AUTOCHECKBOX;
        if (style_contains_flag(expr, L"BS_AUTORADIOBUTTON"))
            style |= BS_AUTORADIOBUTTON;
        if (style_contains_flag(expr, L"CBS_DROPDOWNLIST"))
            style |= CBS_DROPDOWNLIST;
        if (style_contains_flag(expr, L"TBS_AUTOTICKS"))
            style |= TBS_AUTOTICKS;
        if (style_contains_flag(expr, L"LVS_REPORT"))
            style |= LVS_REPORT;

        // Editor semantics: buttons non-interactive (preview only)
        if (c.type == wui::ControlType::Button ||
            c.type == wui::ControlType::Checkbox ||
            c.type == wui::ControlType::Radio ||
            c.type == wui::ControlType::GroupBox)
        {
            style |= WS_DISABLED;
        }

        return style;
    }

    HWND EnsureControlCreated(int index)
    {
        if (index < 0 || index >= (int)CurrentControls().size())
            return g_hDesign;

        // already created?
        if (index < (int)g_hwndControls.size() && g_hwndControls[index])
            return g_hwndControls[index];

        if ((int)g_hwndControls.size() < (int)CurrentControls().size())
            g_hwndControls.resize(CurrentControls().size(), nullptr);

        auto& c = CurrentControls()[index];

        ParentInfo pinfo = GetParentInfoFor(c);
        HWND parent = pinfo.hwnd ? pinfo.hwnd : g_hDesign;
        RECT parentRect = pinfo.rect;

        // *** FIX 1: convert to parent-relative coordinates ***
        int absX = c.rect.left;
        int absY = c.rect.top;
        int absW = c.rect.right - c.rect.left;
        int absH = c.rect.bottom - c.rect.top;

        int relX = absX - parentRect.left;
        int relY = absY - parentRect.top;

        wstring cls = wui::DefaultClassName(c.type);
        DWORD style = BuildStyleFlags(c);

        // *** FIX 2: prevent controls covering everything ***
        style |= WS_CLIPCHILDREN | WS_CLIPSIBLINGS;

        HWND hCtrl = CreateWindowExW(
            0,
            cls.c_str(),
            c.text.c_str(),
            style,
            relX,
            relY,
            std::max(absW, 4),
            std::max(absH, 4),
            parent,
            (HMENU)(INT_PTR)c.id,
            g_hInst,
            nullptr);

        g_hwndControls[index] = hCtrl;
        if (hCtrl) SubclassControl(hCtrl);

        if (c.type == wui::ControlType::Tab)
        {
            if (hCtrl)
            {
                // Ensure we have at least two visible tabs by default
                if (c.tabPages.empty())
                    c.tabPages = { L"Page 1", L"Page 2" };

                TCITEMW tci{};
                tci.mask = TCIF_TEXT;
                TabCtrl_DeleteAllItems(hCtrl);
                for (size_t i = 0; i < c.tabPages.size(); ++i)
                {
                    tci.pszText = const_cast<wchar_t*>(c.tabPages[i].c_str());
                    TabCtrl_InsertItem(hCtrl, static_cast<int>(i), &tci);
                }
                TabCtrl_SetCurSel(hCtrl, 0);
                EnsureTabPageContainers(index);
            }
        }
        else if (c.parentIndex >= 0 &&
            c.parentIndex < static_cast<int>(CurrentControls().size()) &&
            CurrentControls()[c.parentIndex].type == wui::ControlType::Tab)
        {
            // Ensure the parent's tab containers are materialized before children
            EnsureTabPageContainers(c.parentIndex);
        }

        return hCtrl ? hCtrl : parent;
    }

    RECT TabPageRectInDesignCoords(const wui::ControlDef& tabDef, HWND hTab)
    {
        RECT rc{ 0, 0, tabDef.rect.right - tabDef.rect.left, tabDef.rect.bottom - tabDef.rect.top };
        TabCtrl_AdjustRect(hTab, FALSE, &rc);

        RECT pageRect{};
        pageRect.left = tabDef.rect.left + rc.left;
        pageRect.top = tabDef.rect.top + rc.top;
        pageRect.right = pageRect.left + std::max<LONG>(rc.right - rc.left, 4);
        pageRect.bottom = pageRect.top + std::max<LONG>(rc.bottom - rc.top, 4);
        return pageRect;
    }

    void EnsureTabPageContainers(int tabIndex)
    {
        if (tabIndex < 0 || tabIndex >= static_cast<int>(CurrentControls().size()))
            return;

        if (static_cast<size_t>(tabIndex) >= g_hwndControls.size())
            return;

        HWND hTab = g_hwndControls[tabIndex];
        if (!hTab)
            return;

        auto& tabDef = CurrentControls()[tabIndex];
        if (tabDef.tabPages.empty())
            tabDef.tabPages = { L"Page 1", L"Page 2" };

        const size_t pageCount = std::max<size_t>(1, tabDef.tabPages.size());
        auto& containers = g_tabPageContainers[tabIndex];

        RECT pageRectDesign = TabPageRectInDesignCoords(tabDef, hTab);
        const int pageW = pageRectDesign.right - pageRectDesign.left;
        const int pageH = pageRectDesign.bottom - pageRectDesign.top;

        if (containers.size() > pageCount)
        {
            for (size_t i = pageCount; i < containers.size(); ++i)
            {
                if (containers[i] && ::IsWindow(containers[i]))
                    DestroyWindow(containers[i]);
            }
            containers.resize(pageCount);
        }
        else if (containers.size() < pageCount)
        {
            containers.resize(pageCount, nullptr);
        }

        for (size_t i = 0; i < pageCount; ++i)
        {
            HWND hPage = containers[i];
            if (!hPage || !::IsWindow(hPage))
            {
                hPage = CreateWindowExW(
                    0, L"STATIC", nullptr,
                    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                    pageRectDesign.left - tabDef.rect.left,
                    pageRectDesign.top - tabDef.rect.top,
                    pageW, pageH,
                    hTab, nullptr, g_hInst, nullptr);
                containers[i] = hPage;
            }

            MoveWindow(
                hPage,
                pageRectDesign.left - tabDef.rect.left,
                pageRectDesign.top - tabDef.rect.top,
                pageW,
                pageH,
                TRUE);
        }

        UpdateTabPageVisibility(tabIndex);
    }

    HWND GetTabPageContainer(int tabIndex, int pageIndex)
    {
        EnsureTabPageContainers(tabIndex);

        auto it = g_tabPageContainers.find(tabIndex);
        if (it == g_tabPageContainers.end())
            return (tabIndex < static_cast<int>(g_hwndControls.size())) ? g_hwndControls[tabIndex] : g_hDesign;

        if (it->second.empty())
            return (tabIndex < static_cast<int>(g_hwndControls.size())) ? g_hwndControls[tabIndex] : g_hDesign;

        if (pageIndex < 0)
            pageIndex = 0;
        if (pageIndex >= static_cast<int>(it->second.size()))
            pageIndex = static_cast<int>(it->second.size()) - 1;

        return it->second[pageIndex];
    }

    void UpdateTabPageVisibility(int tabIndex)
    {
        auto it = g_tabPageContainers.find(tabIndex);
        if (it == g_tabPageContainers.end())
            return;

        if (static_cast<size_t>(tabIndex) >= g_hwndControls.size())
            return;

        HWND hTab = g_hwndControls[tabIndex];
        if (!hTab)
            return;

        int sel = TabCtrl_GetCurSel(hTab);
        if (sel < 0)
            sel = 0;

        for (size_t i = 0; i < it->second.size(); ++i)
        {
            HWND hPage = it->second[i];
            if (!hPage)
                continue;

            ShowWindow(hPage, (static_cast<int>(i) == sel) ? SW_SHOW : SW_HIDE);
        }

        RedrawDesignOverlay();
    }

    void DestroyRuntimeControls()
    {
        for (auto& kv : g_tabPageContainers)
        {
            for (HWND hPage : kv.second)
            {
                if (hPage && ::IsWindow(hPage))
                    DestroyWindow(hPage);
            }
        }
        g_tabPageContainers.clear();

        for (HWND h : g_hwndControls)
        {
            if (h && ::IsWindow(h))
                DestroyWindow(h);
        }
        g_hwndControls.clear();
        g_originalProcs.clear();
    }

    void ApplyZOrderToWindows()
    {
        if (g_hwndControls.empty())
            return;

        std::unordered_map<HWND, std::vector<HWND>> perParent;
        for (HWND h : g_hwndControls)
        {
            if (!h || !::IsWindow(h))
                continue;
            HWND parent = GetParent(h);
            perParent[parent].push_back(h);
        }

        for (auto& kv : perParent)
        {
            HWND insertAfter = HWND_BOTTOM;
            for (HWND h : kv.second)
            {
                SetWindowPos(h, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                insertAfter = h;
            }
        }
    }

    void RebuildRuntimeControls()
    {
        DestroyRuntimeControls();

        g_hwndControls.resize(CurrentControls().size(), nullptr);

        for (int i = 0; i < (int)CurrentControls().size(); ++i)
            EnsureControlCreated(i);

        ApplyZOrderToWindows();
    }
}

// -----------------------------------------------------------------------------
// EXPORT (C-style Win32 CreateWindowExW)
// -----------------------------------------------------------------------------

namespace
{
    wstring sanitize_identifier(const wstring& s)
    {
        if (s.empty()) return L"ID_CTRL";

        wstring out;
        out.reserve(s.size());

        wchar_t c0 = s[0];
        if (!(std::iswalpha(c0) || c0 == L'_'))
            out.push_back(L'_');
        out.push_back(c0);

        for (size_t i = 1; i < s.size(); ++i)
        {
            wchar_t c = s[i];
            if (std::iswalnum(c) || c == L'_')
                out.push_back(c);
            else
                out.push_back(L'_');
        }
        return out;
    }

    wstring escape_wstring_literal(const wstring& s)
    {
        wstring out;
        out.reserve(s.size());
        for (wchar_t ch : s)
        {
            if (ch == L'\\' || ch == L'"')
                out.push_back(L'\\');
            out.push_back(ch);
        }
        return out;
    }

    wstring BuildExportText()
    {
        if (CurrentControls().empty())
            return L"// No controls defined.\n";

        std::wstring result;

        // ID definitions
        result += L"// Control ID definitions\n";
        std::unordered_set<int> seenIds;
        for (auto const& c : CurrentControls())
        {
            if (seenIds.contains(c.id))
                continue;
            seenIds.insert(c.id);

            wstring idToken = !c.idName.empty()
                ? c.idName
                : (L"ID_CTRL_" + to_wstring(c.id));

            idToken = sanitize_identifier(idToken);
            result += L"#define " + idToken + L" " + to_wstring(c.id) + L"\n";
        }

        result += L"\n// Creation code (C / Win32)\n";
        result += L"// Replace hwndParent with your parent window handle\n";
        result += L"HWND hwndParent = hWnd;\n";
        result += L"HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwndParent, GWLP_HINSTANCE);\n\n";

        for (size_t i = 0; i < CurrentControls().size(); ++i)
        {
            const auto& c = CurrentControls()[i];

            const int x = c.rect.left;
            const int y = c.rect.top;
            const int w = c.rect.right - c.rect.left;
            const int h = c.rect.bottom - c.rect.top;

            wstring idToken = !c.idName.empty()
                ? c.idName
                : (L"ID_CTRL_" + to_wstring(c.id));

            idToken = sanitize_identifier(idToken);

            wstring styleToken = !c.styleExpr.empty()
                ? c.styleExpr
                : wui::default_style_expr(c.type);

            const std::wstring className = wui::DefaultClassName(c.type);
            const std::wstring escapedClass = escape_wstring_literal(className);
            const std::wstring escapedText = escape_wstring_literal(c.text);

            wstring varName = std::format(L"hwnd_{}_{}",
                wui::ControlTypeLabel(c.type),
                i);

            result += L"HWND " + varName + L" = CreateWindowExW(\n";
            result += L"    0,\n";
            result += L"    L\"" + escapedClass + L"\",\n";
            result += L"    L\"" + escapedText + L"\",\n";
            result += L"    " + styleToken + L",\n";
            result += std::format(L"    {}, {}, {}, {},\n", x, y, w, h);
            result += L"    hwndParent,\n";
            result += L"    (HMENU)(INT_PTR)" + idToken + L",\n";
            result += L"    hInst,\n";
            result += L"    NULL);\n";

            if (c.type == wui::ControlType::Tab)
            {
                const auto& pages = TabPagesFor(c);
                result += L"    {\n";
                result += L"        TCITEMW tci{};\n";
                result += L"        tci.mask = TCIF_TEXT;\n";
                for (size_t pi = 0; pi < pages.size(); ++pi)
                {
                    result += L"        tci.pszText = const_cast<wchar_t*>(L\"";
                    result += escape_wstring_literal(pages[pi]);
                    result += L"\");\n";
                    result += std::format(L"        TabCtrl_InsertItem({}, {}, &tci);\n", varName, pi);
                }
                result += L"    }\n";

                result += std::format(L"// TAB_ITEMS id={}: ", c.id);
                for (size_t pi = 0; pi < pages.size(); ++pi)
                {
                    result += pages[pi];
                    if (pi + 1 < pages.size())
                        result += L" | ";
                }
                result += L"\n\n";
            }
            else if (c.parentIndex >= 0 &&
                c.parentIndex < static_cast<int>(CurrentControls().size()) &&
                CurrentControls()[c.parentIndex].type == wui::ControlType::Tab)
            {
                const int page = std::max(0, c.tabPageId);
                result += std::format(L"// TAB_PAGE id={} page={}\n\n", c.id, page);
            }
            else
            {
                result += L"\n";
            }

            int parentId = -1;
            if (c.parentIndex >= 0 && c.parentIndex < static_cast<int>(CurrentControls().size()))
                parentId = CurrentControls()[c.parentIndex].id;

            result += std::format(L"// HIERARCHY child={} parent={}", c.id, parentId);
            if (parentId >= 0 &&
                c.parentIndex >= 0 &&
                c.parentIndex < static_cast<int>(CurrentControls().size()) &&
                CurrentControls()[c.parentIndex].type == wui::ControlType::Tab)
            {
                int page = NormalizedTabPage(c.parentIndex, c.tabPageId);
                if (page < 0)
                    page = 0;
                result += std::format(L" page={}", page);
            }
            result += L"\n\n";
        }

        return result;
    }

    void ExportLayoutToClipboard()
    {
        std::wstring result = BuildExportText();

        const size_t bytes = (result.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!hMem)
            return;

        void* p = GlobalLock(hMem);
        std::memcpy(p, result.c_str(), bytes);
        GlobalUnlock(hMem);

        if (OpenClipboard(g_hMain))
        {
            EmptyClipboard();
            SetClipboardData(CF_UNICODETEXT, hMem);
            CloseClipboard();
        }
        else
        {
            GlobalFree(hMem);
        }

        MessageBoxW(g_hMain,
            L"Exported layout as C / Win32 CreateWindowExW code to clipboard.\n"
            L"(IDs emitted as #define macros.)",
            L"Export",
            MB_OK | MB_ICONINFORMATION);
    }

    void ExportLayoutToFile()
    {
        std::wstring result = BuildExportText();

        wchar_t pathBuf[MAX_PATH] = L"layout_export.c";
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = g_hMain;
        ofn.lpstrFilter = L"C Source\0*.c;*.cpp;*.txt\0All Files\0*.*\0\0";
        ofn.lpstrFile = pathBuf;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = L"Save exported layout";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

        if (!GetSaveFileNameW(&ofn))
            return;

        std::ofstream out(pathBuf, std::ios::binary);
        if (!out)
        {
            MessageBoxW(g_hMain, L"Failed to open file for writing.", L"Export", MB_OK | MB_ICONERROR);
            return;
        }

        // encode as UTF-8
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0,
            result.c_str(),
            (int)result.size(),
            nullptr, 0, nullptr, nullptr);
        std::string utf8;
        utf8.resize(utf8Len);
        WideCharToMultiByte(CP_UTF8, 0,
            result.c_str(),
            (int)result.size(),
            utf8.data(), utf8Len,
            nullptr, nullptr);
        out.write(utf8.data(), utf8.size());
        out.close();

        MessageBoxW(g_hMain, L"Exported layout to file.", L"Export", MB_OK | MB_ICONINFORMATION);
    }
}

// -----------------------------------------------------------------------------
// IMPORT ENTRY (uses parser module, then builds live preview)
// -----------------------------------------------------------------------------

namespace
{
    void ImportFromCppSource()
    {
        wchar_t pathBuf[MAX_PATH] = L"";
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = g_hMain;
        ofn.lpstrFilter = L"C/C++ Files\0*.c;*.cpp;*.cxx;*.ixx;*.hpp;*.h\0All Files\0*.*\0\0";
        ofn.lpstrFile = pathBuf;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        ofn.lpstrTitle = L"Import UI from C/C++ source";

        if (!GetOpenFileNameW(&ofn))
            return;

        std::ifstream in(pathBuf, std::ios::binary);
        if (!in)
        {
            MessageBoxW(g_hMain, L"Failed to open file.", L"Import", MB_OK | MB_ICONERROR);
            return;
        }

        std::string code((std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());

        auto imported = wimp::parse_controls_from_code(code);

        CurrentControls() = std::move(imported);

        for (size_t i = 0; i < CurrentControls().size(); ++i)
        {
            auto& c = CurrentControls()[i];
            if (c.id <= 0)
                c.id = 1000 + static_cast<int>(i);
            if (c.styleExpr.empty())
                c.styleExpr = wui::default_style_expr(c.type);
        }

        g_selectedIndex = CurrentControls().empty() ? -1 : 0;

        RebuildZOrderTree();
        RebuildRuntimeControls();
        RebuildHierarchyTree();
        RefreshPropertyPanel();
        RedrawDesignOverlay();

        wchar_t msg[128];
        std::swprintf(msg, std::size(msg),
            L"Imported %zu controls from source.", CurrentControls().size());
        MessageBoxW(g_hMain, msg, L"Import", MB_OK | MB_ICONINFORMATION);
    }
}

// -----------------------------------------------------------------------------
// MENU + COMMANDS
// -----------------------------------------------------------------------------

namespace
{
    enum : UINT
    {
        IDM_NEW = 1,
        IDM_EXPORT,
        IDM_EXPORT_FILE,
        IDM_IMPORT,
        IDM_TOGGLE_DRAW_MODE,

        IDM_ADD_STATIC,
        IDM_ADD_BUTTON,
        IDM_ADD_EDIT,
        IDM_ADD_CHECK,
        IDM_ADD_RADIO,
        IDM_ADD_GROUP,
        IDM_ADD_LIST,
        IDM_ADD_COMBO,
        IDM_ADD_PROGRESS,
        IDM_ADD_SLIDER,
        IDM_ADD_TAB,

        IDM_ARRANGE_BRING_FRONT,
        IDM_ARRANGE_SEND_BACK,
        IDM_ARRANGE_FORWARD,
        IDM_ARRANGE_BACKWARD
    };

    enum class ZOrderCommand
    {
        BringToFront,
        SendToBack,
        MoveForward,
        MoveBackward,
    };

    bool IsDescendant(int possibleParent, int child)
    {
        if (possibleParent < 0 || possibleParent >= static_cast<int>(CurrentControls().size()))
            return false;

        int cursor = possibleParent;
        while (cursor >= 0 && cursor < static_cast<int>(CurrentControls().size()))
        {
            if (cursor == child)
                return true;
            cursor = CurrentControls()[cursor].parentIndex;
        }
        return false;
    }

    ParentPickResult DefaultParentForNewControl()
    {
        ParentPickResult result{};

        if (g_selectedIndex >= 0 && g_selectedIndex < static_cast<int>(CurrentControls().size()))
        {
            int candidate = g_selectedIndex;
            if (!IsContainerControl(CurrentControls()[candidate]))
                candidate = CurrentControls()[candidate].parentIndex;

            if (candidate >= 0 && candidate < static_cast<int>(CurrentControls().size()) &&
                IsContainerControl(CurrentControls()[candidate]))
            {
                result.parentIndex = candidate;
                if (CurrentControls()[candidate].type == wui::ControlType::Tab)
                {
                    int page = 0;
                    if (candidate < static_cast<int>(g_hwndControls.size()) && g_hwndControls[candidate])
                    {
                        page = TabCtrl_GetCurSel(g_hwndControls[candidate]);
                        if (page < 0) page = 0;
                    }
                    result.tabPageId = page;
                }
            }
        }

        return result;
    }

    std::optional<ParentPickResult> PickParentFromMenu(const ParentPickResult& def, int excludeChild)
    {
        HMENU hMenu = CreatePopupMenu();
        if (!hMenu)
            return std::nullopt;

        std::vector<std::pair<UINT, ParentPickResult>> map;
        auto addChoice = [&](const wstring& text, int parent, int page, bool checked)
            {
                UINT id = 50000 + static_cast<UINT>(map.size());
                AppendMenuW(hMenu, MF_STRING | (checked ? MF_CHECKED : 0), id, text.c_str());
                map.push_back({ id, ParentPickResult{ parent, page } });
            };

        addChoice(L"Top-level (no parent)", -1, -1, def.parentIndex == -1);

        for (int i = 0; i < static_cast<int>(CurrentControls().size()); ++i)
        {
            if (i == excludeChild)
                continue;

            const auto& c = CurrentControls()[i];
            if (!IsContainerControl(c))
                continue;

            if (excludeChild >= 0 && IsDescendant(i, excludeChild))
                continue;

            wstring label = HierarchyLabelForControl(c, i);
            if (c.type == wui::ControlType::Tab)
            {
                HMENU hSub = CreatePopupMenu();
                const auto& pages = TabPagesFor(c);
                for (size_t pi = 0; pi < pages.size(); ++pi)
                {
                    UINT id = 50000 + static_cast<UINT>(map.size());
                    AppendMenuW(hSub, MF_STRING | ((def.parentIndex == i && def.tabPageId == static_cast<int>(pi)) ? MF_CHECKED : 0), id, pages[pi].c_str());
                    map.push_back({ id, ParentPickResult{ i, static_cast<int>(pi) } });
                }

                AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSub, label.c_str());
            }
            else
            {
                addChoice(label, i, -1, def.parentIndex == i);
            }
        }

        POINT pt{};
        GetCursorPos(&pt);
        UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, g_hMain, nullptr);
        DestroyMenu(hMenu);

        if (cmd == 0)
            return std::nullopt;

        for (auto& [id, res] : map)
        {
            if (id == cmd)
                return res;
        }

        return std::nullopt;
    }

    int NormalizedTabPage(int tabIndex, int requested)
    {
        if (tabIndex < 0 || tabIndex >= static_cast<int>(CurrentControls().size()))
            return -1;

        const auto& pages = TabPagesFor(CurrentControls()[tabIndex]);
        if (pages.empty())
            return -1;

        if (requested < 0 || requested >= static_cast<int>(pages.size()))
            return 0;

        return requested;
    }

    bool ApplyParentChoice(int childIdx, const ParentPickResult& choice)
    {
        const int count = static_cast<int>(CurrentControls().size());
        if (childIdx < 0 || childIdx >= count)
            return false;

        if (choice.parentIndex == childIdx)
            return false;

        if (choice.parentIndex >= count)
            return false;

        if (choice.parentIndex >= 0 && IsDescendant(choice.parentIndex, childIdx))
            return false;

        auto& child = CurrentControls()[childIdx];
        child.parentIndex = choice.parentIndex;

        if (choice.parentIndex >= 0 && CurrentControls()[choice.parentIndex].type == wui::ControlType::Tab)
            child.tabPageId = NormalizedTabPage(choice.parentIndex, choice.tabPageId);
        else
            child.tabPageId = -1;

        RebuildRuntimeControls();
        RebuildHierarchyTree();
        RebuildZOrderTree();
        RefreshPropertyPanel();
        RedrawDesignOverlay();
        RestackAndRefreshSelection();
        return true;
    }


    int CreateControlWithRect(wui::ControlType type, RECT rc, const ParentPickResult& parentChoice)
    {
        rc.left = SnapToGrid(rc.left);
        rc.top = SnapToGrid(rc.top);
        rc.right = SnapToGrid(rc.right);
        rc.bottom = SnapToGrid(rc.bottom);
        rc = ClampToDesignSurface(rc);

        wui::ControlDef c{};
        c.type = type;
        c.rect = rc;
        c.text = wui::ControlTypeLabel(type);
        c.id = 1000 + (int)CurrentControls().size();
        c.styleExpr = wui::default_style_expr(type);
        c.className = wui::DefaultClassName(type);
        if (type == wui::ControlType::Tab)
            c.tabPages = { L"Page 1", L"Page 2" };
        c.parentIndex = parentChoice.parentIndex;
        if (c.parentIndex >= 0 && c.parentIndex < static_cast<int>(CurrentControls().size()) &&
            CurrentControls()[c.parentIndex].type == wui::ControlType::Tab)
        {
            c.tabPageId = (parentChoice.tabPageId >= 0) ? parentChoice.tabPageId : 0;
        }
        else
        {
            c.tabPageId = -1;
        }
        c.isContainer = (type == wui::ControlType::GroupBox ||
            type == wui::ControlType::Tab ||
            type == wui::ControlType::ListBox ||
            type == wui::ControlType::ComboBox ||
            type == wui::ControlType::ListView);

        CurrentControls().push_back(std::move(c));
        g_selectedIndex = (int)CurrentControls().size() - 1;

        if (!ApplyParentChoice(g_selectedIndex, parentChoice))
        {
            RebuildRuntimeControls();
            RebuildHierarchyTree();
            RebuildZOrderTree();
            RefreshPropertyPanel();
            RedrawDesignOverlay();
        }

        return g_selectedIndex;
    }

    RECT NormalizeRect(const POINT& a, const POINT& b)
    {
        RECT rc{};
        rc.left = std::min(a.x, b.x);
        rc.top = std::min(a.y, b.y);
        rc.right = std::max(a.x, b.x);
        rc.bottom = std::max(a.y, b.y);
        return rc;
    }

    void StartPendingCreation(wui::ControlType type, const ParentPickResult& parentChoice)
    {
        g_create = {};
        g_create.pending = true;
        g_create.type = type;
        g_create.parentChoice = parentChoice;
        RedrawDesignOverlay();
    }

    void UpdateCreatePreview(const POINT& current)
    {
        if (!g_create.drawing)
            return;

        RECT rc = NormalizeRect(g_create.startPt, current);
        rc.left = SnapToGrid(rc.left);
        rc.top = SnapToGrid(rc.top);
        rc.right = SnapToGrid(rc.right);
        rc.bottom = SnapToGrid(rc.bottom);
        g_create.previewRect = ClampToDesignSurface(rc);
        RedrawDesignOverlay();
    }

    bool BeginCreateDrag(const POINT& designPt)
    {
        if (!g_create.pending)
            return false;

        g_create.drawing = true;
        g_create.startPt = designPt;
        UpdateCreatePreview(designPt);
        SetCapture(g_hDesign);
        return true;
    }

    void UpdateCreateDrag(const POINT& designPt)
    {
        if (!g_create.drawing)
            return;

        UpdateCreatePreview(designPt);
    }

    void EndCreateDrag()
    {
        if (!g_create.drawing)
            return;

        const RECT finalRect = g_create.previewRect;
        const auto type = g_create.type;
        const auto parentChoice = g_create.parentChoice;
        g_create = {};
        ReleaseCapture();
        CreateControlWithRect(type, finalRect, parentChoice);
    }


    void AddControl(wui::ControlType type)
    {
        ParentPickResult parentChoice = DefaultParentForNewControl();
        if (auto picked = PickParentFromMenu(parentChoice, -1))
            parentChoice = *picked;

        RECT rc{ 20, 20, 150, 40 };
        if (!CurrentControls().empty())
        {
            rc = CurrentControls().back().rect;
            OffsetRect(&rc, 10, 10);
        }

        if (g_drawToCreateMode)
        {
            StartPendingCreation(type, parentChoice);
            return;
        }

        CreateControlWithRect(type, rc, parentChoice);
    }

    void ApplyZOrderCommand(ZOrderCommand cmd)
    {
        if (g_selectedIndex < 0 || CurrentControls().empty())
            return;

        const int count = static_cast<int>(CurrentControls().size());
        const int oldIndex = g_selectedIndex;

        std::vector<int> siblings = CollectZOrderSiblings(oldIndex);
        const int oldPos = IndexInSiblings(oldIndex, siblings);
        int newPos = oldPos;

        if (oldPos < 0)
            return;

        switch (cmd)
        {
        case ZOrderCommand::BringToFront: newPos = static_cast<int>(siblings.size()) - 1; break;
        case ZOrderCommand::SendToBack:   newPos = 0;   break;
        case ZOrderCommand::MoveForward:  newPos = std::min(static_cast<int>(siblings.size()) - 1, oldPos + 1); break;
        case ZOrderCommand::MoveBackward: newPos = std::max(0, oldPos - 1); break;
        }

        if (newPos == oldPos)
            return;

        auto newSiblingOrder = siblings;
        int movedId = siblings[oldPos];
        newSiblingOrder.erase(newSiblingOrder.begin() + oldPos);
        newSiblingOrder.insert(newSiblingOrder.begin() + newPos, movedId);

        std::unordered_set<int> siblingSet(siblings.begin(), siblings.end());
        std::vector<int> order(count);
        std::iota(order.begin(), order.end(), 0);

        std::vector<int> rebuiltOrder;
        rebuiltOrder.reserve(count);
        size_t sibCursor = 0;
        for (int idx : order)
        {
            if (siblingSet.contains(idx))
            {
                rebuiltOrder.push_back(newSiblingOrder[sibCursor++]);
            }
            else
            {
                rebuiltOrder.push_back(idx);
            }
        }

        std::vector<int> oldToNew(count, -1);
        for (int i = 0; i < count; ++i)
            oldToNew[rebuiltOrder[i]] = i;

        auto reordered = CurrentControls();
        for (int i = 0; i < count; ++i)
        {
            auto c = CurrentControls()[rebuiltOrder[i]];
            if (c.parentIndex >= 0 && c.parentIndex < count)
                c.parentIndex = oldToNew[c.parentIndex];
            reordered[i] = std::move(c);
        }

        CurrentControls() = std::move(reordered);
        g_selectedIndex = oldToNew[oldIndex];

        RebuildRuntimeControls();
        RebuildZOrderTree();
        RebuildHierarchyTree();
        RefreshPropertyPanel();
        RestackAndRefreshSelection();
    }

    void ShowArrangeContextMenu(POINT screenPt)
    {
        HMENU hMenu = CreatePopupMenu();
        if (!hMenu)
            return;

        const int idx = g_selectedIndex;
        const bool hasSel = (idx >= 0 && idx < static_cast<int>(CurrentControls().size()));

        auto siblings = CollectZOrderSiblings(idx);
        const int pos = hasSel ? IndexInSiblings(idx, siblings) : -1;
        const bool canFront = hasSel && pos >= 0 && pos < static_cast<int>(siblings.size()) - 1;
        const bool canBack = hasSel && pos > 0;

        AppendMenuW(hMenu, MF_STRING | (canFront ? MF_ENABLED : MF_GRAYED), IDM_ARRANGE_BRING_FRONT, L"Bring to Front");
        AppendMenuW(hMenu, MF_STRING | (canFront ? MF_ENABLED : MF_GRAYED), IDM_ARRANGE_FORWARD, L"Move Forward");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING | (canBack ? MF_ENABLED : MF_GRAYED), IDM_ARRANGE_BACKWARD, L"Move Backward");
        AppendMenuW(hMenu, MF_STRING | (canBack ? MF_ENABLED : MF_GRAYED), IDM_ARRANGE_SEND_BACK, L"Send to Back");

        constexpr UINT kMenuFlags = TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RC_ANCHOR;
        TrackPopupMenuEx(hMenu, kMenuFlags, screenPt.x, screenPt.y, g_hDesign ? g_hDesign : g_hMain, nullptr);
        DestroyMenu(hMenu);
    }

    void BuildMenus(HWND hwnd)
    {
        HMENU hMenuBar = CreateMenu();
        HMENU hFile = CreateMenu();
        HMENU hInsert = CreateMenu();
        HMENU hArrange = CreateMenu();

        AppendMenuW(hFile, MF_STRING, IDM_NEW, L"&New");
        AppendMenuW(hFile, MF_STRING, IDM_EXPORT, L"E&xport to Clipboard");
        AppendMenuW(hFile, MF_STRING, IDM_EXPORT_FILE, L"Export to &File...");
        AppendMenuW(hFile, MF_STRING, IDM_IMPORT, L"&Import from C/C++...");

        AppendMenuW(hInsert, MF_STRING | (g_drawToCreateMode ? MF_CHECKED : 0), IDM_TOGGLE_DRAW_MODE, L"Draw to Create");
        AppendMenuW(hInsert, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hInsert, MF_STRING, IDM_ADD_STATIC, L"Static");
        AppendMenuW(hInsert, MF_STRING, IDM_ADD_BUTTON, L"Button");
        AppendMenuW(hInsert, MF_STRING, IDM_ADD_EDIT, L"Edit");
        AppendMenuW(hInsert, MF_STRING, IDM_ADD_CHECK, L"Checkbox");
        AppendMenuW(hInsert, MF_STRING, IDM_ADD_RADIO, L"Radio");
        AppendMenuW(hInsert, MF_STRING, IDM_ADD_GROUP, L"GroupBox");
        AppendMenuW(hInsert, MF_STRING, IDM_ADD_LIST, L"ListBox");
        AppendMenuW(hInsert, MF_STRING, IDM_ADD_COMBO, L"ComboBox");
        AppendMenuW(hInsert, MF_STRING, IDM_ADD_PROGRESS, L"Progress");
        AppendMenuW(hInsert, MF_STRING, IDM_ADD_SLIDER, L"Slider");
        AppendMenuW(hInsert, MF_STRING, IDM_ADD_TAB, L"Tab Control");

        AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hFile, L"&File");
        AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hInsert, L"&Insert");
        AppendMenuW(hArrange, MF_STRING, IDM_ARRANGE_BRING_FRONT, L"Bring to &Front");
        AppendMenuW(hArrange, MF_STRING, IDM_ARRANGE_FORWARD, L"Move &Forward");
        AppendMenuW(hArrange, MF_STRING, IDM_ARRANGE_BACKWARD, L"Move &Backward");
        AppendMenuW(hArrange, MF_STRING, IDM_ARRANGE_SEND_BACK, L"Send to &Back");
        AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hArrange, L"&Arrange");

        SetMenu(hwnd, hMenuBar);
        g_hInsertMenu = hInsert;
    }

    void BuildToolbar(HWND hwnd)
    {
        g_hToolbar = CreateWindowExW(
            0, TOOLBARCLASSNAMEW, nullptr,
            WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NORESIZE | CCS_NODIVIDER,
            0, 0, 0, 0,
            hwnd, nullptr, g_hInst, nullptr);

        if (!g_hToolbar)
            return;

        SendMessageW(g_hToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
        SendMessageW(g_hToolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);
        SendMessageW(g_hToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(90, 24));

        int strId = (int)SendMessageW(g_hToolbar, TB_ADDSTRING, 0, (LPARAM)L"Draw to create");
        TBBUTTON btn{};
        btn.iBitmap = I_IMAGENONE;
        btn.idCommand = IDM_TOGGLE_DRAW_MODE;
        btn.fsState = TBSTATE_ENABLED | (g_drawToCreateMode ? TBSTATE_CHECKED : 0);
        btn.fsStyle = BTNS_CHECK | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
        btn.iString = strId;
        SendMessageW(g_hToolbar, TB_ADDBUTTONS, 1, (LPARAM)&btn);
        SendMessageW(g_hToolbar, TB_AUTOSIZE, 0, 0);
        ShowWindow(g_hToolbar, SW_SHOW);
    }

    void UpdatePlacementModeUI()
    {
        if (g_hInsertMenu)
            CheckMenuItem(g_hInsertMenu, IDM_TOGGLE_DRAW_MODE, MF_BYCOMMAND | (g_drawToCreateMode ? MF_CHECKED : MF_UNCHECKED));

        if (g_hToolbar)
            SendMessageW(g_hToolbar, TB_CHECKBUTTON, IDM_TOGGLE_DRAW_MODE, MAKELONG(g_drawToCreateMode ? TRUE : FALSE, 0));
    }
}

// -----------------------------------------------------------------------------
// PROPERTY PANEL CREATION + LAYOUT
// -----------------------------------------------------------------------------

namespace
{
    void CreatePropertyPanel(HWND hwnd)
    {
        g_hPropPanel = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"STATIC", nullptr,
            WS_CHILD | WS_VISIBLE,
            0, 0, kPropPanelWidth, 0,
            hwnd, nullptr, g_hInst, nullptr);

        const wchar_t* labels[] = { L"X:", L"Y:", L"W:", L"H:", L"Text:", L"ID:" };
        const int labelCount = 6;

        int y = 8;
        for (int i = 0; i < labelCount; ++i)
        {
            CreateWindowExW(
                0, L"STATIC", labels[i],
                WS_CHILD | WS_VISIBLE,
                8, y + 4, 40, 20,
                g_hPropPanel, nullptr, g_hInst, nullptr);

            g_hPropEdits[i] = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                52, y, kPropPanelWidth - 60, 22,
                g_hPropPanel, (HMENU)(INT_PTR)(100 + i), g_hInst, nullptr);

            y += 28;
        }

        // Style label
        CreateWindowExW(
            0, L"STATIC", L"Style:",
            WS_CHILD | WS_VISIBLE,
            8, y + 4, 50, 20,
            g_hPropPanel, nullptr, g_hInst, nullptr);

        g_hStyleEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            60, y, kPropPanelWidth - 68, 22,
            g_hPropPanel, (HMENU)(INT_PTR)200, g_hInst, nullptr);

        y += 28;

        CreateWindowExW(
            0, L"STATIC", L"Common flags:",
            WS_CHILD | WS_VISIBLE,
            8, y + 4, 100, 20,
            g_hPropPanel, nullptr, g_hInst, nullptr);

        y += 24;

        g_hStyleChkChild = CreateWindowExW(
            0, L"BUTTON", L"WS_CHILD",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            8, y, kPropPanelWidth - 16, 20,
            g_hPropPanel, (HMENU)(INT_PTR)210, g_hInst, nullptr);

        y += 22;
        g_hStyleChkVisible = CreateWindowExW(
            0, L"BUTTON", L"WS_VISIBLE",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            8, y, kPropPanelWidth - 16, 20,
            g_hPropPanel, (HMENU)(INT_PTR)211, g_hInst, nullptr);

        y += 22;
        g_hStyleChkTabstop = CreateWindowExW(
            0, L"BUTTON", L"WS_TABSTOP",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            8, y, kPropPanelWidth - 16, 20,
            g_hPropPanel, (HMENU)(INT_PTR)212, g_hInst, nullptr);

        y += 22;
        g_hStyleChkBorder = CreateWindowExW(
            0, L"BUTTON", L"WS_BORDER",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            8, y, kPropPanelWidth - 16, 20,
            g_hPropPanel, (HMENU)(INT_PTR)213, g_hInst, nullptr);

        y += 28;

        g_hTabPageLabel = CreateWindowExW(
            0, L"STATIC", L"Tab Page:",
            WS_CHILD,
            8, y + 4, 70, 20,
            g_hPropPanel, nullptr, g_hInst, nullptr);

        g_hTabPageCombo = CreateWindowExW(
            0, L"COMBOBOX", L"",
            WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST,
            80, y, kPropPanelWidth - 88, 200,
            g_hPropPanel, (HMENU)(INT_PTR)214, g_hInst, nullptr);

        ShowWindow(g_hTabPageLabel, SW_HIDE);
        ShowWindow(g_hTabPageCombo, SW_HIDE);

        y += 32;

        CreateWindowExW(
            0, L"STATIC", L"Hierarchy:",
            WS_CHILD | WS_VISIBLE,
            8, y, 100, 18,
            g_hPropPanel, nullptr, g_hInst, nullptr);

        g_treeTop = y + 20;
        g_hHierarchyTree = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            8, g_treeTop, kPropPanelWidth - 16, 180,
            g_hPropPanel, nullptr, g_hInst, nullptr);

        y = g_treeTop + 188;
        g_hReparentBtn = CreateWindowExW(
            0, L"BUTTON", L"Reparent Selection...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            8, y, kPropPanelWidth - 16, 24,
            g_hPropPanel, (HMENU)(INT_PTR)IDC_REPARENT_BTN, g_hInst, nullptr);

        y += 36;
        CreateWindowExW(
            0, L"STATIC", L"Z-Order:",
            WS_CHILD | WS_VISIBLE,
            8, y, 100, 18,
            g_hPropPanel, nullptr, g_hInst, nullptr);

        g_zListTop = y + 20;
        g_hZOrderTree = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
            8, g_zListTop, kPropPanelWidth - 16, 200,
            g_hPropPanel, (HMENU)(INT_PTR)IDC_ZORDER_TREE, g_hInst, nullptr);

        if (g_hZOrderTree)
            g_originalZTreeProc = (WNDPROC)SetWindowLongPtrW(g_hZOrderTree, GWLP_WNDPROC, (LONG_PTR)ZOrderTreeProc);

        const int btnWidth = (kPropPanelWidth - 24) / 2;
        int btnY = g_zListTop + 208;

        g_hZBringFront = CreateWindowExW(
            0, L"BUTTON", L"Bring to Front",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            8, btnY, btnWidth, 24,
            g_hPropPanel, (HMENU)(INT_PTR)IDC_ZORDER_BRING_FRONT, g_hInst, nullptr);

        g_hZSendBack = CreateWindowExW(
            0, L"BUTTON", L"Send to Back",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            16 + btnWidth, btnY, btnWidth, 24,
            g_hPropPanel, (HMENU)(INT_PTR)IDC_ZORDER_SEND_BACK, g_hInst, nullptr);

        btnY += 28;
        g_hZForward = CreateWindowExW(
            0, L"BUTTON", L"Move Forward",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            8, btnY, btnWidth, 24,
            g_hPropPanel, (HMENU)(INT_PTR)IDC_ZORDER_FORWARD, g_hInst, nullptr);

        g_hZBackward = CreateWindowExW(
            0, L"BUTTON", L"Move Backward",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            16 + btnWidth, btnY, btnWidth, 24,
            g_hPropPanel, (HMENU)(INT_PTR)IDC_ZORDER_BACKWARD, g_hInst, nullptr);

        RebuildZOrderTree();
    }

    void LayoutZOrderPanel()
    {
        if (!g_hPropPanel || !g_hZOrderTree)
            return;

        RECT rc{};
        GetClientRect(g_hPropPanel, &rc);

        const int margin = 8;
        const int buttonHeight = 24;
        const int buttonSpacing = 6;
        const int treeTop = g_treeTop;
        const int listTop = g_zListTop;
        const int minListHeight = 80;
        const int minTreeHeight = 80;
        const int listWidth = kPropPanelWidth - margin * 2;

        int treeHeight = std::max(minTreeHeight,
            listTop - treeTop - buttonHeight - buttonSpacing - margin);
        if (g_hHierarchyTree)
            MoveWindow(g_hHierarchyTree, margin, treeTop, listWidth, treeHeight, TRUE);

        if (g_hReparentBtn)
        {
            int reparentY = treeTop + treeHeight + buttonSpacing;
            MoveWindow(g_hReparentBtn, margin, reparentY, listWidth, buttonHeight, TRUE);
        }

        const int available = rc.bottom - listTop - (buttonHeight * 2 + buttonSpacing * 2 + margin);
        const int listHeight = std::max(minListHeight, available);

        MoveWindow(g_hZOrderTree, margin, listTop, listWidth, listHeight, TRUE);

        int btnY = listTop + listHeight + buttonSpacing;
        const int btnWidth = (kPropPanelWidth - margin * 3) / 2;

        MoveWindow(g_hZBringFront, margin, btnY, btnWidth, buttonHeight, TRUE);
        MoveWindow(g_hZSendBack, margin * 2 + btnWidth, btnY, btnWidth, buttonHeight, TRUE);

        btnY += buttonHeight + buttonSpacing;
        MoveWindow(g_hZForward, margin, btnY, btnWidth, buttonHeight, TRUE);
        MoveWindow(g_hZBackward, margin * 2 + btnWidth, btnY, btnWidth, buttonHeight, TRUE);
    }

    void LayoutChildren(HWND hwnd)
    {
        RECT rcClient{};
        GetClientRect(hwnd, &rcClient);

        int toolbarHeight = 0;
        if (g_hToolbar && ::IsWindow(g_hToolbar))
        {
            SendMessageW(g_hToolbar, TB_AUTOSIZE, 0, 0);
            RECT rcTb{};
            GetWindowRect(g_hToolbar, &rcTb);
            toolbarHeight = rcTb.bottom - rcTb.top;
            MoveWindow(g_hToolbar, 0, 0, rcClient.right, toolbarHeight, TRUE);
        }

        const int propW = kPropPanelWidth;
        const int designRight = rcClient.right - propW;
        const int contentTop = toolbarHeight;

        if (g_hDesign)
        {
            int w = std::max<int>(designRight - 2 * kDesignMargin, 100);
            int h = std::max<int>(rcClient.bottom - contentTop - 2 * kDesignMargin, 100);

            MoveWindow(
                g_hDesign,
                kDesignMargin,
                contentTop + kDesignMargin,
                w,
                h,
                TRUE
            );
        }

        if (g_hPropPanel)
        {
            MoveWindow(
                g_hPropPanel,
                designRight,
                contentTop,
                propW,
                rcClient.bottom - contentTop,
                TRUE
            );

            LayoutZOrderPanel();
        }
    }
}

// -----------------------------------------------------------------------------
// Z-ORDER TREE SUBCLASS
// -----------------------------------------------------------------------------

namespace
{
    LRESULT CALLBACK ZOrderTreeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_MOUSEWHEEL)
        {
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            if (delta != 0)
            {
                if (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT)
                {
                    HTREEITEM sel = TreeView_GetSelection(hwnd);
                    if (sel)
                    {
                        TreeView_Expand(hwnd, sel, (delta > 0) ? TVE_EXPAND : TVE_COLLAPSE);
                    }
                }
                else
                {
                    ApplyZOrderCommand(delta > 0 ? ZOrderCommand::MoveForward : ZOrderCommand::MoveBackward);
                }
                return 0;
            }
        }

        if (g_originalZTreeProc)
            return CallWindowProcW(g_originalZTreeProc, hwnd, msg, wp, lp);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// -----------------------------------------------------------------------------
// DESIGN SURFACE SUBCLASS (notifications, etc.)
// -----------------------------------------------------------------------------

namespace
{
    LRESULT CALLBACK DesignWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_NOTIFY:
        {
            auto hdr = reinterpret_cast<LPNMHDR>(lp);
            if (hdr && hdr->hwndFrom == g_hHierarchyTree && hdr->code == TVN_SELCHANGEDW)
            {
                auto* tv = reinterpret_cast<NMTREEVIEWW*>(lp);
                if (!g_inTreeUpdate)
                    SetSelectedIndex(static_cast<int>(tv->itemNew.lParam));
                return 0;
            }
            if (hdr && hdr->code == TCN_SELCHANGE)
            {
                int idx = FindControlIndexFromHwnd(hdr->hwndFrom);
                if (idx >= 0)
                    UpdateTabPageVisibility(idx);
            }
            break;
        }

        case WM_LBUTTONDOWN:
        {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            POINT designPt = pt;
            if (BeginCreateDrag(designPt))
                return 0;
            if (BeginDrag(designPt))
                return 0;

            int hit = HitTestTopmostControl(designPt);
            SetSelectedIndex(hit);
            RedrawDesignOverlay();
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            if (g_create.drawing)
            {
                POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                UpdateCreateDrag(pt);
                return 0;
            }
            else if (g_drag.active)
            {
                POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                UpdateDrag(pt);
                return 0;
            }
            break;
        }

        case WM_LBUTTONUP:
        {
            if (g_create.drawing)
            {
                EndCreateDrag();
                return 0;
            }
            else if (g_drag.active)
            {
                EndDrag();
                return 0;
            }
            break;
        }

        case WM_CONTEXTMENU:
        {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (pt.x == -1 && pt.y == -1)
            {
                GetCursorPos(&pt);
            }
            else
            {
                MapWindowPoints(hwnd, HWND_DESKTOP, &pt, 1);
            }

            pt = PhysicalScreenToLogical(g_hDesign, pt);

            ShowArrangeContextMenu(pt);
            return 0;
        }

        case WM_MOUSEWHEEL:
        {
            if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL)
            {
                const int delta = GET_WHEEL_DELTA_WPARAM(wp);
                if (delta > 0)
                    ApplyZOrderCommand(ZOrderCommand::MoveForward);
                else if (delta < 0)
                    ApplyZOrderCommand(ZOrderCommand::MoveBackward);
                return 0;
            }
            break;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            if (hdc)
            {
                FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));
                DrawCreationOverlay(hdc);
                DrawSelectionOverlay(hdc);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        }

        if (g_originalDesignProc)
            return CallWindowProcW(g_originalDesignProc, hwnd, msg, wp, lp);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// -----------------------------------------------------------------------------
// MAIN WINDOW PROC
// -----------------------------------------------------------------------------

namespace
{
    LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_CREATE:
        {
            INITCOMMONCONTROLSEX icc{};
            icc.dwSize = sizeof(icc);
            icc.dwICC = ICC_WIN95_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES;
            InitCommonControlsEx(&icc);

            g_hDesign = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"STATIC", nullptr,
                WS_CHILD | WS_VISIBLE,
                kDesignMargin, kDesignMargin, 400, 400,
                hwnd, nullptr, g_hInst, nullptr);

            g_originalDesignProc = (WNDPROC)GetWindowLongPtrW(g_hDesign, GWLP_WNDPROC);
            SetWindowLongPtrW(g_hDesign, GWLP_WNDPROC, (LONG_PTR)DesignWndProc);

            BuildToolbar(hwnd);
            CreatePropertyPanel(hwnd);
            BuildMenus(hwnd);
            UpdatePlacementModeUI();
            LayoutChildren(hwnd);

            return 0;
        }
        case WM_SIZE:
            LayoutChildren(hwnd);
            return 0;

        case WM_NOTIFY:
        {
            auto hdr = reinterpret_cast<LPNMHDR>(lp);
            if (hdr && hdr->hwndFrom == g_hZOrderTree && hdr->code == TVN_SELCHANGEDW)
            {
                auto* tv = reinterpret_cast<NMTREEVIEWW*>(lp);
                auto* node = reinterpret_cast<ZOrderNodeData*>(tv->itemNew.lParam);
                if (!g_inZTreeUpdate && node && node->controlIndex >= 0 && node->controlIndex < static_cast<int>(CurrentControls().size()))
                {
                    SetSelectedIndex(node->controlIndex);
                    RestackAndRefreshSelection();
                }
                return 0;
            }
            break;
        }

        case WM_COMMAND:
        {
            const int id = LOWORD(wp);
            const int code = HIWORD(wp);

            // Property edits
            if (id >= 100 && id < 106 && code == EN_CHANGE)
            {
                const int idx = id - 100;
                ApplyPropertyChange((PropIndex)idx);
                return 0;
            }

            // Style text edit
            if (id == 200 && code == EN_CHANGE && !g_inStyleUpdate)
            {
                if (g_selectedIndex >= 0 &&
                    g_selectedIndex < (int)CurrentControls().size())
                {
                    auto& c = CurrentControls()[g_selectedIndex];
                    c.styleExpr = get_window_text_w(g_hStyleEdit);
                    sync_style_checkboxes_from_expr(c.styleExpr);
                    RebuildRuntimeControls();
                    RedrawDesignOverlay();
                    RefreshPropertyPanel();
                }
                return 0;
            }

            // Style checkboxes
            if (!g_inStyleUpdate)
            {
                switch (id)
                {
                case 210:
                    apply_style_checkbox_change(g_hStyleChkChild, L"WS_CHILD");
                    RebuildRuntimeControls();
                    RedrawDesignOverlay();
                    RefreshPropertyPanel();
                    return 0;
                case 211:
                    apply_style_checkbox_change(g_hStyleChkVisible, L"WS_VISIBLE");
                    RebuildRuntimeControls();
                    RedrawDesignOverlay();
                    RefreshPropertyPanel();
                    return 0;
                case 212:
                    apply_style_checkbox_change(g_hStyleChkTabstop, L"WS_TABSTOP");
                    RebuildRuntimeControls();
                    RedrawDesignOverlay();
                    RefreshPropertyPanel();
                    return 0;
                case 213:
                    apply_style_checkbox_change(g_hStyleChkBorder, L"WS_BORDER");
                    RebuildRuntimeControls();
                    RedrawDesignOverlay();
                    RefreshPropertyPanel();
                    return 0;
                default:
                    break;
                }
            }

            if (id == 214 && code == CBN_SELCHANGE && !g_inTabPageUpdate)
            {
                if (g_selectedIndex >= 0 &&
                    g_selectedIndex < static_cast<int>(CurrentControls().size()))
                {
                    auto& c = CurrentControls()[g_selectedIndex];
                    if (c.parentIndex >= 0 &&
                        c.parentIndex < static_cast<int>(CurrentControls().size()) &&
                        CurrentControls()[c.parentIndex].type == wui::ControlType::Tab)
                    {
                        int sel = static_cast<int>(SendMessageW(g_hTabPageCombo, CB_GETCURSEL, 0, 0));
                        if (sel < 0) sel = 0;
                        c.tabPageId = sel;
                        const int tabIndex = c.parentIndex;
                        RebuildRuntimeControls();
                        if (tabIndex >= 0 && tabIndex < static_cast<int>(g_hwndControls.size()))
                        {
                            HWND hTab = g_hwndControls[tabIndex];
                            if (hTab)
                            {
                                TabCtrl_SetCurSel(hTab, sel);
                                UpdateTabPageVisibility(tabIndex);
                            }
                        }
                        RefreshPropertyPanel();
                    }
                }
                return 0;
            }

            if (code == BN_CLICKED)
            {
                switch (id)
                {
                case IDC_REPARENT_BTN:
                {
                    if (g_selectedIndex >= 0 &&
                        g_selectedIndex < static_cast<int>(CurrentControls().size()))
                    {
                        ParentPickResult def{ CurrentControls()[g_selectedIndex].parentIndex, CurrentControls()[g_selectedIndex].tabPageId };
                        if (auto picked = PickParentFromMenu(def, g_selectedIndex))
                            ApplyParentChoice(g_selectedIndex, *picked);
                    }
                    return 0;
                }
                case IDC_ZORDER_BRING_FRONT: ApplyZOrderCommand(ZOrderCommand::BringToFront); return 0;
                case IDC_ZORDER_SEND_BACK:   ApplyZOrderCommand(ZOrderCommand::SendToBack);   return 0;
                case IDC_ZORDER_FORWARD:     ApplyZOrderCommand(ZOrderCommand::MoveForward);  return 0;
                case IDC_ZORDER_BACKWARD:    ApplyZOrderCommand(ZOrderCommand::MoveBackward); return 0;
                default: break;
                }
            }

            switch (id)
            {
            case IDM_NEW:
                CurrentControls().clear();
                g_selectedIndex = -1;
                RebuildZOrderTree();
                RebuildRuntimeControls();
                RebuildHierarchyTree();
                RefreshPropertyPanel();
                RedrawDesignOverlay();
                return 0;

            case IDM_EXPORT:
                ExportLayoutToClipboard();
                return 0;

            case IDM_EXPORT_FILE:
                ExportLayoutToFile();
                return 0;

            case IDM_IMPORT:
                ImportFromCppSource();
                return 0;

            case IDM_TOGGLE_DRAW_MODE:
                g_drawToCreateMode = !g_drawToCreateMode;
                if (g_create.drawing)
                    ReleaseCapture();
                g_create = {};
                UpdatePlacementModeUI();
                RedrawDesignOverlay();
                return 0;

            case IDM_ADD_STATIC:   AddControl(wui::ControlType::Static);   return 0;
            case IDM_ADD_BUTTON:   AddControl(wui::ControlType::Button);   return 0;
            case IDM_ADD_EDIT:     AddControl(wui::ControlType::Edit);     return 0;
            case IDM_ADD_CHECK:    AddControl(wui::ControlType::Checkbox); return 0;
            case IDM_ADD_RADIO:    AddControl(wui::ControlType::Radio);    return 0;
            case IDM_ADD_GROUP:    AddControl(wui::ControlType::GroupBox); return 0;
            case IDM_ADD_LIST:     AddControl(wui::ControlType::ListBox);  return 0;
            case IDM_ADD_COMBO:    AddControl(wui::ControlType::ComboBox); return 0;
            case IDM_ADD_PROGRESS: AddControl(wui::ControlType::Progress); return 0;
            case IDM_ADD_SLIDER:   AddControl(wui::ControlType::Slider);   return 0;
            case IDM_ADD_TAB:      AddControl(wui::ControlType::Tab);      return 0;

            case IDM_ARRANGE_BRING_FRONT: ApplyZOrderCommand(ZOrderCommand::BringToFront); return 0;
            case IDM_ARRANGE_SEND_BACK:   ApplyZOrderCommand(ZOrderCommand::SendToBack);   return 0;
            case IDM_ARRANGE_FORWARD:     ApplyZOrderCommand(ZOrderCommand::MoveForward);  return 0;
            case IDM_ARRANGE_BACKWARD:    ApplyZOrderCommand(ZOrderCommand::MoveBackward); return 0;

            default:
                break;
            }
            break;
        }

        case WM_DESTROY:
            DestroyRuntimeControls();
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// -----------------------------------------------------------------------------
// PUBLIC ENTRYPOINT
// -----------------------------------------------------------------------------

export int RunWin32UIEditor(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow)
{
    g_hInst = hInst;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"WIN32_UI_EDITOR";

    if (!RegisterClassW(&wc))
        return 0;

    g_hMain = CreateWindowExW(
        0, L"WIN32_UI_EDITOR", L"Win32 UI Editor",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 750,
        nullptr, nullptr, hInst, nullptr);

    if (!g_hMain)
        return 0;

    ShowWindow(g_hMain, nCmdShow);
    UpdateWindow(g_hMain);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
