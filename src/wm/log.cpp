#include "wm.hpp"

#include "core/chrono.hpp"
#include "core/color.hpp"
#include "core/stacktrace.hpp"

void wm_log_init(WindowManager* wm)
{
    log_history_enable(true);

    log_history_add_listener([wm = Weak(wm)](LogEntry*) {
        if (wm) {
            ui_request_frame(wm->ui.get());
        }
    });
}

void wm_log_frame(WindowManager* wm)
{
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
    ImGui::Checkbox("Details", &wm->log.show_details);

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

        switch (entry.level) {
            break;case LogLevel::trace: format = "[TRACE] %.*s"; color = to_imvec(color_trace);
            break;case LogLevel::debug: format = "[DEBUG] %.*s"; color = to_imvec(color_debug);
            break;case LogLevel::info:  format = " [INFO] %.*s"; color = to_imvec(color_info);
            break;case LogLevel::warn:  format = " [WARN] %.*s"; color = to_imvec(color_warn);
            break;case LogLevel::error: format = "[ERROR] %.*s"; color = to_imvec(color_error);
            break;case LogLevel::fatal: format = "[FATAL] %.*s"; color = to_imvec(color_fatal);
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

            bool is_hovered = i == wm->log.selected;
            if (draw_entry(i, *entry, &is_hovered)) {
                wm->log.selected = (wm->log.selected == i) ? -1 : i;
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

    if (wm->log.selected >= i64(history.entries.size())) {
        wm->log.selected = -1;
    }

    if (wm->log.show_details) {
        defer { ImGui::End(); };
        if (ImGui::Begin(wm->log.selected >= 0
                ? "Log Details (locked)###Log Details"
                : "Log Details###Log Details", &wm->log.show_details, ImGuiWindowFlags_HorizontalScrollbar)) {
            auto effective = wm->log.selected >= 0 ? wm->log.selected : hovered;
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

    if (!wm->log.show_details) {
        wm->log.selected = -1;
    }
}
