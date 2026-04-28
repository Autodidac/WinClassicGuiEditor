Win32UIEditor v0.3.0 (2025-12-11)
=================================

Classic Win32 form editor implemented in C++23 with a module-based core.

Features
--------

- Pure Win32 API (no MFC, no .NET, no ImGui).
- Design surface: drag controls with the mouse, resize with selection handles, and use arrow keys for keyboard placement.
- Keyboard editing: Delete removes the selection, Ctrl+D duplicates it, arrows nudge, Ctrl+arrows fine-nudge, and Shift+arrows resize.
- Property panel: edit X/Y/W/H/Text/ID/style flags for the selected control.
- Standard tab navigation across property fields and click-to-select controls on the design surface.
- Toolbox via menu: insert Static, Button, Edit, Checkbox, Radio, GroupBox, ListBox, ComboBox, Progress, Slider, Tab, ListView, Tooltip.
- Parenting: drag controls onto container widgets or individual tab pages to nest them while preserving parent-relative coordinates.
- Interaction patterns: draw new controls with click-and-drag rectangles, duplicate/delete selections, reorder siblings via arrange commands, and switch tab pages while editing.
- Import from C/C++ source that contains `CreateWindowEx*` calls.
- Export to clipboard or file: generates C/C++ `CreateWindowExW` code, control ID definitions, hierarchy metadata, and tab-page host windows.

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

- Open `WinClassicGuiEditor.sln` in Visual Studio.
- Build the `Win32UIEditor` project (x64 Debug or Release).
- Run it (it creates a classic Win32 top-level editor window).

Usage
-----

1. Insert controls from the **Insert** menu.
2. Drag them on the canvas to place them.
3. Click any control on the design surface to select it, then use **Tab** and **Shift+Tab** to move between property fields.
4. Use **Delete**, **Ctrl+D**, arrow keys, and **Shift+arrow** to edit quickly from the keyboard.
5. Use the property panel to set **X, Y, W, H, Text, ID**, and common style flags.
6. Use **File -> Import from C/C++...** to load an existing Win32 layout.
7. Use **File -> Export to Clipboard** or **File -> Export to File...**.
8. Paste the generated code into your own Win32 UI module (WM_CREATE or initialization path).

Tests
-----

```bash
cmake --build build --config Debug --target ImportParserTests
ctest --test-dir build -C Debug
```
