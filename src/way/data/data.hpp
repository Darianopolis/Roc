#pragma once

#include "../util.hpp"

#include <seat/seat.hpp>

struct WayClient;
struct WaySeatClient;

struct WayDataSource : SeatDataSource
{
    WayClient* client;

    WayResource resource;

    virtual void on_cancel() final override;
    virtual void on_send(const char* mime_type, fd_t target) final override;

    ~WayDataSource();
};

struct WayDataSourceHolder : WayObject
{
    Ref<WayDataSource> source;
};

struct WayDataOffer : WayObject
{
    WaySeatClient* seat_client;

    WayResource resource;

    Ref<SeatDataSource> source;
};

void way_data_offer_selection(WaySeatClient*);

// -----------------------------------------------------------------------------

WAY_INTERFACE_DECLARE(wl_data_device_manager, 3);
WAY_INTERFACE_DECLARE(wl_data_offer);
WAY_INTERFACE_DECLARE(wl_data_source);
WAY_INTERFACE_DECLARE(wl_data_device);
