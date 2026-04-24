#include "../roc.hpp"

#include "way/server.hpp"

struct RocLauncher
{
    Roc* roc;

    Ref<Ui> ui;

    std::vector<struct WmLauncherApp> apps;
    std::string filter;
    const WmLauncherApp* selected = 0;

    Ref<SeatEventFilter> event_filter;

    bool show = false;
    bool grab_focus = false;

    ~RocLauncher();
};

struct WmLauncherApp
{
    GAppInfo* app_info;
    std::string display_name;
    std::string filter_string;

    bool shown;
};

static
void clear_apps(RocLauncher* launcher)
{
    for (auto& entry : launcher->apps) g_object_unref(entry.app_info);
    launcher->apps.clear();
}

RocLauncher::~RocLauncher()
{
    clear_apps(this);
}

static
auto match_string(std::string haystack, std::string needle) -> bool
{
    if (needle.empty()) return true;

    auto it = std::ranges::search(haystack, needle, [](char a, char b) {
        return std::tolower(a) == std::tolower(b);
    });

    return !it.empty();
}

static
void filter(RocLauncher* launcher, bool up, bool down)
{
    const WmLauncherApp* first_matched = nullptr;
    const WmLauncherApp* last_matched = nullptr;
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

static
void scan_apps(RocLauncher* launcher)
{
    clear_apps(launcher);

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

    std::ranges::sort(launcher->apps, std::less{}, &WmLauncherApp::display_name);

    filter(launcher, false, false);
}

static
void show(RocLauncher* launcher)
{
    launcher->show = true;
    launcher->grab_focus = true;
    launcher->filter = {};
    ui_request_frame(launcher->ui.get());

    scan_apps(launcher);
}

static
void frame(RocLauncher* launcher);

auto roc_init_launcher(Roc* roc) -> Ref<void>
{
    auto launcher = ref_create<RocLauncher>();
    launcher->roc = roc;

    launcher->ui = ui_create(roc->gpu, roc->wm, roc->app_share / "launcher");
    ui_set_frame_handler(launcher->ui.get(), [launcher = launcher.get()] {
        frame(launcher);
    });

    launcher->event_filter = seat_add_event_filter(wm_get_seat(roc->wm), [launcher = launcher.get()](SeatEvent* event) -> SeatEventFilterResult {
        if (event->type != SeatEventType::keyboard_key) return {};

        auto key = event->keyboard.key;
        if (!key.pressed || key.code != KEY_D) return {};

        auto mods = seat_keyboard_get_modifiers(event->keyboard.keyboard);
        if (!mods.contains(launcher->roc->main_mod)) return {};

        show(launcher);
        return SeatEventFilterResult::capture;
    });

    return launcher;
}

static
void launch(RocLauncher* launcher, WmLauncherApp& app)
{
    auto* name = g_app_info_get_display_name(app.app_info) ?: g_app_info_get_name(app.app_info);
    log_info("Running: {}", name);
    log_info("  command line: {}", g_app_info_get_commandline(app.app_info) ?: "");
    log_info("  WAYLAND_DISPLAY = {}", launcher->roc->way->socket_name);

    auto* ctx = g_app_launch_context_new();
    defer { g_object_unref(ctx); };

    g_app_launch_context_setenv(ctx, "WAYLAND_DISPLAY", launcher->roc->way->socket_name.c_str());
    if (!launcher->roc->xwayland_socket.empty()) {
        g_app_launch_context_setenv(ctx, "DISPLAY", launcher->roc->xwayland_socket.c_str());
    } else {
        g_app_launch_context_unsetenv(ctx, "DISPLAY");
    }

    GError* err = nullptr;
    if (!g_app_info_launch(app.app_info, nullptr, ctx, &err)) {
        log_error("Error launching {}: {}", name, err->message);
    }

    launcher->show = false;
}

static
void frame(RocLauncher* launcher)
{
    if (!launcher->show) return;

    // Window

    auto outputs = wm_list_outputs(launcher->roc->wm);
    if (outputs.empty()) return;

    auto workarea = wm_output_get_viewport(outputs.front());
    auto center = workarea.origin + workarea.extent * 0.5f;

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
            launch(launcher, app);
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
