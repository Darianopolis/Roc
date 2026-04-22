#include "internal.hpp"

SeatKeyboard::~SeatKeyboard()
{
    xkb_keymap_unref(keymap);
    xkb_state_unref(state);
    xkb_context_unref(context);
}

auto seat_keyboard_create(const SeatKeyboardCreateInfo& info) -> Ref<SeatKeyboard>
{
    auto keyboard = ref_create<SeatKeyboard>();

    keyboard->rate = info.rate;
    keyboard->delay = info.delay;

    // Init XKB

    keyboard->context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    keyboard->keymap = xkb_keymap_new_from_names(keyboard->context, ptr_to(xkb_rule_names {
        .layout = info.layout,
    }), XKB_KEYMAP_COMPILE_NO_FLAGS);

    keyboard->state = xkb_state_new(keyboard->keymap);

    // Get XKB modifier masks

    keyboard->mod_masks[SeatModifier::shift] = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_MOD_NAME_SHIFT);
    keyboard->mod_masks[SeatModifier::ctrl]  = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_MOD_NAME_CTRL);
    keyboard->mod_masks[SeatModifier::caps]  = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_MOD_NAME_CAPS);
    keyboard->mod_masks[SeatModifier::super] = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_SUPER);
    keyboard->mod_masks[SeatModifier::alt]   = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_ALT)
                                             | xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_LEVEL3);
    keyboard->mod_masks[SeatModifier::num]   = xkb_keymap_mod_get_mask(keyboard->keymap, XKB_VMOD_NAME_NUM);

    return keyboard;
}

// -----------------------------------------------------------------------------

static
auto get_modifiers(SeatKeyboard* keyboard, Flags<xkb_state_component> component) -> Flags<SeatModifier>
{
    Flags<SeatModifier> down = {};
    auto xkb_mods = xkb_state_serialize_mods(keyboard->state, component.get());
    for (auto mod : keyboard->mod_masks.enum_values) {
        if (xkb_mods & keyboard->mod_masks[mod]) down |= mod;
    }
    return down;
}

auto seat_keyboard_get_modifiers(SeatKeyboard* keyboard, Flags<SeatModifierFlag> flags) -> Flags<SeatModifier>
{
    auto mods = keyboard->depressed | keyboard->latched;
    if (!flags.contains(SeatModifierFlag::ignore_locked)) mods |= keyboard->locked;
    return mods;
}

auto seat_get_modifiers(Seat* seat, Flags<SeatModifierFlag> flags) -> Flags<SeatModifier>
{
    return seat_keyboard_get_modifiers(seat_get_keyboard(seat), flags);
}

static
auto evdev_to_xkb(SeatInputCode code) -> xkb_keycode_t
{
    return code + 8;
}

auto seat_keyboard_get_leds(SeatKeyboard* keyboard) -> Flags<libinput_led>
{
    Flags<libinput_led> leds = {};
    if (xkb_state_led_name_is_active(keyboard->state, XKB_LED_NAME_NUM)    > 0) leds |= LIBINPUT_LED_NUM_LOCK;
    if (xkb_state_led_name_is_active(keyboard->state, XKB_LED_NAME_CAPS)   > 0) leds |= LIBINPUT_LED_CAPS_LOCK;
    if (xkb_state_led_name_is_active(keyboard->state, XKB_LED_NAME_SCROLL) > 0) leds |= LIBINPUT_LED_SCROLL_LOCK;
    return leds;
}

static
void handle_xkb_component_updates(SeatKeyboard* keyboard, Flags<xkb_state_component> changed)
{
    if (changed & XKB_STATE_MODS_DEPRESSED) keyboard->depressed = get_modifiers(keyboard, XKB_STATE_MODS_DEPRESSED);
    if (changed & XKB_STATE_MODS_LATCHED)   keyboard->depressed = get_modifiers(keyboard, XKB_STATE_MODS_LATCHED);
    if (changed & XKB_STATE_MODS_LOCKED)    keyboard->depressed = get_modifiers(keyboard, XKB_STATE_MODS_LOCKED);

    if (changed & XKB_STATE_MODS_EFFECTIVE) {
        seat_post_input_event(keyboard, ptr_to(SeatEvent {
            .keyboard = {
                .type = SeatEventType::keyboard_modifier,
                .keyboard = keyboard,
            },
        }));
    }
}

auto seat_keyboard_key(SeatKeyboard* keyboard, SeatInputCode code, bool pressed, bool quiet) -> Flags<xkb_state_component>
{
    Flags<xkb_state_component> changed = {};

    if (pressed ? keyboard->pressed.inc(code) : keyboard->pressed.dec(code)) {
        // TODO: Query symbol *before* update
        changed = xkb_state_update_key(keyboard->state, evdev_to_xkb(code), pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

        seat_post_input_event(keyboard, ptr_to(SeatEvent {
            .keyboard = {
                .type = SeatEventType::keyboard_key,
                .keyboard = keyboard,
                .key = {
                    .code = code,
                    .pressed = pressed,
                    .quiet = quiet,
                },
            }
        }));

        handle_xkb_component_updates(keyboard, changed);
    }

    return changed;
}

void seat_keyboard_focus(SeatKeyboard* keyboard, SeatInputRegion* new_focus)
{
    auto* old_focus = seat_keyboard_get_focus(keyboard);

    keyboard->focus = new_focus;

    if (old_focus == new_focus) return;

    auto old_client = seat_get_focus_client(old_focus);
    auto new_client = seat_get_focus_client(new_focus);

    if (old_client && (old_client != new_client)) {
        seat_post_event(keyboard->seat, old_client, ptr_to(SeatEvent {
            .keyboard = {
                .type = SeatEventType::keyboard_leave,
                .keyboard = keyboard,
            },
        }));
    }

    if (new_focus) {
        seat_post_event(keyboard->seat, new_client, ptr_to(SeatEvent {
            .keyboard = {
                .type = SeatEventType::keyboard_enter,
                .keyboard = keyboard,
                .focus = new_focus,
            },
        }));
    }
}

// -----------------------------------------------------------------------------

auto seat_keyboard_get_focus(SeatKeyboard* keyboard) -> SeatInputRegion*
{
    return keyboard->focus;
}

auto seat_keyboard_get_seat(SeatKeyboard* keyboard) -> Seat*
{
    return keyboard->seat;
}

auto seat_keyboard_get_pressed(SeatKeyboard* keyboard) -> std::span<const SeatInputCode>
{
    return keyboard->pressed;
}

auto seat_keyboard_get_sym(SeatKeyboard* keyboard, SeatInputCode code) -> xkb_keysym_t
{
    return xkb_state_key_get_one_sym(keyboard->state, evdev_to_xkb(code));
}

auto seat_keyboard_get_utf8(SeatKeyboard* keyboard, SeatInputCode code) -> std::string
{
    std::string utf8;
    utf8.resize(xkb_state_key_get_utf8(keyboard->state, evdev_to_xkb(code), nullptr, 0));
    xkb_state_key_get_utf8(keyboard->state, evdev_to_xkb(code), utf8.data(), utf8.size() + 1);
    return utf8;
}

auto seat_keyboard_get_info(SeatKeyboard* keyboard) -> const SeatKeyboardInfo&
{
    return *keyboard;
}
