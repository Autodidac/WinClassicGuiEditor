#include <cassert>
#include <string>
#include <windows.h>
#include <commctrl.h>

import win32_ui_editor.importparser;
import win32_ui_editor.model;

using win32_ui_editor::importparser::parse_controls_from_code;
using win32_ui_editor::model::ControlType;

int main()
{
    const std::string code = R"(
//* CreateWindowExW should be ignored even when the comment starts with //* followed by the marker
HWND hIgnoredLine = CreateWindowExW(0, L"BUTTON", L"Ignored", WS_CHILD | WS_VISIBLE,
    10, 10, 100, 20, hwndParent, (HMENU)999, hInst, nullptr);
/*/ Another ignored CreateWindowExW should not slip through when the opener is /*/
HWND hIgnoredBlock = CreateWindowExW(0, L"STATIC", L"Also Ignored", WS_CHILD | WS_VISIBLE,
    10, 10, 100, 20, hwndParent, (HMENU)998, hInst, nullptr);
*/
HWND hLabel = CreateWindowExW(0, L"STATIC", L"Wide Label", WS_CHILD | WS_VISIBLE,
    10, 20, 120, 30, hwndParent, (HMENU)101, hInst, nullptr);
HWND hButton = CreateWindowExW(0, u8"BUTTON", u"Wide Button", WS_CHILD | WS_VISIBLE,
    10, 60, 120, 30, hwndParent, 202, hInst, nullptr);
HWND hCastNumber = CreateWindowExW(0, L"BUTTON", L"Number Cast", WS_CHILD | WS_VISIBLE,
    10, 100, 120, 30, hwndParent, (HMENU)303, hInst, nullptr);
HWND hNamedCast = CreateWindowExW(0, L"BUTTON", L"Named Cast", WS_CHILD | WS_VISIBLE,
    10, 140, 120, 30, hwndParent, (HMENU)IDC_STATIC_TEXT, hInst, nullptr);
HWND hNamedReinterpret = CreateWindowExW(0, L"BUTTON", L"Named Reinterpret", WS_CHILD | WS_VISIBLE,
    10, 180, 120, 30, hwndParent, reinterpret_cast<HMENU>(IDC_OK), hInst, nullptr);
HWND hProgress = CreateWindowExW(0, PROGRESS_CLASS, L"", WS_CHILD | WS_VISIBLE,
    10, 220, 120, 30, hwndParent, (HMENU)404, hInst, nullptr);
HWND hSlider = CreateWindowExW(0, L"msctls_trackbar", L"", WS_CHILD | WS_VISIBLE,
    10, 260, 120, 30, hwndParent, (HMENU)505, hInst, nullptr);
HWND hCustomClass = CreateWindowExW(0, L"MyButtonClass", L"Custom", WS_CHILD | WS_VISIBLE,
    10, 300, 120, 30, hwndParent, (HMENU)606, hInst, nullptr);
HWND hUrlText = CreateWindowExW(0, L"STATIC", L"https://example.com/a//b", WS_CHILD | WS_VISIBLE,
    10, 340, 180, 30, hwndParent, (HMENU)707, hInst, nullptr);
HWND hSlashClass = CreateWindowExW(0, L"Custom/*Class//Name", L"Class Slash Test", WS_CHILD | WS_VISIBLE,
    10, 380, 180, 30, hwndParent, (HMENU)808, hInst, nullptr);
HWND hStyleLiteral = CreateWindowExW(0, L"BUTTON", L"Style // literal", L"WS_CHILD/*|*/WS_VISIBLE//BS_PUSHBUTTON",
    10, 420, 180, 30, hwndParent, (HMENU)909, hInst, nullptr);
    )";

    auto controls = parse_controls_from_code(code);
    assert(controls.size() == 11);

    assert(controls[0].className == L"STATIC");
    assert(controls[0].text == L"Wide Label");
    assert(controls[0].id == 101);

    assert(controls[1].className == L"BUTTON");
    assert(controls[1].text == L"Wide Button");
    assert(controls[1].id == 202);

    assert(controls[2].id == 303);

    assert(controls[3].idName == L"IDC_STATIC_TEXT");
    assert(controls[4].idName == L"IDC_OK");

    assert(controls[5].className == L"PROGRESS_CLASS");
    assert(controls[5].type == ControlType::Progress);
    assert(controls[5].id == 404);

    assert(controls[6].className == L"msctls_trackbar");
    assert(controls[6].type == ControlType::Slider);
    assert(controls[6].id == 505);
    assert(controls[7].className == L"MyButtonClass");
    assert(controls[7].text == L"Custom");
    assert(controls[7].id == 606);

    assert(controls[8].className == L"STATIC");
    assert(controls[8].text == L"https://example.com/a//b");
    assert(controls[8].id == 707);

    assert(controls[9].className == L"Custom/*Class//Name");
    assert(controls[9].text == L"Class Slash Test");
    assert(controls[9].id == 808);

    assert(controls[10].className == L"BUTTON");
    assert(controls[10].text == L"Style // literal");
    assert(controls[10].id == 909);

    assert(win32_ui_editor::model::ExportClassName(controls[7]) == L"MyButtonClass");

    win32_ui_editor::model::ControlDef nativeControl{};
    nativeControl.type = ControlType::Button;
    assert(nativeControl.className.empty());
    assert(win32_ui_editor::model::ExportClassName(nativeControl) == L"BUTTON");

    return 0;
}
