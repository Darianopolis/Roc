#include "internal.hpp"

static
void get_pointer(wl_client* client, wl_resource* cursor_shape_manager, u32 id, wl_resource* wl_pointer)
{
    auto* seat_client = way_get_userdata<way_seat_client>(wl_pointer);
    way_resource_create_refcounted(wp_cursor_shape_device_v1, client, cursor_shape_manager, id, seat_client);
}

WAY_INTERFACE(wp_cursor_shape_manager_v1) = {
    .destroy = way_simple_destroy,
    .get_pointer = get_pointer,
    WAY_STUB(get_tablet_tool_v2),
};

WAY_BIND_GLOBAL(wp_cursor_shape_manager_v1, bind)
{
    way_resource_create_unsafe(wp_cursor_shape_manager_v1, bind.client, bind.version, bind.id, way_get_userdata<way_server>(bind.data));
}

// -----------------------------------------------------------------------------

static
auto cursor_shape_to_xcursor(wp_cursor_shape_device_v1_shape shape) -> const char*
{
    switch (shape) {
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT:       return "default";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CONTEXT_MENU:  return "context-menu";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_HELP:          return "help";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER:       return "pointer";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_PROGRESS:      return "progress";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT:          return "wait";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CELL:          return "cell";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR:     return "crosshair";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT:          return "text";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_VERTICAL_TEXT: return "vertical-text";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALIAS:         return "alias";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COPY:          return "copy";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE:          return "move";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NO_DROP:       return "no-drop";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED:   return "not-allowed";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB:          return "grab";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING:      return "grabbing";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE:      return "e-resize";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE:      return "n-resize";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE:     return "ne-resize";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE:     return "nw-resize";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE:      return "s-resize";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE:     return "se-resize";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE:     return "sw-resize";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE:      return "w-resize";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE:     return "ew-resize";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE:     return "ns-resize";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE:   return "nesw-resize";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE:   return "nwse-resize";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COL_RESIZE:    return "col-resize";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ROW_RESIZE:    return "row-resize";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_SCROLL:    return "all-scroll";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ZOOM_IN:       return "zoom-in";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ZOOM_OUT:      return "zoom-out";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DND_ASK:       return "dnd-ask";
        break;case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_RESIZE:    return "all-resize";
    }

    return nullptr;
}

static
void set_shape(wl_client* client, wl_resource* resource, u32 serial, u32 shape)
{
    auto* seat_client = way_get_userdata<way_seat_client>(resource);
    auto* seat = seat_client->seat;
    if (seat->pointer.scene) {
        scene_pointer_set_xcursor(seat->pointer.scene, cursor_shape_to_xcursor(wp_cursor_shape_device_v1_shape(shape)));
    }
}

WAY_INTERFACE(wp_cursor_shape_device_v1) = {
    .destroy = way_simple_destroy,
    .set_shape = set_shape,
};
