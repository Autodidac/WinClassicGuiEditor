module;
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>

export module win32_ui_editor.model;

import std;

export namespace win32_ui_editor::model
{
    using std::wstring;
    using std::vector;

    // ------------------------------------------------------------
    // Control types supported by the editor/importer/exporter
    // ------------------------------------------------------------
    export enum class ControlType
    {
        Static,
        Button,
        Edit,
        Checkbox,
        Radio,
        GroupBox,
        ListBox,
        ComboBox,
        Progress,
        Slider,
        Tab,
        ListView,
        Tooltip,
    };

    // ------------------------------------------------------------
    // Core data for a single control
    // ------------------------------------------------------------
    export struct ControlDef
    {
        ControlType  type{};        // logical type
        RECT         rect{};        // coordinates on the design surface (editor coords)

        // Tab metadata: for controls inside a tab, which page are they on?
        int          tabPageId{ -1 }; // 0-based index into parent tab's pages

        wstring      text{};        // caption or text
        int          id{ 1000 };    // numeric ID

        // Optional metadata for round-tripping
        wstring      idName{};      // e.g. L"ID_OK"
        wstring      styleExpr{};   // L"WS_CHILD | WS_VISIBLE | WS_TABSTOP"
        wstring      className{};   // e.g. L"BUTTON", L"EDIT", WC_LISTVIEWW, etc.

        // Tab container pages (only used when type == ControlType::Tab)
        vector<wstring> tabPages{}; // labels for each tab page

        // Container hierarchy (editor semantics based on import)
        int          parentIndex{ -1 }; // index into vector<ControlDef>, -1 = top-level
        bool         isContainer{ false };
    };

    // ------------------------------------------------------------
    // Name shown inside the designer canvas
    // ------------------------------------------------------------
    export inline const wchar_t* ControlTypeLabel(ControlType t) noexcept
    {
        switch (t)
        {
        case ControlType::Static:   return L"Static";
        case ControlType::Button:   return L"Button";
        case ControlType::Edit:     return L"Edit";
        case ControlType::Checkbox: return L"Checkbox";
        case ControlType::Radio:    return L"Radio";
        case ControlType::GroupBox: return L"GroupBox";
        case ControlType::ListBox:  return L"ListBox";
        case ControlType::ComboBox: return L"ComboBox";
        case ControlType::Progress: return L"Progress";
        case ControlType::Slider:   return L"Slider";
        case ControlType::Tab:      return L"Tab";
        case ControlType::ListView: return L"ListView";
        case ControlType::Tooltip:  return L"Tooltip";
        }
        return L"?";
    }

    // ------------------------------------------------------------
    // Default Windows class names for round-trip import/export
    // ------------------------------------------------------------
    export inline wstring DefaultClassName(ControlType t)
    {
        switch (t)
        {
        case ControlType::Static:   return L"STATIC";
        case ControlType::Button:   return L"BUTTON";
        case ControlType::Edit:     return L"EDIT";
        case ControlType::Checkbox: return L"BUTTON";        // BS_AUTOCHECKBOX
        case ControlType::Radio:    return L"BUTTON";        // BS_AUTORADIOBUTTON
        case ControlType::GroupBox: return L"BUTTON";        // BS_GROUPBOX
        case ControlType::ListBox:  return L"LISTBOX";
        case ControlType::ComboBox: return L"COMBOBOX";
        case ControlType::Progress: return PROGRESS_CLASSW;
        case ControlType::Slider:   return TRACKBAR_CLASSW;
        case ControlType::Tab:      return WC_TABCONTROLW;
        case ControlType::ListView: return WC_LISTVIEWW;
        case ControlType::Tooltip:  return TOOLTIPS_CLASS;
        }
        return L"STATIC";
    }

    // ------------------------------------------------------------
    // Default style expressions for new controls
    // ------------------------------------------------------------
    export inline wstring default_style_expr(ControlType t)
    {
        switch (t)
        {
        case ControlType::Static:
            return L"WS_CHILD | WS_VISIBLE";

        case ControlType::Button:
            return L"WS_CHILD | WS_VISIBLE | WS_TABSTOP";

        case ControlType::Edit:
            return L"WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER";

        case ControlType::Checkbox:
            return L"WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX";

        case ControlType::Radio:
            return L"WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON";

        case ControlType::GroupBox:
            return L"WS_CHILD | WS_VISIBLE | BS_GROUPBOX";

        case ControlType::ListBox:
            return L"WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | WS_BORDER";

        case ControlType::ComboBox:
            return L"WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST";

        case ControlType::Progress:
            return L"WS_CHILD | WS_VISIBLE";

        case ControlType::Slider:
            return L"WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS";

        case ControlType::Tab:
            return L"WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN";

        case ControlType::ListView:
            return L"WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT";

        case ControlType::Tooltip:
            return L"TTS_ALWAYSTIP | TTS_NOPREFIX";
        }

        return L"WS_CHILD | WS_VISIBLE";
    }

    // ------------------------------------------------------------
    // Helper: is this type treated as a container in the editor?
    // ------------------------------------------------------------
    export inline bool is_container_type(ControlType t) noexcept
    {
        switch (t)
        {
        case ControlType::GroupBox:
        case ControlType::Tab:
            // You can add ListView/ComboBox here if you want to treat them
            // as logical containers as well.
            return true;
        default:
            return false;
        }
    }
}
