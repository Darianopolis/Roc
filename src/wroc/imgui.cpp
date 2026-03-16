#include "wroc.hpp"
#include "event.hpp"

#include "wroc_imgui_shader.hpp"
#include "shaders/imgui.h"

static
wp_cursor_shape_device_v1_shape from_imgui_cursor(ImGuiMouseCursor cursor)
{
    switch (cursor) {
        break;case ImGuiMouseCursor_None:       return {};
        break;case ImGuiMouseCursor_Arrow:      return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
        break;case ImGuiMouseCursor_TextInput:  return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT;
        break;case ImGuiMouseCursor_ResizeAll:  return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_RESIZE;
        break;case ImGuiMouseCursor_ResizeNS:   return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE;
        break;case ImGuiMouseCursor_ResizeEW:   return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE;
        break;case ImGuiMouseCursor_ResizeNESW: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE;
        break;case ImGuiMouseCursor_ResizeNWSE: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE;
        break;case ImGuiMouseCursor_Hand:       return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB;
        break;case ImGuiMouseCursor_Wait:       return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT;
        break;case ImGuiMouseCursor_Progress:   return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_PROGRESS;
        break;case ImGuiMouseCursor_NotAllowed: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED;
    }

    core_unreachable();
}

void wroc_imgui_init()
{
    auto* gpu = server->gpu;

    server->imgui = core_create<wroc_imgui>();
    auto* imgui = server->imgui.get();

    imgui->vertex = gpu_shader_create(gpu, {
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .code = wroc_imgui_shader,
        .entry = "vertex"
    });
    imgui->fragment = gpu_shader_create(gpu, {
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .code = wroc_imgui_shader,
        .entry = "fragment"
    });

    imgui->context = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui->context);

    auto& style = ImGui::GetStyle();
    style.FrameRounding = 3;
    style.ScrollbarRounding = 3;
    style.WindowRounding = 5;
    style.WindowBorderSize = 0;

    auto& io = ImGui::GetIO();

    {
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        imgui->font_image = gpu_image_create(gpu, {
            .extent = {width, height},
            .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
            .usage = gpu_image_usage::texture | gpu_image_usage::transfer
        });
        gpu_copy_memory_to_image(imgui->font_image.get(), {pixels, usz(width * height * 4)}, {{{width, height}}});

        io.Fonts->SetTexID(ImTextureID(wroc_imgui_texture(imgui->font_image.get(), server->renderer->sampler.get())));
    }

    imgui->cursor_shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;

    wroc_launcher_init();
}

static
bool wroc_imgui_handle_pointer_event(wroc_imgui* imgui, const wroc_pointer_event& event)
{
    auto& io = ImGui::GetIO();

    switch (wroc_event_get_type(event)) {
        break;case wroc_event_type::pointer_motion: {
            auto pos = event.pointer->position - imgui->layout_rect.origin;
            io.AddMousePosEvent(pos.x, pos.y);
            // Never consume motion events (for now)
            return false;
        }
        break;case wroc_event_type::pointer_button:
            switch (event.button.button) {
                break;case BTN_LEFT:   io.AddMouseButtonEvent(ImGuiMouseButton_Left, event.button.pressed);
                break;case BTN_RIGHT:  io.AddMouseButtonEvent(ImGuiMouseButton_Right, event.button.pressed);
                break;case BTN_MIDDLE: io.AddMouseButtonEvent(ImGuiMouseButton_Middle, event.button.pressed);
            }
        break;case wroc_event_type::pointer_axis:
            io.AddMouseWheelEvent(event.axis.delta.x, -event.axis.delta.y);
        break;default:
            ;
    }

    return io.WantCaptureMouse;
}

static
ImGuiKey wroc_imgui_key_from_xkb_sym(u32 code)
{
    switch (code) {
        case XKB_KEY_Shift_L:   return ImGuiKey_LeftShift;
        case XKB_KEY_Shift_R:   return ImGuiKey_RightShift;
        case XKB_KEY_Control_L: return ImGuiKey_LeftCtrl;
        case XKB_KEY_Control_R: return ImGuiKey_RightCtrl;
        case XKB_KEY_Alt_L:     return ImGuiKey_LeftAlt;
        case XKB_KEY_Alt_R:     return ImGuiKey_RightAlt;
        case XKB_KEY_Super_L:   return ImGuiKey_LeftSuper;
        case XKB_KEY_Super_R:   return ImGuiKey_RightSuper;

        case XKB_KEY_Tab:       return ImGuiKey_Tab;
        case XKB_KEY_Left:      return ImGuiKey_LeftArrow;
        case XKB_KEY_Right:     return ImGuiKey_RightArrow;
        case XKB_KEY_Up:        return ImGuiKey_UpArrow;
        case XKB_KEY_Down:      return ImGuiKey_DownArrow;
        case XKB_KEY_Page_Up:   return ImGuiKey_PageUp;
        case XKB_KEY_Page_Down: return ImGuiKey_PageDown;
        case XKB_KEY_Home:      return ImGuiKey_Home;
        case XKB_KEY_End:       return ImGuiKey_End;
        case XKB_KEY_Insert:    return ImGuiKey_Insert;
        case XKB_KEY_Delete:    return ImGuiKey_Delete;
        case XKB_KEY_BackSpace: return ImGuiKey_Backspace;
        case XKB_KEY_space:     return ImGuiKey_Space;
        case XKB_KEY_Return:    return ImGuiKey_Enter;
        case XKB_KEY_Escape:    return ImGuiKey_Escape;
        case XKB_KEY_Menu:      return ImGuiKey_Menu;

        case XKB_KEY_0: return ImGuiKey_0;
        case XKB_KEY_1: return ImGuiKey_1;
        case XKB_KEY_2: return ImGuiKey_2;
        case XKB_KEY_3: return ImGuiKey_3;
        case XKB_KEY_4: return ImGuiKey_4;
        case XKB_KEY_5: return ImGuiKey_5;
        case XKB_KEY_6: return ImGuiKey_6;
        case XKB_KEY_7: return ImGuiKey_7;
        case XKB_KEY_8: return ImGuiKey_8;
        case XKB_KEY_9: return ImGuiKey_9;

        case XKB_KEY_A: case XKB_KEY_a: return ImGuiKey_A;
        case XKB_KEY_B: case XKB_KEY_b: return ImGuiKey_B;
        case XKB_KEY_C: case XKB_KEY_c: return ImGuiKey_C;
        case XKB_KEY_D: case XKB_KEY_d: return ImGuiKey_D;
        case XKB_KEY_E: case XKB_KEY_e: return ImGuiKey_E;
        case XKB_KEY_F: case XKB_KEY_f: return ImGuiKey_F;
        case XKB_KEY_G: case XKB_KEY_g: return ImGuiKey_G;
        case XKB_KEY_H: case XKB_KEY_h: return ImGuiKey_H;
        case XKB_KEY_I: case XKB_KEY_i: return ImGuiKey_I;
        case XKB_KEY_J: case XKB_KEY_j: return ImGuiKey_J;
        case XKB_KEY_K: case XKB_KEY_k: return ImGuiKey_K;
        case XKB_KEY_L: case XKB_KEY_l: return ImGuiKey_L;
        case XKB_KEY_M: case XKB_KEY_m: return ImGuiKey_M;
        case XKB_KEY_N: case XKB_KEY_n: return ImGuiKey_N;
        case XKB_KEY_O: case XKB_KEY_o: return ImGuiKey_O;
        case XKB_KEY_P: case XKB_KEY_p: return ImGuiKey_P;
        case XKB_KEY_Q: case XKB_KEY_q: return ImGuiKey_Q;
        case XKB_KEY_R: case XKB_KEY_r: return ImGuiKey_R;
        case XKB_KEY_S: case XKB_KEY_s: return ImGuiKey_S;
        case XKB_KEY_T: case XKB_KEY_t: return ImGuiKey_T;
        case XKB_KEY_U: case XKB_KEY_u: return ImGuiKey_U;
        case XKB_KEY_V: case XKB_KEY_v: return ImGuiKey_V;
        case XKB_KEY_W: case XKB_KEY_w: return ImGuiKey_W;
        case XKB_KEY_X: case XKB_KEY_x: return ImGuiKey_X;
        case XKB_KEY_Y: case XKB_KEY_y: return ImGuiKey_Y;
        case XKB_KEY_Z: case XKB_KEY_z: return ImGuiKey_Z;

        case XKB_KEY_F1:  return ImGuiKey_F1;
        case XKB_KEY_F2:  return ImGuiKey_F2;
        case XKB_KEY_F3:  return ImGuiKey_F3;
        case XKB_KEY_F4:  return ImGuiKey_F4;
        case XKB_KEY_F5:  return ImGuiKey_F5;
        case XKB_KEY_F6:  return ImGuiKey_F6;
        case XKB_KEY_F7:  return ImGuiKey_F7;
        case XKB_KEY_F8:  return ImGuiKey_F8;
        case XKB_KEY_F9:  return ImGuiKey_F9;
        case XKB_KEY_F10: return ImGuiKey_F10;
        case XKB_KEY_F11: return ImGuiKey_F11;
        case XKB_KEY_F12: return ImGuiKey_F12;
        case XKB_KEY_F13: return ImGuiKey_F13;
        case XKB_KEY_F14: return ImGuiKey_F14;
        case XKB_KEY_F15: return ImGuiKey_F15;
        case XKB_KEY_F16: return ImGuiKey_F16;
        case XKB_KEY_F17: return ImGuiKey_F17;
        case XKB_KEY_F18: return ImGuiKey_F18;
        case XKB_KEY_F19: return ImGuiKey_F19;
        case XKB_KEY_F20: return ImGuiKey_F20;
        case XKB_KEY_F21: return ImGuiKey_F21;
        case XKB_KEY_F22: return ImGuiKey_F22;
        case XKB_KEY_F23: return ImGuiKey_F23;
        case XKB_KEY_F24: return ImGuiKey_F24;

        case XKB_KEY_apostrophe:   return ImGuiKey_Apostrophe;
        case XKB_KEY_comma:        return ImGuiKey_Comma;
        case XKB_KEY_minus:        return ImGuiKey_Minus;
        case XKB_KEY_period:       return ImGuiKey_Period;
        case XKB_KEY_slash:        return ImGuiKey_Slash;
        case XKB_KEY_semicolon:    return ImGuiKey_Semicolon;
        case XKB_KEY_equal:        return ImGuiKey_Equal;
        case XKB_KEY_bracketleft:  return ImGuiKey_LeftBracket;
        case XKB_KEY_backslash:    return ImGuiKey_Backslash;
        case XKB_KEY_bracketright: return ImGuiKey_RightBracket;
        case XKB_KEY_grave:        return ImGuiKey_GraveAccent;
        case XKB_KEY_Caps_Lock:    return ImGuiKey_CapsLock;
        case XKB_KEY_Scroll_Lock:  return ImGuiKey_ScrollLock;
        case XKB_KEY_Num_Lock:     return ImGuiKey_NumLock;
        case XKB_KEY_Print:        return ImGuiKey_PrintScreen;
        case XKB_KEY_Pause:        return ImGuiKey_Pause;

        case XKB_KEY_KP_0: return ImGuiKey_Keypad0;
        case XKB_KEY_KP_1: return ImGuiKey_Keypad1;
        case XKB_KEY_KP_2: return ImGuiKey_Keypad2;
        case XKB_KEY_KP_3: return ImGuiKey_Keypad3;
        case XKB_KEY_KP_4: return ImGuiKey_Keypad4;
        case XKB_KEY_KP_5: return ImGuiKey_Keypad5;
        case XKB_KEY_KP_6: return ImGuiKey_Keypad6;
        case XKB_KEY_KP_7: return ImGuiKey_Keypad7;
        case XKB_KEY_KP_8: return ImGuiKey_Keypad8;
        case XKB_KEY_KP_9: return ImGuiKey_Keypad9;

        case XKB_KEY_KP_Decimal:  return ImGuiKey_KeypadDecimal;
        case XKB_KEY_KP_Divide:   return ImGuiKey_KeypadDivide;
        case XKB_KEY_KP_Multiply: return ImGuiKey_KeypadMultiply;
        case XKB_KEY_KP_Subtract: return ImGuiKey_KeypadSubtract;
        case XKB_KEY_KP_Add:      return ImGuiKey_KeypadAdd;
        case XKB_KEY_KP_Enter:    return ImGuiKey_KeypadEnter;
        case XKB_KEY_KP_Equal:    return ImGuiKey_KeypadEqual;

        case XKB_KEY_KP_Home:     return ImGuiKey_Home;
        case XKB_KEY_KP_End:      return ImGuiKey_End;
        case XKB_KEY_KP_Prior:    return ImGuiKey_PageUp;
        case XKB_KEY_KP_Next:     return ImGuiKey_PageDown;
        case XKB_KEY_KP_Insert:   return ImGuiKey_Insert;
        case XKB_KEY_KP_Delete:   return ImGuiKey_Delete;

        case XKB_KEY_KP_Left:     return ImGuiKey_LeftArrow;
        case XKB_KEY_KP_Right:    return ImGuiKey_RightArrow;
        case XKB_KEY_KP_Up:       return ImGuiKey_UpArrow;
        case XKB_KEY_KP_Down:     return ImGuiKey_DownArrow;

        // TODO: ImGuiKey_AppBack
        // TODO: ImGuiKey_AppForward
        // TODO: ImGuiKey_Oem102
    }

    return ImGuiKey_None;
}

static
bool wroc_imgui_handle_key(wroc_imgui* imgui, const wroc_keyboard_event& event)
{
    auto& io = ImGui::GetIO();

    // Keys

    auto imkey = wroc_imgui_key_from_xkb_sym(event.key.symbol);
    io.AddKeyEvent(imkey, event.key.pressed);

    // Text

    if (event.key.pressed) {
        io.AddInputCharactersUTF8(event.key.utf8);
    }

    return event.key.pressed && io.WantCaptureKeyboard;
}

static
bool wroc_imgui_handle_mods(wroc_imgui* imgui, const wroc_keyboard_event& event)
{
    auto& io = ImGui::GetIO();

    auto mods = wroc_get_active_modifiers();
    io.AddKeyEvent(ImGuiMod_Shift, mods.contains(wroc_modifier::shift));
    io.AddKeyEvent(ImGuiMod_Ctrl,  mods.contains(wroc_modifier::ctrl));
    io.AddKeyEvent(ImGuiMod_Alt,   mods.contains(wroc_modifier::alt));
    io.AddKeyEvent(ImGuiMod_Super, mods.contains(wroc_modifier::super));

    return false;
}

bool wroc_imgui_handle_event(wroc_imgui* imgui, const wroc_event& event)
{
    switch (wroc_event_get_type(event)) {
        break;case wroc_event_type::pointer_motion:
              case wroc_event_type::pointer_button:
              case wroc_event_type::pointer_axis:
            return wroc_imgui_handle_pointer_event(imgui, static_cast<const wroc_pointer_event&>(event));
        break;case wroc_event_type::keyboard_key:
            return wroc_imgui_handle_key(imgui, static_cast<const wroc_keyboard_event&>(event));
        break;case wroc_event_type::keyboard_modifiers:
            return wroc_imgui_handle_mods(imgui, static_cast<const wroc_keyboard_event&>(event));
        break;default:
            ;
    }

    return false;
}

void wroc_imgui_frame(wroc_imgui* imgui, rect2f64 layout_rect)
{
    imgui->layout_rect = layout_rect;

    {
        auto& io = ImGui::GetIO();

        io.DisplaySize = ImVec2(layout_rect.extent.x, layout_rect.extent.y);

        auto now = std::chrono::steady_clock::now();
        if (imgui->last_frame != std::chrono::steady_clock::time_point{}) {
            io.DeltaTime = std::min(std::chrono::duration_cast<std::chrono::duration<f32>>(now - imgui->last_frame).count(), 1.f);
        }
        imgui->last_frame = now;
    }

    auto& io = ImGui::GetIO();

    ImGui::NewFrame();

    wroc_debug_gui_frame(server->debug_gui.get());
    wroc_launcher_frame(server->launcher.get(), layout_rect.origin + layout_rect.extent * 0.5);

    ImGui::Render();

    imgui->wants_mouse = io.WantCaptureMouse;
    imgui->cursor_shape = from_imgui_cursor(ImGui::GetMouseCursor());
    imgui->wants_keyboard = io.WantCaptureKeyboard;

    // Run callbacks

    for (auto& callback : imgui->on_render) {
        callback();
    }
    imgui->on_render.clear();
}

void wroc_imgui_render(wroc_imgui* imgui, gpu_commands* commands, rect2f64 viewport, vec2u32 framebuffer_extent)
{
    auto data = ImGui::GetDrawData();
    if (!data || !data->TotalIdxCount) return;

    // Consider imgui space as "global" for purposes of this function

    viewport.origin -= imgui->layout_rect.origin;

    wroc_coord_space space {
        .origin = viewport.origin,
        .scale = viewport.extent / vec2f64(framebuffer_extent),
    };

    auto* gpu = server->gpu;

    // Dynamically allocated per-frame data

    struct frame_guard : wroc_object
    {
        wroc_imgui_frame_data frame_data;
        weak<wroc_imgui> imgui;

        ~frame_guard()
        {
            if (imgui) {
                imgui->available_frames.emplace_back(std::move(frame_data));
            }
        }
    };
    auto guard = core_create<frame_guard>();
    guard->imgui = imgui;
    gpu_protect(commands, guard.get());

    wroc_imgui_frame_data* frame = &guard->frame_data;
    if (!imgui->available_frames.empty()) {
        *frame = std::move(imgui->available_frames.back());
        imgui->available_frames.pop_back();
    } else {
        log_debug("Allocating new ImGui frame data");
    }

    if (frame->vertices.count < usz(data->TotalVtxCount)) {
        auto new_size = core_compute_geometric_growth(frame->vertices.count, data->TotalVtxCount);
        log_debug("ImGui - reallocating vertex buffer, size: {}", new_size);
        frame->vertices = {gpu_buffer_create(gpu, new_size * sizeof(ImDrawVert), {}), usz(new_size)};
    }

    if (frame->indices.count < usz(data->TotalIdxCount)) {
        auto new_size = core_compute_geometric_growth(frame->indices.count, data->TotalIdxCount);
        log_debug("ImGui - reallocating index buffer, size: {}", new_size);
        frame->indices = {gpu_buffer_create(gpu, new_size * sizeof(ImDrawIdx), {}), usz(new_size)};
    }

    // TODO: Protect images
    auto cmd = commands->buffer;

    gpu_cmd_bind_shaders(commands, {imgui->vertex.get(), imgui->fragment.get()});
    gpu_cmd_reset_graphics_state(commands);
    gpu_cmd_set_blend_state(commands, {gpu_blend_mode::postmultiplied});
    gpu->vk.CmdBindIndexBuffer(cmd,
        frame->indices.buffer->buffer, frame->indices.byte_offset,
        sizeof(ImDrawIdx) == sizeof(u16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);

    u32 vertex_offset = 0;
    u32 index_offset = 0;
    for (i32 i = 0; i < data->CmdListsCount; ++i) {
        auto list = data->CmdLists[i];

        std::memcpy(frame->vertices.host() + vertex_offset, list->VtxBuffer.Data, list->VtxBuffer.size() * sizeof(ImDrawVert));
        std::memcpy(frame->indices.host()  + index_offset,  list->IdxBuffer.Data, list->IdxBuffer.size() * sizeof(ImDrawIdx));

        for (i32 j = 0; j < list->CmdBuffer.size(); ++j) {
            const auto& im_cmd = list->CmdBuffer[j];

            core_assert(!im_cmd.UserCallback);

            auto clip_min = glm::max(space.from_global({im_cmd.ClipRect.x, im_cmd.ClipRect.y}), {});
            auto clip_max = glm::min(space.from_global({im_cmd.ClipRect.z, im_cmd.ClipRect.w}), vec2f64(framebuffer_extent));

            if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) {
                continue;
            }

            gpu_cmd_set_scissors(commands, {{clip_min, clip_max, core_minmax}});

            auto draw_scale = 2.f / vec2f32(viewport.extent);
            gpu_cmd_push_constants(commands, 0, core_view_bytes(wroc_imgui_shader_in {
                .vertices = frame->vertices.device(),
                .scale = draw_scale,
                .offset = vec2f32(-1.f) - (vec2f32(viewport.origin) * draw_scale),
                .texture = std::bit_cast<wroc_imgui_texture>(im_cmd.GetTexID()).handle,
            }));
            gpu->vk.CmdDrawIndexed(cmd, im_cmd.ElemCount, 1, index_offset + im_cmd.IdxOffset, vertex_offset + im_cmd.VtxOffset, 0);
        }

        vertex_offset += list->VtxBuffer.size();
        index_offset += list->IdxBuffer.size();
    }
}
