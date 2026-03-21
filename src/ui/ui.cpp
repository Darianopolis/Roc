#include "internal.hpp"

#include "core/stack.hpp"

thread_local ImGuiContext* ui_imgui_ctx;

// -----------------------------------------------------------------------------

static auto imgui_cursor_to_xcursor(ImGuiMouseCursor) -> const char*;
static auto imgui_key_from_xkb_sym(xkb_keysym_t) -> ImGuiKey;

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
auto get_context(ImGuiContext* ctx = ImGui::GetCurrentContext()) -> ui_context*
{
    return static_cast<ui_context*>(ctx->IO.BackendPlatformUserData);
}

static
auto get_viewports() -> std::span<ImGuiViewport* const>
{
    return to_span(ImGui::GetPlatformIO().Viewports).subspan(1);
}

static
auto get_data(ImGuiViewport* vp) -> ui_viewport_data*
{
    return static_cast<ui_viewport_data*>(vp->PlatformUserData);
}

// -----------------------------------------------------------------------------

static
auto find_viewport_for_input_region(ui_context* ctx, scene_input_region* region) -> ImGuiViewport*
{
    for (auto* vp : get_viewports()) {
        if (auto* data = get_data(vp); data && data->input_plane.get() == region) {
            return vp;
        }
    }

    core_assert_fail("find_viewport_for_input_plane", "Failed to find viewport for region");
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

auto ui_get_window(ImGuiWindow* window) -> scene_window*
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
    auto* data = new ui_viewport_data();

    data->window = scene_window_create(ctx->client.get());

    data->input_plane = scene_input_region_create(ctx->client.get(), data->window.get());
    scene_tree_place_above(scene_window_get_tree(data->window.get()), nullptr, data->input_plane.get());

    data->draws = scene_tree_create(ctx->scene);
    scene_tree_place_above(scene_window_get_tree(data->window.get()), nullptr, data->draws.get());

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

static
const char* text_mime_types[] = {
    "text/plain;charset=utf-8",
    "text/plain",
    "text/html",
};

static
void Platform_SetClipboardTextFn(ImGuiContext* imctx, const char* text)
{
    auto* ctx = get_context(imctx);

    auto data_source = scene_data_source_create(ctx->client.get(), {
        .send = [message = std::string(text)](const char* mime, int fd) {
            unix_check<write>(fd, message.data(), message.size());
        }
    });
    for (auto* mime : text_mime_types) {
        scene_data_source_offer(data_source.get(), mime);
    }
    scene_set_selection(ctx->scene, data_source.get());
}

static
auto read_to_string(int fd) -> std::string
{
    std::string str;
    for (;;) {
        char buffer[4096];
        auto[len, err] = unix_check<read>(fd, buffer, sizeof(buffer));
        if (len <= 0) break;
        str.append_range(std::span(buffer, len));
    }
    return str;
}

static
auto Platform_GetClipboardTextFn(ImGuiContext* imctx) -> const char*
{
    auto* ctx = get_context(imctx);

    if (auto* source = scene_get_selection(ctx->scene)) {
        auto available = scene_data_source_get_offered(source);
        for (auto mime : text_mime_types) {
            if (std::ranges::contains(available, std::string_view(mime))) {
                auto[read, write] = [] {
                    int fd[2] = {-1, -1};
                    unix_check<pipe>(fd);
                    return std::make_pair(core_fd(fd[0]), core_fd(fd[1]));
                }();
                scene_data_source_send(source, mime, write.get());
                write.reset();

                ctx->clipboard.text = read_to_string(read.get());
                return ctx->clipboard.text.c_str();
            }
        }
    }

    return nullptr;
}

// -----------------------------------------------------------------------------

struct ui_context_guard
{
    ImGuiContext* old;

    ui_context_guard(ImGuiContext* ctx)
        : old(ImGui::GetCurrentContext())
    {
        ImGui::SetCurrentContext(ctx);
    }

    ~ui_context_guard()
    {
        ImGui::SetCurrentContext(old);
    }
};

ui_context::~ui_context()
{
    ui_context_guard _{context};
    ImGui::DestroyPlatformWindows();
    ImGui::GetIO().BackendPlatformUserData = nullptr;
    ImGui::DestroyContext(context);
}

// -----------------------------------------------------------------------------

static
void reset_frame_textures(ui_context* ctx)
{
    ctx->textures.clear();
    // Leave 0 as an invalid texture id
    ctx->textures.emplace_back();
}

auto ui_get_texture(ui_context* ctx, gpu_image* image, gpu_sampler* sampler, gpu_blend_mode blend) -> ImTextureID
{
    auto idx = ctx->textures.size();
    ctx->textures.emplace_back(image, sampler, blend);
    return idx;
}

// -----------------------------------------------------------------------------

void ui_init(ui_context* ctx, const std::filesystem::path& path)
{
    ctx->context = ImGui::CreateContext();
    ImGui::SetCurrentContext(nullptr);
    ui_context_guard _{ctx->context};

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

    std::filesystem::create_directories(path);
    ctx->ini_path = (path / "imgui.ini").string();
    io.IniFilename = ctx->ini_path.c_str();

    {
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        ctx->font_image = gpu_image_create(ctx->gpu, {
            .extent = {width, height},
            .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
            .usage = gpu_image_usage::texture | gpu_image_usage::transfer
        });
        gpu_copy_memory_to_image(ctx->font_image.get(), {pixels, usz(width * height * 4)}, {{{width, height}}});
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

    platform_io.Platform_SetClipboardTextFn = Platform_SetClipboardTextFn;
    platform_io.Platform_GetClipboardTextFn = Platform_GetClipboardTextFn;

    // Create dummy main viewport.
    // This will never be used to draw anything as it will be zero sized, but
    // it needs to exist until ImGui removes the requirement for a main viewport.
    ImGui::GetMainViewport()->PlatformUserData = new ui_viewport_data();
}

// -----------------------------------------------------------------------------

void ui_request_frame(ui_context* ctx)
{
    // Double-pump frames: ImGui always works based on last frame state,
    // so input needs a second frame to react against the updated state.
    ctx->frames_requested = 2;
    scene_request_frame(ctx->scene);
}

static
void render_viewport(ui_context* ctx, ImGuiViewport* vp)
{
    auto* data = get_data(vp);
    if (!data || !vp->DrawData) return;

    scene_tree_clear(data->draws.get());

    auto translation = from_imvec(vp->Pos);

    for (auto* list : to_span(vp->DrawData->CmdLists)) {
        for (auto& cmd : to_span(list->CmdBuffer)) {
            auto indices = to_span(list->IdxBuffer).subspan(cmd.IdxOffset, cmd.ElemCount);
            auto vtx_count = std::ranges::max(indices) + 1;

            core_thread_stack stack;

            auto* vertices = stack.allocate<scene_vertex>(vtx_count);
            for (auto[i, imvert] : to_span(list->VtxBuffer).subspan(cmd.VtxOffset, vtx_count) | std::views::enumerate) {
                vertices[i] = scene_vertex {
                    .pos = from_imvec(imvert.pos) - translation,
                    .uv  = from_imvec(imvert.uv),
                    .color = std::bit_cast<vec4u8>(imvert.col),
                };
            }

            rect2f32 clip = {{cmd.ClipRect.x, cmd.ClipRect.y}, {cmd.ClipRect.z, cmd.ClipRect.w}, core_minmax};
            clip.origin -= translation;

            auto[image, sampler, blend] = ctx->textures[cmd.GetTexID()];
            auto mesh = scene_mesh_create(ctx->scene);
            scene_mesh_update(mesh.get(), image.get(), sampler.get(), blend, clip, {vertices, usz(vtx_count)}, indices);
            scene_tree_place_above(data->draws.get(), nullptr, mesh.get());
        }
    }

    // Update visual frame

    {
        rect2f32 rect {translation, from_imvec(vp->Size), core_xywh};
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
        ui_request_frame(ctx);
    }
}

void ui_frame(ui_context* ctx)
{
    if (!ctx->frames_requested) return;
    ctx->frames_requested--;

    if (ctx->frames_requested) scene_request_frame(ctx->scene);

    auto& io = ImGui::GetIO();
    io.DisplaySize = {};

    reset_frame_textures(ctx);
    io.Fonts->SetTexID(ui_get_texture(ctx, ctx->font_image.get(), ctx->sampler.get(),
                                        gpu_blend_mode::postmultiplied));

    ImGui::NewFrame();
    ctx->frame_handler();
    ImGui::Render();

    if (ctx->pointer) {
        scene_pointer_set_xcursor(ctx->pointer, imgui_cursor_to_xcursor(ImGui::GetMouseCursor()));
    }

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
void handle_reposition(ui_context* ctx, scene_window* window, rect2f32 frame)
{
    if (auto* vp = find_viewport_for_window(window)) {
        get_data(vp)->reposition = frame;
        ui_request_frame(ctx);
    }
}

static
void handle_close(ui_context* ctx, scene_window* window)
{
    if (auto* vp = find_viewport_for_window(window)) {
        vp->PlatformRequestClose = true;
        ui_request_frame(ctx);
    }
}

auto ui_create(gpu_context* gpu, scene_context* scene, const std::filesystem::path& path) -> ref<ui_context>
{
    auto ctx = core_create<ui_context>();
    ctx->scene   = scene;
    ctx->gpu     = gpu;
    ctx->sampler = gpu_sampler_create(ctx->gpu, {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_LINEAR,
    });
    ctx->client  = scene_client_create(scene);

    ui_init(ctx.get(), path);

    scene_client_set_event_handler(ctx->client.get(), [ctx = ctx.get()](scene_event* event) {
        ui_context_guard _{ctx->context};
        switch (event->type) {
            // seat
            break;case scene_event_type::seat_add:
                  case scene_event_type::seat_configure:
                  case scene_event_type::seat_remove:
                ;

            // keyboard
            break;case scene_event_type::keyboard_enter:
                ui_handle_keyboard_enter(ctx, event->keyboard.keyboard, event->keyboard.focus.region);
            break;case scene_event_type::keyboard_leave:
                ui_handle_keyboard_leave(ctx);
            break;case scene_event_type::keyboard_key:
                ui_handle_key(ctx, event->keyboard.key.code, event->keyboard.key.pressed);
            break;case scene_event_type::keyboard_modifier:
                ui_handle_mods(ctx);

            // pointer
            break;case scene_event_type::pointer_enter:
                ui_handle_pointer_enter(ctx, event->pointer.pointer, event->pointer.focus.region);
            break;case scene_event_type::pointer_leave:
                ui_handle_pointer_leave(ctx);
            break;case scene_event_type::pointer_motion:
                ui_handle_motion(ctx);
            break;case scene_event_type::pointer_button:
                ui_handle_button(ctx, event->pointer.button.code, event->pointer.button.pressed);
            break;case scene_event_type::pointer_scroll:
                ui_handle_wheel(ctx, event->pointer.scroll.delta);

            // window
            break;case scene_event_type::window_reposition:
                handle_reposition(ctx, event->window.window, event->window.reposition.frame);
            break;case scene_event_type::window_close:
                handle_close(ctx, event->window.window);

            // output
            break;case scene_event_type::output_added:
                  case scene_event_type::output_configured:
                  case scene_event_type::output_removed:
                  case scene_event_type::output_frame_request:
                ;
            break;case scene_event_type::output_frame:
                ui_frame(ctx);
            break;case scene_event_type::output_layout:
                ui_handle_output_layout(ctx);

            // hotkey
            break;case scene_event_type::hotkey:
                ;

            // selection
            break;case scene_event_type::selection:
                ;
        }
    });

    return ctx;
}

void ui_set_frame_handler(ui_context* ctx, std::move_only_function<ui_frame_fn>&& handler)
{
    ctx->frame_handler = std::move(handler);
}

// -----------------------------------------------------------------------------

void ui_handle_keyboard_enter(ui_context* ctx, scene_keyboard* keyboard, scene_input_region* region)
{
    ctx->keyboard = keyboard;

    if (auto* vp = find_viewport_for_input_region(ctx, region)) {
        scene_window_raise(get_data(vp)->window.get());
    }

    auto& io = ImGui::GetIO();
    io.AddFocusEvent(true);

    ui_request_frame(ctx);
}

void ui_handle_keyboard_leave(ui_context* ctx)
{
    ctx->keyboard = nullptr;

    auto& io = ImGui::GetIO();
    io.AddFocusEvent(false);

    ui_request_frame(ctx);
}

void ui_handle_key(ui_context* ctx, scene_scancode code, bool pressed)
{
    auto& io = ImGui::GetIO();

    io.AddKeyEvent(imgui_key_from_xkb_sym(scene_keyboard_get_sym(ctx->keyboard, code)), pressed);
    if (pressed) io.AddInputCharactersUTF8(scene_keyboard_get_utf8(ctx->keyboard, code).c_str());

    ui_request_frame(ctx);
}

void ui_handle_mods(ui_context* ctx)
{
    auto& io = ImGui::GetIO();

    auto mods = scene_keyboard_get_modifiers(ctx->keyboard);

    io.AddKeyEvent(ImGuiMod_Shift, mods.contains(scene_modifier::shift));
    io.AddKeyEvent(ImGuiMod_Ctrl,  mods.contains(scene_modifier::ctrl));
    io.AddKeyEvent(ImGuiMod_Alt,   mods.contains(scene_modifier::alt));
    io.AddKeyEvent(ImGuiMod_Super, mods.contains(scene_modifier::super));

    ui_request_frame(ctx);
}

void ui_handle_motion(ui_context* ctx)
{
    auto& io = ImGui::GetIO();

    auto pos = scene_pointer_get_position(ctx->pointer);
    io.AddMousePosEvent(pos.x, pos.y);

    ui_request_frame(ctx);
}

void ui_handle_button(ui_context* ctx, scene_scancode code, bool pressed)
{
    auto& io = ImGui::GetIO();

    bool handled = false;
    switch (code) {
        break;case BTN_LEFT:   io.AddMouseButtonEvent(ImGuiMouseButton_Left,   pressed); handled = true;
        break;case BTN_RIGHT:  io.AddMouseButtonEvent(ImGuiMouseButton_Right,  pressed); handled = true;
        break;case BTN_MIDDLE: io.AddMouseButtonEvent(ImGuiMouseButton_Middle, pressed); handled = true;
    }

    if (!handled) return;

    ui_request_frame(ctx);
}

void ui_handle_wheel(ui_context* ctx, vec2f32 delta)
{
    auto& io = ImGui::GetIO();

    io.AddMouseWheelEvent(delta.x, -delta.y);
    ui_request_frame(ctx);
}

void ui_handle_pointer_enter(ui_context* ctx, scene_pointer* pointer, scene_input_region* region)
{
    ctx->pointer = pointer;

    auto& io = ImGui::GetIO();

    if (region) {
        io.AddMouseViewportEvent(find_viewport_for_input_region(ctx, region)->ID);
    }

    auto pos = scene_pointer_get_position(pointer);
    io.AddMousePosEvent(pos.x, pos.y);

    ui_request_frame(ctx);
}

void ui_handle_pointer_leave(ui_context* ctx)
{
    auto& io = ImGui::GetIO();

    io.AddMouseViewportEvent(0);
    io.AddMousePosEvent(-FLT_MAX,-FLT_MAX);

    for (auto code : scene_pointer_get_pressed(ctx->pointer)) {
        ui_handle_button(ctx, code, false);
    }

    ctx->pointer = nullptr;

    ui_request_frame(ctx);
}

void ui_handle_output_layout(ui_context* ctx)
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

    ui_request_frame(ctx);
}

// -----------------------------------------------------------------------------

static
auto imgui_cursor_to_xcursor(ImGuiMouseCursor cursor) -> const char*
{
    switch (cursor) {
        break;case ImGuiMouseCursor_None:       return nullptr;
        break;case ImGuiMouseCursor_Arrow:      return "arrow";
        break;case ImGuiMouseCursor_TextInput:  return "text";
        break;case ImGuiMouseCursor_ResizeAll:  return "all-resize";
        break;case ImGuiMouseCursor_ResizeNS:   return "ns-resize";
        break;case ImGuiMouseCursor_ResizeEW:   return "ew-resize";
        break;case ImGuiMouseCursor_ResizeNESW: return "nesw-resize";
        break;case ImGuiMouseCursor_ResizeNWSE: return "nwse-resize";
        break;case ImGuiMouseCursor_Hand:       return "grab";
        break;case ImGuiMouseCursor_Wait:       return "wait";
        break;case ImGuiMouseCursor_Progress:   return "progress";
        break;case ImGuiMouseCursor_NotAllowed: return "not-allowed";
    }

    core_unreachable();
}

static
auto imgui_key_from_xkb_sym(xkb_keysym_t sym) -> ImGuiKey
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
