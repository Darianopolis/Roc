#pragma once

#include "scene.hpp"

#include "io/io.hpp"

// -----------------------------------------------------------------------------

struct Scene
{
    Gpu* gpu;

    struct {
        Ref<GpuShader> vertex;
        Ref<GpuShader> fragment;
        Ref<GpuImage> white;
        Ref<GpuSampler> sampler;
    } render;

    Ref<SceneTree> root;

    std::vector<SceneDamageListener> damage_listeners;

    ~Scene();
};

void scene_render_init(Scene*);

// -----------------------------------------------------------------------------

struct Seat
{
    Ref<SeatKeyboard> keyboard;
    Ref<SeatPointer> pointer;
    std::vector<IoInputDevice*> led_devices;

    Ref<SeatDataSource> selection;

    std::vector<SeatEventFilter*> input_event_filters;
};

// -----------------------------------------------------------------------------

struct SeatClient
{
    std::move_only_function<SeatEventHandlerFn> event_handler;

    u32 input_regions = 0;

    ~SeatClient();
};

void seat_client_post_event(SeatClient*, SeatEvent*);

// -----------------------------------------------------------------------------

struct SeatInputDevice
{
    Seat* seat;

    Weak<SeatInputRegion> focus;
};

// -----------------------------------------------------------------------------

struct SeatKeyboard : SeatInputDevice, SeatKeyboardInfo
{
    CountingSet<u32> pressed;

    Flags<SeatModifier> depressed;
    Flags<SeatModifier> latched;
    Flags<SeatModifier> locked;

    EnumMap<SeatModifier, xkb_mod_mask_t> mod_masks;

    ~SeatKeyboard();
};

auto seat_keyboard_create(Seat*) -> Ref<SeatKeyboard>;

// -----------------------------------------------------------------------------

struct SeatPointer : SeatInputDevice
{
    CountingSet<u32> pressed;

    SeatCursorManager* cursor_manager;
    SceneTree* root;

    Ref<SceneTree> tree;

    std::move_only_function<SeatPointerAccelFn> accel;
};

auto seat_pointer_create(Seat*, SeatCursorManager*, SceneTree* root, SceneTree* layer) -> Ref<SeatPointer>;

// -----------------------------------------------------------------------------

auto seat_find_input_region_at(SceneTree*, vec2f32 pos) -> SeatInputRegion*;

inline
auto seat_get_focus_client(SeatInputRegion* focus)
{
    return focus ? focus->client : nullptr;
}

// -----------------------------------------------------------------------------

struct SeatDataSource
{
    SeatClient* client;

    std::flat_set<std::string> offered;

    SeatDataSourceOps ops;

    ~SeatDataSource();
};

void seat_offer_selection(SeatClient*, SeatDataSource*);

// -----------------------------------------------------------------------------

void scene_post_damage(Scene*, SceneNode*);

// -----------------------------------------------------------------------------

struct SeatEventFilter
{
    Weak<Seat> seat;

    std::move_only_function<SeatEventFilterResult(SeatEvent*)> filter;

    ~SeatEventFilter();
};
