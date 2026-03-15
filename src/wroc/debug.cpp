#include "wroc.hpp"
#include "event.hpp"

#include "core/color.hpp"
#include "core/math.hpp"
#include "core/stacktrace.hpp"
#include "core/chrono.hpp"

static constexpr int x11_disabled = 0;
static constexpr int x11_enabled = 1;
static constexpr int x11_forced = 2;

struct wroc_debug_gui : wroc_object
{
    bool show_debug_menu = false;
    bool show_log_window = false;
    bool show_demo_window = false;

    weak<wroc_toplevel> toplevel;

    struct {
        i64 selected = -1;
        bool show_details;
    } log;

    struct {
        u32 frames = 0;
        std::chrono::steady_clock::time_point last_report;
        std::chrono::steady_clock::duration frametime;
        f64 fps = 0.0;

        u64 last_events_handled = 0;
        u64 events_per_second = 0;

        // Statistic helper
        u64 last_poll_waits = 0;
        u64 poll_waits_per_second = 0;
    } stats;

    struct {
        std::string launch_text = "konsole";
        std::string x11_socket = ":2";
        int x11_mode = x11_disabled;
    } launch;
};

void wroc_debug_gui_init(bool show_on_startup)
{
    auto debug = (server->debug_gui = core_create<wroc_debug_gui>()).get();

    if (show_on_startup) {
        debug->show_debug_menu = true;
        debug->show_log_window = true;
    }
}

bool wroc_debug_gui_handle_event(wroc_debug_gui* debug, const wroc_event& _event)
{
    if (wroc_event_get_type(_event) == wroc_event_type::keyboard_key) {
        auto& event = static_cast<const wroc_keyboard_event&>(_event);

        if (event.key.upper() == XKB_KEY_A && event.key.pressed && wroc_get_active_modifiers().contains(wroc_modifier::mod)) {
            debug->show_debug_menu = !debug->show_debug_menu;
            return true;
        }
    }

    return false;
}

void wroc_imgui_show_debug(wroc_debug_gui* debug)
{
    if (!debug->show_debug_menu) return;

    {
        wroc_surface* surface = server->seat->keyboard->focused_surface.get();
        if (wroc_toplevel* toplevel = wroc_surface_get_addon<wroc_toplevel>(surface)) {
            debug->toplevel = toplevel;
        }
    }
    auto* toplevel = debug->toplevel.get();

    defer { ImGui::End(); };
    ImGui::Begin(PROJECT_NAME, &debug->show_debug_menu);

    static constexpr f32 second_column_offset = 113.f;
    static constexpr f32 third_column_offset  = 226.f;

    // Window toggles

    ImGui::Checkbox("Show Log", &debug->show_log_window);

    ImGui::SameLine(second_column_offset);
    ImGui::Checkbox("Show Cursor", &server->renderer->debug.show_debug_cursor);

    ImGui::SameLine(third_column_offset);
    if (ImGui::Button("Quit " PROJECT_NAME)) {
        wroc_terminate();
    }

    ImGui::Checkbox("Show Demo Window", &debug->show_demo_window);

    ImGui::SameLine(third_column_offset);
    if (ImGui::Button("New Output")) {
        server->backend->create_output();
    }

    auto category_separator = [] { ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, 2.f); };

    category_separator();

    // Focused window toggles

    {
        ImGui::BeginDisabled(!toplevel);
        defer { ImGui::EndDisabled(); };

        ImGui_Text("App ID:        {}", toplevel ? toplevel->current.app_id : "N/A");
        ImGui_Text("Title:         {}", toplevel ? toplevel->current.title : "N/A");

        {
            bool force_rescale = toplevel && toplevel->layout_size.has_value();
            if (ImGui::Checkbox("Rescale", &force_rescale) && toplevel) {
                wroc_toplevel_force_rescale(toplevel, force_rescale);
            }
        }

        ImGui::SameLine(second_column_offset);

        {
            bool fullscreen = toplevel && toplevel->fullscreen.output;
            if (ImGui::Checkbox("Fullscreen", &fullscreen) && toplevel) {
                if (fullscreen) {
                    auto output = wroc_output_layout_output_for_surface(
                        server->output_layout.get(), toplevel->surface.get());
                    if (output) {
                        wroc_toplevel_set_fullscreen(toplevel, output);
                    }
                } else {
                    wroc_toplevel_set_fullscreen(toplevel, nullptr);
                }
            }
        }

        {
            rect2i32 geom = toplevel ? wroc_xdg_surface_get_geometry(toplevel->base()) : rect2i32{};
            ImGui::SetNextItemWidth(208);
            int size[2] = { geom.extent.x, geom.extent.y };
            ImGui::InputInt2("Geometry", size);

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                size[0] = std::clamp(size[0], 100, 7680);
                size[1] = std::clamp(size[1], 100, 4320);
                wroc_toplevel_set_size(toplevel, vec2i32{size[0], size[1]});
                wroc_toplevel_flush_configure(toplevel);
            }
        }

        {
            ImGui::Checkbox("Force Accel", toplevel ? &toplevel->tweaks.force_accel : ptr_to(false));
        }
    }

    category_separator();

    // Frametime

    {
        auto& stats = debug->stats;

        stats.frames++;
        auto now = std::chrono::steady_clock::now();

        if (now - stats.last_report > 0.5s) {
            auto dur = now - stats.last_report;
            auto seconds = std::chrono::duration_cast<std::chrono::duration<f64>>(dur).count();

            stats.frametime = dur / stats.frames;
            stats.fps = stats.frames / seconds;

            stats.last_report = now;
            stats.frames = 0;

            stats.events_per_second = glm::round((server->event_loop->stats.events_handled - stats.last_events_handled) / seconds);
            stats.last_events_handled = server->event_loop->stats.events_handled;

            stats.poll_waits_per_second = glm::round((server->event_loop->stats.poll_waits - stats.last_poll_waits) / seconds);
            stats.last_poll_waits = server->event_loop->stats.poll_waits;
        }

        ImGui_Text("Date:          {}", core_to_string(std::chrono::system_clock::now(), core_time_format::date_pretty));
        ImGui_Text("Time:          {}", core_to_string(std::chrono::system_clock::now(), core_time_format::datetime));
        ImGui_Text("Elapsed:       {}", core_to_string(std::chrono::milliseconds(wroc_get_elapsed_milliseconds())));
        ImGui_Text("Events:        {}/s ({}/s)", stats.events_per_second, stats.poll_waits_per_second);
        ImGui_Text("Frametime:     {} ({:.2f} Hz)", core_to_string(stats.frametime), stats.fps);
    }

    category_separator();

    // Frame control

    bool try_dispatch_frames = false;

    try_dispatch_frames |= ImGui::Checkbox("FPS Limit:", &server->renderer->fps_limit_enabled);
    ImGui::SameLine(second_column_offset);
    ImGui::SetNextItemWidth(103);
    try_dispatch_frames |= ImGui::DragInt("###max-framerate-slider", &server->renderer->fps_limit, 1, 30, 240, "%d", ImGuiSliderFlags_AlwaysClamp);

    ImGui::BeginDisabled(server->backend->type == wroc_backend_type::direct);
    try_dispatch_frames |= ImGui::Checkbox("V-Sync", &server->renderer->vsync);
    ImGui::EndDisabled();

    ImGui::SameLine(second_column_offset);
    ImGui::Checkbox("Noisy Frames", &server->debug.noisy_frames);

    {
        ImGui::SetNextItemWidth(second_column_offset + 11);
        u32 current_fif = 0;
        for (auto& output : server->output_layout->outputs) {
            current_fif = std::max(current_fif, output->frames_in_flight);
        }
        auto label = std::format("Max Frames in Flight ({})###max-frames-in-flight", current_fif);
        int max_fif = server->renderer->max_frames_in_flight;
        if (ImGui::SliderInt(label.c_str(), &max_fif, 1, 6)) {
            server->renderer->max_frames_in_flight = max_fif;
            try_dispatch_frames = true;
        }
    }

    {
        ImGui::SetNextItemWidth(second_column_offset + 11);
        usz current_images = 0;
        for (auto& output : server->output_layout->outputs) {
            current_images = std::max(current_images, output->swapchain.images_in_flight + output->swapchain.free_images.size());
        }
        auto label = std::format("Max Swapchain Images ({})###max-swapchain-images", current_images);
        int max_images = server->renderer->max_swapchain_images;
        if (ImGui::SliderInt(label.c_str(), &max_images, 2, 6)) {
            server->renderer->max_swapchain_images = max_images;
            try_dispatch_frames = true;
        }
    }

    if (try_dispatch_frames) {
        for (auto& output : server->output_layout->outputs) {
            wroc_output_try_dispatch_frame_later(output.get());
        }
    }

    category_separator();

    // Allocations

    ImGui_Text("Objects:       {}/{}",
        core_registry_get_stats().active_allocations,
        core_registry_get_stats().inactive_allocations);

    ImGui::Separator();

    auto* gpu = server->gpu;
    ImGui_Text("GPU.Images:   {} ({})", gpu->stats.active_images,  core_to_string(core_fmt_bytes(gpu->stats.active_image_memory)));
    ImGui_Text("GPU.Buffers:  {} ({})", gpu->stats.active_buffers, core_to_string(core_fmt_bytes(gpu->stats.active_buffer_memory)));

    ImGui::Separator();

    // Surfaces

    {
        core_enum_map<wroc_surface_role, u32> counts = {};
        for (auto* surface : server->surfaces) {
            counts[surface->role]++;
        }

        ImGui_Text("Surfaces:      {}", server->surfaces.size());
        for (auto[role, str] : {
            std::make_pair(wroc_surface_role::none,         "Unassigned:"),
            std::make_pair(wroc_surface_role::cursor,       "Cursor:    "),
            std::make_pair(wroc_surface_role::drag_icon,    "Drag Icon: "),
            std::make_pair(wroc_surface_role::subsurface,   "Subsurface:"),
            std::make_pair(wroc_surface_role::xdg_toplevel, "Toplevel:  "),
            std::make_pair(wroc_surface_role::xdg_popup,    "Popup:     "),
        }) {
            if (auto count = counts[role]) ImGui_Text("  {}  {}", str, count);
            else                           ImGui_Text("  {}",     str);
        }
    }

    category_separator();

    // Screenshot

    if (ImGui::Button("Screenshot")) {
        auto& outputs = server->output_layout->outputs;
        if (server->renderer->screenshot_queued) {
            log_error("Screenshot already queued");
        } else if (!outputs.empty()) {
            server->renderer->screenshot_queued = true;
            aabb2f64 rect = outputs.front()->layout_rect;
            for (auto& output : outputs) {
                rect = core_aabb_outer<f64>(rect, output->layout_rect);
            }
            server->imgui->on_render.emplace_back([rect] {
                wroc_screenshot(rect);
            });
        }
    }

    category_separator();
    ImGui::Dummy(ImVec2(0.f, 0.f));

    // Application launch

    auto& launch = debug->launch;

    ImGui::SetNextItemWidth(30.f);
    ImGui::InputText("DISPLAY##x11-socket", &launch.x11_socket);
    ImGui::SameLine(second_column_offset);

    ImGui::SetNextItemWidth(76.f);
    ImGui::Combo("X11", &launch.x11_mode, "Disable\0Enable\0Force\0");

    bool run = ImGui::InputText("##launch", &launch.launch_text, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Run") || run) {
        wroc_spawn("sh", {"sh", "-c", launch.launch_text.c_str()}, {
            wroc_spawn_x11_action{
                launch.x11_mode >= x11_enabled ? launch.x11_socket.c_str() : nullptr,
                launch.x11_mode == x11_forced
            }
        });
    }
}

void wroc_imgui_show_log(wroc_debug_gui* debug)
{
    if (!debug->show_log_window) return;

    auto history = core_log_get_history();

    defer { ImGui::End(); };
    if (!ImGui::Begin(std::format("Log ({} entries - {})###Log",
            history.entries.size(),
            core_to_string(core_fmt_bytes(history.entries.size_bytes() + history.buffer_size))
        ).c_str(),
        &debug->show_log_window, ImGuiWindowFlags_NoCollapse)) return;

    auto scroll_to_bottom = ImGui::Button("Follow");

    ImGui::SameLine();
    bool history_enabled = core_log_is_history_enabled();
    if (ImGui::Checkbox("Enabled", &history_enabled)) {
        core_log_set_history_enabled(history_enabled);
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        core_log_clear_history();
    }

    ImGui::SameLine();
    ImGui::Checkbox("Details", &debug->log.show_details);

    static constexpr auto make_color = [](std::string_view hex) { return vec4f32(core_color_from_hex(hex)) / 255.f; };
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

    auto draw_entry = [&](int id, const core_log_entry& entry, bool* hovered = nullptr) ->  bool {
        ImVec4 color;
        const char* format;

        switch (entry.level) {
            break;case core_log_level::trace: format = "[TRACE] %.*s"; color = to_imvec(color_trace);
            break;case core_log_level::debug: format = "[DEBUG] %.*s"; color = to_imvec(color_debug);
            break;case core_log_level::info:  format = " [INFO] %.*s"; color = to_imvec(color_info);
            break;case core_log_level::warn:  format = " [WARN] %.*s"; color = to_imvec(color_warn);
            break;case core_log_level::error: format = "[ERROR] %.*s"; color = to_imvec(color_error);
            break;case core_log_level::fatal: format = "[FATAL] %.*s"; color = to_imvec(color_fatal);
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

            bool is_hovered = i == debug->log.selected;
            if (draw_entry(i, *entry, &is_hovered)) {
                debug->log.selected = (debug->log.selected == i) ? -1 : i;
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

    if (debug->log.selected >= i64(history.entries.size())) {
        debug->log.selected = -1;
    }

    if (debug->log.show_details) {
        defer { ImGui::End(); };
        if (ImGui::Begin(debug->log.selected >= 0
                ? "Log Details (locked)###Log Details"
                : "Log Details###Log Details", &debug->log.show_details, ImGuiWindowFlags_HorizontalScrollbar)) {
            auto effective = debug->log.selected >= 0 ? debug->log.selected : hovered;
            if (effective != -1) {
                base_x = ImGui::GetCursorPosX();
                auto& entry = history.entries[effective];

                int section_id = 0;
                {
                    // Timestamp

                    ImGui::PushID(section_id++);
                    ImGui::Selectable("##selectable", false, ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_SpanAllColumns);
                    ImGui::SameLine();
                    ImGui::TextUnformatted(core_to_string(entry.timestamp, core_time_format::datetime_ms).c_str());
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

    if (!debug->log.show_details) {
        debug->log.selected = -1;
    }
}

void wroc_debug_gui_frame(wroc_debug_gui* debug)
{
    wroc_imgui_show_debug(debug);
    wroc_imgui_show_log(debug);
    if (debug->show_demo_window) {
        ImGui::ShowDemoWindow(&debug->show_demo_window);
    }
}
