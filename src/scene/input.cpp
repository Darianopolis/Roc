#include "internal.hpp"

#include "core/math.hpp"

auto scene_input_device_get_type(SceneInputDevice* device) -> SceneInputDeviceType
{
    return device->type;
}

auto scene_input_device_get_pointer(SceneInputDevice* device) -> ScenePointer*
{
    return device->type == SceneInputDeviceType::pointer
        ? static_cast<ScenePointer*>(device)
        : nullptr;
}

auto scene_input_device_get_keyboard(SceneInputDevice* device) -> SceneKeyboard*
{
    return device->type == SceneInputDeviceType::keyboard
        ? static_cast<SceneKeyboard*>(device)
        : nullptr;
}

auto scene_input_device_get_focus(SceneInputDevice* device) -> SceneInputRegion*
{
    return device->focus;
}

auto scene_input_device_get_seat(SceneInputDevice* device)  -> SceneSeat*
{
    return device->seat;
}

// -----------------------------------------------------------------------------

SceneKeyboard::~SceneKeyboard()
{
    xkb_keymap_unref(keymap);
    xkb_state_unref(state);
    xkb_context_unref(context);
}

auto scene_keyboard_create(SceneSeat* seat) -> Ref<SceneKeyboard>
{
    auto keyboard = ref_create<SceneKeyboard>();
    keyboard->type = SceneInputDeviceType::keyboard;
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

    keyboard->mod_masks[SceneModifier::shift] = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_MOD_NAME_SHIFT);
    keyboard->mod_masks[SceneModifier::ctrl]  = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_MOD_NAME_CTRL);
    keyboard->mod_masks[SceneModifier::caps]  = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_MOD_NAME_CAPS);
    keyboard->mod_masks[SceneModifier::super] = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_SUPER);
    keyboard->mod_masks[SceneModifier::alt]   = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_ALT)
                                               | xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_LEVEL3);
    keyboard->mod_masks[SceneModifier::num]   = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_NUM);

    return keyboard;
}

static
auto get_modifiers(SceneKeyboard* keyboard, Flags<xkb_state_component> component) -> Flags<SceneModifier>
{
    Flags<SceneModifier> down = {};
    auto xkb_mods = xkb_state_serialize_mods(keyboard->state, component.get());
    for (auto mod : keyboard->mod_masks.enum_values) {
        if (xkb_mods & keyboard->mod_masks[mod]) down |= mod;
    }
    return down;
}

auto scene_keyboard_get_modifiers(SceneKeyboard* keyboard, Flags<SceneModifierFlag> flags) -> Flags<SceneModifier>
{
    auto mods = keyboard->depressed | keyboard->latched;
    if (!flags.contains(SceneModifierFlag::ignore_locked)) mods |= keyboard->locked;
    return mods;
}

auto scene_seat_get_modifiers(SceneSeat* seat, Flags<SceneModifierFlag> flags) -> Flags<SceneModifier>
{
    return scene_keyboard_get_modifiers(scene_seat_get_keyboard(seat), flags);
}

static
xkb_keycode_t evdev_to_xkb(SceneScancode code)
{
    return code + 8;
}

static
bool post_input_event(Weak<SceneInputDevice> device, SceneEvent* event)
{
    auto* scene = device->seat->scene;
    for (auto* filter : scene->input_event_filters) {
        if (filter->filter(event) == SceneEventFilterResult::capture) {
            return false;
        }
    }

    if (!device) return false;
    if (device->focus) {
        scene_client_post_event(device->focus->client, event);
        return true;
    }

    return false;
}

static
Flags<xkb_state_component> handle_key(SceneKeyboard* keyboard, SceneScancode code, bool pressed, bool quiet)
{
    if (pressed ? keyboard->pressed.inc(code) : keyboard->pressed.dec(code)) {
        auto changed_components = xkb_state_update_key(keyboard->state, evdev_to_xkb(code), pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

        post_input_event(keyboard, ptr_to(SceneEvent {
            .type = SceneEventType::keyboard_key,
            .keyboard = {
                .keyboard = keyboard,
                .key = {
                    .code = code,
                    .pressed = pressed,
                    .quiet = quiet,
                },
            }
        }));

        return changed_components;
    }

    return {};
}

static
auto get_keyboard_leds(SceneKeyboard* keyboard) -> Flags<libinput_led>
{
    Flags<libinput_led> leds = {};
    if (xkb_state_led_name_is_active(keyboard->state, XKB_LED_NAME_NUM)    > 0) leds |= LIBINPUT_LED_NUM_LOCK;
    if (xkb_state_led_name_is_active(keyboard->state, XKB_LED_NAME_CAPS)   > 0) leds |= LIBINPUT_LED_CAPS_LOCK;
    if (xkb_state_led_name_is_active(keyboard->state, XKB_LED_NAME_SCROLL) > 0) leds |= LIBINPUT_LED_SCROLL_LOCK;
    return leds;
}

static
void update_leds(SceneSeat* seat)
{
    if (seat->led_devices.empty()) return;

    // TODO: How to manage LED output across multiple keyboards
    auto leds = get_keyboard_leds(scene_seat_get_keyboard(seat));

    for (auto& device : seat->led_devices) {
        device->update_leds(leds);
    }
}

static
void handle_xkb_component_updates(SceneKeyboard* keyboard, Flags<xkb_state_component> changed)
{
    if (changed & XKB_STATE_MODS_DEPRESSED) keyboard->depressed = get_modifiers(keyboard, XKB_STATE_MODS_DEPRESSED);
    if (changed & XKB_STATE_MODS_LATCHED)   keyboard->depressed = get_modifiers(keyboard, XKB_STATE_MODS_LATCHED);
    if (changed & XKB_STATE_MODS_LOCKED)    keyboard->depressed = get_modifiers(keyboard, XKB_STATE_MODS_LOCKED);

    if (changed & XKB_STATE_MODS_EFFECTIVE) {
        post_input_event(keyboard, ptr_to(SceneEvent {
            .type = SceneEventType::keyboard_modifier,
            .keyboard = {
                .keyboard = keyboard,
            },
        }));
    }

    if (changed & XKB_STATE_LEDS) update_leds(keyboard->seat);
}

void scene_keyboard_focus(SceneKeyboard* keyboard, SceneInputRegion* new_focus)
{
    auto old_focus = keyboard->focus;

    keyboard->focus = new_focus;

    if (old_focus == new_focus) return;

    auto old_client = scene_get_focus_client(old_focus);
    auto new_client = scene_get_focus_client(new_focus);

    if (old_client && (old_client != new_client)) {
        scene_client_post_event(old_client, ptr_to(SceneEvent {
            .type = SceneEventType::keyboard_leave,
            .keyboard = {
                .keyboard = keyboard,
            },
        }));
    }

    if (new_focus) {
        scene_client_post_event(new_client, ptr_to(SceneEvent {
            .type = SceneEventType::keyboard_enter,
            .keyboard = {
                .keyboard = keyboard,
                .focus = new_focus,
            },
        }));
    }
}

auto scene_keyboard_get_focus(SceneKeyboard* keyboard) -> SceneInputRegion*
{
    return scene_input_device_get_focus(keyboard);
}

auto scene_keyboard_get_base(SceneKeyboard* keyboard) -> SceneInputDevice*
{
    return keyboard;
}

auto scene_keyboard_get_pressed(SceneKeyboard* keyboard) -> std::span<const SceneScancode>
{
    return keyboard->pressed;
}

auto scene_keyboard_get_sym(SceneKeyboard* keyboard, SceneScancode code) -> xkb_keysym_t
{
    return xkb_state_key_get_one_sym(keyboard->state, evdev_to_xkb(code));
}

auto scene_keyboard_get_utf8(SceneKeyboard* keyboard, SceneScancode code) -> std::string
{
    std::string utf8;
    utf8.resize(xkb_state_key_get_utf8(keyboard->state, evdev_to_xkb(code), nullptr, 0));
    xkb_state_key_get_utf8(keyboard->state, evdev_to_xkb(code), utf8.data(), utf8.size() + 1);
    return utf8;
}

auto scene_keyboard_get_info(SceneKeyboard* keyboard) -> const SceneKeyboardInfo&
{
    return *keyboard;
}

// -----------------------------------------------------------------------------

auto scene_find_input_region_at(SceneTree* tree, vec2f32 pos) -> SceneInputRegion*
{
    SceneInputRegion* region = nullptr;

    scene_iterate<SceneIterateDirection::front_to_back>(tree,
        scene_iterate_default,
        [&](SceneNode* node) {
            if (auto input_region = dynamic_cast<SceneInputRegion*>(node)) {
                if (input_region->region.contains(pos - scene_tree_get_position(input_region->parent))) {
                    region = input_region;
                    return SceneIterateAction::stop;
                }
            }
            return SceneIterateAction::next;
        },
        scene_iterate_default);

    return region;
}

void scene_pointer_focus(ScenePointer* pointer, SceneInputRegion* new_focus)
{
    auto* old_focus = pointer->focus;

    if (old_focus == new_focus) return;

    auto old_client = scene_get_focus_client(old_focus);
    auto new_client = scene_get_focus_client(new_focus);

    pointer->focus = new_focus;

    if (old_client && old_client != new_client) {
        scene_client_post_event(old_client, ptr_to(SceneEvent {
            .type = SceneEventType::pointer_leave,
            .pointer = {
                .pointer = pointer,
            },
        }));
    }

    if (new_client) {
        scene_client_post_event(new_client, ptr_to(SceneEvent {
            .type = SceneEventType::pointer_enter,
            .pointer = {
                .pointer = pointer,
                .focus= new_focus,
            }
        }));
    }

    if (!new_focus) {
        scene_pointer_set_xcursor(pointer, "default");
    }
}

static
void update_pointer_focus(ScenePointer* pointer)
{
    SceneInputRegion* new_focus = nullptr;

    if (!scene_pointer_get_pressed(pointer).empty()) {
        // Pointer retains old focus while any pointer buttons pressed
        new_focus = pointer->focus;

    } else if (auto* region = scene_find_input_region_at(pointer->seat->scene->root_tree.get(), scene_pointer_get_position(pointer))) {
        new_focus = region;
    }

    scene_pointer_focus(pointer, new_focus);
}

auto scene_pointer_get_base(ScenePointer* pointer) -> SceneInputDevice*
{
    return pointer;
}

auto scene_pointer_get_position(ScenePointer* pointer) -> vec2f32
{
    return scene_tree_get_position(pointer->tree.get());
}

auto scene_pointer_get_pressed(ScenePointer* pointer) -> std::span<const SceneScancode>
{
    return pointer->pressed;
}

// -----------------------------------------------------------------------------

auto scene_pointer_create(SceneSeat* seat) -> Ref<ScenePointer>
{
    auto pointer = ref_create<ScenePointer>();
    pointer->seat = seat;
    pointer->type = SceneInputDeviceType::pointer;

    pointer->accel = [](vec2f32 delta) { return delta; };

    pointer->tree = scene_tree_create();
    scene_tree_place_above(scene_get_layer(seat->scene, SceneLayer::overlay), nullptr, pointer->tree .get());

    return pointer;
}

static
void handle_button(ScenePointer* pointer, SceneScancode code, bool pressed, bool quiet)
{
    if (pressed ? pointer->pressed.inc(code) : pointer->pressed.dec(code)) {
        if (post_input_event(pointer, ptr_to(SceneEvent {
            .type = SceneEventType::pointer_button,
            .pointer = {
                .pointer = pointer,
                .button = {
                    .code    = code,
                    .pressed = pressed,
                    .quiet   = quiet,
                },
        }}))) {
            if (pressed) {
                scene_keyboard_focus(
                    scene_seat_get_keyboard(pointer->seat),
                    pointer->focus);
            }
        } else if (pressed) {
            scene_keyboard_focus(scene_seat_get_keyboard(pointer->seat), nullptr);
        }
        if (!pressed) {
            update_pointer_focus(pointer);
        }
    }
}

static
void handle_motion(ScenePointer* pointer, vec2f32 delta)
{
    auto cur = scene_pointer_get_position(pointer);

    auto delta_accel = pointer->accel(delta);

    // TODO: Handle pointer constraints in `wm`
    auto pos = cur + delta_accel;

    scene_tree_set_translation(pointer->tree.get(), pos);

    update_pointer_focus(pointer);

    post_input_event(pointer, ptr_to(SceneEvent {
        .type = SceneEventType::pointer_motion,
        .pointer = {
            .pointer = pointer,
            .motion = {
                .rel_accel   = delta_accel,
                .rel_unaccel = delta,
            },
        },
    }));
}

void scene_update_pointers(Scene* scene)
{
    for (auto* seat : scene_get_seats(scene)) {
        if (auto* pointer = scene_seat_get_pointer(seat)) {
            handle_motion(pointer, {});
        }
    }
}

static
void handle_scroll(ScenePointer* pointer, vec2f32 delta)
{
    post_input_event(pointer, ptr_to(SceneEvent {
        .type = SceneEventType::pointer_scroll,
        .pointer = {
            .pointer = pointer,
            .scroll = {
                .delta = delta,
            }
        },
    }));
}

auto scene_pointer_get_focus(ScenePointer* pointer) -> SceneInputRegion*
{
    return scene_input_device_get_focus(pointer);
}

void scene_pointer_set_accel(ScenePointer* pointer, std::move_only_function<ScenePointerAccelFn>&& accel)
{
    pointer->accel = std::move(accel);
}

// -----------------------------------------------------------------------------

void scene_handle_input_added(SceneSeat* seat, IoInputDevice* device)
{
    if (device->info().capabilities.contains(IoInputDeviceCapability::libinput_led)) {
        seat->led_devices.emplace_back(device);
    }
}

void scene_handle_input_removed(SceneSeat* seat, IoInputDevice* device)
{
    std::erase(seat->led_devices, device);
}

static
auto categorize_key(SceneScancode code) -> SceneInputDeviceType
{
    switch (code) {
        break;case BTN_MOUSE ... BTN_TASK:
            return SceneInputDeviceType::pointer;
        break;case KEY_ESC        ... KEY_MICMUTE:
              case KEY_OK         ... KEY_LIGHTS_TOGGLE:
              case KEY_ALS_TOGGLE ... KEY_PERFORMANCE:
            return SceneInputDeviceType::keyboard;
        break;default:
            return SceneInputDeviceType::invalid;
    }
}

void scene_handle_input(SceneSeat* seat, const IoInputEvent& event)
{
    vec2f32 motion = {};
    vec2f32 scroll = {};
    Flags<xkb_state_component> xkb_updates = {};

    // TODO: Multiple input devices
    auto* pointer = scene_seat_get_pointer(seat);
    auto* keyboard  = scene_seat_get_keyboard(seat);

    for (auto& channel : event.channels) {
        switch (channel.type) {
            break;case EV_KEY:
                switch (categorize_key(channel.code)) {
                    break;case SceneInputDeviceType::pointer:
                        handle_button(pointer, channel.code, channel.value, event.quiet);
                    break;case SceneInputDeviceType::keyboard:
                        xkb_updates |= handle_key(keyboard, channel.code, channel.value, event.quiet);
                    break;case SceneInputDeviceType::invalid:
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
