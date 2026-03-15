#include "wroc.hpp"

#include "wroc/event.hpp"

static void wroc_seat_keyboard_update_state(wroc_seat_keyboard*, wroc_key_action, std::span<const u32> actioned_keys);
static libinput_led wroc_seat_keyboard_get_leds(wroc_seat_keyboard*);

void wroc_keyboard::press(u32 keycode)
{
    if (!pressed.insert(keycode).second) return;

    wroc_seat_keyboard_update_state(target.get(), wroc_key_action::press, {keycode});
}

void wroc_keyboard::release(u32 keycode)
{
    if (!pressed.erase(keycode)) return;

    wroc_seat_keyboard_update_state(target.get(), wroc_key_action::release, {keycode});
}

void wroc_keyboard::enter(std::span<const u32> keycodes)
{
    std::vector<u32> filtered;

    for (auto& keycode : keycodes) {
        if (pressed.insert(keycode).second) {
            filtered.emplace_back(keycode);
        }
    }

    if (filtered.empty()) return;

    wroc_seat_keyboard_update_state(target.get(), wroc_key_action::enter, filtered);
}

void wroc_keyboard::leave()
{
    wroc_seat_keyboard_update_state(target.get(), wroc_key_action::release, pressed);

    pressed.clear();
}

wroc_keyboard::~wroc_keyboard()
{
    leave();

    if (target) {
        std::erase(target->sources, this);
    }
}

void wroc_seat_keyboard::attach(wroc_keyboard* kb)
{
    core_assert(!kb->target && "wroc_keyboard already attached to seat keyboard");

    sources.emplace_back(kb);
    kb->target = this;

    wroc_seat_keyboard_update_state(this, wroc_key_action::enter, kb->pressed);
    kb->update_leds(wroc_seat_keyboard_get_leds(this));
}

void wroc_seat_init_keyboard(wroc_seat* seat)
{
    seat->keyboard = core::create<wroc_seat_keyboard>();
    seat->keyboard->seat = seat;

    auto* kb = seat->keyboard.get();

    // Initialize XKB

    kb->context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    kb->keymap = xkb_keymap_new_from_names(kb->context, core::ptr_to(xkb_rule_names{
        .layout = "gb",
    }), XKB_KEYMAP_COMPILE_NO_FLAGS);

    kb->state = xkb_state_new(kb->keymap);

    // Get XKB modifier masks

    kb->mod_masks[wroc_modifier::super] = xkb_keymap_mod_get_mask(kb->keymap, XKB_MOD_NAME_LOGO);
    kb->mod_masks[wroc_modifier::shift] = xkb_keymap_mod_get_mask(kb->keymap, XKB_MOD_NAME_SHIFT);
    kb->mod_masks[wroc_modifier::ctrl]  = xkb_keymap_mod_get_mask(kb->keymap, XKB_MOD_NAME_CTRL);
    kb->mod_masks[wroc_modifier::alt]   = xkb_keymap_mod_get_mask(kb->keymap, XKB_MOD_NAME_ALT);
    kb->mod_masks[wroc_modifier::num]   = xkb_keymap_mod_get_mask(kb->keymap, XKB_MOD_NAME_NUM);
    kb->mod_masks[wroc_modifier::caps]  = xkb_keymap_mod_get_mask(kb->keymap, XKB_MOD_NAME_CAPS);

    // Create keymap file

    char* keymap_str = xkb_keymap_get_as_string(kb->keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    if (!keymap_str) {
        log_error("Failed to get string version of keymap");
        return;
    }
    usz keymap_size = strlen(keymap_str) + 1;

    int rw_fd = -1;
    int ro_fd = -1;
    if (!core_allocate_shm_file_pair(keymap_size, &rw_fd, &ro_fd)) {
        log_error("Failed to allocate shm file for keymap");
        return;
    }

    void* dst = core::check<mmap>(nullptr, keymap_size, PROT_READ | PROT_WRITE, MAP_SHARED, rw_fd, 0).value;
    close(rw_fd);
    if (!dst) {
        log_error("mmap failed");
        close(ro_fd);
        return;
    }

    memcpy(dst, keymap_str, keymap_size);
    munmap(dst, keymap_size);
    free(keymap_str);

    kb->keymap_fd = ro_fd;
    kb->keymap_size = keymap_size;

    // Start with numlock enabled

    kb->set_locked(wroc_modifier::num, true);
}

wroc_seat_keyboard::~wroc_seat_keyboard()
{
    xkb_keymap_unref(keymap);
    xkb_state_unref(state);
    xkb_context_unref(context);
}

void wroc_seat_keyboard_send_configuration(wroc_seat_keyboard* kb, wl_client*, wl_resource* resource)
{
    wroc_send(wl_keyboard_send_keymap, resource, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, kb->keymap_fd, kb->keymap_size);

    if (wl_resource_get_version(resource) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION) {
        wroc_send(wl_keyboard_send_repeat_info, resource, 25, 600);
    }
}

static
libinput_led wroc_seat_keyboard_get_leds(wroc_seat_keyboard* kb)
{
    int leds = 0;
    if (xkb_state_led_name_is_active(kb->state, XKB_LED_NAME_NUM))    leds |= LIBINPUT_LED_NUM_LOCK;
    if (xkb_state_led_name_is_active(kb->state, XKB_LED_NAME_CAPS))   leds |= LIBINPUT_LED_CAPS_LOCK;
    if (xkb_state_led_name_is_active(kb->state, XKB_LED_NAME_SCROLL)) leds |= LIBINPUT_LED_SCROLL_LOCK;

    return libinput_led(leds);
}

static
void wroc_seat_keyboard_update_leds(wroc_seat_keyboard* kb)
{
    auto leds = wroc_seat_keyboard_get_leds(kb);
    for (auto* source : kb->sources) {
        source->update_leds(leds);
    }
}

bool wroc_seat_keyboard::is_locked(wroc_modifier mod) const
{
    auto locked = xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED);
    return locked & (mod_masks[mod]);
}

static
void wroc_seat_keyboard_handle_component_updates(wroc_seat_keyboard* kb, core::Flags<xkb_state_component> changed_components)
{
    if (changed_components & core::Flags(XKB_STATE_MODS_DEPRESSED, XKB_STATE_MODS_LATCHED, XKB_STATE_MODS_LOCKED, XKB_STATE_LAYOUT_EFFECTIVE)) {
        log_debug("Updated modifiers");
        wroc_post_event(wroc_keyboard_event {
            .type = wroc_event_type::keyboard_modifiers,
            .keyboard = kb,
            .mods {
                .depressed = xkb_state_serialize_mods(kb->state, XKB_STATE_MODS_DEPRESSED),
                .latched   = xkb_state_serialize_mods(kb->state, XKB_STATE_MODS_LATCHED),
                .locked    = xkb_state_serialize_mods(kb->state, XKB_STATE_MODS_LOCKED),
                .group     = xkb_state_serialize_mods(kb->state, XKB_STATE_LAYOUT_EFFECTIVE),
            }
        });
    }

    if (changed_components & XKB_STATE_LEDS) {
        log_debug("Updated LEDs");
        wroc_seat_keyboard_update_leds(kb);
    }
}

void wroc_seat_keyboard::set_locked(wroc_modifier mod, bool locked)
{
    auto mask = mod_masks[mod];

    auto updated = xkb_state_update_latched_locked(state,
        0,    0,                 false, 0,
        mask, locked ? mask : 0, false, 0);
    wroc_seat_keyboard_handle_component_updates(this, updated);
}

static
void wroc_seat_keyboard_update_state(wroc_seat_keyboard* kb, wroc_key_action action, std::span<const u32> actioned_keys)
{
    core::Flags<xkb_state_component> updated = {};

    for (auto key : actioned_keys) {
        if (action == wroc_key_action::release ? kb->pressed.dec(key) : kb->pressed.inc(key)) {
            if (action != wroc_key_action::enter) {

                char utf8[128] = {};
                if (action == wroc_key_action::press) {
                    xkb_state_key_get_utf8(kb->state, wroc_key_to_xkb(key), utf8, sizeof(utf8));
                }

                wroc_post_event(wroc_keyboard_event {
                    .type = wroc_event_type::keyboard_key,
                    .keyboard = kb,
                    .key = {
                        .code = key,
                        .symbol = xkb_state_key_get_one_sym(kb->state, wroc_key_to_xkb(key)),
                        .utf8 = utf8,
                        .pressed = action == wroc_key_action::press
                    },
                });
            }
        }

        updated |= xkb_state_update_key(kb->state, wroc_key_to_xkb(key), action == wroc_key_action::release ?  XKB_KEY_UP : XKB_KEY_DOWN);
    }

    wroc_seat_keyboard_handle_component_updates(kb, updated);
}

core::Flags<wroc_modifier> wroc_keyboard_get_active_modifiers(wroc_seat_keyboard* kb)
{
    core::Flags<wroc_modifier> down = {};

    auto xkb_mods = xkb_state_serialize_mods(kb->state, XKB_STATE_MODS_EFFECTIVE);
    for (auto mod : kb->mod_masks.enum_values) {
        if (xkb_mods & kb->mod_masks[mod]) down |= mod;
    }

    if (down.contains(server->main_mod)) {
        down |= wroc_modifier::mod;
    }

    return down;
}

core::Flags<wroc_modifier> wroc_get_active_modifiers()
{
    core::Flags<wroc_modifier> mods = {};
    if (server->seat->keyboard) {
        mods |= wroc_keyboard_get_active_modifiers(server->seat->keyboard.get());
    }
    return mods;
}

static
bool wroc_keyboard_resource_matches_focus_client(wroc_seat_keyboard* kb, wl_resource* resource)
{
    if (!kb->focused_surface) return false;
    if (!kb->focused_surface->resource) return false;
    return wroc_resource_get_client(resource) == wroc_resource_get_client(kb->focused_surface->resource);
}

void wroc_keyboard_clear_focus(wroc_seat_keyboard* kb)
{
    if (auto* surface = kb->focused_surface.get(); surface && surface->resource) {
        log_debug("Keyboard left surface: {}", (void*)surface);
        auto serial = wl_display_next_serial(server->display);

        for (auto* resource : kb->resources) {
            if (!wroc_keyboard_resource_matches_focus_client(kb, resource)) continue;
            for (auto keycode : kb->pressed) {
                wroc_send(wl_keyboard_send_key, resource,
                    serial,
                    wroc_get_elapsed_milliseconds(),
                    u32(keycode), WL_KEYBOARD_KEY_STATE_RELEASED);
            }
            wroc_send(wl_keyboard_send_modifiers, resource, serial, 0, 0, 0, 0);
            wroc_send(wl_keyboard_send_leave, resource, serial, surface->resource);
        }
    }
    kb->focused_surface = nullptr;
    wroc_update_pointer_constraint_state();
}

void wroc_keyboard_enter(wroc_seat_keyboard* kb, wroc_surface* surface)
{
    if (surface == kb->focused_surface.get()) return;

    if (surface && !wroc_surface_is_focusable(surface)) {
        log_error("Keyboard enter failed: surface is not focusable");
        return;
    }

    wroc_keyboard_clear_focus(kb);

    if (!surface) return;

    // TODO: Consolidate "client seat" into a with client's wl_keyboard/wl_pointer handles
    //       To deduplicate code between this and `wroc_pointer`

    log_debug("Keyboard entered surface: {}", (void*)surface);
    kb->focused_surface = surface;

    wroc_update_pointer_constraint_state();

    auto serial = wl_display_next_serial(server->display);

    for (auto* resource : kb->resources) {
        if (!wroc_keyboard_resource_matches_focus_client(kb, resource)) continue;

        wroc_data_manager_offer_selection(wroc_resource_get_client(resource));

        wroc_send(wl_keyboard_send_enter, resource,
            serial,
            surface->resource, core::ptr_to(wroc_to_wl_array<const u32>(kb->pressed)));

        wroc_send(wl_keyboard_send_modifiers, resource,
            serial,
            xkb_state_serialize_mods(kb->state, XKB_STATE_MODS_DEPRESSED),
            xkb_state_serialize_mods(kb->state, XKB_STATE_MODS_LATCHED),
            xkb_state_serialize_mods(kb->state, XKB_STATE_MODS_LOCKED),
            xkb_state_serialize_layout(kb->state, XKB_STATE_LAYOUT_EFFECTIVE));

        break;
    }

    wroc_surface_raise(surface);
}

#define WROC_NOISY_KEYBOARD 0

#if WROC_NOISY_KEYBOARD
static
void wroc_debug_print_key(wroc_seat_keyboard* kb, const wroc_keyboard_event& event)
{
    auto evdev_keycode = event.key.code;
    auto sym = event.key.symbol;
    auto pressed = event.key.pressed;

    char name[128] = {};
    xkb_keysym_get_name(sym, name, sizeof(name) - 1);

    auto utf8_escaped = core::escape_utf8(event.key.utf8);

    if (strcmp(name, event.key.utf8) == 0) {
        log_debug("key '{}' (sym: {}, evdev: {}) = {}", utf8_escaped, sym, evdev_keycode, pressed ? "press" : "release");
    } else if (!utf8_escaped.empty()) {
        log_debug("key <{}> '{}' (sym: {}, evdev: {}) = {}", name, utf8_escaped, sym, evdev_keycode, pressed ? "press" : "release");
    } else {
        log_debug("key <{}> (sym: {}, evdev: {}) = {}", name, sym, evdev_keycode, pressed ? "press" : "release");
    }
}
#endif

static
void wroc_keyboard_key(wroc_seat_keyboard* kb, const wroc_keyboard_event& event)
{
#if WROC_NOISY_KEYBOARD
    wroc_debug_print_key(kb, event);
#endif

    for (auto* resource : kb->resources) {
        if (!wroc_keyboard_resource_matches_focus_client(kb, resource)) continue;
        wroc_send(wl_keyboard_send_key, resource,
            wl_display_next_serial(server->display),
            wroc_get_elapsed_milliseconds(),
            event.key.code, event.key.pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
    }
}

static
void wroc_keyboard_modifiers(wroc_seat_keyboard* kb, u32 mods_depressed, u32 mods_latched, u32 mods_locked, u32 group)
{
    for (auto* resource : kb->resources) {
        if (!wroc_keyboard_resource_matches_focus_client(kb, resource)) continue;
        wroc_send(wl_keyboard_send_modifiers, resource,
            wl_display_next_serial(server->display),
            mods_depressed,
            mods_latched,
            mods_locked,
            group);
    }
}

void wroc_handle_keyboard_event(const wroc_keyboard_event& event)
{
    switch (event.type) {
        case wroc_event_type::keyboard_key:
            wroc_keyboard_key(event.keyboard, event);
            break;
        case wroc_event_type::keyboard_modifiers:
            wroc_keyboard_modifiers(event.keyboard, event.mods.depressed, event.mods.latched, event.mods.locked, event.mods.group);
            break;
        default:
            break;
    }
}
