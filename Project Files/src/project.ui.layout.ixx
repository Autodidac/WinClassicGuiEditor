module;
export module project.ui.layout;

import <cstdint>;
import <algorithm>;
import std;

export namespace project::ui::layout
{
    struct Rect
    {
        int x{};
        int y{};
        int w{};
        int h{};
    };

    export struct LauncherLayout
    {
        Rect title;
        Rect subtitle;

        Rect btn_start;
        Rect btn_editor;
        Rect btn_settings;
        Rect btn_exit;

        Rect group_basic;
        Rect group_lists;

        Rect chk_demo;
        Rect radio1;
        Rect radio2;
        Rect radio3;
        Rect edit_single;
        Rect edit_multi;

        Rect listbox;
        Rect combo;

        Rect progress;
        Rect slider;
        Rect slider_value;

        Rect status;
    };

    export inline float clamp_scale(float s) noexcept
    {
        return std::clamp(s, 0.75f, 3.0f);
    }

    export inline LauncherLayout compute_launcher_layout(
        int   client_w,
        int   client_h,
        float dpi_scale)
    {
        LauncherLayout L{};
        const float s = clamp_scale(dpi_scale);

        const int base_margin_x = 16;
        const int base_margin_y = 16;
        const int base_title_h = 26;
        const int base_sub_h = 18;

        const int base_btn_w = 120;
        const int base_btn_h = 32;
        const int base_btn_gap = 12;

        // Larger group height so multiline + listbox have room
        const int base_group_h = 220;
        const int base_group_gap = 12;

        const int base_status_h = 22;
        const int base_v_gap = 8;

        const int base_progress_h = 18;
        const int base_slider_h = 28;

        const int margin_x = static_cast<int>(base_margin_x * s);
        const int margin_y = static_cast<int>(base_margin_y * s);
        const int title_h = static_cast<int>(base_title_h * s);
        const int sub_h = static_cast<int>(base_sub_h * s);

        const int btn_w = static_cast<int>(base_btn_w * s);
        const int btn_h = static_cast<int>(base_btn_h * s);
        const int btn_gap = static_cast<int>(base_btn_gap * s);

        const int group_h = static_cast<int>(base_group_h * s);
        const int group_gap = static_cast<int>(base_group_gap * s);

        const int status_h = static_cast<int>(base_status_h * s);
        const int v_gap = static_cast<int>(base_v_gap * s);

        const int progress_h = static_cast<int>(base_progress_h * s);
        const int slider_h = static_cast<int>(base_slider_h * s);

        const int content_w = std::max(client_w - 2 * margin_x, btn_w * 2);
        const int header_x = (client_w - content_w) / 2;
        int       y = margin_y;

        // ----------------------------------------------------
        // Title, subtitle
        // ----------------------------------------------------
        L.title = Rect{ header_x, y, content_w, title_h };
        y += title_h + v_gap;

        L.subtitle = Rect{ header_x, y, content_w, sub_h };
        y += sub_h + (v_gap * 2);

        // ----------------------------------------------------
        // Buttons row
        // ----------------------------------------------------
        const int total_btn_w = btn_w * 4 + btn_gap * 3;
        const int btn_row_x = std::max((client_w - total_btn_w) / 2, margin_x);

        L.btn_start = Rect{ btn_row_x,                              y, btn_w, btn_h };
        L.btn_editor = Rect{ btn_row_x + (btn_w + btn_gap),          y, btn_w, btn_h };
        L.btn_settings = Rect{ btn_row_x + 2 * (btn_w + btn_gap),      y, btn_w, btn_h };
        L.btn_exit = Rect{ btn_row_x + 3 * (btn_w + btn_gap),      y, btn_w, btn_h };

        y += btn_h + (v_gap * 2);

        // ----------------------------------------------------
        // Two groups side by side
        // ----------------------------------------------------
        const int group_w = (content_w - group_gap) / 2;
        const int group_x_left = header_x;
        const int group_x_right = header_x + group_w + group_gap;

        L.group_basic = Rect{ group_x_left,  y, group_w, group_h };
        L.group_lists = Rect{ group_x_right, y, group_w, group_h };

        // ----------------------------------------------------
        // Inside "Basic Widgets" group
        // ----------------------------------------------------
        int gx = L.group_basic.x + static_cast<int>(8 * s);
        int gy = L.group_basic.y + static_cast<int>(24 * s);
        const int row_h = static_cast<int>(22 * s);
        const int row_gap = static_cast<int>(6 * s);
        const int edit_pad = static_cast<int>(16 * s);

        L.chk_demo = Rect{ gx, gy, group_w - edit_pad, row_h };
        gy += row_h + row_gap;

        L.radio1 = Rect{ gx, gy, group_w - edit_pad, row_h };
        gy += row_h + row_gap;

        L.radio2 = Rect{ gx, gy, group_w - edit_pad, row_h };
        gy += row_h + row_gap;

        L.radio3 = Rect{ gx, gy, group_w - edit_pad, row_h };
        gy += row_h + row_gap;

        L.edit_single = Rect{
            gx,
            gy,
            group_w - edit_pad,
            row_h
        };
        gy += row_h + row_gap;

        int multi_h = L.group_basic.y + L.group_basic.h
            - gy - static_cast<int>(8 * s);

        // Make sure multiline edit is actually multi-line (at least 2 rows)
        const int min_multi_h = row_h * 2;
        multi_h = std::max(multi_h, min_multi_h);

        L.edit_multi = Rect{
            gx,
            gy,
            group_w - edit_pad,
            multi_h
        };

        // ----------------------------------------------------
        // Inside "Lists / Combo" group
        // ----------------------------------------------------
        gx = L.group_lists.x + static_cast<int>(8 * s);
        gy = L.group_lists.y + static_cast<int>(24 * s);

        const int half_w = (group_w - static_cast<int>(16 * s) - static_cast<int>(8 * s)) / 2;
        int       half_h_raw = L.group_lists.h - static_cast<int>(48 * s);

        // Make listbox tall enough for several rows
        const int min_list_rows = 6;
        const int min_list_h = row_h * min_list_rows;
        const int half_h = std::max(half_h_raw, min_list_h);

        L.listbox = Rect{ gx, gy, half_w, half_h };

        // Combo sits to the right, with a reasonable visible height.
        // Dropdown list height is controlled at creation time (project.entry.win).
        L.combo = Rect{
            gx + half_w + static_cast<int>(8 * s),
            gy + static_cast<int>(4 * s),
            half_w,
            static_cast<int>(row_h * 1.5f)
        };

        // ----------------------------------------------------
        // Status bar anchored at bottom
        // ----------------------------------------------------
        const int status_y = client_h - margin_y - status_h;
        const int status_x = margin_x;
        const int status_w = std::max(client_w - 2 * margin_x, btn_w);

        L.status = Rect{ status_x, status_y, status_w, status_h };

        // ----------------------------------------------------
        // Progress + slider anchored just above status
        // ----------------------------------------------------
        const int bar_x = header_x;
        const int bar_w = content_w;

        // Stack from bottom upwards: [status] [gap] [slider+label] [gap] [progress]
        const int slider_y = status_y - v_gap - slider_h;
        const int progress_y = slider_y - v_gap - progress_h;

        const int slider_w = static_cast<int>(bar_w * 0.65f);
        const int value_w = bar_w - slider_w - static_cast<int>(8 * s);

        L.progress = Rect{ bar_x,              progress_y, bar_w,   progress_h };
        L.slider = Rect{ bar_x,              slider_y,   slider_w, slider_h };
        L.slider_value = Rect{
            bar_x + slider_w + static_cast<int>(8 * s),
            slider_y,
            value_w,
            slider_h
        };

        return L;
    }
}
