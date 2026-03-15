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

void scene_cursor_manager_init(scene_context*);

struct scene_context
{
    gpu::Context* gpu;

    struct {
        core::Ref<gpu::Shader> vertex;
        core::Ref<gpu::Shader> fragment;
        core::Ref<gpu::Image>    white;
        core::Ref<gpu::Sampler>  sampler;
    } render;

    scene_system_id prev_system_id = {};

    core::Ref<scene_tree> root_tree;
    core::EnumMap<scene_layer, core::Ref<scene_tree>> layers;

    std::vector<scene_output*> outputs;

    std::vector<scene_client*> clients;
    std::vector<scene_window*> windows;
    scene_system_id            window_system;

    struct {
        core::Ref<scene_keyboard> keyboard;
        core::Ref<scene_pointer>  pointer;
        std::vector<io_input_device*> led_devices;
    } seat;

    core::Ref<scene_data_source> selection;

    core::Ref<scene_cursor_manager> cursor_manager;

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

struct scene_window
{
    scene_client* client;

    vec2f32 extent;
    bool mapped;

    std::string title;

    core::Ref<scene_tree> tree;

    ~scene_window();
};

// -----------------------------------------------------------------------------

struct scene_hotkey_press_state
{
    core::Flags<scene_modifier> modifiers;
    scene_client*         client;
};

struct scene_hotkey_map {
    core::Map<scene_hotkey, scene_client*> registered;
    core::Map<scene_scancode, scene_hotkey_press_state> pressed;
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
    core::CountingSet<u32> pressed;

    core::Flags<scene_modifier> depressed;
    core::Flags<scene_modifier> latched;
    core::Flags<scene_modifier> locked;

    core::EnumMap<scene_modifier, xkb_mod_mask_t> mod_masks;

    scene_focus focus;

    ~scene_keyboard();
};

auto scene_keyboard_create(scene_context*) -> core::Ref<scene_keyboard>;

// -----------------------------------------------------------------------------

struct scene_pointer : scene_input_device
{
    core::CountingSet<u32> pressed;

    core::Ref<scene_tree> tree;

    std::move_only_function<scene_pointer_driver_fn> driver;

    scene_focus focus;
};

void scene_update_pointer_focus(scene_context*);

auto scene_pointer_create(scene_context*) -> core::Ref<scene_pointer>;

// -----------------------------------------------------------------------------

auto scene_find_input_region_at(scene_tree* tree, vec2f32 pos) -> scene_input_region*;

// -----------------------------------------------------------------------------

struct scene_data_source
{
    scene_client* client;

    core::FlatSet<std::string> offered;

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
