# Changelog

## [0.3.0] - 2025-12-11
### Added
- Tab, ListView, and Tooltip controls are now available in the toolbox, including tab-page aware parenting when dragging children into a tab control.
- Controls can be placed with click-and-drag rectangles so their initial size matches the drawn region on the design surface.

### Fixed
- Nested parenting is preserved when moving controls, ensuring tab-page and container children stay correctly attached.
- Hi-DPI coordinates and adorners now scale consistently to avoid drifting selection rectangles on scaled displays.
- Z-order raise/lower operations respect parent stacking and keep the active selection visible.

## [0.2.0] - 2025-05-12
### Fixed
- Restored keyboard tab switching between property fields so navigation follows the standard Windows behavior.
- Re-enabled design-surface click-to-select so individual controls can be selected directly with the mouse.

## [0.1.0] - 2024-01-01
### Added
- Initial release of the Win32 UI editor with drag/drop design surface, property panel, toolbox, and code export.
