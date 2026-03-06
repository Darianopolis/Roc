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

struct scene_context
{
    gpu_context* gpu;

    struct {
        ref<gpu_pipeline> premult;
        ref<gpu_pipeline> postmult;
        ref<gpu_image>    white;
        ref<gpu_sampler>  sampler;
        flags<gpu_image_usage> usage;
    } render;

    ref<scene_transform> root_transform;
    ref<scene_tree>      root_tree;
    core_enum_map<scene_layer, ref<scene_tree>> layers;

    std::vector<scene_output*> outputs;

    std::vector<scene_client*> clients;
    std::vector<scene_window*> windows;

    ref<scene_keyboard> keyboard;
    ref<scene_pointer>  pointer;

    struct {
        ankerl::unordered_dense::map<scene_hotkey, scene_client*> registered;
        ankerl::unordered_dense::map<scene_scancode, std::pair<flags<scene_modifier>, scene_client*>> pressed;
    } hotkey;

    ref<scene_data_source> selection;
};

void scene_broadcast_event(scene_context*, scene_event*);

void scene_render_init(scene_context*);

// -----------------------------------------------------------------------------

struct scene_client
{
    scene_context* ctx;

    std::move_only_function<scene_event_handler_fn> event_handler;

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
    ref<scene_transform> transform;

    ~scene_window();
};

// -----------------------------------------------------------------------------

struct scene_keyboard : scene_keyboard_info
{
    core_counting_set<u32> pressed;

    std::vector<io_input_device*> led_devices;

    flags<scene_modifier> depressed;
    flags<scene_modifier> latched;
    flags<scene_modifier> locked;

    core_enum_map<scene_modifier, xkb_mod_mask_t> mod_masks;

    scene_focus focus;

    ~scene_keyboard();
};

auto scene_keyboard_create(scene_context*) -> ref<scene_keyboard>;

// -----------------------------------------------------------------------------

struct scene_pointer
{
    core_counting_set<u32> pressed;

    ref<scene_transform> transform;
    ref<scene_texture>   visual;

    std::move_only_function<scene_pointer_driver_fn> driver;

    scene_client* grab;
    scene_focus   focus;
};

void scene_update_pointer_focus(scene_context*);

auto scene_pointer_create(scene_context*) -> ref<scene_pointer>;

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
