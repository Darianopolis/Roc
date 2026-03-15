#include "protocol.hpp"

static
std::vector<std::string> wroc_cursor_list_themes()
{
    std::vector<std::string> themes;

    auto* paths_raw = XcursorLibraryPath();
    if (!paths_raw) return themes;

    std::stringstream paths(paths_raw);
    std::string dir;
    while (std::getline(paths, dir, ':')) {
        if (!std::filesystem::exists(dir)) continue;
        for (auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_directory()) continue;

            auto theme = entry.path();
            if (       std::filesystem::exists(theme / "cursors")
                    && std::filesystem::exists(theme / "index.theme")) {
                themes.push_back(theme.filename().string());
            }
        }
    }

    return themes;
}

void wroc_cursor_create()
{
    server->cursor = core::create<wroc_cursor>();
    auto cursor = server->cursor.get();

    auto themes = wroc_cursor_list_themes();
    for (auto& theme : themes) {
        log_info("Found theme: {}", theme);
    }

    const char* theme = nullptr;

    auto try_theme = [&](const char* name) {
        if (theme) return;
        if (std::ranges::contains(themes, name)) theme = name;
    };

    // Prefer breeze > Adwaita
    try_theme("breeze_cursors");
    try_theme("Adwaita");

    log_info("Using theme: {}", theme ?: "<xcursor-fallback>");

    static constexpr int xcursor_size = 24;

    wroc_setenv("XCURSOR_SIZE", std::to_string(xcursor_size).c_str(), wroc_setenv_option::system_wide);

    if (theme) {
        wroc_setenv("XCURSOR_THEME", theme, wroc_setenv_option::system_wide);
    }

    cursor->theme = theme;
    cursor->size = xcursor_size;
}

void set_cursor_states(wl_client* client, wroc_surface* surface)
{
    // TODO: Track and update only last focused surface
    //       OR track cursor at the client level directly
    for (auto* target_surface : server->surfaces) {
        if (!target_surface->resource) continue;
        if (wroc_resource_get_client(target_surface->resource) != client) continue;
        if (target_surface->role == wroc_surface_role::cursor) continue;

        target_surface->cursor = surface;
    }
}

void wroc_cursor_set(wroc_cursor* cursor, wl_client* client, wroc_surface* surface, vec2i32 hotspot)
{
    bool created;
    auto* cursor_surface = surface ? wroc_surface_get_or_create_addon<wroc_cursor_surface>(surface, &created) : nullptr;
    if (cursor_surface) {
        surface->buffer_dst.origin = -hotspot;
        wroc_surface_update_map_state(surface);
    }
    set_cursor_states(client, surface);
}

// -----------------------------------------------------------------------------

static constexpr auto shape_names = [] {
    core::EnumMap<wp_cursor_shape_device_v1_shape, const char*> shapes;
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT]       = "default";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CONTEXT_MENU]  = "context-menu";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_HELP]          = "help";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER]       = "pointer";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_PROGRESS]      = "progress";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT]          = "wait";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CELL]          = "cell";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR]     = "crosshair";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT]          = "text";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_VERTICAL_TEXT] = "vertical-text";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALIAS]         = "alias";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COPY]          = "copy";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE]          = "move";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NO_DROP]       = "no-drop";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED]   = "not-allowed";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB]          = "grab";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING]      = "grabbing";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE]      = "e-resize";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE]      = "n-resize";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE]     = "ne-resize";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE]     = "nw-resize";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE]      = "s-resize";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE]     = "se-resize";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE]     = "sw-resize";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE]      = "w-resize";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE]     = "ew-resize";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE]     = "ns-resize";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE]   = "nesw-resize";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE]   = "nwse-resize";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COL_RESIZE]    = "col-resize";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ROW_RESIZE]    = "row-resize";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_SCROLL]    = "all-scroll";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ZOOM_IN]       = "zoom-in";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ZOOM_OUT]      = "zoom-out";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DND_ASK]       = "dnd-ask";
    shapes[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_RESIZE]    = "all-resize";
    return shapes;
}();

wroc_surface* wroc_cursor_get_shape(wroc_cursor* cursor, wp_cursor_shape_device_v1_shape shape)
{
    if (!shape) return nullptr;

    auto& surface = cursor->shapes[shape];
    if (surface) return surface.get();

    log_info("Loading cursor shape {} \"{}\"", core::to_string(shape), shape_names[shape]);

    // TODO: This is a a hacky mess, we should implement a proper way to have locally managed surfaces
    //       We'll want something like this for integrating compositor UI with the surface stack properly later.

    surface = core::create<wroc_surface>();
    surface->current.surface_stack.emplace_back(surface.get());

    auto cursor_surface = core::create<wroc_cursor_surface>();
    wroc_surface_put_addon(surface.get(), cursor_surface.get());

    auto cursor_buffer = core::create<wroc_shm_buffer>();
    cursor_buffer->released = false;
    surface->current.buffer = cursor_buffer;
    surface->current.buffer_lock = cursor_buffer->lock();
    surface->mapped = true;

    XcursorImage* image = XcursorLibraryLoadImage(shape_names[shape], cursor->theme, cursor->size);
    if (!image) {
        core_assert(shape != WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);

        log_error("  failed to load Xcursor image, using fallback");
        auto default_surface = wroc_cursor_get_shape(cursor, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
        surface = default_surface;
        return default_surface;
    }
    defer {
        XcursorImageDestroy(image);
    };
    log_info("  size ({}, {}) hotspot ({}, {})", image->width, image->height, image->xhot, image->yhot);

    cursor_buffer->image = gpu::image::create(server->gpu, {
        .extent = {image->width, image->height},
        .format = gpu::format::from_drm(DRM_FORMAT_ABGR8888),
        .usage = gpu::ImageUsage::texture | gpu::ImageUsage::transfer
    });
    gpu::copy_memory_to_image(cursor_buffer->image.get(),
        {image->pixels, image->width * image->height * 4},
        {{{image->width, image->height}}});

    surface->buffer_dst = {{-image->xhot, -image->yhot}, {image->width, image->height}, core::xywh};
    surface->buffer_src = {{}, {image->width, image->height}, core::xywh};

    return surface.get();
}

void wroc_cursor_set(wroc_cursor* cursor, wl_client* client, wp_cursor_shape_device_v1_shape shape)
{
    set_cursor_states(client, wroc_cursor_get_shape(cursor, shape));
}

wroc_surface* wroc_cursor_get_current(wroc_seat_pointer* pointer, wroc_cursor* cursor)
{
    if (server->interaction_mode != wroc_interaction_mode::normal) {
        // TODO: Per-interaction shapes
        return wroc_cursor_get_shape(cursor, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
    }

    auto pointer_focus = pointer->focused_surface;

    // Only allow client to hide pointer (with null surface) if surface has keyboard focus.
    // This heuristic isn't perfect. A client can get around it by committing a transparent
    // cursor image. But this catches all non-pathological cases in practice.
    bool is_visible = pointer_focus && pointer_focus->cursor && pointer_focus->cursor->mapped;

    return pointer_focus && (is_visible || pointer_focus == server->seat->keyboard->focused_surface)
        ? pointer_focus->cursor.get()
        : wroc_cursor_get_shape(cursor, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
}

// -----------------------------------------------------------------------------

const u32 wroc_wp_cursor_shape_manager_v1_version = 2;

static
void cursor_manager_get_pointer(wl_client* client, wl_resource* cursor_shape_manager, u32 id, wl_resource* pointer)
{
    auto resource = wroc_resource_create(client, &wp_cursor_shape_device_v1_interface, wl_resource_get_version(cursor_shape_manager), id);
    wroc_resource_set_implementation(resource, &wroc_wp_cursor_shape_device_v1_impl, nullptr);
}

const struct wp_cursor_shape_manager_v1_interface wroc_wp_cursor_shape_manager_v1_impl = {
    .destroy = wroc_simple_resource_destroy_callback,
    .get_pointer = cursor_manager_get_pointer,
    WROC_STUB(get_tablet_tool_v2),
};

void wroc_wp_cursor_shape_manager_v1_bind_global(wl_client* client, void*, u32 version, u32 id)
{
    auto resource = wroc_resource_create(client, &wp_cursor_shape_manager_v1_interface, version, id);
    wroc_resource_set_implementation(resource, &wroc_wp_cursor_shape_manager_v1_impl, nullptr);
}

static
void cursor_device_set_shape(wl_client* client, wl_resource* resource, u32 serial, u32 shape)
{
    wroc_cursor_set(server->cursor.get(), client, wp_cursor_shape_device_v1_shape(shape));
}

const struct wp_cursor_shape_device_v1_interface wroc_wp_cursor_shape_device_v1_impl = {
    .destroy = wroc_simple_resource_destroy_callback,
    .set_shape = cursor_device_set_shape,
};
