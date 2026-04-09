#pragma once

#include "scene.hpp"

#include "io/io.hpp"

// -----------------------------------------------------------------------------

struct scene_cursor_manager;

void scene_cursor_manager_init(Scene*);

enum class SceneDamageType : u32
{
    visual = 1 << 0,
    input  = 1 << 1,
};

struct Scene
{
    ExecContext* exec;
    Gpu* gpu;

    struct {
        Ref<GpuShader> vertex;
        Ref<GpuShader> fragment;
        Ref<GpuImage> white;
        Ref<GpuSampler> sampler;
    } render;

    Ref<SceneTree> root_tree;
    EnumMap<SceneLayer, Ref<SceneTree>> layers;

    std::vector<SceneDamageListener> damage_listeners;

    std::vector<SceneClient*> clients;

    RefVector<SceneSeat> seats;

    struct {
        Flags<SceneDamageType> queued;
    } damage;

    Ref<scene_cursor_manager> cursor_manager;

    std::vector<SceneEventFilter*> input_event_filters;

    ~Scene();
};

void scene_broadcast_event(Scene*, SceneEvent*);

void scene_render_init(Scene*);

// -----------------------------------------------------------------------------

struct SceneSeat
{
    Scene* scene;

    Ref<SceneKeyboard> keyboard;
    Ref<ScenePointer> pointer;
    std::vector<IoInputDevice*> led_devices;

    Ref<SceneDataSource> selection;
};

void scene_seat_init(Scene*);

auto scene_get_exclusive_seat(Scene*) -> SceneSeat*;

// -----------------------------------------------------------------------------

struct SceneClient
{
    Scene* scene;

    std::move_only_function<SceneEventHandlerFn> event_handler;

    u32 input_regions = 0;

    ~SceneClient();
};

void scene_client_post_event(SceneClient*, SceneEvent*);

// -----------------------------------------------------------------------------

struct SceneInputDevice
{
    SceneInputDeviceType type;
    SceneSeat* seat;

    SceneInputRegion* focus;
};

// -----------------------------------------------------------------------------

struct SceneKeyboard : SceneInputDevice, SceneKeyboardInfo
{
    CountingSet<u32> pressed;

    Flags<SceneModifier> depressed;
    Flags<SceneModifier> latched;
    Flags<SceneModifier> locked;

    EnumMap<SceneModifier, xkb_mod_mask_t> mod_masks;

    ~SceneKeyboard();
};

auto scene_keyboard_create(SceneSeat*) -> Ref<SceneKeyboard>;

// -----------------------------------------------------------------------------

struct ScenePointer : SceneInputDevice
{
    CountingSet<u32> pressed;

    Ref<SceneTree> tree;

    std::move_only_function<ScenePointerAccelFn> accel;
};

auto scene_pointer_create(SceneSeat*) -> Ref<ScenePointer>;

void scene_update_pointers(Scene*);

// -----------------------------------------------------------------------------

auto scene_find_input_region_at(SceneTree*, vec2f32 pos) -> SceneInputRegion*;

inline
auto scene_get_focus_client(SceneInputRegion* focus)
{
    return focus ? focus->client : nullptr;
}

// -----------------------------------------------------------------------------

struct SceneDataSource
{
    SceneClient* client;

    std::flat_set<std::string> offered;

    SceneDataSourceOps ops;

    ~SceneDataSource();
};

void scene_offer_selection(SceneClient*, SceneDataSource*);

// -----------------------------------------------------------------------------

void scene_enqueue_damage(Scene*, SceneDamageType);

// -----------------------------------------------------------------------------

void scene_handle_input_added(  SceneSeat*, IoInputDevice*);
void scene_handle_input_removed(SceneSeat*, IoInputDevice*);
void scene_handle_input(        SceneSeat*, const IoInputEvent&);

// -----------------------------------------------------------------------------

struct SceneEventFilter
{
    Weak<Scene> scene;

    std::move_only_function<SceneEventFilterResult(SceneEvent*)> filter;

    ~SceneEventFilter();
};
