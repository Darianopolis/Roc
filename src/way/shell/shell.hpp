#pragma once

#include "../util.hpp"

#include <wayland/server/xdg-shell.h>

struct WaySurface;
struct WaySurfaceState;

// -----------------------------------------------------------------------------

void way_xdg_surface_apply(WaySurface*, WaySurfaceState&);
void way_xdg_surface_configure(WaySurface*);

// -----------------------------------------------------------------------------

void way_toplevel_apply(        WaySurface*, WaySurfaceState&);
void way_toplevel_on_map_change(WaySurface*, bool mapped);
void way_toplevel_on_reposition(WaySurface*, rect2f32 frame, vec2f32 gravity);
void way_toplevel_on_close(     WaySurface*);

// -----------------------------------------------------------------------------

void way_create_positioner(wl_client*, wl_resource*, u32 id);
void way_get_popup(        wl_client*, wl_resource*, u32 id,
                           wl_resource* parent, wl_resource* positioner);

void way_popup_apply(WaySurface*, WaySurfaceState&);

// -----------------------------------------------------------------------------

WAY_INTERFACE_DECLARE(xdg_wm_base, 7);
WAY_INTERFACE_DECLARE(xdg_surface);
WAY_INTERFACE_DECLARE(xdg_toplevel);
WAY_INTERFACE_DECLARE(xdg_positioner);
WAY_INTERFACE_DECLARE(xdg_popup);
