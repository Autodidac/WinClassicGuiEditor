#include <windows.h>

import win32_ui_editor;
using namespace win32_ui_editor;

// Thin entrypoint that delegates to the module.
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR cmd, int nCmdShow)
{
    return win32_ui_editor::RunWin32UIEditor(hInst, hPrev, cmd, nCmdShow);
}
