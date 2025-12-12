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

namespace win32_ui_editor
{
    namespace wui = win32_ui_editor::model;
    namespace wimp = win32_ui_editor::importparser;

    // -------------------------------------------------------------------------
    // EDITOR INTERNALS
    // -------------------------------------------------------------------------

    namespace editor
    {
        using std::wstring;
        using std::vector;

        // ----------------------------
        // State (one place, one owner)
        // ----------------------------

        struct ZOrderNodeData
        {
            int  controlIndex{ -1 };
            int  tabPage{ -1 };
            bool isPageNode{ false };
        };

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

        struct ParentInfo
        {
            HWND hwnd{};
            RECT rect{};
        };

        struct EditorState
        {
            // Instance + main windows
            HINSTANCE hInst{};
            HWND      hMain{};
            HWND      hDesign{};
            HWND      hPropPanel{};
            HWND      hToolbar{};
            HMENU     hInsertMenu{};

            // Property panel controls
            HWND hPropEdits[6]{}; // X,Y,W,H,Text,ID
            HWND hStyleEdit{};
            HWND hStyleChkChild{};
            HWND hStyleChkVisible{};
            HWND hStyleChkTabstop{};
            HWND hStyleChkBorder{};
            HWND hTabPageLabel{};
            HWND hTabPageCombo{};
            HWND hHierarchyTree{};
            HWND hReparentBtn{};
            HWND hZOrderTree{};
            HWND hZBringFront{};
            HWND hZSendBack{};
            HWND hZForward{};
            HWND hZBackward{};

            // Layout anchors (panel)
            int  zListTop{ 0 };
            int  treeTop{ 0 };

            // Update guards
            bool inZTreeUpdate{ false };
            bool inTreeUpdate{ false };
            bool inStyleUpdate{ false };
            bool inTabPageUpdate{ false };

            // Model
            vector<wui::ControlDef> controls;
            int selectedIndex{ -1 };

            // Runtime HWNDs per control index
            vector<HWND> hwndControls;
            std::unordered_map<int, std::vector<HWND>> tabPageContainers;
            std::vector<HTREEITEM> treeItems;
            std::vector<HTREEITEM> zTreeItems;
            std::deque<ZOrderNodeData> zTreeNodes;

            // Subclassing map for live controls
            std::unordered_map<HWND, WNDPROC> originalProcs;
            WNDPROC originalDesignProc{};
            WNDPROC originalZTreeProc{};

            // Interaction
            DragState     drag{};
            CreationState create{};
            bool          drawToCreateMode{ true };

            // Debounce
            DWORD lastPropRefreshTick{ 0 };
        };

        export EditorState g{};

        // ----------------------------
        // Constants
        // ----------------------------

        constexpr int   kDesignMargin = 8;
        constexpr int   kPropPanelWidth = 320;
        constexpr int   kHandleSize = 6;
        constexpr int   kGridSize = 4;
        constexpr DWORD kPropPanelDebounceMs = 60;

        // ----------------------------
        // IDs
        // ----------------------------

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

        // ----------------------------
        // Forward declarations
        // ----------------------------

        // util
        wstring get_window_text_w(HWND h);
        void    set_window_text_w(HWND h, const wstring& s);
        POINT   PhysicalScreenToLogical(HWND hwnd, POINT screenPt);
        void    set_edit_int(HWND h, int value);
        int     parse_int_or(const wstring& s, int fallback);

        bool    style_contains_flag(const wstring& expr, const wchar_t* flag);
        void    style_add_flag(wstring& expr, const wchar_t* flag);
        void    style_remove_flag(wstring& expr, const wchar_t* flag);
        void    sync_style_checkboxes_from_expr(const wstring& expr);
        void    apply_style_checkbox_change(HWND chk, const wchar_t* flag);

        // model access
        vector<wui::ControlDef>& CurrentControls() noexcept;

        // selection + UI refresh
        void SetSelectedIndex(int idx);
        void RefreshPropertyPanel();
        void RefreshPropertyPanelDebounced(bool force = false);

        // design + dragging
        POINT ScreenToDesign(POINT pt);
        POINT ClientToDesign(HWND from, POINT pt);
        RECT  SelectedRect();
        int   SnapToGrid(int value);
        RECT  ClampToDesignSurface(const RECT& rc);
        std::array<RECT, 8> BuildHandleRects(const RECT& rc);
        DragHandle HitTestHandles(const POINT& pt, const RECT& rc);
        bool  BeginDrag(const POINT& designPt);
        void  UpdateDrag(const POINT& designPt);
        void  EndDrag();
        void  RedrawDesignOverlay();
        RECT  ModelRectToClient(const RECT& rc);
        void  DrawSelectionOverlay(HDC hdc);
        void  DrawCreationOverlay(HDC hdc);

        // runtime
        ParentInfo GetParentInfoFor(const wui::ControlDef& c);
        DWORD      BuildStyleFlags(const wui::ControlDef& c);
        HWND       EnsureControlCreated(int index);
        RECT       TabPageRectInDesignCoords(const wui::ControlDef& tabDef, HWND hTab);
        void       EnsureTabPageContainers(int tabIndex);
        void       DestroyRuntimeControls();
        void       ApplyZOrderToWindows();
        void       RebuildRuntimeControls();

        // tab containers
        HWND GetTabPageContainer(int tabIndex, int pageIndex);
        void UpdateTabPageVisibility(int tabIndex);

        // hierarchy + z-order
        bool  IsContainerControl(const wui::ControlDef& c);
        wstring HierarchyLabelForControl(const wui::ControlDef& c, int idx);
        void  SyncHierarchySelection();
        void  RebuildHierarchyTree();

        int   NormalizedTabPage(int tabIndex, int requested);
        int   NormalizedPageForChild(int parentIndex, int tabPageId);
        std::vector<int> CollectZOrderSiblings(int idx);
        int   IndexInSiblings(int idx, const std::vector<int>& siblings);
        wstring ZOrderLabelForControl(const wui::ControlDef& c, int idx);
        void  UpdateZOrderButtons();
        void  SyncZOrderSelection();
        void  RestackAndRefreshSelection();
        void  RebuildZOrderTree();
        void  ApplyZOrderCommand(ZOrderCommand cmd);

        // creation mode
        RECT NormalizeRect(const POINT& a, const POINT& b);
        void StartPendingCreation(wui::ControlType type, const ParentPickResult& parentChoice);
        void UpdateCreatePreview(const POINT& current);
        bool BeginCreateDrag(const POINT& designPt);
        bool UpdateCreateDrag(const POINT& designPt);
        void EndCreateDrag();

        // parenting + menus
        bool  IsDescendant(int possibleParent, int child);
        ParentPickResult DefaultParentForNewControl();
        std::optional<ParentPickResult> PickParentFromMenu(const ParentPickResult& def, int excludeChild);
        bool  ApplyParentChoice(int childIdx, const ParentPickResult& choice);
        int   CreateControlWithRect(wui::ControlType type, RECT rc, const ParentPickResult& parentChoice);
        void  AddControl(wui::ControlType type);
        void  ShowArrangeContextMenu(POINT screenPt);

        // export/import
        wstring sanitize_identifier(const wstring& s);
        wstring escape_wstring_literal(const wstring& s);
        wstring BuildExportText();
        void    ExportLayoutToClipboard();
        void    ExportLayoutToFile();
        void    ImportFromCppSource();

        // UI creation/layout
        void BuildMenus(HWND hwnd);
        void BuildToolbar(HWND hwnd);
        void UpdatePlacementModeUI();
        void CreatePropertyPanel(HWND hwnd);
        void LayoutZOrderPanel();
        void LayoutChildren(HWND hwnd);

        // subclass procs
        LRESULT CALLBACK ControlSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
        void    SubclassControl(HWND h);

        LRESULT CALLBACK ZOrderTreeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
        LRESULT CALLBACK DesignWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
        LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

        // ---------------------------------------------------------------------
        // UTIL
        // ---------------------------------------------------------------------

        vector<wui::ControlDef>& CurrentControls() noexcept { return g.controls; }

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
            set_window_text_w(h, std::to_wstring(value));
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

            for (;;)
            {
                auto pos = expr.find(L"||");
                if (pos == wstring::npos)
                    break;
                expr.erase(pos, 1);
            }

            while (!expr.empty() && (expr.front() == L' ' || expr.front() == L'|'))
                expr.erase(expr.begin());
            while (!expr.empty() && (expr.back() == L' ' || expr.back() == L'|'))
                expr.pop_back();
        }

        void sync_style_checkboxes_from_expr(const wstring& expr)
        {
            if (!g.hStyleChkChild) return;
            g.inStyleUpdate = true;

            auto set_chk = [](HWND h, bool on)
                {
                    if (h)
                        SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
                };

            set_chk(g.hStyleChkChild, style_contains_flag(expr, L"WS_CHILD"));
            set_chk(g.hStyleChkVisible, style_contains_flag(expr, L"WS_VISIBLE"));
            set_chk(g.hStyleChkTabstop, style_contains_flag(expr, L"WS_TABSTOP"));
            set_chk(g.hStyleChkBorder, style_contains_flag(expr, L"WS_BORDER"));

            g.inStyleUpdate = false;
        }

        void apply_style_checkbox_change(HWND chk, const wchar_t* flag)
        {
            if (g.selectedIndex < 0 || g.selectedIndex >= (int)CurrentControls().size())
                return;

            auto& c = CurrentControls()[g.selectedIndex];
            const auto checked = (SendMessageW(chk, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (checked) style_add_flag(c.styleExpr, flag);
            else         style_remove_flag(c.styleExpr, flag);

            set_window_text_w(g.hStyleEdit, c.styleExpr);
            sync_style_checkboxes_from_expr(c.styleExpr);
        }

        // ---------------------------------------------------------------------
        // SELECTION / REFRESH
        // ---------------------------------------------------------------------

        void RefreshPropertyPanelDebounced(bool force)
        {
            const DWORD tick = GetTickCount();
            if (force || tick - g.lastPropRefreshTick >= kPropPanelDebounceMs)
            {
                g.lastPropRefreshTick = tick;
                RefreshPropertyPanel();
            }
        }

        // ---------------------------------------------------------------------
        // COORDINATE HELPERS
        // ---------------------------------------------------------------------

        POINT ScreenToDesign(POINT pt)
        {
            MapWindowPoints(HWND_DESKTOP, g.hDesign, &pt, 1);
            return pt;
        }

        POINT ClientToDesign(HWND from, POINT pt)
        {
            MapWindowPoints(from, g.hDesign, &pt, 1);
            return pt;
        }

        RECT SelectedRect()
        {
            RECT rc{};
            if (g.selectedIndex >= 0 && g.selectedIndex < (int)CurrentControls().size())
            {
                if (g.drag.active) rc = g.drag.previewRect;
                else               rc = CurrentControls()[g.selectedIndex].rect;
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
            GetClientRect(g.hDesign, &bounds);

            RECT out = rc;
            const int minW = 4;
            const int minH = 4;

            if (out.right - out.left < minW) out.right = out.left + minW;
            if (out.bottom - out.top < minH) out.bottom = out.top + minH;

            if (out.left < bounds.left)
            {
                int delta = bounds.left - out.left;
                out.left += delta; out.right += delta;
            }
            if (out.top < bounds.top)
            {
                int delta = bounds.top - out.top;
                out.top += delta; out.bottom += delta;
            }
            if (out.right > bounds.right)
            {
                int delta = out.right - bounds.right;
                out.right -= delta; out.left -= delta;
            }
            if (out.bottom > bounds.bottom)
            {
                int delta = out.bottom - bounds.bottom;
                out.bottom -= delta; out.top -= delta;
            }

            return out;
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

        // ---------------------------------------------------------------------
        // DESIGN OVERLAY
        // ---------------------------------------------------------------------

        RECT ModelRectToClient(const RECT& rc)
        {
            // Stored in design coords already (future-proof hook for scroll/zoom).
            return rc;
        }

        void RedrawDesignOverlay()
        {
            if (!g.hDesign) return;
            InvalidateRect(g.hDesign, nullptr, TRUE);
            UpdateWindow(g.hDesign);
        }

        void DrawSelectionOverlay(HDC hdc)
        {
            if (!g.hDesign || !hdc || g.selectedIndex < 0 || g.selectedIndex >= (int)CurrentControls().size())
                return;

            const auto& c = CurrentControls()[g.selectedIndex];
            RECT rc = ModelRectToClient(SelectedRect());
            RECT client{};
            GetClientRect(g.hDesign, &client);
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

            // Crosshairs
            SelectObject(hdc, dashPen);
            SetROP2(hdc, R2_NOTXORPEN);
            const int midX = rc.left + (rc.right - rc.left) / 2;
            const int midY = rc.top + (rc.bottom - rc.top) / 2;
            MoveToEx(hdc, midX, rc.top, nullptr); LineTo(hdc, midX, rc.bottom);
            MoveToEx(hdc, rc.left, midY, nullptr); LineTo(hdc, rc.right, midY);

            std::wstring label = wui::ControlTypeLabel(c.type);
            if (!c.text.empty())
                label += L" | " + c.text;

            SIZE textSize{};
            GetTextExtentPoint32W(hdc, label.c_str(), (int)label.size(), &textSize);
            int textX = rc.left;
            int textY = rc.top - textSize.cy - 2;
            if (textY < client.top) textY = rc.top + 2;

            RECT textBg{ textX - 2, textY - 1, textX + textSize.cx + 2, textY + textSize.cy + 1 };
            FillRect(hdc, &textBg, (HBRUSH)GetStockObject(WHITE_BRUSH));
            SetTextColor(hdc, accent);
            TextOutW(hdc, textX, textY, label.c_str(), (int)label.size());

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
            if (!g.hDesign || !hdc || !g.create.drawing)
                return;

            RECT rc = ModelRectToClient(g.create.previewRect);
            RECT client{};
            GetClientRect(g.hDesign, &client);
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

        // ---------------------------------------------------------------------
        // HIT TEST (topmost in model order, honoring active tab)
        // ---------------------------------------------------------------------

        int FindControlIndexFromHwnd(HWND hwnd)
        {
            for (int i = 0; i < (int)g.hwndControls.size(); ++i)
                if (g.hwndControls[i] == hwnd)
                    return i;
            return -1;
        }

        const std::vector<std::wstring>& TabPagesFor(const wui::ControlDef& tab)
        {
            static const vector<wstring> kDefaultPages{ L"Page 1", L"Page 2" };
            return tab.tabPages.empty() ? kDefaultPages : tab.tabPages;
        }

        bool IsOnActiveTabPage(int index)
        {
            if (index < 0 || index >= (int)CurrentControls().size())
                return false;

            int child = index;
            int parent = CurrentControls()[child].parentIndex;

            while (parent >= 0 && parent < (int)CurrentControls().size())
            {
                const auto& parentCtrl = CurrentControls()[parent];
                if (parentCtrl.type == wui::ControlType::Tab)
                {
                    const auto& childCtrl = CurrentControls()[child];
                    int pageId = childCtrl.tabPageId < 0 ? 0 : childCtrl.tabPageId;

                    HWND hTab = (parent < (int)g.hwndControls.size())
                        ? g.hwndControls[parent]
                        : nullptr;

                    int sel = hTab ? TabCtrl_GetCurSel(hTab) : 0;
                    if (sel < 0) sel = 0;

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
            for (int i = (int)CurrentControls().size() - 1; i >= 0; --i)
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

        // ---------------------------------------------------------------------
        // DRAGGING
        // ---------------------------------------------------------------------

        void ApplyRectToControl(int index, const RECT& rc)
        {
            if (index < 0 || index >= (int)CurrentControls().size())
                return;

            auto& c = CurrentControls()[index];
            c.rect = rc;

            if (index >= 0 && index < (int)g.hwndControls.size())
            {
                HWND h = g.hwndControls[index];
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

        bool BeginDrag(const POINT& designPt)
        {
            if (g.selectedIndex < 0 || g.selectedIndex >= (int)CurrentControls().size())
                return false;

            RECT rc = SelectedRect();
            DragHandle h = HitTestHandles(designPt, rc);
            if (h == DragHandle::None && PtInRect(&rc, designPt))
                h = DragHandle::Move;

            if (h == DragHandle::None)
                return false;

            g.drag.active = true;
            g.drag.handle = h;
            g.drag.startPt = designPt;
            g.drag.startRect = rc;
            g.drag.previewRect = rc;

            SetCapture(g.hDesign);
            return true;
        }

        void UpdateDrag(const POINT& designPt)
        {
            if (!g.drag.active || g.selectedIndex < 0 || g.selectedIndex >= (int)CurrentControls().size())
                return;

            RECT rc = g.drag.startRect;
            const int dx = designPt.x - g.drag.startPt.x;
            const int dy = designPt.y - g.drag.startPt.y;

            const int origW = rc.right - rc.left;
            const int origH = rc.bottom - rc.top;

            switch (g.drag.handle)
            {
            case DragHandle::Move:
                OffsetRect(&rc, dx, dy);
                rc.left = SnapToGrid(rc.left);
                rc.top = SnapToGrid(rc.top);
                rc.right = rc.left + origW;
                rc.bottom = rc.top + origH;
                break;
            case DragHandle::Left:        rc.left += dx; break;
            case DragHandle::Right:       rc.right += dx; break;
            case DragHandle::Top:         rc.top += dy; break;
            case DragHandle::Bottom:      rc.bottom += dy; break;
            case DragHandle::TopLeft:     rc.left += dx; rc.top += dy; break;
            case DragHandle::TopRight:    rc.right += dx; rc.top += dy; break;
            case DragHandle::BottomLeft:  rc.left += dx; rc.bottom += dy; break;
            case DragHandle::BottomRight: rc.right += dx; rc.bottom += dy; break;
            default: break;
            }

            if (g.drag.handle != DragHandle::Move)
            {
                rc.left = SnapToGrid(rc.left);
                rc.top = SnapToGrid(rc.top);
                rc.right = SnapToGrid(rc.right);
                rc.bottom = SnapToGrid(rc.bottom);
            }

            rc = ClampToDesignSurface(rc);
            g.drag.previewRect = rc;
            RedrawDesignOverlay();
        }

        void EndDrag()
        {
            if (!g.drag.active)
                return;

            const RECT finalRect = g.drag.previewRect;
            g.drag = {};
            ReleaseCapture();
            ApplyRectToControl(g.selectedIndex, finalRect);
            RebuildRuntimeControls();
            RefreshPropertyPanelDebounced(true);
            RedrawDesignOverlay();
        }

        // ---------------------------------------------------------------------
        // PROPERTY PANEL
        // ---------------------------------------------------------------------

        void RefreshStylePanel(const wui::ControlDef& c)
        {
            if (!g.hStyleEdit) return;

            wstring style = c.styleExpr.empty()
                ? wui::default_style_expr(c.type)
                : c.styleExpr;

            g.inStyleUpdate = true;
            set_window_text_w(g.hStyleEdit, style);
            g.inStyleUpdate = false;

            sync_style_checkboxes_from_expr(style);
        }

        void RefreshTabPageSelector(const wui::ControlDef& c)
        {
            if (!g.hTabPageCombo || !g.hTabPageLabel)
                return;

            bool hasTabParent = false;
            const wui::ControlDef* parent = nullptr;
            if (c.parentIndex >= 0 && c.parentIndex < (int)CurrentControls().size())
            {
                parent = &CurrentControls()[c.parentIndex];
                hasTabParent = (parent->type == wui::ControlType::Tab);
            }

            ShowWindow(g.hTabPageCombo, hasTabParent ? SW_SHOW : SW_HIDE);
            ShowWindow(g.hTabPageLabel, hasTabParent ? SW_SHOW : SW_HIDE);

            if (!hasTabParent)
                return;

            const auto& pages = TabPagesFor(*parent);
            g.inTabPageUpdate = true;
            SendMessageW(g.hTabPageCombo, CB_RESETCONTENT, 0, 0);
            for (const auto& p : pages)
                SendMessageW(g.hTabPageCombo, CB_ADDSTRING, 0, (LPARAM)p.c_str());

            int sel = c.tabPageId;
            if (sel < 0 || sel >= (int)pages.size())
                sel = 0;
            SendMessageW(g.hTabPageCombo, CB_SETCURSEL, sel, 0);
            g.inTabPageUpdate = false;
        }

        void RefreshPropertyPanel()
        {
            if (!g.hPropPanel)
                return;

            if (g.selectedIndex < 0 || g.selectedIndex >= (int)CurrentControls().size())
            {
                for (HWND hEdit : g.hPropEdits)
                    if (hEdit) set_window_text_w(hEdit, L"");

                if (g.hStyleEdit) set_window_text_w(g.hStyleEdit, L"");
                sync_style_checkboxes_from_expr(L"");
                ShowWindow(g.hTabPageCombo, SW_HIDE);
                ShowWindow(g.hTabPageLabel, SW_HIDE);
                return;
            }

            const auto& c = CurrentControls()[g.selectedIndex];

            const int x = c.rect.left;
            const int y = c.rect.top;
            const int w = c.rect.right - c.rect.left;
            const int h = c.rect.bottom - c.rect.top;

            set_edit_int(g.hPropEdits[(int)PropIndex::X], x);
            set_edit_int(g.hPropEdits[(int)PropIndex::Y], y);
            set_edit_int(g.hPropEdits[(int)PropIndex::W], w);
            set_edit_int(g.hPropEdits[(int)PropIndex::H], h);

            if (g.hPropEdits[(int)PropIndex::Text])
                set_window_text_w(g.hPropEdits[(int)PropIndex::Text], c.text);

            set_edit_int(g.hPropEdits[(int)PropIndex::ID], c.id);

            RefreshStylePanel(c);
            RefreshTabPageSelector(c);
        }

        void ApplyPropertyChange(PropIndex idx)
        {
            if (g.selectedIndex < 0 || g.selectedIndex >= (int)CurrentControls().size())
                return;

            auto& c = CurrentControls()[g.selectedIndex];

            const int curX = c.rect.left;
            const int curY = c.rect.top;
            const int curW = c.rect.right - c.rect.left;
            const int curH = c.rect.bottom - c.rect.top;

            auto read_int = [&](PropIndex p, int current)
                {
                    const int i = (int)p;
                    if (!g.hPropEdits[i])
                        return current;
                    return parse_int_or(get_window_text_w(g.hPropEdits[i]), current);
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
                c.text = get_window_text_w(g.hPropEdits[(int)PropIndex::Text]);
            }
            else if (idx == PropIndex::ID)
            {
                c.id = read_int(PropIndex::ID, c.id);
            }

            if (g.selectedIndex >= 0 && g.selectedIndex < (int)g.hwndControls.size())
            {
                HWND h = g.hwndControls[g.selectedIndex];
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
                        EnsureTabPageContainers(g.selectedIndex);
                }
            }

            if (idx == PropIndex::Text || idx == PropIndex::ID)
                RebuildZOrderTree();

            RedrawDesignOverlay();
        }

        // ---------------------------------------------------------------------
        // CONTAINER DETECTION
        // ---------------------------------------------------------------------

        bool IsContainerControl(const wui::ControlDef& c)
        {
            return c.isContainer || wui::is_container_type(c.type);
        }

        // ---------------------------------------------------------------------
        // Z-ORDER / HIERARCHY HELPERS
        // ---------------------------------------------------------------------

        wstring HierarchyLabelForControl(const wui::ControlDef& c, int idx)
        {
            wstring label = std::format(L"{}: {}", idx, wui::ControlTypeLabel(c.type));
            if (!c.text.empty())
                label += std::format(L" - {}", c.text);
            return label;
        }

        void SyncHierarchySelection()
        {
            if (!g.hHierarchyTree)
                return;

            if (g.treeItems.size() < CurrentControls().size())
                g.treeItems.resize(CurrentControls().size(), nullptr);

            g.inTreeUpdate = true;
            HTREEITEM target = nullptr;
            if (g.selectedIndex >= 0 && g.selectedIndex < (int)g.treeItems.size())
                target = g.treeItems[g.selectedIndex];

            TreeView_SelectItem(g.hHierarchyTree, target);
            g.inTreeUpdate = false;
        }

        void RebuildHierarchyTree()
        {
            if (!g.hHierarchyTree)
                return;

            g.inTreeUpdate = true;
            TreeView_DeleteAllItems(g.hHierarchyTree);
            g.treeItems.assign(CurrentControls().size(), nullptr);

            const int count = (int)CurrentControls().size();
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

            // iterative DFS to avoid recursive lambda / compiler nonsense
            struct StackItem { int idx; HTREEITEM parent; size_t nextChild; };
            std::vector<StackItem> stack;

            auto insert_one = [&](int idx, HTREEITEM hParent) -> HTREEITEM
                {
                    TVINSERTSTRUCTW tvis{};
                    tvis.hParent = hParent ? hParent : TVI_ROOT;
                    tvis.hInsertAfter = TVI_LAST;
                    wstring label = HierarchyLabelForControl(CurrentControls()[idx], idx);
                    tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
                    tvis.item.pszText = label.data();
                    tvis.item.lParam = idx;
                    HTREEITEM hItem = TreeView_InsertItem(g.hHierarchyTree, &tvis);
                    g.treeItems[idx] = hItem;

                    if (IsContainerControl(CurrentControls()[idx]) && hItem)
                        TreeView_Expand(g.hHierarchyTree, hItem, TVE_EXPAND);

                    return hItem;
                };

            for (int root : roots)
            {
                HTREEITEM hRoot = insert_one(root, nullptr);
                stack.push_back({ root, hRoot, 0 });

                while (!stack.empty())
                {
                    auto& top = stack.back();
                    auto& kids = children[top.idx];

                    if (top.nextChild >= kids.size())
                    {
                        stack.pop_back();
                        continue;
                    }

                    int child = kids[top.nextChild++];
                    HTREEITEM hChild = insert_one(child, top.parent);
                    stack.push_back({ child, hChild, 0 });
                    stack.back().parent = hChild;
                }
            }

            g.inTreeUpdate = false;
            SyncHierarchySelection();
        }

        int NormalizedTabPage(int tabIndex, int requested)
        {
            if (tabIndex < 0 || tabIndex >= (int)CurrentControls().size())
                return -1;

            const auto& pages = TabPagesFor(CurrentControls()[tabIndex]);
            if (pages.empty())
                return -1;

            if (requested < 0 || requested >= (int)pages.size())
                return 0;

            return requested;
        }

        int NormalizedPageForChild(int parentIndex, int tabPageId)
        {
            if (parentIndex < 0 || parentIndex >= (int)CurrentControls().size())
                return -1;
            if (CurrentControls()[parentIndex].type != wui::ControlType::Tab)
                return -1;
            return NormalizedTabPage(parentIndex, tabPageId);
        }

        std::vector<int> CollectZOrderSiblings(int idx)
        {
            if (idx < 0 || idx >= (int)CurrentControls().size())
                return {};

            const int parentIndex = CurrentControls()[idx].parentIndex;
            const int page = NormalizedPageForChild(parentIndex, CurrentControls()[idx].tabPageId);

            std::vector<int> siblings;
            siblings.reserve(CurrentControls().size());

            for (int i = 0; i < (int)CurrentControls().size(); ++i)
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
                if (siblings[i] == idx)
                    return (int)i;
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
            auto set_enabled = [](HWND h, bool on) { if (h) EnableWindow(h, on); };

            const int idx = g.selectedIndex;
            const bool hasSel = (idx >= 0 && idx < (int)CurrentControls().size());

            auto siblings = CollectZOrderSiblings(idx);
            const int pos = hasSel ? IndexInSiblings(idx, siblings) : -1;
            const int last = (int)siblings.size() - 1;

            set_enabled(g.hZBringFront, hasSel && pos >= 0 && pos < last);
            set_enabled(g.hZForward, hasSel && pos >= 0 && pos < last);
            set_enabled(g.hZSendBack, hasSel && pos > 0);
            set_enabled(g.hZBackward, hasSel && pos > 0);
        }

        void SyncZOrderSelection()
        {
            if (!g.hZOrderTree)
                return;

            g.inZTreeUpdate = true;
            if (g.zTreeItems.size() < CurrentControls().size())
                g.zTreeItems.resize(CurrentControls().size(), nullptr);

            HTREEITEM target = nullptr;
            if (g.selectedIndex >= 0 && g.selectedIndex < (int)g.zTreeItems.size())
                target = g.zTreeItems[g.selectedIndex];

            TreeView_SelectItem(g.hZOrderTree, target);
            g.inZTreeUpdate = false;

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
            if (!g.hZOrderTree)
                return;

            g.inZTreeUpdate = true;
            TreeView_DeleteAllItems(g.hZOrderTree);
            g.zTreeItems.assign(CurrentControls().size(), nullptr);
            g.zTreeNodes.clear();

            const int count = (int)CurrentControls().size();
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
                    g.zTreeNodes.push_back(ZOrderNodeData{ controlIdx, tabPage, isPage });
                    return reinterpret_cast<LPARAM>(&g.zTreeNodes.back());
                };

            struct Frame { int idx; HTREEITEM parentItem; size_t nextChild; bool isTab; int tabPage; HTREEITEM tabItem; size_t nextPage; };

            auto insert_item = [&](int idx, HTREEITEM hParent) -> HTREEITEM
                {
                    TVINSERTSTRUCTW tvis{};
                    tvis.hParent = hParent ? hParent : TVI_ROOT;
                    tvis.hInsertAfter = TVI_LAST;
                    wstring label = ZOrderLabelForControl(CurrentControls()[idx], idx);
                    tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
                    tvis.item.pszText = label.data();
                    tvis.item.lParam = addNodeData(idx, -1, false);

                    HTREEITEM hItem = TreeView_InsertItem(g.hZOrderTree, &tvis);
                    g.zTreeItems[idx] = hItem;

                    if (IsContainerControl(CurrentControls()[idx]) && hItem)
                        TreeView_Expand(g.hZOrderTree, hItem, TVE_EXPAND);

                    return hItem;
                };

            auto insert_page_node = [&](int tabIdx, int pageIndex, HTREEITEM tabItem) -> HTREEITEM
                {
                    const auto& pages = TabPagesFor(CurrentControls()[tabIdx]);
                    const wstring pageName = (pageIndex < (int)pages.size()) ? pages[pageIndex] : std::format(L"Page {}", pageIndex + 1);
                    wstring pageLabel = std::format(L"Page {}: {}", pageIndex, pageName);

                    TVINSERTSTRUCTW tvis{};
                    tvis.hParent = tabItem;
                    tvis.hInsertAfter = TVI_LAST;
                    tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
                    tvis.item.pszText = pageLabel.data();
                    tvis.item.lParam = addNodeData(tabIdx, pageIndex, true);

                    HTREEITEM hPage = TreeView_InsertItem(g.hZOrderTree, &tvis);
                    TreeView_Expand(g.hZOrderTree, hPage, TVE_EXPAND);
                    return hPage;
                };

            // iterative tree build
            std::vector<std::pair<int, HTREEITEM>> stack; // control index + tree item
            for (int root : roots)
            {
                HTREEITEM rootItem = insert_item(root, nullptr);
                stack.push_back({ root, rootItem });

                while (!stack.empty())
                {
                    auto [idx, hItem] = stack.back();
                    stack.pop_back();

                    if (CurrentControls()[idx].type == wui::ControlType::Tab)
                    {
                        auto tabIt = tabPageChildren.find(idx);
                        if (tabIt != tabPageChildren.end())
                        {
                            const auto& pages = TabPagesFor(CurrentControls()[idx]);
                            const size_t pageCount = std::max<size_t>(1, pages.size());

                            for (size_t pi = 0; pi < pageCount; ++pi)
                            {
                                auto childrenIt = tabIt->second.find((int)pi);
                                if (childrenIt == tabIt->second.end())
                                    continue;

                                HTREEITEM pageNode = insert_page_node(idx, (int)pi, hItem);

                                // children under page, preserve order -> reverse push
                                auto& kids = childrenIt->second;
                                for (int k = (int)kids.size() - 1; k >= 0; --k)
                                {
                                    int child = kids[(size_t)k];
                                    HTREEITEM childItem = insert_item(child, pageNode);
                                    stack.push_back({ child, childItem });
                                }
                            }
                        }
                    }
                    else
                    {
                        auto& kids = children[idx];
                        for (int k = (int)kids.size() - 1; k >= 0; --k)
                        {
                            int child = kids[(size_t)k];
                            HTREEITEM childItem = insert_item(child, hItem);
                            stack.push_back({ child, childItem });
                        }
                    }
                }
            }

            g.inZTreeUpdate = false;
            SyncZOrderSelection();
        }

        // ---------------------------------------------------------------------
        // SELECTION SETTER (central authority)
        // ---------------------------------------------------------------------

        void SetSelectedIndex(int idx)
        {
            if (idx < -1 || idx >= (int)CurrentControls().size())
                return;

            if (g.drag.active)
                EndDrag();

            g.selectedIndex = idx;
            InvalidateRect(g.hDesign, nullptr, TRUE);
            RefreshPropertyPanel();
            RebuildZOrderTree();
            SyncZOrderSelection();
            SyncHierarchySelection();
            UpdateZOrderButtons();
        }

        // ---------------------------------------------------------------------
        // SUBCLASSING (live controls)
        // ---------------------------------------------------------------------

        LRESULT CALLBACK ControlSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
        {
            auto it = g.originalProcs.find(hwnd);
            WNDPROC orig = (it != g.originalProcs.end()) ? it->second : DefWindowProcW;

            switch (msg)
            {
            case WM_LBUTTONDOWN:
            {
                for (int i = 0; i < (int)g.hwndControls.size(); ++i)
                {
                    if (g.hwndControls[i] == hwnd)
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
                if (g.create.drawing)
                {
                    POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                    UpdateCreateDrag(ClientToDesign(hwnd, pt));
                    return 0;
                }
                if (g.drag.active)
                {
                    POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                    UpdateDrag(ClientToDesign(hwnd, pt));
                    return 0;
                }
                break;
            }

            case WM_LBUTTONUP:
            {
                if (g.create.drawing) { EndCreateDrag(); return 0; }
                if (g.drag.active) { EndDrag();      return 0; }
                break;
            }

            case WM_CONTEXTMENU:
            {
                int idx = FindControlIndexFromHwnd(hwnd);
                if (idx >= 0)
                    SetSelectedIndex(idx);

                POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                if (pt.x == -1 && pt.y == -1) GetCursorPos(&pt);
                else ClientToScreen(hwnd, &pt);

                pt = PhysicalScreenToLogical(g.hDesign, pt);
                ShowArrangeContextMenu(pt);
                return 0;
            }

            case WM_CLOSE:
                return 0; // embedded children must not close in editor
            }

            return CallWindowProcW(orig, hwnd, msg, wp, lp);
        }

        void SubclassControl(HWND h)
        {
            if (!h) return;
            if (g.originalProcs.contains(h))
                return;

            WNDPROC orig = (WNDPROC)GetWindowLongPtrW(h, GWLP_WNDPROC);
            if (!orig) return;

            g.originalProcs[h] = orig;
            SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)ControlSubclassProc);
        }

        // ---------------------------------------------------------------------
        // RUNTIME CONTROL CREATION
        // ---------------------------------------------------------------------

        ParentInfo GetParentInfoFor(const wui::ControlDef& c)
        {
            ParentInfo info{};
            info.hwnd = g.hDesign;
            GetClientRect(g.hDesign, &info.rect);

            if (c.parentIndex >= 0 && c.parentIndex < (int)CurrentControls().size())
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
            DWORD style = WS_CHILD | WS_VISIBLE;

            const wstring& expr = c.styleExpr;

            if (style_contains_flag(expr, L"WS_TABSTOP"))      style |= WS_TABSTOP;
            if (style_contains_flag(expr, L"ES_AUTOHSCROLL"))  style |= ES_AUTOHSCROLL;
            if (style_contains_flag(expr, L"WS_BORDER"))       style |= WS_BORDER;
            if (style_contains_flag(expr, L"LBS_NOTIFY"))      style |= LBS_NOTIFY;
            if (style_contains_flag(expr, L"BS_GROUPBOX"))     style |= BS_GROUPBOX;
            if (style_contains_flag(expr, L"BS_AUTOCHECKBOX")) style |= BS_AUTOCHECKBOX;
            if (style_contains_flag(expr, L"BS_AUTORADIOBUTTON")) style |= BS_AUTORADIOBUTTON;
            if (style_contains_flag(expr, L"CBS_DROPDOWNLIST")) style |= CBS_DROPDOWNLIST;
            if (style_contains_flag(expr, L"TBS_AUTOTICKS"))    style |= TBS_AUTOTICKS;
            if (style_contains_flag(expr, L"LVS_REPORT"))       style |= LVS_REPORT;

            // editor preview semantics
            if (c.type == wui::ControlType::Button ||
                c.type == wui::ControlType::Checkbox ||
                c.type == wui::ControlType::Radio ||
                c.type == wui::ControlType::GroupBox)
            {
                style |= WS_DISABLED;
            }

            style |= WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
            return style;
        }

        HWND EnsureControlCreated(int index)
        {
            if (index < 0 || index >= (int)CurrentControls().size())
                return g.hDesign;

            if (index < (int)g.hwndControls.size() && g.hwndControls[index])
                return g.hwndControls[index];

            if ((int)g.hwndControls.size() < (int)CurrentControls().size())
                g.hwndControls.resize(CurrentControls().size(), nullptr);

            auto& c = CurrentControls()[index];

            ParentInfo pinfo = GetParentInfoFor(c);
            HWND parent = pinfo.hwnd ? pinfo.hwnd : g.hDesign;
            RECT parentRect = pinfo.rect;

            const int absX = c.rect.left;
            const int absY = c.rect.top;
            const int absW = c.rect.right - c.rect.left;
            const int absH = c.rect.bottom - c.rect.top;

            const int relX = absX - parentRect.left;
            const int relY = absY - parentRect.top;

            wstring cls = wui::DefaultClassName(c.type);
            DWORD style = BuildStyleFlags(c);

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
                g.hInst,
                nullptr);

            g.hwndControls[index] = hCtrl;
            if (hCtrl) SubclassControl(hCtrl);

            if (c.type == wui::ControlType::Tab)
            {
                if (hCtrl)
                {
                    if (c.tabPages.empty())
                        c.tabPages = { L"Page 1", L"Page 2" };

                    TCITEMW tci{};
                    tci.mask = TCIF_TEXT;
                    TabCtrl_DeleteAllItems(hCtrl);
                    for (size_t i = 0; i < c.tabPages.size(); ++i)
                    {
                        tci.pszText = const_cast<wchar_t*>(c.tabPages[i].c_str());
                        TabCtrl_InsertItem(hCtrl, (int)i, &tci);
                    }
                    TabCtrl_SetCurSel(hCtrl, 0);
                    EnsureTabPageContainers(index);
                }
            }
            else if (c.parentIndex >= 0 &&
                c.parentIndex < (int)CurrentControls().size() &&
                CurrentControls()[c.parentIndex].type == wui::ControlType::Tab)
            {
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
            if (tabIndex < 0 || tabIndex >= (int)CurrentControls().size())
                return;
            if ((size_t)tabIndex >= g.hwndControls.size())
                return;

            HWND hTab = g.hwndControls[tabIndex];
            if (!hTab) return;

            auto& tabDef = CurrentControls()[tabIndex];
            if (tabDef.tabPages.empty())
                tabDef.tabPages = { L"Page 1", L"Page 2" };

            const size_t pageCount = std::max<size_t>(1, tabDef.tabPages.size());
            auto& containers = g.tabPageContainers[tabIndex];

            RECT pageRectDesign = TabPageRectInDesignCoords(tabDef, hTab);
            const int pageW = pageRectDesign.right - pageRectDesign.left;
            const int pageH = pageRectDesign.bottom - pageRectDesign.top;

            if (containers.size() > pageCount)
            {
                for (size_t i = pageCount; i < containers.size(); ++i)
                    if (containers[i] && IsWindow(containers[i]))
                        DestroyWindow(containers[i]);
                containers.resize(pageCount);
            }
            else if (containers.size() < pageCount)
            {
                containers.resize(pageCount, nullptr);
            }

            for (size_t i = 0; i < pageCount; ++i)
            {
                HWND hPage = containers[i];
                if (!hPage || !IsWindow(hPage))
                {
                    hPage = CreateWindowExW(
                        0, L"STATIC", nullptr,
                        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                        pageRectDesign.left - tabDef.rect.left,
                        pageRectDesign.top - tabDef.rect.top,
                        pageW, pageH,
                        hTab, nullptr, g.hInst, nullptr);
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

            auto it = g.tabPageContainers.find(tabIndex);
            if (it == g.tabPageContainers.end())
                return (tabIndex < (int)g.hwndControls.size()) ? g.hwndControls[tabIndex] : g.hDesign;

            if (it->second.empty())
                return (tabIndex < (int)g.hwndControls.size()) ? g.hwndControls[tabIndex] : g.hDesign;

            if (pageIndex < 0) pageIndex = 0;
            if (pageIndex >= (int)it->second.size())
                pageIndex = (int)it->second.size() - 1;

            return it->second[(size_t)pageIndex];
        }

        void UpdateTabPageVisibility(int tabIndex)
        {
            auto it = g.tabPageContainers.find(tabIndex);
            if (it == g.tabPageContainers.end())
                return;

            if ((size_t)tabIndex >= g.hwndControls.size())
                return;

            HWND hTab = g.hwndControls[tabIndex];
            if (!hTab) return;

            int sel = TabCtrl_GetCurSel(hTab);
            if (sel < 0) sel = 0;

            for (size_t i = 0; i < it->second.size(); ++i)
            {
                HWND hPage = it->second[i];
                if (!hPage) continue;
                ShowWindow(hPage, ((int)i == sel) ? SW_SHOW : SW_HIDE);
            }

            RedrawDesignOverlay();
        }

        void DestroyRuntimeControls()
        {
            for (auto& kv : g.tabPageContainers)
                for (HWND hPage : kv.second)
                    if (hPage && IsWindow(hPage))
                        DestroyWindow(hPage);
            g.tabPageContainers.clear();

            for (HWND h : g.hwndControls)
                if (h && IsWindow(h))
                    DestroyWindow(h);

            g.hwndControls.clear();
            g.originalProcs.clear();
        }

        void ApplyZOrderToWindows()
        {
            if (g.hwndControls.empty())
                return;

            std::unordered_map<HWND, std::vector<HWND>> perParent;
            for (HWND h : g.hwndControls)
            {
                if (!h || !IsWindow(h))
                    continue;
                HWND parent = GetParent(h);
                perParent[parent].push_back(h);
            }

            for (auto& kv : perParent)
            {
                HWND insertAfter = HWND_BOTTOM;
                for (HWND h : kv.second)
                {
                    SetWindowPos(h, insertAfter, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                    insertAfter = h;
                }
            }
        }

        void RebuildRuntimeControls()
        {
            DestroyRuntimeControls();

            g.hwndControls.resize(CurrentControls().size(), nullptr);
            for (int i = 0; i < (int)CurrentControls().size(); ++i)
                EnsureControlCreated(i);

            ApplyZOrderToWindows();
        }

        // ---------------------------------------------------------------------
        // CREATION MODE
        // ---------------------------------------------------------------------

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
            g.create = {};
            g.create.pending = true;
            g.create.type = type;
            g.create.parentChoice = parentChoice;
            RedrawDesignOverlay();
        }

        void UpdateCreatePreview(const POINT& current)
        {
            if (!g.create.drawing)
                return;

            RECT rc = NormalizeRect(g.create.startPt, current);
            rc.left = SnapToGrid(rc.left);
            rc.top = SnapToGrid(rc.top);
            rc.right = SnapToGrid(rc.right);
            rc.bottom = SnapToGrid(rc.bottom);
            g.create.previewRect = ClampToDesignSurface(rc);
            RedrawDesignOverlay();
        }

        bool BeginCreateDrag(const POINT& designPt)
        {
            if (!g.create.pending)
                return false;

            g.create.drawing = true;
            g.create.startPt = designPt;
            UpdateCreatePreview(designPt);
            SetCapture(g.hDesign);
            return true;
        }

        bool UpdateCreateDrag(const POINT& designPt)
        {
            if (!g.create.drawing)
                return false;

            UpdateCreatePreview(designPt);
            return true;
        }

        // forward-defined later
        int CreateControlWithRect(wui::ControlType type, RECT rc, const ParentPickResult& parentChoice);

        void EndCreateDrag()
        {
            if (!g.create.drawing)
                return;

            const RECT finalRect = g.create.previewRect;
            const auto type = g.create.type;
            const auto parentChoice = g.create.parentChoice;

            g.create = {};
            ReleaseCapture();
            CreateControlWithRect(type, finalRect, parentChoice);
        }

        // ---------------------------------------------------------------------
        // PARENTING / MENUS / COMMANDS
        // ---------------------------------------------------------------------

        bool IsDescendant(int possibleParent, int child)
        {
            if (possibleParent < 0 || possibleParent >= (int)CurrentControls().size())
                return false;

            int cursor = possibleParent;
            while (cursor >= 0 && cursor < (int)CurrentControls().size())
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

            if (g.selectedIndex >= 0 && g.selectedIndex < (int)CurrentControls().size())
            {
                int candidate = g.selectedIndex;
                if (!IsContainerControl(CurrentControls()[candidate]))
                    candidate = CurrentControls()[candidate].parentIndex;

                if (candidate >= 0 && candidate < (int)CurrentControls().size() &&
                    IsContainerControl(CurrentControls()[candidate]))
                {
                    result.parentIndex = candidate;
                    if (CurrentControls()[candidate].type == wui::ControlType::Tab)
                    {
                        int page = 0;
                        if (candidate < (int)g.hwndControls.size() && g.hwndControls[candidate])
                        {
                            page = TabCtrl_GetCurSel(g.hwndControls[candidate]);
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
                    UINT id = 50000 + (UINT)map.size();
                    AppendMenuW(hMenu, MF_STRING | (checked ? MF_CHECKED : 0), id, text.c_str());
                    map.push_back({ id, ParentPickResult{ parent, page } });
                };

            addChoice(L"Top-level (no parent)", -1, -1, def.parentIndex == -1);

            for (int i = 0; i < (int)CurrentControls().size(); ++i)
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
                        UINT id = 50000 + (UINT)map.size();
                        AppendMenuW(hSub,
                            MF_STRING | ((def.parentIndex == i && def.tabPageId == (int)pi) ? MF_CHECKED : 0),
                            id, pages[pi].c_str());
                        map.push_back({ id, ParentPickResult{ i, (int)pi } });
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
            UINT cmd = TrackPopupMenu(hMenu,
                TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_LEFTALIGN,
                pt.x, pt.y, 0, g.hMain, nullptr);

            DestroyMenu(hMenu);

            if (cmd == 0)
                return std::nullopt;

            for (auto& [id, res] : map)
                if (id == cmd)
                    return res;

            return std::nullopt;
        }

        bool ApplyParentChoice(int childIdx, const ParentPickResult& choice)
        {
            const int count = (int)CurrentControls().size();
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
            if (c.parentIndex >= 0 && c.parentIndex < (int)CurrentControls().size() &&
                CurrentControls()[c.parentIndex].type == wui::ControlType::Tab)
            {
                c.tabPageId = (parentChoice.tabPageId >= 0) ? parentChoice.tabPageId : 0;
            }
            else
            {
                c.tabPageId = -1;
            }

            c.isContainer =
                (type == wui::ControlType::GroupBox ||
                    type == wui::ControlType::Tab ||
                    type == wui::ControlType::ListBox ||
                    type == wui::ControlType::ComboBox ||
                    type == wui::ControlType::ListView);

            CurrentControls().push_back(std::move(c));
            g.selectedIndex = (int)CurrentControls().size() - 1;

            if (!ApplyParentChoice(g.selectedIndex, parentChoice))
            {
                RebuildRuntimeControls();
                RebuildHierarchyTree();
                RebuildZOrderTree();
                RefreshPropertyPanel();
                RedrawDesignOverlay();
            }

            return g.selectedIndex;
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

            if (g.drawToCreateMode)
            {
                StartPendingCreation(type, parentChoice);
                return;
            }

            CreateControlWithRect(type, rc, parentChoice);
        }

        void ApplyZOrderCommand(ZOrderCommand cmd)
        {
            if (g.selectedIndex < 0 || CurrentControls().empty())
                return;

            const int count = (int)CurrentControls().size();
            const int oldIndex = g.selectedIndex;

            std::vector<int> siblings = CollectZOrderSiblings(oldIndex);
            const int oldPos = IndexInSiblings(oldIndex, siblings);
            int newPos = oldPos;

            if (oldPos < 0)
                return;

            switch (cmd)
            {
            case ZOrderCommand::BringToFront: newPos = (int)siblings.size() - 1; break;
            case ZOrderCommand::SendToBack:   newPos = 0; break;
            case ZOrderCommand::MoveForward:  newPos = std::min((int)siblings.size() - 1, oldPos + 1); break;
            case ZOrderCommand::MoveBackward: newPos = std::max(0, oldPos - 1); break;
            }

            if (newPos == oldPos)
                return;

            auto newSiblingOrder = siblings;
            int movedId = siblings[(size_t)oldPos];
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
                    rebuiltOrder.push_back(newSiblingOrder[sibCursor++]);
                else
                    rebuiltOrder.push_back(idx);
            }

            std::vector<int> oldToNew(count, -1);
            for (int i = 0; i < count; ++i)
                oldToNew[rebuiltOrder[(size_t)i]] = i;

            auto reordered = CurrentControls();
            for (int i = 0; i < count; ++i)
            {
                auto c = CurrentControls()[rebuiltOrder[(size_t)i]];
                if (c.parentIndex >= 0 && c.parentIndex < count)
                    c.parentIndex = oldToNew[c.parentIndex];
                reordered[(size_t)i] = std::move(c);
            }

            CurrentControls() = std::move(reordered);
            g.selectedIndex = oldToNew[oldIndex];

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

            const int idx = g.selectedIndex;
            const bool hasSel = (idx >= 0 && idx < (int)CurrentControls().size());

            auto siblings = CollectZOrderSiblings(idx);
            const int pos = hasSel ? IndexInSiblings(idx, siblings) : -1;
            const bool canFront = hasSel && pos >= 0 && pos < (int)siblings.size() - 1;
            const bool canBack = hasSel && pos > 0;

            AppendMenuW(hMenu, MF_STRING | (canFront ? MF_ENABLED : MF_GRAYED), IDM_ARRANGE_BRING_FRONT, L"Bring to Front");
            AppendMenuW(hMenu, MF_STRING | (canFront ? MF_ENABLED : MF_GRAYED), IDM_ARRANGE_FORWARD, L"Move Forward");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING | (canBack ? MF_ENABLED : MF_GRAYED), IDM_ARRANGE_BACKWARD, L"Move Backward");
            AppendMenuW(hMenu, MF_STRING | (canBack ? MF_ENABLED : MF_GRAYED), IDM_ARRANGE_SEND_BACK, L"Send to Back");

            constexpr UINT kMenuFlags = TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RC_ANCHOR;
            TrackPopupMenuEx(hMenu, kMenuFlags, screenPt.x, screenPt.y, g.hDesign ? g.hDesign : g.hMain, nullptr);
            DestroyMenu(hMenu);
        }

        // ---------------------------------------------------------------------
        // EXPORT (CreateWindowExW code)
        // ---------------------------------------------------------------------

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

            result += L"// Control ID definitions\n";
            std::unordered_set<int> seenIds;
            for (auto const& c : CurrentControls())
            {
                if (seenIds.contains(c.id))
                    continue;
                seenIds.insert(c.id);

                wstring idToken = !c.idName.empty()
                    ? c.idName
                    : (L"ID_CTRL_" + std::to_wstring(c.id));

                idToken = sanitize_identifier(idToken);
                result += L"#define " + idToken + L" " + std::to_wstring(c.id) + L"\n";
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
                    : (L"ID_CTRL_" + std::to_wstring(c.id));

                idToken = sanitize_identifier(idToken);

                wstring styleToken = !c.styleExpr.empty()
                    ? c.styleExpr
                    : wui::default_style_expr(c.type);

                const std::wstring className = wui::DefaultClassName(c.type);
                const std::wstring escapedClass = escape_wstring_literal(className);
                const std::wstring escapedText = escape_wstring_literal(c.text);

                wstring varName = std::format(L"hwnd_{}_{}",
                    wui::ControlTypeLabel(c.type),
                    (int)i);

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
                        result += std::format(L"        TabCtrl_InsertItem({}, {}, &tci);\n", varName, (int)pi);
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
                    c.parentIndex < (int)CurrentControls().size() &&
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
                if (c.parentIndex >= 0 && c.parentIndex < (int)CurrentControls().size())
                    parentId = CurrentControls()[c.parentIndex].id;

                result += std::format(L"// HIERARCHY child={} parent={}", c.id, parentId);
                if (parentId >= 0 &&
                    c.parentIndex >= 0 &&
                    c.parentIndex < (int)CurrentControls().size() &&
                    CurrentControls()[c.parentIndex].type == wui::ControlType::Tab)
                {
                    int page = NormalizedTabPage(c.parentIndex, c.tabPageId);
                    if (page < 0) page = 0;
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
            if (!hMem) return;

            void* p = GlobalLock(hMem);
            std::memcpy(p, result.c_str(), bytes);
            GlobalUnlock(hMem);

            if (OpenClipboard(g.hMain))
            {
                EmptyClipboard();
                SetClipboardData(CF_UNICODETEXT, hMem);
                CloseClipboard();
            }
            else
            {
                GlobalFree(hMem);
            }

            MessageBoxW(g.hMain,
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
            ofn.hwndOwner = g.hMain;
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
                MessageBoxW(g.hMain, L"Failed to open file for writing.", L"Export", MB_OK | MB_ICONERROR);
                return;
            }

            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, result.c_str(), (int)result.size(), nullptr, 0, nullptr, nullptr);
            std::string utf8;
            utf8.resize(utf8Len);
            WideCharToMultiByte(CP_UTF8, 0, result.c_str(), (int)result.size(), utf8.data(), utf8Len, nullptr, nullptr);
            out.write(utf8.data(), (std::streamsize)utf8.size());
            out.close();

            MessageBoxW(g.hMain, L"Exported layout to file.", L"Export", MB_OK | MB_ICONINFORMATION);
        }

        void ImportFromCppSource()
        {
            wchar_t pathBuf[MAX_PATH] = L"";
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = g.hMain;
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
                MessageBoxW(g.hMain, L"Failed to open file.", L"Import", MB_OK | MB_ICONERROR);
                return;
            }

            std::string code((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

            auto imported = wimp::parse_controls_from_code(code);
            CurrentControls() = std::move(imported);

            for (size_t i = 0; i < CurrentControls().size(); ++i)
            {
                auto& c = CurrentControls()[i];
                if (c.id <= 0)
                    c.id = 1000 + (int)i;
                if (c.styleExpr.empty())
                    c.styleExpr = wui::default_style_expr(c.type);
            }

            g.selectedIndex = CurrentControls().empty() ? -1 : 0;

            RebuildZOrderTree();
            RebuildRuntimeControls();
            RebuildHierarchyTree();
            RefreshPropertyPanel();
            RedrawDesignOverlay();

            wchar_t msg[128];
            std::swprintf(msg, std::size(msg), L"Imported %zu controls from source.", CurrentControls().size());
            MessageBoxW(g.hMain, msg, L"Import", MB_OK | MB_ICONINFORMATION);
        }

        // ---------------------------------------------------------------------
        // UI (menus/toolbar/layout)
        // ---------------------------------------------------------------------

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

            AppendMenuW(hInsert, MF_STRING | (g.drawToCreateMode ? MF_CHECKED : 0), IDM_TOGGLE_DRAW_MODE, L"Draw to Create");
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
            g.hInsertMenu = hInsert;
        }

        void BuildToolbar(HWND hwnd)
        {
            g.hToolbar = CreateWindowExW(
                0, TOOLBARCLASSNAMEW, nullptr,
                WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NORESIZE | CCS_NODIVIDER,
                0, 0, 0, 0,
                hwnd, nullptr, g.hInst, nullptr);

            if (!g.hToolbar)
                return;

            SendMessageW(g.hToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
            SendMessageW(g.hToolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);
            SendMessageW(g.hToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(90, 24));

            int strId = (int)SendMessageW(g.hToolbar, TB_ADDSTRING, 0, (LPARAM)L"Draw to create");
            TBBUTTON btn{};
            btn.iBitmap = I_IMAGENONE;
            btn.idCommand = IDM_TOGGLE_DRAW_MODE;
            btn.fsState = TBSTATE_ENABLED | (g.drawToCreateMode ? TBSTATE_CHECKED : 0);
            btn.fsStyle = BTNS_CHECK | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
            btn.iString = strId;
            SendMessageW(g.hToolbar, TB_ADDBUTTONS, 1, (LPARAM)&btn);
            SendMessageW(g.hToolbar, TB_AUTOSIZE, 0, 0);
            ShowWindow(g.hToolbar, SW_SHOW);
        }

        void UpdatePlacementModeUI()
        {
            if (g.hInsertMenu)
                CheckMenuItem(g.hInsertMenu, IDM_TOGGLE_DRAW_MODE,
                    MF_BYCOMMAND | (g.drawToCreateMode ? MF_CHECKED : MF_UNCHECKED));

            if (g.hToolbar)
                SendMessageW(g.hToolbar, TB_CHECKBUTTON, IDM_TOGGLE_DRAW_MODE,
                    MAKELONG(g.drawToCreateMode ? TRUE : FALSE, 0));
        }

        void CreatePropertyPanel(HWND hwnd)
        {
            g.hPropPanel = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"STATIC", nullptr,
                WS_CHILD | WS_VISIBLE,
                0, 0, kPropPanelWidth, 0,
                hwnd, nullptr, g.hInst, nullptr);

            const wchar_t* labels[] = { L"X:", L"Y:", L"W:", L"H:", L"Text:", L"ID:" };

            int y = 8;
            for (int i = 0; i < 6; ++i)
            {
                CreateWindowExW(
                    0, L"STATIC", labels[i],
                    WS_CHILD | WS_VISIBLE,
                    8, y + 4, 40, 20,
                    g.hPropPanel, nullptr, g.hInst, nullptr);

                g.hPropEdits[i] = CreateWindowExW(
                    WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                    52, y, kPropPanelWidth - 60, 22,
                    g.hPropPanel, (HMENU)(INT_PTR)(100 + i), g.hInst, nullptr);

                y += 28;
            }

            CreateWindowExW(0, L"STATIC", L"Style:", WS_CHILD | WS_VISIBLE, 8, y + 4, 50, 20, g.hPropPanel, nullptr, g.hInst, nullptr);
            g.hStyleEdit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                60, y, kPropPanelWidth - 68, 22,
                g.hPropPanel, (HMENU)(INT_PTR)200, g.hInst, nullptr);

            y += 28;
            CreateWindowExW(0, L"STATIC", L"Common flags:", WS_CHILD | WS_VISIBLE, 8, y + 4, 100, 20, g.hPropPanel, nullptr, g.hInst, nullptr);

            y += 24;
            g.hStyleChkChild = CreateWindowExW(0, L"BUTTON", L"WS_CHILD", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 8, y, kPropPanelWidth - 16, 20, g.hPropPanel, (HMENU)(INT_PTR)210, g.hInst, nullptr);

            y += 22;
            g.hStyleChkVisible = CreateWindowExW(0, L"BUTTON", L"WS_VISIBLE", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 8, y, kPropPanelWidth - 16, 20, g.hPropPanel, (HMENU)(INT_PTR)211, g.hInst, nullptr);

            y += 22;
            g.hStyleChkTabstop = CreateWindowExW(0, L"BUTTON", L"WS_TABSTOP", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 8, y, kPropPanelWidth - 16, 20, g.hPropPanel, (HMENU)(INT_PTR)212, g.hInst, nullptr);

            y += 22;
            g.hStyleChkBorder = CreateWindowExW(0, L"BUTTON", L"WS_BORDER", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 8, y, kPropPanelWidth - 16, 20, g.hPropPanel, (HMENU)(INT_PTR)213, g.hInst, nullptr);

            y += 28;
            g.hTabPageLabel = CreateWindowExW(0, L"STATIC", L"Tab Page:", WS_CHILD, 8, y + 4, 70, 20, g.hPropPanel, nullptr, g.hInst, nullptr);
            g.hTabPageCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST, 80, y, kPropPanelWidth - 88, 200, g.hPropPanel, (HMENU)(INT_PTR)214, g.hInst, nullptr);
            ShowWindow(g.hTabPageLabel, SW_HIDE);
            ShowWindow(g.hTabPageCombo, SW_HIDE);

            y += 32;
            CreateWindowExW(0, L"STATIC", L"Hierarchy:", WS_CHILD | WS_VISIBLE, 8, y, 100, 18, g.hPropPanel, nullptr, g.hInst, nullptr);

            g.treeTop = y + 20;
            g.hHierarchyTree = CreateWindowExW(
                WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                8, g.treeTop, kPropPanelWidth - 16, 180,
                g.hPropPanel, nullptr, g.hInst, nullptr);

            y = g.treeTop + 188;
            g.hReparentBtn = CreateWindowExW(
                0, L"BUTTON", L"Reparent Selection...",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                8, y, kPropPanelWidth - 16, 24,
                g.hPropPanel, (HMENU)(INT_PTR)IDC_REPARENT_BTN, g.hInst, nullptr);

            y += 36;
            CreateWindowExW(0, L"STATIC", L"Z-Order:", WS_CHILD | WS_VISIBLE, 8, y, 100, 18, g.hPropPanel, nullptr, g.hInst, nullptr);

            g.zListTop = y + 20;
            g.hZOrderTree = CreateWindowExW(
                WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
                8, g.zListTop, kPropPanelWidth - 16, 200,
                g.hPropPanel, (HMENU)(INT_PTR)IDC_ZORDER_TREE, g.hInst, nullptr);

            if (g.hZOrderTree)
                g.originalZTreeProc = (WNDPROC)SetWindowLongPtrW(g.hZOrderTree, GWLP_WNDPROC, (LONG_PTR)ZOrderTreeProc);

            const int btnWidth = (kPropPanelWidth - 24) / 2;
            int btnY = g.zListTop + 208;

            g.hZBringFront = CreateWindowExW(0, L"BUTTON", L"Bring to Front", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 8, btnY, btnWidth, 24, g.hPropPanel, (HMENU)(INT_PTR)IDC_ZORDER_BRING_FRONT, g.hInst, nullptr);
            g.hZSendBack = CreateWindowExW(0, L"BUTTON", L"Send to Back", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 16 + btnWidth, btnY, btnWidth, 24, g.hPropPanel, (HMENU)(INT_PTR)IDC_ZORDER_SEND_BACK, g.hInst, nullptr);

            btnY += 28;
            g.hZForward = CreateWindowExW(0, L"BUTTON", L"Move Forward", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 8, btnY, btnWidth, 24, g.hPropPanel, (HMENU)(INT_PTR)IDC_ZORDER_FORWARD, g.hInst, nullptr);
            g.hZBackward = CreateWindowExW(0, L"BUTTON", L"Move Backward", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 16 + btnWidth, btnY, btnWidth, 24, g.hPropPanel, (HMENU)(INT_PTR)IDC_ZORDER_BACKWARD, g.hInst, nullptr);

            RebuildZOrderTree();
            RebuildHierarchyTree();
            RefreshPropertyPanel();
        }

        void LayoutZOrderPanel()
        {
            if (!g.hPropPanel || !g.hZOrderTree)
                return;

            RECT rc{};
            GetClientRect(g.hPropPanel, &rc);

            const int margin = 8;
            const int buttonHeight = 24;
            const int buttonSpacing = 6;
            const int treeTop = g.treeTop;
            const int listTop = g.zListTop;
            const int minListHeight = 80;
            const int minTreeHeight = 80;
            const int listWidth = kPropPanelWidth - margin * 2;

            int treeHeight = std::max(minTreeHeight, listTop - treeTop - buttonHeight - buttonSpacing - margin);
            if (g.hHierarchyTree)
                MoveWindow(g.hHierarchyTree, margin, treeTop, listWidth, treeHeight, TRUE);

            if (g.hReparentBtn)
            {
                int reparentY = treeTop + treeHeight + buttonSpacing;
                MoveWindow(g.hReparentBtn, margin, reparentY, listWidth, buttonHeight, TRUE);
            }

            const int available = rc.bottom - listTop - (buttonHeight * 2 + buttonSpacing * 2 + margin);
            const int listHeight = std::max(minListHeight, available);

            MoveWindow(g.hZOrderTree, margin, listTop, listWidth, listHeight, TRUE);

            int btnY = listTop + listHeight + buttonSpacing;
            const int btnWidth = (kPropPanelWidth - margin * 3) / 2;

            MoveWindow(g.hZBringFront, margin, btnY, btnWidth, buttonHeight, TRUE);
            MoveWindow(g.hZSendBack, margin * 2 + btnWidth, btnY, btnWidth, buttonHeight, TRUE);

            btnY += buttonHeight + buttonSpacing;
            MoveWindow(g.hZForward, margin, btnY, btnWidth, buttonHeight, TRUE);
            MoveWindow(g.hZBackward, margin * 2 + btnWidth, btnY, btnWidth, buttonHeight, TRUE);
        }

        void LayoutChildren(HWND hwnd)
        {
            RECT rcClient{};
            GetClientRect(hwnd, &rcClient);

            int toolbarHeight = 0;
            if (g.hToolbar && IsWindow(g.hToolbar))
            {
                SendMessageW(g.hToolbar, TB_AUTOSIZE, 0, 0);
                RECT rcTb{};
                GetWindowRect(g.hToolbar, &rcTb);
                toolbarHeight = rcTb.bottom - rcTb.top;
                MoveWindow(g.hToolbar, 0, 0, rcClient.right, toolbarHeight, TRUE);
            }

            const int propW = kPropPanelWidth;
            const int designRight = rcClient.right - propW;
            const int contentTop = toolbarHeight;

            if (g.hDesign)
            {
                int w = std::max<int>(designRight - 2 * kDesignMargin, 100);
                int h = std::max<int>(rcClient.bottom - contentTop - 2 * kDesignMargin, 100);

                MoveWindow(g.hDesign,
                    kDesignMargin,
                    contentTop + kDesignMargin,
                    w, h,
                    TRUE);
            }

            if (g.hPropPanel)
            {
                MoveWindow(g.hPropPanel,
                    designRight,
                    contentTop,
                    propW,
                    rcClient.bottom - contentTop,
                    TRUE);

                LayoutZOrderPanel();
            }
        }

        // ---------------------------------------------------------------------
        // Z-ORDER TREE PROC (mousewheel drives z-order)
        // ---------------------------------------------------------------------

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
                            TreeView_Expand(hwnd, sel, (delta > 0) ? TVE_EXPAND : TVE_COLLAPSE);
                    }
                    else
                    {
                        ApplyZOrderCommand(delta > 0 ? ZOrderCommand::MoveForward : ZOrderCommand::MoveBackward);
                    }
                    return 0;
                }
            }

            if (g.originalZTreeProc)
                return CallWindowProcW(g.originalZTreeProc, hwnd, msg, wp, lp);
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        // ---------------------------------------------------------------------
        // DESIGN PROC
        // ---------------------------------------------------------------------

        LRESULT CALLBACK DesignWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
        {
            switch (msg)
            {
            case WM_NOTIFY:
            {
                auto hdr = reinterpret_cast<LPNMHDR>(lp);
                if (hdr && hdr->hwndFrom == g.hHierarchyTree && hdr->code == TVN_SELCHANGEDW)
                {
                    auto* tv = reinterpret_cast<NMTREEVIEWW*>(lp);
                    if (!g.inTreeUpdate)
                        SetSelectedIndex((int)tv->itemNew.lParam);
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
                if (BeginCreateDrag(pt)) return 0;
                if (BeginDrag(pt))       return 0;

                int hit = HitTestTopmostControl(pt);
                SetSelectedIndex(hit);
                RedrawDesignOverlay();
                return 0;
            }

            case WM_MOUSEMOVE:
            {
                if (g.create.drawing)
                {
                    POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                    UpdateCreateDrag(pt);
                    return 0;
                }
                if (g.drag.active)
                {
                    POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                    UpdateDrag(pt);
                    return 0;
                }
                break;
            }

            case WM_LBUTTONUP:
            {
                if (g.create.drawing) { EndCreateDrag(); return 0; }
                if (g.drag.active) { EndDrag();      return 0; }
                break;
            }

            case WM_CONTEXTMENU:
            {
                POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                if (pt.x == -1 && pt.y == -1) GetCursorPos(&pt);
                else MapWindowPoints(hwnd, HWND_DESKTOP, &pt, 1);

                pt = PhysicalScreenToLogical(g.hDesign, pt);
                ShowArrangeContextMenu(pt);
                return 0;
            }

            case WM_MOUSEWHEEL:
            {
                if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL)
                {
                    const int delta = GET_WHEEL_DELTA_WPARAM(wp);
                    if (delta > 0)      ApplyZOrderCommand(ZOrderCommand::MoveForward);
                    else if (delta < 0) ApplyZOrderCommand(ZOrderCommand::MoveBackward);
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

            if (g.originalDesignProc)
                return CallWindowProcW(g.originalDesignProc, hwnd, msg, wp, lp);
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        // ---------------------------------------------------------------------
        // MAIN WINDOW PROC
        // ---------------------------------------------------------------------

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

                g.hDesign = CreateWindowExW(
                    WS_EX_CLIENTEDGE, L"STATIC", nullptr,
                    WS_CHILD | WS_VISIBLE,
                    kDesignMargin, kDesignMargin, 400, 400,
                    hwnd, nullptr, g.hInst, nullptr);

                g.originalDesignProc = (WNDPROC)GetWindowLongPtrW(g.hDesign, GWLP_WNDPROC);
                SetWindowLongPtrW(g.hDesign, GWLP_WNDPROC, (LONG_PTR)DesignWndProc);

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
                if (hdr && hdr->hwndFrom == g.hZOrderTree && hdr->code == TVN_SELCHANGEDW)
                {
                    auto* tv = reinterpret_cast<NMTREEVIEWW*>(lp);
                    auto* node = reinterpret_cast<ZOrderNodeData*>(tv->itemNew.lParam);

                    if (!g.inZTreeUpdate && node &&
                        node->controlIndex >= 0 && node->controlIndex < (int)CurrentControls().size())
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

                if (id >= 100 && id < 106 && code == EN_CHANGE)
                {
                    ApplyPropertyChange((PropIndex)(id - 100));
                    return 0;
                }

                if (id == 200 && code == EN_CHANGE && !g.inStyleUpdate)
                {
                    if (g.selectedIndex >= 0 && g.selectedIndex < (int)CurrentControls().size())
                    {
                        auto& c = CurrentControls()[g.selectedIndex];
                        c.styleExpr = get_window_text_w(g.hStyleEdit);
                        sync_style_checkboxes_from_expr(c.styleExpr);
                        RebuildRuntimeControls();
                        RedrawDesignOverlay();
                        RefreshPropertyPanel();
                    }
                    return 0;
                }

                if (!g.inStyleUpdate)
                {
                    switch (id)
                    {
                    case 210: apply_style_checkbox_change(g.hStyleChkChild, L"WS_CHILD");   RebuildRuntimeControls(); RedrawDesignOverlay(); RefreshPropertyPanel(); return 0;
                    case 211: apply_style_checkbox_change(g.hStyleChkVisible, L"WS_VISIBLE"); RebuildRuntimeControls(); RedrawDesignOverlay(); RefreshPropertyPanel(); return 0;
                    case 212: apply_style_checkbox_change(g.hStyleChkTabstop, L"WS_TABSTOP"); RebuildRuntimeControls(); RedrawDesignOverlay(); RefreshPropertyPanel(); return 0;
                    case 213: apply_style_checkbox_change(g.hStyleChkBorder, L"WS_BORDER");  RebuildRuntimeControls(); RedrawDesignOverlay(); RefreshPropertyPanel(); return 0;
                    default: break;
                    }
                }

                if (id == 214 && code == CBN_SELCHANGE && !g.inTabPageUpdate)
                {
                    if (g.selectedIndex >= 0 && g.selectedIndex < (int)CurrentControls().size())
                    {
                        auto& c = CurrentControls()[g.selectedIndex];
                        if (c.parentIndex >= 0 && c.parentIndex < (int)CurrentControls().size() &&
                            CurrentControls()[c.parentIndex].type == wui::ControlType::Tab)
                        {
                            int sel = (int)SendMessageW(g.hTabPageCombo, CB_GETCURSEL, 0, 0);
                            if (sel < 0) sel = 0;
                            c.tabPageId = sel;
                            const int tabIndex = c.parentIndex;

                            RebuildRuntimeControls();

                            if (tabIndex >= 0 && tabIndex < (int)g.hwndControls.size())
                            {
                                HWND hTab = g.hwndControls[tabIndex];
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
                        if (g.selectedIndex >= 0 && g.selectedIndex < (int)CurrentControls().size())
                        {
                            ParentPickResult def{ CurrentControls()[g.selectedIndex].parentIndex, CurrentControls()[g.selectedIndex].tabPageId };
                            if (auto picked = PickParentFromMenu(def, g.selectedIndex))
                                ApplyParentChoice(g.selectedIndex, *picked);
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
                    g.selectedIndex = -1;
                    RebuildZOrderTree();
                    RebuildRuntimeControls();
                    RebuildHierarchyTree();
                    RefreshPropertyPanel();
                    RedrawDesignOverlay();
                    return 0;

                case IDM_EXPORT:      ExportLayoutToClipboard(); return 0;
                case IDM_EXPORT_FILE: ExportLayoutToFile();      return 0;
                case IDM_IMPORT:      ImportFromCppSource();     return 0;

                case IDM_TOGGLE_DRAW_MODE:
                    g.drawToCreateMode = !g.drawToCreateMode;
                    if (g.create.drawing)
                        ReleaseCapture();
                    g.create = {};
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
                default: break;
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
    } // namespace editor

    // -------------------------------------------------------------------------
    // PUBLIC ENTRYPOINT
    // -------------------------------------------------------------------------

    export int RunWin32UIEditor(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow)
    {
        using namespace editor;

        g.hInst = hInst;

        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        WNDCLASSW wc{};
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = editor::MainWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"WIN32_UI_EDITOR";

        if (!RegisterClassW(&wc))
            return 0;

        g.hMain = CreateWindowExW(
            0, L"WIN32_UI_EDITOR", L"Win32 UI Editor",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT, 1200, 750,
            nullptr, nullptr, hInst, nullptr);

        if (!g.hMain)
            return 0;

        ShowWindow(g.hMain, nCmdShow);
        UpdateWindow(g.hMain);

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return (int)msg.wParam;
    }
} // namespace win32_ui_editor
