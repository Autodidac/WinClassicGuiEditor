Win32UIEditor
==============

Classic Win32 form editor implemented in C++23 with a module-based core.

Features
--------

- Pure Win32 API (no MFC, no .NET, no ImGui).
- Design surface: drag controls around with the mouse, arrow keys to nudge.
- Property panel: edit X/Y/W/H/Text/ID for the selected control.
- Standard tab navigation across property fields and click-to-select controls on the design surface.
- Toolbox via menu: insert Static, Button, Edit, Checkbox, Radio, GroupBox, ListBox, ComboBox, Progress, Slider.
- Export to clipboard: generates C++ `CreateWindowExW` code and an `enum class ControlId`.

Building with CMake + MSVC (recommended)
----------------------------------------

```bash
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

You may substitute the generator with your Visual Studio 2026 generator once available.

Opening directly in Visual Studio
---------------------------------

- Open `Win32UIEditor.sln` in Visual Studio.
- Build the `Win32UIEditor` project (x64 Debug or Release).
- Run it (it creates a classic Win32 top-level editor window).

Usage
-----

1. Insert controls from the **Insert** menu.
2. Drag them on the canvas to place them.
3. Click any control on the design surface to select it, then use **Tab** and **Shift+Tab** to move between property fields.
4. Use the property panel to set **X, Y, W, H, Text, ID**.
5. Use **File → Export to Clipboard**.
6. Paste the generated code into your own Win32 UI module (WM_CREATE or initialization path).
