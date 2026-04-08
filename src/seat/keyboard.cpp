#include "seat.hpp"

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

SeatKeyboard::~SeatKeyboard()
{
    xkb_keymap_unref(keymap);
    xkb_state_unref(state);
    xkb_context_unref(context);
}

void seat_keyboard_press( SeatKeyboard* keyboard, SeatInputCode code)
{
    if (keyboard->pressed.inc(code)) {
        log_warn("key {} pressed", libevdev_event_code_get_name(EV_KEY, code));
    }
}

void seat_keyboard_release(SeatKeyboard* keyboard, SeatInputCode code)
{
    if (keyboard->pressed.dec(code)) {
        log_warn("key {} released", libevdev_event_code_get_name(EV_KEY, code));
    }
}
