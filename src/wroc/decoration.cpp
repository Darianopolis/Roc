#include "protocol.hpp"

const u32 wroc_zxdg_decoration_manager_v1_version = 1;

struct wroc_decoration : wroc_object
{
    core::Weak<wroc_toplevel> toplevel;
    wroc_resource resource;
};

static
void send_mode(wroc_decoration* decoration)
{
    if (!decoration->toplevel) return;

    // We always tell the client that we'll provide server-side decorations.
    // Since our decorations are just a border, it doesn't matter if a client decides to draw its own title bar or not.
    static constexpr auto mode = ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;

    wroc_send(zxdg_toplevel_decoration_v1_send_configure, decoration->resource, mode);
    wroc_xdg_surface_flush_configure(decoration->toplevel->base());
}

static
void get_toplevel_decoration(wl_client* client, wl_resource* resource, u32 id, wl_resource* _toplevel)
{
    auto* toplevel = wroc_get_userdata<wroc_toplevel>(_toplevel);
    auto decoration = core::create_unsafe<wroc_decoration>();
    decoration->toplevel = toplevel;
    decoration->resource = wroc_resource_create(client, &zxdg_toplevel_decoration_v1_interface, wl_resource_get_version(resource), id);
    wroc_resource_set_implementation_refcounted(decoration->resource, &wroc_zxdg_toplevel_decoration_v1_impl, decoration);

    send_mode(decoration);
}

const struct zxdg_decoration_manager_v1_interface wroc_zxdg_decoration_manager_v1_impl = {
    .destroy = wroc_simple_resource_destroy_callback,
    .get_toplevel_decoration = get_toplevel_decoration,
};

void wroc_zxdg_decoration_manager_v1_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto resource = wroc_resource_create(client, &zxdg_decoration_manager_v1_interface, version, id);
    wroc_resource_set_implementation(resource, &wroc_zxdg_decoration_manager_v1_impl, nullptr);
}

// -----------------------------------------------------------------------------

static
void set_mode(wl_client* client, wl_resource* resource, u32 mode)
{
    auto* decoration = wroc_get_userdata<wroc_decoration>(resource);
    send_mode(decoration);
}

static
void unset_mode(wl_client* client, wl_resource* resource)
{
    auto* decoration = wroc_get_userdata<wroc_decoration>(resource);
    send_mode(decoration);
}

const struct zxdg_toplevel_decoration_v1_interface wroc_zxdg_toplevel_decoration_v1_impl = {
    .destroy = wroc_simple_resource_destroy_callback,
    .set_mode = set_mode,
    .unset_mode = unset_mode,
};

// -----------------------------------------------------------------------------

const u32 wroc_org_kde_kwin_server_decoration_manager_version = 1;

static constexpr auto kde_decoration_mode = ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_MODE_SERVER;

static
void kwin_decoration_manager_create(wl_client* client, wl_resource* resource, u32 id, wl_resource* surface)
{
    auto decoration = wroc_resource_create(client, &org_kde_kwin_server_decoration_interface, wl_resource_get_version(resource), id);
    wroc_resource_set_implementation(decoration, &wroc_org_kde_kwin_server_decoration_impl, nullptr);

    wroc_send(org_kde_kwin_server_decoration_send_mode, decoration, kde_decoration_mode);
}

const struct org_kde_kwin_server_decoration_manager_interface wroc_org_kde_kwin_server_decoration_manager_impl = {
    .create = kwin_decoration_manager_create,
};

void wroc_org_kde_kwin_server_decoration_manager_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto resource = wroc_resource_create(client, &org_kde_kwin_server_decoration_manager_interface, version, id);
    wroc_resource_set_implementation(resource, &wroc_org_kde_kwin_server_decoration_manager_impl, nullptr);

    wroc_send(org_kde_kwin_server_decoration_manager_send_default_mode, resource, kde_decoration_mode);
}

static
void request_mode(wl_client* client, wl_resource* resource, u32 _mode)
{
    auto mode = org_kde_kwin_server_decoration_manager_mode(_mode);
    if (mode != kde_decoration_mode) {
        log_warn("org.kde.kwin-server-decoration :: client requested mode: {}", core::to_string(mode));
    }
}

const struct org_kde_kwin_server_decoration_interface wroc_org_kde_kwin_server_decoration_impl = {
    .release = wroc_simple_resource_destroy_callback,
    .request_mode = request_mode,
};
