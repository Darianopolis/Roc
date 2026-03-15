#include <wroc/wroc.hpp>
#include <wroc/event.hpp>

struct wroc_launcher : wroc_object
{
    std::vector<struct wroc_launcher_app> apps;
    std::string filter;
    const wroc_launcher_app* selected = 0;

    bool show = false;
    bool grab_focus = false;

    ~wroc_launcher();
};

struct wroc_launcher_app
{
    GAppInfo* app_info;
    std::string display_name;
    std::string filter_string;

    bool shown;
};

wroc_launcher::~wroc_launcher()
{
    for (auto& entry : apps) g_object_unref(entry.app_info);
}

static
bool match_string(std::string haystack, std::string needle)
{
    if (needle.empty()) return true;

    auto it = std::ranges::search(haystack, needle, [](char a, char b) {
        return std::tolower(a) == std::tolower(b);
    });

    return !it.empty();
}

static
void filter(wroc_launcher* launcher, bool up, bool down)
{
    const wroc_launcher_app* first_matched = nullptr;
    const wroc_launcher_app* last_matched = nullptr;
    for (auto& app : launcher->apps) {
        app.shown = match_string(app.filter_string, launcher->filter);

        if (!app.shown) continue;

        if (up && last_matched && &app == launcher->selected) {
            launcher->selected = last_matched;
            up = false;
        }

        if (down && last_matched && last_matched == launcher->selected) {
            launcher->selected = &app;
            down = false;
        }

        if (!first_matched) first_matched = &app;
        last_matched = &app;
    }

    if (!launcher->selected || !launcher->selected->shown) {
        launcher->selected = first_matched;
    }
}

void wroc_launcher_init()
{
    server->launcher = core_create<wroc_launcher>();

    auto* launcher = server->launcher.get();

    auto* apps = g_app_info_get_all();
    defer { g_list_free(apps); };

    for (auto* l = apps; l; l = l->next) {
        GAppInfo* app = G_APP_INFO(l->data);
        defer {
            if (app) g_object_unref(app);
        };

        if (!G_IS_DESKTOP_APP_INFO(app)) continue;
        if (!g_app_info_should_show(app)) continue;
        if (!g_app_info_get_executable(app)) continue;

        auto* desktop = G_DESKTOP_APP_INFO(app);

        auto& entry = launcher->apps.emplace_back();
        entry.app_info = app;
        entry.display_name = g_app_info_get_display_name(app) ?: g_app_info_get_name(app);

        entry.filter_string += g_app_info_get_display_name(app) ?: "";
        entry.filter_string += '\0';
        entry.filter_string += g_app_info_get_executable(app);

        // TODO: Categories
        // log_debug("Categories: {}", g_desktop_app_info_get_categories(desktop) ?: "");

        for (auto* keyword = g_desktop_app_info_get_keywords(desktop); keyword && *keyword; ++keyword) {
            entry.filter_string += '\0';
            entry.filter_string += *keyword;
        }

        std::string_view id = g_app_info_get_id(app) ?: "";
        if (id.ends_with(".desktop")) {
            id.remove_suffix(strlen(".desktop"));
        }
        entry.filter_string += '\0';
        entry.filter_string += id;

        app = nullptr;
    }

    std::ranges::sort(launcher->apps, std::less{}, &wroc_launcher_app::display_name);

    filter(launcher, false, false);
}

static
void wroc_launcher_run(wroc_launcher* launcher, wroc_launcher_app& app)
{
    auto& x11 = server->x11_socket;

    // TODO: Select action on right-click
    wroc_spawn(app.app_info, {
        wroc_spawn_x11_action{x11.empty() ? nullptr : x11.c_str(), false},
    });

    launcher->show = false;
}

bool wroc_launcher_handle_event(wroc_launcher* launcher, const struct wroc_event& event)
{
    if (wroc_event_get_type(event) == wroc_event_type::keyboard_key) {
        auto& key_event = static_cast<const wroc_keyboard_event&>(event);
        if (key_event.key.upper() == XKB_KEY_D && key_event.key.pressed && wroc_get_active_modifiers().contains(wroc_modifier::mod)) {
            log_info("Showing launcher");
            launcher->show = true;
            launcher->grab_focus = true;
            launcher->filter = {};
            return true;
        }
    }

    return false;
}

void wroc_launcher_frame(wroc_launcher* launcher, vec2f64 global_pos)
{
    if (!launcher->show) return;

    // Window

    auto center = global_pos - server->imgui->layout_rect.origin;

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(center.x, center.y), ImGuiCond_Appearing, ImVec2(0.5, 0.5));

    defer { ImGui::End(); };
    bool dont_close = true;
    if (!ImGui::Begin("Launch", &dont_close, ImGuiWindowFlags_NoCollapse)) return;

    bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    if (!dont_close || (focused && ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        launcher->filter = {};
        launcher->show = false;
    }

    auto& io = ImGui::GetIO();

    // Search bar

    if (launcher->grab_focus) {
        ImGui::SetKeyboardFocusHere(0);
    }

    bool check_scroll = false;
    bool up = focused && ImGui::IsKeyPressed(ImGuiKey_UpArrow);
    bool down = focused && ImGui::IsKeyPressed(ImGuiKey_DownArrow);
    if (up && down) { up = down = false; }
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputTextWithHint("##filter", "Search applications...", &launcher->filter) || up || down || launcher->grab_focus) {
        filter(launcher, up, down);
        check_scroll = true;
    }
    ImGui::PopItemWidth();

    launcher->grab_focus = false;

    // Results

    ImGui::BeginChild("results", ImVec2(), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    defer { ImGui::EndChild(); };

    bool mouse_moving = io.MouseDelta.x || io.MouseDelta.y || io.MouseWheel;

    for (auto& app : launcher->apps) {
        if (!app.shown) continue;

        bool highlight = &app == launcher->selected;

        int select_flags = 0;
        if (highlight) select_flags |= ImGuiSelectableFlags_Highlight;
        if (!highlight) ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));

        bool selected = highlight && focused && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
        if (ImGui::Selectable(app.display_name.c_str(), selected, select_flags) || selected) {
            wroc_launcher_run(launcher, app);
        }

        if (!highlight) ImGui::PopStyleColor();

        // Only update highlight from hover if mouse is moving/scrolling
        if (ImGui::IsItemHovered() && mouse_moving) {
            highlight = true;
            launcher->selected = &app;
        }

        if (highlight && check_scroll) {
            ImGui::SetScrollHereY();
        }
    }
}
