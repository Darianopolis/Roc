#include "../shell.hpp"

#include <core/chrono.hpp>
#include <core/color.hpp>
#include <core/stacktrace.hpp>

struct ShellLogViewer
{
    Ref<Ui> ui;
    Shell* shell;
    bool requested;
    bool show_details;
    i64 selected = -1;
};

static
void frame(ShellLogViewer*);

auto shell_init_log_viewer(Shell* shell) -> Ref<void>
{
    auto viewer = ref_create<ShellLogViewer>();
    viewer->shell = shell;

    log_history_enable(true);

    log_history_add_listener([viewer = Weak(viewer.get())](LogEntry*) {
        if (!viewer) return;
        if (std::exchange(viewer->requested, true)) return;
        exec_enqueue(viewer->shell->exec, [viewer] {
            if (viewer) {
                ui_request_frame(viewer->ui.get());
            }
        });
    });

    viewer->ui = ui_create(shell->gpu, shell->wm, shell->app_share / "log-viewer");
    ui_set_frame_handler(viewer->ui.get(), [viewer = viewer.get()] {
        frame(viewer);
    });

    return viewer;
}

static
void frame(ShellLogViewer* viewer)
{
    viewer->requested = false;

    auto history = log_history_get();

    bool show_log_window = true;

    defer { ImGui::End(); };
    if (!ImGui::Begin(std::format("Log ({} entries - {})###Log",
            history.entries.size(),
            FmtBytes(history.entries.size_bytes() + history.buffer_size)
        ).c_str(),
        &show_log_window, ImGuiWindowFlags_NoCollapse)) return;

    auto scroll_to_bottom = ImGui::Button("Follow");

    ImGui::SameLine();
    bool history_enabled = log_history_is_enabled();
    if (ImGui::Checkbox("Enabled", &history_enabled)) {
        log_history_enable(history_enabled);
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        log_history_clear();
    }

    ImGui::SameLine();
    ImGui::Checkbox("Details", &viewer->show_details);

    static constexpr auto make_color = [](std::string_view hex) { return vec4f32(color_from_hex(hex)) / 255.f; };
    static constexpr auto to_imvec =   [](vec4f32 v)            { return ImVec4(v.x, v.y, v.z, v.w); };

    static constexpr vec4f32 color_trace = make_color("#63686D");
    static constexpr vec4f32 color_debug = make_color("#16a085");
    static constexpr vec4f32 color_info  = make_color("#1d99f3");
    static constexpr vec4f32 color_warn  = make_color("#fdbc4b");
    static constexpr vec4f32 color_error = make_color("#c0392b");
    static constexpr vec4f32 color_fatal = make_color("#c0392b");

    static constexpr vec4f32 color_hover_bg  = make_color("#242424");
    static constexpr vec4f32 color_active_bg = make_color("#0b3b5e");

    static constexpr vec4f32 color_stacktrace_description = make_color("#ffffff");
    static constexpr vec4f32 color_stacktrace_location    = make_color("#9a5cb3");

    int hovered = -1;

    ImGui::Separator();

    ImGui::BeginChild("scroll-region", ImVec2(), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_HorizontalScrollbar);
    defer { ImGui::EndChild(); };

    // We need to track the baseline position of the current child window as otherwise
    // the offset into the *parent* window is incorrectly applied instead, resulting in
    // twice the expected content padding.
    auto base_x = ImGui::GetCursorPosX();

    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, to_imvec(color_hover_bg));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, to_imvec(color_active_bg));
    defer { ImGui::PopStyleColor(2); };

    // Font metrics for computing spacing
    auto font_height = ImGui::CalcTextSize("").y;
    auto spacing = ImGui::GetStyle().ItemSpacing.y;
    auto line_height = font_height + spacing;

    auto draw_entry = [&](int id, const LogEntry& entry, bool* hovered = nullptr) ->  bool {
        ImVec4 color;
        const char* format;

        switch (entry.semantic) {
            break;case LogSemantic::trace: format = "[TRACE] %.*s"; color = to_imvec(color_trace);
            break;case LogSemantic::debug: format = "[DEBUG] %.*s"; color = to_imvec(color_debug);
            break;case LogSemantic::info:  format = " [INFO] %.*s"; color = to_imvec(color_info);
            break;case LogSemantic::warn:  format = " [WARN] %.*s"; color = to_imvec(color_warn);
            break;case LogSemantic::error: format = "[ERROR] %.*s"; color = to_imvec(color_error);
            break;case LogSemantic::fatal: format = "[FATAL] %.*s"; color = to_imvec(color_fatal);
        }

        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::PushID(id);
        defer {
            ImGui::PopID();
            ImGui::PopStyleColor();
        };

        // We need to manually set Y positions for each following line in
        // multi-line messages as ImGui only honours "ImGuiSelectableFlags_AllowOverlap"
        // for the *first* item following a Selectable.
        auto y = ImGui::GetCursorPosY();

        bool selected = ImGui::Selectable("##selectable", false,
            ImGuiSelectableFlags_AllowOverlap
            | ImGuiSelectableFlags_SpanAllColumns
            | ((hovered && *hovered) ? ImGuiSelectableFlags_Highlight : 0),
            ImVec2(0, font_height + (std::max(1u, entry.lines) - 1) * line_height));
        if (hovered) {
            *hovered = ImGui::IsItemHovered();
        }
        ImGui::SameLine();
        ImGui::SetCursorPosX(base_x);

        auto message = entry.message();

        usz new_line = message.find_first_of('\n');
        if (new_line == std::string::npos) {
            ImGui::Text(format, message.size(), message.data());
        } else {
            ImGui::Text(format, new_line, message.data());
            do {
                y += line_height;
                ImGui::SetCursorPos(ImVec2(base_x, y));
                auto start = new_line + 1;
                new_line = message.find_first_of('\n', start);
                i32 len = std::min(new_line, message.size()) - start;
                ImGui::Text("        %.*s", len, message.data() + start);
            } while (new_line != std::string::npos);
        }

        return selected;
    };

    ImGuiListClipper clipper;

    // ImGui's clipper requires equally sized elements, so we clip to lines
    clipper.Begin(history.lines, line_height);

    while (clipper.Step()) {

        // DisplayStart and DisplayEnd represent *line* indices,
        // so we need to find the first log entry that contains that line.
        auto entry = history.find(clipper.DisplayStart);
        if (!entry) continue;

        // Then offset the cursor position back to handle cases where the
        // clipper starts partway through a multi-line message.
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (clipper.DisplayStart - entry->line_start) * line_height);

        int line = entry->line_start;
        while (line < clipper.DisplayEnd) {
            auto i = entry - history.entries.data();

            bool is_hovered = i == viewer->selected;
            if (draw_entry(i, *entry, &is_hovered)) {
                viewer->selected = (viewer->selected == i) ? -1 : i;
            }
            if (is_hovered) {
                hovered = i;
            }

            line += entry->lines;
            ++entry;
        }
    }

    clipper.End();

    // We need an extra dummy item to trigger the final spacing
    // Otherwise the log contents are clipped off early
    ImGui::Dummy(ImVec2(0, 0));

    if (scroll_to_bottom || (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())) {
        ImGui::SetScrollHereY(1.f);
    }

    // Log Details for selected log entry

    if (viewer->selected >= i64(history.entries.size())) {
        viewer->selected = -1;
    }

    if (viewer->show_details) {
        defer { ImGui::End(); };
        if (ImGui::Begin(viewer->selected >= 0
                ? "Log Details (locked)###Log Details"
                : "Log Details###Log Details", &viewer->show_details, ImGuiWindowFlags_HorizontalScrollbar)) {
            auto effective = viewer->selected >= 0 ? viewer->selected : hovered;
            if (effective != -1) {
                base_x = ImGui::GetCursorPosX();
                auto& entry = history.entries[effective];

                int section_id = 0;
                {
                    // Timestamp

                    ImGui::PushID(section_id++);
                    ImGui::Selectable("##selectable", false, ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns);
                    ImGui::SameLine();
                    ui_text("{}", FmtTime(entry.timestamp, TimeFormat::datetime_ms));
                    ImGui::PopID();
                }

                ImGui::Separator();

                {
                    // Log message

                    ImGui::PushID(section_id++);
                    draw_entry(0, entry);
                    ImGui::PopID();
                }

                ImGui::Separator();

                {
                    // Stacktrace

                    ImGui::PushID(section_id++);
                    defer { ImGui::PopID(); };

                    for (auto[i, e] : *entry.stacktrace | std::views::enumerate) {
                        if (e.description().empty() && e.source_file().empty()) continue;

                        ImGui::PushID(i);
                        defer {  ImGui::PopID(); };

                        auto height = e.source_file().empty() ? font_height : font_height + line_height;

                        auto y = ImGui::GetCursorPosY();
                        ImGui::Selectable("##selectable", false,
                            ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns,
                            ImVec2(0, height));

                        ImGui::SameLine();
                        ImGui::SetCursorPosX(base_x);
                        ImGui::PushStyleColor(ImGuiCol_Text, to_imvec(color_stacktrace_description));
                        ImGui::Text("%4li# %s", i, e.description().c_str());
                        ImGui::PopStyleColor();

                        if (!e.source_file().empty()) {
                            ImGui::SetCursorPos(ImVec2(base_x, y + line_height));
                            ImGui::PushStyleColor(ImGuiCol_Text, to_imvec(color_stacktrace_location));
                            ImGui::Text("      %s:%u", e.source_file().c_str(), e.source_line());
                            ImGui::PopStyleColor();
                        }
                    }
                }
            }
        }
    }

    if (!viewer->show_details) {
        viewer->selected = -1;
    }
}
