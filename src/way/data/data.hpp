#pragma once

#include "../util.hpp"

#include "scene/scene.hpp"

struct WayClient;
struct WaySeatClient;

struct WayDataSource : WayObject
{
    WayClient* client;

    WayResource resource;

    Ref<SceneDataSource> source;
};

struct WayDataOffer : WayObject
{
    WaySeatClient* seat_client;

    WayResource resource;

    Ref<SceneDataSource> source;
};

void way_data_offer_selection(WaySeatClient*);

// -----------------------------------------------------------------------------

WAY_INTERFACE_DECLARE(wl_data_device_manager, 3);
WAY_INTERFACE_DECLARE(wl_data_offer);
WAY_INTERFACE_DECLARE(wl_data_source);
WAY_INTERFACE_DECLARE(wl_data_device);
