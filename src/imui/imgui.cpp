#include "internal.hpp"

static ImGuiKey imgui_key_from_xkb_sym(xkb_keysym_t);

// -----------------------------------------------------------------------------

static
auto to_imvec(vec2f32 v) -> ImVec2
{
    return {v.x, v.y};
}

static
auto from_imvec(ImVec2 v) -> vec2f32
{
    return {v.x, v.y};
}

template<typename T>
auto to_span(ImVector<T>& v) -> std::span<T>
{
    return std::span(v.Data, v.Size);
}

static
auto get_context() -> imui_context*
{
    return static_cast<imui_context*>(ImGui::GetIO().BackendPlatformUserData);
}

static
auto get_viewports() -> std::span<ImGuiViewport* const>
{
    return to_span(ImGui::GetPlatformIO().Viewports).subspan(1);
}

static
auto get_data(ImGuiViewport* vp) -> imui_viewport_data*
{
    return static_cast<imui_viewport_data*>(vp->PlatformUserData);
}

// -----------------------------------------------------------------------------

static
auto find_viewport_for_input_region(imui_context* ctx, scene_input_region* region) -> ImGuiViewport*
{
    for (auto* vp : get_viewports()) {
        if (auto* data = get_data(vp); data && data->input_plane.get() == region) {
            return vp;
        }
    }

    core_assert_fail("find_viewport_for_input_plane", "Failed to find viewport for region");
}

static
auto find_viewport_for_id(imui_context* ctx, ImGuiID id) -> ImGuiViewport*
{
    for (auto* vp : get_viewports()) {
        if (vp->ID == id) return vp;
    }

    return nullptr;
}

static
auto find_viewport_for_window(scene_window* window) -> ImGuiViewport*
{
    for (auto* vp : get_viewports()) {
        if (auto* data = get_data(vp); data && data->window.get() == window) {
            return vp;
        }
    }

    return nullptr;
}

auto imui_get_window(ImGuiWindow* window) -> scene_window*
{
    if (!window->Viewport) return nullptr;
    if (auto* data = get_data(window->Viewport)) return data->window.get();
    return nullptr;
}

// -----------------------------------------------------------------------------

static
void Platform_CreateWindow(ImGuiViewport* vp)
{
    auto* ctx = get_context();
    auto* data = new imui_viewport_data();

    data->window = scene_window_create(ctx->client.get());

    data->input_plane = scene_input_region_create(ctx->client.get());
    scene_node_set_transform(data->input_plane.get(), scene_window_get_transform(data->window.get()));
    scene_tree_place_above(scene_window_get_tree(data->window.get()), nullptr, data->input_plane.get());

    vp->PlatformUserData = data;
}

static
void Platform_DestroyWindow(ImGuiViewport* vp)
{
    delete get_data(vp);
    vp->PlatformUserData = nullptr;
}

static
void Platform_ShowWindow(ImGuiViewport* vp)
{
    scene_window_map(get_data(vp)->window.get());
}

static
auto Platform_GetWindowPos(ImGuiViewport* vp) -> ImVec2
{
    return vp->Pos;
}

static
void Platform_SetWindowPos(ImGuiViewport* vp, ImVec2 pos)
{
    vp->Pos = pos;
}

static
auto Platform_GetWindowSize(ImGuiViewport* vp) -> ImVec2
{
    return vp->Size;
}

static
void Platform_SetWindowSize(ImGuiViewport* vp, ImVec2 size)
{
    vp->Size = size;
}

static
void Platform_SetWindowTitle(ImGuiViewport* vp, const char* title)
{
    scene_window_set_title(get_data(vp)->window.get(), title);
}

// -----------------------------------------------------------------------------

CORE_OBJECT_EXPLICIT_DEFINE(imui_context);

imui_context::~imui_context()
{
    ImGui::SetCurrentContext(context);
    ImGui::DestroyPlatformWindows();
    ImGui::GetIO().BackendPlatformUserData = nullptr;
    ImGui::DestroyContext(context);
}

// -----------------------------------------------------------------------------

static
void reset_frame_textures(imui_context* ctx)
{
    ctx->textures.clear();
    // Leave 0 as an invalid texture id
    ctx->textures.emplace_back();
}

auto imui_get_texture(imui_context* ctx, gpu_image* image, gpu_sampler* sampler, gpu_blend_mode blend) -> ImTextureID
{
    auto idx = ctx->textures.size();
    ctx->textures.emplace_back(image, sampler, blend);
    return idx;
}

// -----------------------------------------------------------------------------

void imui_init(imui_context* ctx)
{
    ctx->context = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx->context);

    auto& style = ImGui::GetStyle();
    style.FrameRounding     = 3;
    style.ScrollbarRounding = 3;
    style.WindowRounding    = 5;
    style.WindowBorderSize  = 0;

    auto& io = ImGui::GetIO();
    io.ConfigFlags         |= ImGuiConfigFlags_ViewportsEnable;
    io.BackendFlags        |= ImGuiBackendFlags_PlatformHasViewports
                           |  ImGuiBackendFlags_RendererHasViewports
                           |  ImGuiBackendFlags_HasMouseHoveredViewport;
    io.BackendPlatformName  = PROJECT_NAME;
    io.BackendRendererName  = PROJECT_NAME;
    io.BackendPlatformUserData = ctx;

    {
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        ctx->font_image = gpu_image_create(ctx->gpu, {width, height},
            gpu_format_from_drm(DRM_FORMAT_ABGR8888),
            gpu_image_usage::texture | gpu_image_usage::transfer);
        gpu_image_update_immed(ctx->font_image.get(), pixels);
    }

    auto& platform_io = ImGui::GetPlatformIO();
    platform_io.Platform_CreateWindow   = Platform_CreateWindow;
    platform_io.Platform_DestroyWindow  = Platform_DestroyWindow;
    platform_io.Platform_ShowWindow     = Platform_ShowWindow;
    platform_io.Platform_GetWindowPos   = Platform_GetWindowPos;
    platform_io.Platform_SetWindowPos   = Platform_SetWindowPos;
    platform_io.Platform_GetWindowSize  = Platform_GetWindowSize;
    platform_io.Platform_SetWindowSize  = Platform_SetWindowSize;
    platform_io.Platform_SetWindowTitle = Platform_SetWindowTitle;

    // Create dummy main viewport.
    // This will never be used to draw anything as it will be zero sized, but
    // it needs to exist until ImGui removes the requirement for a main viewport.
    ImGui::GetMainViewport()->PlatformUserData = new imui_viewport_data();
}

// -----------------------------------------------------------------------------

void imui_request_frame(imui_context* ctx)
{
    // Double-pump frames: ImGui always works based on last frame state,
    // so input needs a second frame to react against the updated state.
    ctx->frames_requested = 2;
    scene_request_redraw(ctx->scene);
}

static
void render_viewport(imui_context* ctx, ImGuiViewport* vp)
{
    auto* data = get_data(vp);
    if (!data || !vp->DrawData) return;

    if (data->draws) scene_node_unparent(data->draws.get());
    data->draws = scene_tree_create(ctx->scene);
    scene_tree_place_above(scene_window_get_tree(data->window.get()), nullptr, data->draws.get());

    auto* root_transform = scene_get_root_transform(ctx->scene);

    for (auto* list : to_span(vp->DrawData->CmdLists)) {
        for (auto& cmd : to_span(list->CmdBuffer)) {
            auto indices = to_span(list->IdxBuffer).subspan(cmd.IdxOffset, cmd.ElemCount);
            auto max_vtx = std::ranges::max(indices);

            static_assert(  sizeof(scene_vertex)        ==   sizeof(ImDrawVert));
            static_assert( alignof(scene_vertex)        ==  alignof(ImDrawVert));
            static_assert(offsetof(scene_vertex, pos  ) == offsetof(ImDrawVert, pos));
            static_assert(offsetof(scene_vertex, uv   ) == offsetof(ImDrawVert, uv ));
            static_assert(offsetof(scene_vertex, color) == offsetof(ImDrawVert, col));
            auto vertices = std::span(reinterpret_cast<scene_vertex*>(list->VtxBuffer.Data) + cmd.VtxOffset, max_vtx + 1);

            auto [image, sampler, blend] = ctx->textures[cmd.GetTexID()];
            auto mesh = scene_mesh_create(ctx->scene);
            scene_mesh_update(mesh.get(), image.get(), sampler.get(), blend,
                {{cmd.ClipRect.x, cmd.ClipRect.y}, {cmd.ClipRect.z, cmd.ClipRect.w}, core_minmax},
                vertices, indices);
            scene_node_set_transform(mesh.get(), root_transform);
            scene_tree_place_above(data->draws.get(), nullptr, mesh.get());
        }
    }

    // Update visual frame

    {
        rect2f32 rect {from_imvec(vp->Pos), from_imvec(vp->Size), core_xywh};
        if (rect != scene_window_get_frame(data->window.get())) {
            scene_input_region_set_region(data->input_plane.get(), {{{}, rect.extent, core_xywh}});
            scene_window_set_frame(data->window.get(), rect);
        }
    }

    // Apply any pending resizes for next frame

    if (auto frame = std::exchange(data->reposition, std::nullopt)) {
        vp->WorkPos  = vp->Pos  = to_imvec(frame->origin);
        vp->WorkSize = vp->Size = to_imvec(frame->extent);
        vp->PlatformRequestMove = true;
        vp->PlatformRequestResize = true;
        imui_request_frame(ctx);
    }
}

void imui_frame(imui_context* ctx)
{
    if (!ctx->frames_requested) return;
    ctx->frames_requested--;

    if (ctx->frames_requested) scene_request_redraw(ctx->scene);

    auto& io = ImGui::GetIO();
    io.DisplaySize = {};

    reset_frame_textures(ctx);
    io.Fonts->SetTexID(imui_get_texture(ctx, ctx->font_image.get(), ctx->sampler.get(),
                                        gpu_blend_mode::postmultiplied));

    ImGui::NewFrame();
    for (auto& handler : ctx->frame_handlers) handler();
    ImGui::Render();

    // Zero-sized main viewport should never contain draw data
    if (auto* main_draw_data = ImGui::GetDrawData()) {
        if (main_draw_data->TotalIdxCount) {
            log_error("Unexpected geometry ({}, {}) in main viewport", main_draw_data->TotalIdxCount, main_draw_data->TotalVtxCount);
        }
    }

    ImGui::UpdatePlatformWindows();
    for (auto* vp : get_viewports()) {
        if (vp->Flags & ImGuiViewportFlags_IsMinimized) continue;
        render_viewport(ctx, vp);
    }
}

// -----------------------------------------------------------------------------

static
void handle_reposition(imui_context* ctx, scene_window* window, rect2f32 frame)
{
    if (auto* vp = find_viewport_for_window(window)) {
        get_data(vp)->reposition = frame;
        imui_request_frame(ctx);
    }
}

auto imui_create(gpu_context* gpu, scene_context* scene) -> ref<imui_context>
{
    auto ctx = core_create<imui_context>();
    ctx->scene   = scene;
    ctx->gpu     = gpu;
    ctx->sampler = gpu_sampler_create(ctx->gpu, VK_FILTER_NEAREST, VK_FILTER_LINEAR);
    ctx->client  = scene_client_create(scene);

    imui_init(ctx.get());

    scene_client_set_event_handler(ctx->client.get(), [ctx = ctx.get()](scene_event* event) {
        ImGui::SetCurrentContext(ctx->context);
        switch (event->type) {
            // keyboard
            break;case scene_event_type::keyboard_key:
                imui_handle_key(ctx, event->key.code, event->key.pressed);
            break;case scene_event_type::keyboard_modifier:
                imui_handle_mods(ctx, scene_keyboard_get_modifiers(ctx->scene));

            // pointer
            break;case scene_event_type::pointer_motion:
                imui_handle_motion(ctx);
            break;case scene_event_type::pointer_button:
                imui_handle_button(ctx, event->key.code, event->key.pressed);
            break;case scene_event_type::pointer_scroll:
                imui_handle_wheel(ctx, event->pointer.scroll.delta);

            // focus
            break;case scene_event_type::focus_pointer:
                imui_handle_focus_pointer(ctx, event->focus.gained);
            break;case scene_event_type::focus_keyboard:
                ;

            // window
            break;case scene_event_type::window_reposition:
                handle_reposition(ctx, event->window.window, event->window.reposition.frame);

            // output
            break;case scene_event_type::output_added:
                  case scene_event_type::output_configured:
                  case scene_event_type::output_removed:
                  case scene_event_type::output_damaged:
                ;
            break;case scene_event_type::output_frame:
                imui_frame(ctx);
            break;case scene_event_type::output_layout:
                imui_handle_output_layout(ctx);

            // hotkey
            break;case scene_event_type::hotkey:
                ;
        }
    });

    return ctx;
}

void imui_add_frame_handler(imui_context* ctx, std::move_only_function<imui_frame_fn>&& handler)
{
    ctx->frame_handlers.emplace_back(std::move(handler));
}

// -----------------------------------------------------------------------------

void imui_handle_key(imui_context* ctx, scene_scancode code, bool pressed)
{
    auto& io = ImGui::GetIO();

    io.AddKeyEvent(imgui_key_from_xkb_sym(scene_keyboard_get_sym(ctx->scene, code)), pressed);
    if (pressed) io.AddInputCharactersUTF8(scene_keyboard_get_utf8(ctx->scene, code).c_str());

    imui_request_frame(ctx);
}

void imui_handle_mods(imui_context* ctx, flags<scene_modifier> mods)
{
    auto& io = ImGui::GetIO();

    io.AddKeyEvent(ImGuiMod_Shift, mods.contains(scene_modifier::shift));
    io.AddKeyEvent(ImGuiMod_Ctrl,  mods.contains(scene_modifier::ctrl));
    io.AddKeyEvent(ImGuiMod_Alt,   mods.contains(scene_modifier::alt));
    io.AddKeyEvent(ImGuiMod_Super, mods.contains(scene_modifier::super));

    imui_request_frame(ctx);
}

void imui_handle_motion(imui_context* ctx)
{
    auto& io = ImGui::GetIO();

    auto pos = scene_pointer_get_position(ctx->scene);
    io.AddMousePosEvent(pos.x, pos.y);

    imui_request_frame(ctx);
}

void imui_handle_button(imui_context* ctx, scene_scancode code, bool pressed)
{
    auto& io = ImGui::GetIO();

    bool handled = false;
    switch (code) {
        break;case BTN_LEFT:   io.AddMouseButtonEvent(ImGuiMouseButton_Left,   pressed); handled = true;
        break;case BTN_RIGHT:  io.AddMouseButtonEvent(ImGuiMouseButton_Right,  pressed); handled = true;
        break;case BTN_MIDDLE: io.AddMouseButtonEvent(ImGuiMouseButton_Middle, pressed); handled = true;
    }

    if (!handled) return;

    if (pressed) {
        scene_keyboard_grab(ctx->client.get());
        scene_pointer_grab(ctx->client.get());
        if (auto* vp = find_viewport_for_id(ctx, ImGui::GetIO().MouseHoveredViewport)) {
            scene_window_raise(get_data(vp)->window.get());
        }
    } else {
        scene_pointer_ungrab(ctx->client.get());
    }

    imui_request_frame(ctx);
}

void imui_handle_wheel(imui_context* ctx, vec2f32 delta)
{
    auto& io = ImGui::GetIO();

    io.AddMouseWheelEvent(delta.x, -delta.y);
    imui_request_frame(ctx);
}

void imui_handle_focus_pointer(imui_context* ctx, scene_focus gained)
{
    auto& io = ImGui::GetIO();

    if (gained.client != ctx->client.get()) {
        io.AddMouseViewportEvent(0);
        io.AddMousePosEvent(-FLT_MAX,-FLT_MAX);

    } else if (gained.region) {
        io.AddMouseViewportEvent(find_viewport_for_input_region(ctx, gained.region)->ID);

        auto pos = scene_pointer_get_position(ctx->scene);
        io.AddMousePosEvent(pos.x, pos.y);
    }

    imui_request_frame(ctx);
}

void imui_handle_output_layout(imui_context* ctx)
{
    auto& platform_io = ImGui::GetPlatformIO();

    platform_io.Monitors.clear();
    for (auto* output : scene_list_outputs(ctx->scene)) {
        rect2f32 rect = scene_output_get_viewport(output);
        if (rect.extent.x == 0 || rect.extent.y == 0) continue;

        ImGuiPlatformMonitor monitor = {};
        monitor.WorkPos  = monitor.MainPos  = to_imvec(rect.origin);
        monitor.WorkSize = monitor.MainSize = to_imvec(rect.extent);
        platform_io.Monitors.push_back(monitor);
    }

    imui_request_frame(ctx);
}

// -----------------------------------------------------------------------------

static
ImGuiKey imgui_key_from_xkb_sym(xkb_keysym_t sym)
{
    switch (sym) {
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

        case XKB_KEY_KP_Home:   return ImGuiKey_Home;
        case XKB_KEY_KP_End:    return ImGuiKey_End;
        case XKB_KEY_KP_Prior:  return ImGuiKey_PageUp;
        case XKB_KEY_KP_Next:   return ImGuiKey_PageDown;
        case XKB_KEY_KP_Insert: return ImGuiKey_Insert;
        case XKB_KEY_KP_Delete: return ImGuiKey_Delete;

        case XKB_KEY_KP_Left:  return ImGuiKey_LeftArrow;
        case XKB_KEY_KP_Right: return ImGuiKey_RightArrow;
        case XKB_KEY_KP_Up:    return ImGuiKey_UpArrow;
        case XKB_KEY_KP_Down:  return ImGuiKey_DownArrow;

        // TODO: ImGuiKey_AppBack
        // TODO: ImGuiKey_AppForward
        // TODO: ImGuiKey_Oem102
    }

    return ImGuiKey_None;
}
