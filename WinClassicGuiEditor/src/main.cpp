#include <windows.h>

import win32_ui_editor;

// Thin entrypoint that delegates to the module.
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR cmd, int nCmdShow)
{
    return RunWin32UIEditor(hInst, hPrev, cmd, nCmdShow);
}
