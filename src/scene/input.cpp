#include "internal.hpp"

#include "core/math.hpp"

auto scene_input_device_get_type(scene_input_device* device) -> scene_input_device_type
{
    return device->type;
}

auto scene_input_device_get_pointer(scene_input_device* device) -> scene_pointer*
{
    return device->type == scene_input_device_type::pointer
        ? static_cast<scene_pointer*>(device)
        : nullptr;
}

auto scene_input_device_get_keyboard(scene_input_device* device) -> scene_keyboard*
{
    return device->type == scene_input_device_type::keyboard
        ? static_cast<scene_keyboard*>(device)
        : nullptr;
}

auto scene_input_device_get_focus(scene_input_device* device) -> scene_focus
{
    return device->focus;
}

// -----------------------------------------------------------------------------

scene_keyboard::~scene_keyboard()
{
    xkb_keymap_unref(keymap);
    xkb_state_unref(state);
    xkb_context_unref(context);
}

auto scene_keyboard_create(scene_seat* seat) -> ref<scene_keyboard>
{
    auto keyboard = core_create<scene_keyboard>();
    keyboard->type = scene_input_device_type::keyboard;
    keyboard->seat = seat;

    keyboard->rate = 25;
    keyboard->delay = 600;

    // Init XKB

    keyboard->context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    keyboard->keymap = xkb_keymap_new_from_names(keyboard->context, ptr_to(xkb_rule_names {
        .layout = "gb",
    }), XKB_KEYMAP_COMPILE_NO_FLAGS);

    keyboard->state = xkb_state_new(keyboard->keymap);

    // Get XKB modifier masks

    keyboard->mod_masks[scene_modifier::shift] = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_MOD_NAME_SHIFT);
    keyboard->mod_masks[scene_modifier::ctrl]  = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_MOD_NAME_CTRL);
    keyboard->mod_masks[scene_modifier::caps]  = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_MOD_NAME_CAPS);
    keyboard->mod_masks[scene_modifier::super] = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_SUPER);
    keyboard->mod_masks[scene_modifier::alt]   = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_ALT)
                                               | xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_LEVEL3);
    keyboard->mod_masks[scene_modifier::num]   = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_NUM);

    return keyboard;
}

static
auto get_modifiers(scene_keyboard* keyboard, flags<xkb_state_component> component) -> flags<scene_modifier>
{
    flags<scene_modifier> down = {};
    auto xkb_mods = xkb_state_serialize_mods(keyboard->state, component.get());
    for (auto mod : keyboard->mod_masks.enum_values) {
        if (xkb_mods & keyboard->mod_masks[mod]) down |= mod;
    }
    return down;
}

auto scene_keyboard_get_modifiers(scene_keyboard* keyboard, flags<scene_modifier_flag> flags) -> ::flags<scene_modifier>
{
    auto mods = keyboard->depressed | keyboard->latched;
    if (!flags.contains(scene_modifier_flag::ignore_locked)) mods |= keyboard->locked;
    return mods;
}

auto scene_seat_get_modifiers(scene_seat* seat, flags<scene_modifier_flag> flags) -> ::flags<scene_modifier>
{
    return scene_keyboard_get_modifiers(scene_seat_get_keyboard(seat), flags);
}

static
bool try_send_hotkey(scene_input_device* device, scene_scancode code, bool pressed)
{
    auto* seat = device->seat;
    auto& hotkeys = device->hotkeys;

    if (pressed) {
        // Ignore LOCKED modifier state like CAPS and NUMLOCK, as hotkyes require an exact match.
        scene_hotkey hotkey { scene_seat_get_modifiers(seat, scene_modifier_flag::ignore_locked), code };

        auto iter = hotkeys.registered.find(hotkey);
        if (iter != hotkeys.registered.end()) {
            hotkeys.pressed.insert({code, {hotkey.mod, iter->second}});
            scene_client_post_event(iter->second, ptr_to(scene_event {
                .type = scene_event_type::hotkey,
                .hotkey = {
                    .input_device = device,
                    .hotkey  = hotkey,
                    .pressed = true,
                }
            }));
            return true;
        }
    } else {
        auto iter = hotkeys.pressed.find(code);
        if (iter != hotkeys.pressed.end()) {
            auto[mods, client] = iter->second;
            hotkeys.pressed.erase(code);
            scene_client_post_event(client, ptr_to(scene_event {
                .type = scene_event_type::hotkey,
                .hotkey = {
                    .input_device = device,
                    .hotkey  = {mods, code},
                    .pressed = false,
                }
            }));
            return true;
        }
    }

    return false;
}

static
xkb_keycode_t evdev_to_xkb(scene_scancode code)
{
    return code + 8;
}

static
flags<xkb_state_component> handle_key(scene_keyboard* keyboard, scene_scancode code, bool pressed, bool quiet)
{
    if (pressed ? keyboard->pressed.inc(code) : keyboard->pressed.dec(code)) {
        auto changed_components = xkb_state_update_key(keyboard->state, evdev_to_xkb(code), pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

        if (!try_send_hotkey(keyboard, code, pressed)) {
            if (auto* client = keyboard->focus.client) {
                scene_client_post_event(client, ptr_to(scene_event {
                    .type = scene_event_type::keyboard_key,
                    .keyboard = {
                        .keyboard = keyboard,
                        .key = {
                            .code = code,
                            .pressed = pressed,
                            .quiet = quiet,
                        },
                    }
                }));
            }
        }

        return changed_components;
    }

    return {};
}

static
auto get_keyboard_leds(scene_keyboard* keyboard) -> flags<libinput_led>
{
    flags<libinput_led> leds = {};
    if (xkb_state_led_name_is_active(keyboard->state, XKB_LED_NAME_NUM)    > 0) leds |= LIBINPUT_LED_NUM_LOCK;
    if (xkb_state_led_name_is_active(keyboard->state, XKB_LED_NAME_CAPS)   > 0) leds |= LIBINPUT_LED_CAPS_LOCK;
    if (xkb_state_led_name_is_active(keyboard->state, XKB_LED_NAME_SCROLL) > 0) leds |= LIBINPUT_LED_SCROLL_LOCK;
    return leds;
}

static
void update_leds(scene_seat* seat)
{
    if (seat->led_devices.empty()) return;

    // TODO: How to manage LED output across multiple keyboards
    auto leds = get_keyboard_leds(scene_seat_get_keyboard(seat));

    for (auto& device : seat->led_devices) {
        device->update_leds(leds);
    }
}

static
void handle_xkb_component_updates(scene_keyboard* keyboard, flags<xkb_state_component> changed)
{
    if (changed & XKB_STATE_MODS_DEPRESSED) keyboard->depressed = get_modifiers(keyboard, XKB_STATE_MODS_DEPRESSED);
    if (changed & XKB_STATE_MODS_LATCHED)   keyboard->depressed = get_modifiers(keyboard, XKB_STATE_MODS_LATCHED);
    if (changed & XKB_STATE_MODS_LOCKED)    keyboard->depressed = get_modifiers(keyboard, XKB_STATE_MODS_LOCKED);

    if (changed & XKB_STATE_MODS_EFFECTIVE) {
        if (keyboard->focus.client) {
            scene_client_post_event(keyboard->focus.client, ptr_to(scene_event {
                .type = scene_event_type::keyboard_modifier,
                .keyboard = {
                    .keyboard = keyboard,
                },
            }));
        }
    }

    if (changed & XKB_STATE_LEDS) update_leds(scene_keyboard_get_seat(keyboard));
}

void scene_keyboard_set_focus(scene_keyboard* keyboard, scene_focus new_focus)
{
    auto old_focus = keyboard->focus;

    keyboard->focus = new_focus;

    if (old_focus == new_focus) return;

    if (old_focus.client && (new_focus.client != old_focus.client)) {
        scene_client_post_event(old_focus.client, ptr_to(scene_event {
            .type = scene_event_type::keyboard_leave,
            .keyboard = {
                .keyboard = keyboard,
            },
        }));
    }

    if (new_focus.client) {
        scene_client_post_event(new_focus.client, ptr_to(scene_event {
            .type = scene_event_type::keyboard_enter,
            .keyboard = {
                .keyboard = keyboard,
                .focus = {
                    .region = new_focus.region,
                },
            },
        }));
    }
}

void scene_keyboard_clear_focus(scene_keyboard* keyboard)
{
    scene_keyboard_set_focus(keyboard, {});
}

auto scene_keyboard_get_focus(scene_keyboard* keyboard) -> scene_focus
{
    return scene_input_device_get_focus(keyboard);
}

auto scene_keyboard_get_seat(scene_keyboard* keyboard) -> scene_seat*
{
    return keyboard->seat;
}

auto scene_keyboard_get_pressed(scene_keyboard* keyboard) -> std::span<const scene_scancode>
{
    return keyboard->pressed;
}

auto scene_keyboard_get_sym(scene_keyboard* keyboard, scene_scancode code) -> xkb_keysym_t
{
    return xkb_state_key_get_one_sym(keyboard->state, evdev_to_xkb(code));
}

auto scene_keyboard_get_utf8(scene_keyboard* keyboard, scene_scancode code) -> std::string
{
    std::string utf8;
    utf8.resize(xkb_state_key_get_utf8(keyboard->state, evdev_to_xkb(code), nullptr, 0));
    xkb_state_key_get_utf8(keyboard->state, evdev_to_xkb(code), utf8.data(), utf8.size() + 1);
    return utf8;
}

auto scene_keyboard_get_info(scene_keyboard* keyboard) -> const scene_keyboard_info&
{
    return *keyboard;
}

// -----------------------------------------------------------------------------

auto scene_find_input_region_at(scene_tree* tree, vec2f32 pos) -> scene_input_region*
{
    scene_input_region* region = nullptr;

    scene_iterate<scene_iterate_direction::front_to_back>(tree,
        scene_iterate_default,
        [&](scene_node* node) {
            if (node->type == scene_node_type::input_region) {
                auto* input_region = static_cast<scene_input_region*>(node);
                if (input_region->region.contains(pos - scene_tree_get_position(input_region->parent))) {
                    region = input_region;
                    return scene_iterate_action::stop;
                }
            }
            return scene_iterate_action::next;
        },
        scene_iterate_default);

    return region;
}

void scene_pointer_set_focus(scene_pointer* pointer, scene_focus new_focus)
{
    scene_focus old_focus = pointer->focus;

    if (old_focus == new_focus) return;

    pointer->focus = new_focus;

    if (old_focus.client && old_focus.client != new_focus.client) {
        scene_client_post_event(old_focus.client, ptr_to(scene_event {
            .type = scene_event_type::pointer_leave,
            .pointer = {
                .pointer = pointer,
            },
        }));
    }

    if (new_focus.client) {
        scene_client_post_event(new_focus.client, ptr_to(scene_event {
            .type = scene_event_type::pointer_enter,
            .pointer = {
                .pointer = pointer,
                .focus = {
                    .region = new_focus.region,
                }
            }
        }));
    }

    if (!new_focus.client) {
        scene_pointer_set_xcursor(pointer, "default");
    }
}

static
void update_pointer_focus(scene_pointer* pointer)
{
    scene_focus new_focus;

    if (!scene_pointer_get_pressed(pointer).empty()) {
        // Pointer retains old focus while any pointer buttons pressed
        new_focus = pointer->focus;

    } else if (auto* region = scene_find_input_region_at(pointer->seat->ctx->root_tree.get(), scene_pointer_get_position(pointer))) {
        new_focus = {region->client, region};
    }

    scene_pointer_set_focus(pointer, new_focus);
}

auto scene_pointer_get_seat(scene_pointer* pointer) -> scene_seat*
{
    return pointer->seat;
}

auto scene_pointer_get_position(scene_pointer* pointer) -> vec2f32
{
    return scene_tree_get_position(pointer->tree.get());
}

auto scene_pointer_get_pressed(scene_pointer* pointer) -> std::span<const scene_scancode>
{
    return pointer->pressed;
}

// -----------------------------------------------------------------------------

auto scene_pointer_create(scene_seat* seat) -> ref<scene_pointer>
{
    auto pointer = core_create<scene_pointer>();
    pointer->seat = seat;
    pointer->type = scene_input_device_type::pointer;

    pointer->accel = [](vec2f32 delta) { return delta; };

    pointer->tree = scene_tree_create(seat->ctx);
    scene_tree_place_above(scene_get_layer(seat->ctx, scene_layer::overlay), nullptr, pointer->tree .get());

    return pointer;
}

static
void handle_button(scene_pointer* pointer, scene_scancode code, bool pressed, bool quiet)
{
    if (pressed ? pointer->pressed.inc(code) : pointer->pressed.dec(code)) {
        if (!try_send_hotkey(pointer, code, pressed)) {
            if (auto* focus = pointer->focus.client) {
                if (pressed) {
                    scene_keyboard_set_focus(
                        scene_seat_get_keyboard(pointer->seat),
                        {focus, pointer->focus.region});
                }
                scene_client_post_event(focus, ptr_to(scene_event {
                    .type = scene_event_type::pointer_button,
                    .pointer = {
                        .pointer = pointer,
                        .button = {
                            .code    = code,
                            .pressed = pressed,
                            .quiet   = quiet,
                        },
                    }
                }));
            } else if (pressed) {
                scene_keyboard_set_focus(scene_seat_get_keyboard(pointer->seat), {});
            }
        }
        if (!pressed) {
            update_pointer_focus(pointer);
        }
    }
}

static
void handle_motion(scene_pointer* pointer, vec2f32 delta)
{
    auto cur = scene_pointer_get_position(pointer);

    auto delta_accel = pointer->accel(delta);

    auto pos = scene_find_output_for_point(pointer->seat->ctx, cur + delta_accel).position;

    scene_tree_set_translation(pointer->tree.get(), pos);

    update_pointer_focus(pointer);

    if (auto* focus = pointer->focus.client) {
        scene_client_post_event(focus, ptr_to(scene_event {
            .type = scene_event_type::pointer_motion,
            .pointer = {
                .pointer = pointer,
                .motion = {
                    .rel_accel   = delta_accel,
                    .rel_unaccel = delta,
                },
            },
        }));
    }
}

void scene_update_pointers(scene_context* ctx)
{
    for (auto* seat : scene_get_seats(ctx)) {
        if (auto* pointer = scene_seat_get_pointer(seat)) {
            handle_motion(pointer, {});
        }
    }
}

static
void handle_scroll(scene_pointer* pointer, vec2f32 delta)
{
    if (auto* focus = pointer->focus.client) {
        scene_client_post_event(focus, ptr_to(scene_event {
            .type = scene_event_type::pointer_scroll,
            .pointer = {
                .pointer = pointer,
                .scroll = {
                    .delta = delta,
                }
            },
        }));
    }
}

void scene_pointer_focus(scene_pointer* pointer, scene_client* client, scene_input_region* region)
{
    scene_pointer_set_focus(pointer, {client, region});
}

auto scene_pointer_get_focus(scene_pointer* pointer) -> scene_focus
{
    return scene_input_device_get_focus(pointer);
}

void scene_pointer_set_accel(scene_pointer* pointer, std::move_only_function<scene_pointer_accel_fn>&& accel)
{
    pointer->accel = std::move(accel);
}

// -----------------------------------------------------------------------------

void scene_handle_input_added(scene_seat* seat, io_input_device* device)
{
    if (device->info().capabilities.contains(io_input_device_capability::libinput_led)) {
        seat->led_devices.emplace_back(device);
    }
}

void scene_handle_input_removed(scene_seat* seat, io_input_device* device)
{
    std::erase(seat->led_devices, device);
}

static
auto categorize_key(scene_scancode code) -> scene_input_device_type
{
    switch (code) {
        break;case BTN_MOUSE ... BTN_TASK:
            return scene_input_device_type::pointer;
        break;case KEY_ESC        ... KEY_MICMUTE:
              case KEY_OK         ... KEY_LIGHTS_TOGGLE:
              case KEY_ALS_TOGGLE ... KEY_PERFORMANCE:
            return scene_input_device_type::keyboard;
        break;default:
            return scene_input_device_type::invalid;
    }
}

void scene_handle_input(scene_seat* seat, const io_input_event& event)
{
    vec2f32 motion = {};
    vec2f32 scroll = {};
    flags<xkb_state_component> xkb_updates = {};

    // TODO: Multiple input devices
    auto* pointer = scene_seat_get_pointer(seat);
    auto* keyboard  = scene_seat_get_keyboard(seat);

    for (auto& channel : event.channels) {
        switch (channel.type) {
            break;case EV_KEY:
                switch (categorize_key(channel.code)) {
                    break;case scene_input_device_type::pointer:
                        handle_button(pointer, channel.code, channel.value, event.quiet);
                    break;case scene_input_device_type::keyboard:
                        xkb_updates |= handle_key(keyboard, channel.code, channel.value, event.quiet);
                    break;case scene_input_device_type::invalid:
                        log_warn("Unknown  {} = {}", libevdev_event_code_get_name(channel.type, channel.code), channel.value);
                }
            break;case EV_REL:
                switch (channel.code) {
                    break;case REL_X: motion.x += channel.value;
                    break;case REL_Y: motion.y += channel.value;
                    break;case REL_HWHEEL: scroll.x += channel.value;
                    break;case REL_WHEEL:  scroll.y += channel.value;
                }
            break;case EV_ABS:
                log_warn("Unknown  {} = {}", libevdev_event_code_get_name(channel.type, channel.code), channel.value);
        }
    }

    if (motion.x || motion.y) handle_motion(pointer, motion);
    if (scroll.x || scroll.y) handle_scroll(pointer, scroll);

    if (xkb_updates) handle_xkb_component_updates(keyboard, xkb_updates);
}

// -----------------------------------------------------------------------------

static
auto get_map_for_code(scene_seat* seat, scene_scancode code) -> scene_hotkey_map*
{
    switch (categorize_key(code)) {
        break;case scene_input_device_type::pointer:
            return &scene_seat_get_pointer(seat)->hotkeys;
        break;case scene_input_device_type::keyboard:
            return &scene_seat_get_keyboard(seat)->hotkeys;
        break;default:
            return nullptr;
    }
}

auto scene_client_hotkey_register(scene_client* client, scene_hotkey hotkey) -> bool
{
    // TODO: Multi-seat hotkeys
    auto* seat = scene_get_exclusive_seat(client->ctx);

    auto* map = get_map_for_code(seat, hotkey.code);
    if (!map) return false;
    auto& slot = map->registered[hotkey];
    return !slot && (slot = client);
}

void scene_client_hotkey_unregister(scene_client* client, scene_hotkey hotkey)
{
    // TODO: Multi-seat hotkeys
    auto* seat = scene_get_exclusive_seat(client->ctx);

    auto* map = get_map_for_code(seat, hotkey.code);
    if (!map) return;
    auto iter = map->registered.find(hotkey);
    if (iter == map->registered.end()) return;
    if (iter->second != client) return;
    map->registered.erase(hotkey);
    map->pressed.erase(hotkey.code);
}
