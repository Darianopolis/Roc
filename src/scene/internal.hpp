#pragma once

#include "scene.hpp"

#include "io/io.hpp"

// -----------------------------------------------------------------------------

struct scene_output {
    scene_client* client;
    rect2f32      viewport;

    ~scene_output();
};

// -----------------------------------------------------------------------------

struct scene_cursor_manager;
CORE_OBJECT_EXPLICIT_DECLARE(scene_cursor_manager);

void scene_cursor_manager_init(scene_context*);

struct scene_context
{
    gpu_context* gpu;

    struct {
        ref<gpu_shader> vertex;
        ref<gpu_shader> fragment;
        ref<gpu_image>    white;
        ref<gpu_sampler>  sampler;
    } render;

    ref<scene_tree> root_tree;
    core_enum_map<scene_layer, ref<scene_tree>> layers;

    std::vector<scene_output*> outputs;

    std::vector<scene_client*> clients;
    std::vector<scene_window*> windows;

    struct {
        ref<scene_keyboard> keyboard;
        ref<scene_pointer>  pointer;
        std::vector<io_input_device*> led_devices;
    } seat;

    ref<scene_data_source> selection;

    ref<scene_cursor_manager> cursor_manager;

    ~scene_context();
};

void scene_broadcast_event(scene_context*, scene_event*);

void scene_render_init(scene_context*);

// -----------------------------------------------------------------------------

struct scene_client
{
    scene_context* ctx;

    std::move_only_function<scene_event_handler_fn> event_handler;

    u32 input_regions = 0;

    ~scene_client();
};

void scene_client_post_event(scene_client*, scene_event*);

// -----------------------------------------------------------------------------

struct scene_window : core_object
{
    scene_client* client;

    vec2f32 extent;
    bool mapped;

    std::string title;

    ref<scene_tree> tree;

    ~scene_window();
};

// -----------------------------------------------------------------------------

struct scene_hotkey_press_state
{
    flags<scene_modifier> modifiers;
    scene_client*         client;
};

struct scene_hotkey_map {
    ankerl::unordered_dense::map<scene_hotkey, scene_client*> registered;
    ankerl::unordered_dense::map<scene_scancode, scene_hotkey_press_state> pressed;
};

struct scene_input_device
{
    scene_input_device_type type;
    scene_context* ctx;

    scene_hotkey_map hotkeys;
};

struct scene_focus
{
    scene_client*       client = nullptr;
    scene_input_region* region = nullptr;

    constexpr bool operator==(const scene_focus&) const noexcept = default;
};

// -----------------------------------------------------------------------------

struct scene_keyboard : scene_input_device, scene_keyboard_info
{
    core_counting_set<u32> pressed;

    flags<scene_modifier> depressed;
    flags<scene_modifier> latched;
    flags<scene_modifier> locked;

    core_enum_map<scene_modifier, xkb_mod_mask_t> mod_masks;

    scene_focus focus;

    ~scene_keyboard();
};

auto scene_keyboard_create(scene_context*) -> ref<scene_keyboard>;

// -----------------------------------------------------------------------------

struct scene_pointer : scene_input_device
{
    core_counting_set<u32> pressed;

    ref<scene_tree> tree;

    std::move_only_function<scene_pointer_driver_fn> driver;

    scene_focus focus;
};

void scene_update_pointer_focus(scene_context*);

auto scene_pointer_create(scene_context*) -> ref<scene_pointer>;

// -----------------------------------------------------------------------------

auto scene_find_input_region_at(scene_tree* tree, vec2f32 pos) -> scene_input_region*;

// -----------------------------------------------------------------------------

struct scene_data_source
{
    scene_client* client;

    std::flat_set<std::string> offered;

    scene_data_source_ops ops;

    ~scene_data_source();
};

void scene_offer_selection(scene_client*, scene_data_source*);

// -----------------------------------------------------------------------------

void scene_output_request_frame(scene_output*);

// -----------------------------------------------------------------------------

void scene_handle_input_added(  scene_context*, io_input_device*);
void scene_handle_input_removed(scene_context*, io_input_device*);
void scene_handle_input(        scene_context*, const io_input_event&);

// -----------------------------------------------------------------------------

void scene_keyboard_set_focus(scene_keyboard* keyboard, scene_focus new_focus);
void scene_pointer_set_focus( scene_pointer*  pointer,  scene_focus new_focus);
