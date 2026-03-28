#include "internal.hpp"

struct way_decoration : WayObject
{
    Weak<WaySurface> surface;
    WayResource resource;
};

static
void send_mode(way_decoration* decoration)
{
    if (!decoration->surface) return;

    // We always tell the client that we'll provide server-side decorations.
    // Since our decorations are just a border, it doesn't matter if a client decides to draw its own title bar or not.
    static constexpr auto mode = ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;

    way_send(decoration->surface->client->server, zxdg_toplevel_decoration_v1_send_configure, decoration->resource, mode);
    way_xdg_surface_configure(decoration->surface.get());
}

static
void get_toplevel_decoration(wl_client* client, wl_resource* resource, u32 id, wl_resource* _toplevel)
{
    auto* surface = way_get_userdata<WaySurface>(_toplevel);
    auto decoration = ref_create<way_decoration>();
    decoration->surface = surface;
    decoration->resource = way_resource_create_refcounted(zxdg_toplevel_decoration_v1, client, resource, id, decoration.get());

    send_mode(decoration.get());
}

WAY_INTERFACE(zxdg_decoration_manager_v1) = {
    .destroy = way_simple_destroy,
    .get_toplevel_decoration = get_toplevel_decoration,
};

WAY_BIND_GLOBAL(zxdg_decoration_manager_v1, bind)
{
    way_resource_create_unsafe(zxdg_decoration_manager_v1, bind.client, bind.version, bind.id, way_get_userdata<WayServer>(bind.data));
}

// -----------------------------------------------------------------------------

static
void set_mode(wl_client* client, wl_resource* resource, u32 mode)
{
    auto* decoration = way_get_userdata<way_decoration>(resource);
    send_mode(decoration);
}

static
void unset_mode(wl_client* client, wl_resource* resource)
{
    auto* decoration = way_get_userdata<way_decoration>(resource);
    send_mode(decoration);
}

WAY_INTERFACE(zxdg_toplevel_decoration_v1) {
    .destroy = way_simple_destroy,
    .set_mode = set_mode,
    .unset_mode = unset_mode,
};

// -----------------------------------------------------------------------------

static constexpr auto kde_decoration_mode = ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_MODE_SERVER;

static
void kwin_decoration_manager_create(wl_client* client, wl_resource* resource, u32 id, wl_resource* surface)
{
    auto* server = way_get_userdata<WayServer>(resource);
    auto decoration = way_resource_create_unsafe(org_kde_kwin_server_decoration, client, resource, id, server);

    way_send(server, org_kde_kwin_server_decoration_send_mode, decoration, kde_decoration_mode);
}

WAY_INTERFACE(org_kde_kwin_server_decoration_manager) {
    .create = kwin_decoration_manager_create,
};

WAY_BIND_GLOBAL(org_kde_kwin_server_decoration_manager, bind)
{
    auto* server = way_get_userdata<WayServer>(bind.data);
    auto resource = way_resource_create_unsafe(org_kde_kwin_server_decoration_manager, bind.client, bind.version, bind.id, server);
    way_send(server, org_kde_kwin_server_decoration_manager_send_default_mode, resource, kde_decoration_mode);
}

static
void request_mode(wl_client* client, wl_resource* resource, u32 _mode)
{
    auto mode = org_kde_kwin_server_decoration_manager_mode(_mode);
    if (mode != kde_decoration_mode) {
        log_warn("org.kde.kwin-server-decoration :: client requested mode: {}", mode);
    }
}

WAY_INTERFACE(org_kde_kwin_server_decoration) {
    .release = way_simple_destroy,
    .request_mode = request_mode,
};
