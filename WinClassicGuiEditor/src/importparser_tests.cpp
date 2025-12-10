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
    )";

    auto controls = parse_controls_from_code(code);
    assert(controls.size() == 7);

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

    return 0;
}
