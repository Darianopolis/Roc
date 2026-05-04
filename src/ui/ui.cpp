#include "internal.hpp"

#include <core/stack.hpp>

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
auto get_context(ImGuiContext* imgui = ImGui::GetCurrentContext()) -> Ui*
{
    return static_cast<Ui*>(imgui->IO.BackendPlatformUserData);
}

static
auto get_viewports() -> std::span<ImGuiViewport* const>
{
    return to_span(ImGui::GetPlatformIO().Viewports).subspan(1);
}

static
auto get_data(ImGuiViewport* vp) -> UiViewportData*
{
    return static_cast<UiViewportData*>(vp->PlatformUserData);
}

// -----------------------------------------------------------------------------

static
auto find_viewport_for_focus(Ui* ui, SeatFocus* focus) -> ImGuiViewport*
{
    for (auto* vp : get_viewports()) {
        if (auto* data = get_data(vp); data && data->focus.get() == focus) {
            return vp;
        }
    }

    debug_assert_fail("find_viewport_for_focus", "Failed to find viewport for focus");
}

static
auto find_viewport_for_window(WmWindow* window) -> ImGuiViewport*
{
    for (auto* vp : get_viewports()) {
        if (auto* data = get_data(vp); data && data->window.get() == window) {
            return vp;
        }
    }

    return nullptr;
}

auto ui_get_window(ImGuiWindow* window) -> WmWindow*
{
    if (!window->Viewport) return nullptr;
    if (auto* data = get_data(window->Viewport)) return data->window.get();
    return nullptr;
}

// -----------------------------------------------------------------------------

static
void handle_reposition(Ui* ui, WmWindow* window, rect2f32 frame)
{
    if (auto* vp = find_viewport_for_window(window)) {
        get_data(vp)->reposition = frame;
        ui_request_frame(ui);
    }
}

static
void handle_close(Ui* ui, WmWindow* window)
{
    if (auto* vp = find_viewport_for_window(window)) {
        vp->PlatformRequestClose = true;
        ui_request_frame(ui);
    }
}

static
void Platform_CreateWindow(ImGuiViewport* vp)
{
    auto* ui = get_context();
    auto* data = new UiViewportData();

    data->window = wm_window_create(ui->client.get());

    data->input_region = scene_input_region_create();
    data->focus = seat_focus_create(wm_get_seat_client(ui->client.get()), data->input_region.get());
    wm_window_set_focus(data->window.get(), data->focus.get());
    scene_tree_place_above(wm_window_get_tree(data->window.get()), nullptr, data->input_region.get());

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
    auto* ui = get_context();
    auto* data = get_data(vp);

    wm_window_map(data->window.get());
    for (auto* seat : wm_get_seats(ui->wm)) {
        seat_keyboard_focus(seat_get_keyboard(seat), data->focus.get());
    }
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
    wm_window_set_title(get_data(vp)->window.get(), title);
}

// -----------------------------------------------------------------------------

static
const char* text_mime_types[] = {
    "text/plain;charset=utf-8",
    "text/plain",
    "text/html",
};

struct UiDataSource : SeatDataSource
{
    std::string message;

    virtual void on_cancel() final override {}
    virtual void on_send(const char* mime, fd_t fd) final override
    {
        unix_check<write>(fd, message.data(), message.size());
    }
};

static
void Platform_SetClipboardTextFn(ImGuiContext* imgui, const char* text)
{
    auto* ui = get_context(imgui);

    auto data_source = ref_create<UiDataSource>();
    data_source->message = text;

    for (auto* mime : text_mime_types) {
        seat_data_source_offer(data_source.get(), mime);
    }

    for (auto* seat : ui->seats) {
        seat_set_selection(seat, data_source.get());
    }
}

static
auto read_to_string(fd_t fd) -> std::string
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
auto Platform_GetClipboardTextFn(ImGuiContext* imgui) -> const char*
{
    auto* ui = get_context(imgui);

    for (auto* seat : ui->seats) {
        if (auto* source = seat_get_selection(seat)) {
            auto available = seat_data_source_get_offered(source);
            for (auto mime : text_mime_types) {
                if (std::ranges::contains(available, std::string_view(mime))) {
                    auto[read, write] = [] {
                        fd_t fd[2] = {-1, -1};
                        unix_check<pipe>(fd);
                        return std::make_pair(Fd(fd[0]), Fd(fd[1]));
                    }();
                    seat_data_source_receive(source, mime, write.get());
                    write.reset();

                    ui->clipboard.text = read_to_string(read.get());
                    return ui->clipboard.text.c_str();
                }
            }
        }
    }

    return nullptr;
}

// -----------------------------------------------------------------------------

Ui::~Ui()
{
    UiContextGuard _{context};
    ImGui::DestroyPlatformWindows();
    ImGui::GetIO().BackendPlatformUserData = nullptr;
    ImGui::DestroyContext(context);
}

// -----------------------------------------------------------------------------

static
void reset_frame_textures(Ui* ui)
{
    ui->textures.clear();
    // Leave 0 as an invalid texture id
    ui->textures.emplace_back();
}

auto ui_get_texture(Ui* ui, GpuImage* image, GpuSampler* sampler, GpuBlendMode blend) -> ImTextureID
{
    auto idx = ui->textures.size();
    ui->textures.emplace_back(image, sampler, blend);
    return idx;
}

// -----------------------------------------------------------------------------

static
void init(Ui* ui, const std::filesystem::path& path)
{
    ui->context = ImGui::CreateContext();
    ImGui::SetCurrentContext(nullptr);
    UiContextGuard _{ui->context};

    auto& style = ImGui::GetStyle();
    style.FrameRounding     = 3;
    style.ScrollbarRounding = 3;
    style.WindowRounding    = 5;
    style.WindowBorderSize  = 0;

    auto& io = ImGui::GetIO();
    io.ConfigFlags         |= ImGuiConfigFlags_ViewportsEnable;
    io.BackendFlags        |= ImGuiBackendFlags_PlatformHasViewports
                           |  ImGuiBackendFlags_RendererHasViewports
                           |  ImGuiBackendFlags_RendererHasVtxOffset
                           |  ImGuiBackendFlags_HasMouseHoveredViewport;
    io.BackendPlatformName  = PROJECT_NAME;
    io.BackendRendererName  = PROJECT_NAME;
    io.BackendPlatformUserData = ui;

    std::filesystem::create_directories(path);
    ui->ini_path = (path / "imgui.ini").string();
    io.IniFilename = ui->ini_path.c_str();

    {
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        ui->font_image = gpu_image_create(ui->gpu, {
            .extent = {width, height},
            .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
            .usage = GpuImageUsage::texture | GpuImageUsage::transfer
        });
        gpu_copy_memory_to_image(ui->font_image.get(), as_bytes(pixels, width * height * 4), {{{{width, height}}}});
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
    ImGui::GetMainViewport()->PlatformUserData = new UiViewportData();
}

// -----------------------------------------------------------------------------

void ui_request_frame(Ui* ui)
{
    // Double-pump frames: ImGui always works based on last frame state,
    // so input needs a second frame to react against the updated state.
    ui->frames_requested = 2;
    wm_request_frame(ui->wm);
}

static
void render_viewport(Ui* ui, ImGuiViewport* vp)
{
    auto* data = get_data(vp);
    if (!data || !vp->DrawData) return;

    auto translation = from_imvec(vp->Pos);

    if (usz lists = vp->DrawData->CmdListsCount; lists != data->meshes.size()) {
        while (lists > data->meshes.size()) {
            auto mesh = scene_mesh_create();
            data->meshes.emplace_back(mesh.get());
            scene_tree_place_above(wm_window_get_tree(data->window.get()), nullptr, mesh.get());
        }
        while (lists < data->meshes.size()) {
            data->meshes.pop_back();
        }
    }

    for (auto[i, list] : to_span(vp->DrawData->CmdLists) | std::views::enumerate) {

        // Require that ImDrawVert and SceneVertex are layout-compatible
        static_assert(  sizeof(ImDrawVert)      ==   sizeof(SceneVertex));
        static_assert(offsetof(ImDrawVert, pos) == offsetof(SceneVertex, pos));
        static_assert(offsetof(ImDrawVert, col) == offsetof(SceneVertex, color));
        static_assert(offsetof(ImDrawVert, uv)  == offsetof(SceneVertex, uv));

        // Create segment list
        std::vector<SceneMeshSegment> segments(list->CmdBuffer.size());
        for (auto[j, cmd] : to_span(list->CmdBuffer) | std::views::enumerate) {

            auto& segment = segments[j];

            segment.vertex_offset = cmd.VtxOffset;
            segment.first_index = cmd.IdxOffset;
            segment.index_count = cmd.ElemCount;

            rect2f32 clip = {{cmd.ClipRect.x, cmd.ClipRect.y}, {cmd.ClipRect.z, cmd.ClipRect.w}, minmax};
            clip.origin -= translation;
            segment.clip = clip;

            auto[image, sampler, blend] = ui->textures[cmd.GetTexID()];
            segment.image = image;
            segment.sampler = sampler;
            segment.blend = blend;
        }

        // Update mesh content
        scene_mesh_update(data->meshes[i],
            {reinterpret_cast<const SceneVertex*>(list->VtxBuffer.Data), usz(list->VtxBuffer.Size)},
            to_span(list->IdxBuffer),
            segments,
            -translation);
    }

    // Update visual frame

    {
        rect2f32 rect {translation, from_imvec(vp->Size), xywh};
        if (rect != wm_window_get_frame(data->window.get())) {
            scene_input_region_set_clip(data->input_region.get(), {{}, rect.extent, xywh});
            wm_window_set_frame(data->window.get(), rect);
        }
    }

    // Apply any pending resizes for next frame

    if (auto frame = std::exchange(data->reposition, std::nullopt)) {
        vp->WorkPos  = vp->Pos  = to_imvec(frame->origin);
        vp->WorkSize = vp->Size = to_imvec(frame->extent);
        vp->PlatformRequestMove = true;
        vp->PlatformRequestResize = true;
        ui_request_frame(ui);
    }
}

void ui_frame(Ui* ui)
{
    if (!ui->frames_requested) return;
    ui->frames_requested--;

    if (ui->frames_requested) wm_request_frame(ui->wm);

    auto& io = ImGui::GetIO();
    io.DisplaySize = {};

    reset_frame_textures(ui);
    io.Fonts->SetTexID(ui_get_texture(ui, ui->font_image.get(), ui->sampler.get(),
                                      GpuBlendMode::postmultiplied));

    auto now = std::chrono::steady_clock::now();
    if (ui->last_frame != std::chrono::steady_clock::time_point{}) {
        io.DeltaTime = std::chrono::duration_cast<std::chrono::duration<f32>>(now - ui->last_frame).count();
    }
    ui->last_frame = now;

    ImGui::NewFrame();
    ui->frame_handler();
    ImGui::Render();

    if (ui->pointer) {
        seat_pointer_set_xcursor(ui->pointer, imgui_cursor_to_xcursor(ImGui::GetMouseCursor()));
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
        render_viewport(ui, vp);
    }
}

// -----------------------------------------------------------------------------

static
void handle_seat_event(Ui* ui, SeatEvent* event)
{
    switch (event->type) {

        // keyboard
        break;case SeatEventType::keyboard_enter:
            ui_handle_keyboard_enter(ui, event->keyboard.keyboard, event->keyboard.focus);
        break;case SeatEventType::keyboard_leave:
            ui_handle_keyboard_leave(ui);
        break;case SeatEventType::keyboard_key:
            ui_handle_key(ui, event->keyboard.key.code, event->keyboard.key.pressed);
        break;case SeatEventType::keyboard_modifier:
            ui_handle_mods(ui);

        // pointer
        break;case SeatEventType::pointer_enter:
            ui_handle_pointer_enter(ui, event->pointer.pointer, event->pointer.focus);
        break;case SeatEventType::pointer_leave:
            ui_handle_pointer_leave(ui);
        break;case SeatEventType::pointer_motion:
            ui_handle_motion(ui);
        break;case SeatEventType::pointer_button:
            ui_handle_button(ui, event->pointer.button.code, event->pointer.button.pressed);
        break;case SeatEventType::pointer_scroll:
            ui_handle_wheel(ui, event->pointer.scroll.delta);

        // selection
        break;case SeatEventType::selection:
            ;
    }
}

auto ui_create(Gpu* gpu, WmServer* wm, const std::filesystem::path& path) -> Ref<Ui>
{
    auto ui = ref_create<Ui>();
    ui->wm  = wm;
    ui->client = wm_connect(wm);
    ui->gpu = gpu;
    ui->sampler = gpu_sampler_create(ui->gpu, {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_LINEAR,
    });

    init(ui.get(), path);

    wm_listen(ui->client.get(), [ui = ui.get()](WmClient*, WmEvent* event) {
        UiContextGuard _{ui->context};
        switch (event->type) {
            // output
            break;case WmEventType::output_frame:
                ui_frame(ui);
            break;case WmEventType::output_layout:
                ui_handle_output_layout(ui);

            // window
            break;case WmEventType::window_reposition_requested:
                handle_reposition(ui, event->window.window, event->window.reposition.frame);
            break;case WmEventType::window_close_requested:
                handle_close(ui, event->window.window);

            // seat
            break;case WmEventType::seat_event:
                handle_seat_event(ui, event->seat.event);

            break;default:
                ;
        }
    });

    return ui;
}

void ui_set_frame_handler(Ui* ui, std::move_only_function<UiFrameFn>&& handler)
{
    ui->frame_handler = std::move(handler);
}

// -----------------------------------------------------------------------------

void ui_handle_keyboard_enter(Ui* ui, SeatKeyboard* keyboard, SeatFocus* focus)
{
    ui->keyboard = keyboard;

    ui->seats.emplace(seat_keyboard_get_seat(keyboard));

    if (auto* vp = find_viewport_for_focus(ui, focus)) {
        wm_window_raise(get_data(vp)->window.get());
    }

    auto& io = ImGui::GetIO();
    io.AddFocusEvent(true);

    ui_request_frame(ui);
}

void ui_handle_keyboard_leave(Ui* ui)
{
    ui->seats.emplace(seat_keyboard_get_seat(ui->keyboard));

    ui->keyboard = nullptr;

    auto& io = ImGui::GetIO();
    io.AddFocusEvent(false);

    ui_request_frame(ui);
}

void ui_handle_key(Ui* ui, SeatInputCode code, bool pressed)
{
    auto& io = ImGui::GetIO();

    io.AddKeyEvent(imgui_key_from_xkb_sym(seat_keyboard_get_sym(ui->keyboard, code)), pressed);
    if (pressed) io.AddInputCharactersUTF8(seat_keyboard_get_utf8(ui->keyboard, code).c_str());

    ui_request_frame(ui);
}

void ui_handle_mods(Ui* ui)
{
    auto& io = ImGui::GetIO();

    auto mods = seat_keyboard_get_modifiers(ui->keyboard);

    io.AddKeyEvent(ImGuiMod_Shift, mods.contains(SeatModifier::shift));
    io.AddKeyEvent(ImGuiMod_Ctrl,  mods.contains(SeatModifier::ctrl));
    io.AddKeyEvent(ImGuiMod_Alt,   mods.contains(SeatModifier::alt));
    io.AddKeyEvent(ImGuiMod_Super, mods.contains(SeatModifier::super));

    ui_request_frame(ui);
}

void ui_handle_motion(Ui* ui)
{
    auto& io = ImGui::GetIO();

    auto pos = seat_pointer_get_position(ui->pointer);
    io.AddMousePosEvent(pos.x, pos.y);

    ui_request_frame(ui);
}

void ui_handle_button(Ui* ui, SeatInputCode code, bool pressed)
{
    auto& io = ImGui::GetIO();

    bool handled = false;
    switch (code) {
        break;case BTN_LEFT:   io.AddMouseButtonEvent(ImGuiMouseButton_Left,   pressed); handled = true;
        break;case BTN_RIGHT:  io.AddMouseButtonEvent(ImGuiMouseButton_Right,  pressed); handled = true;
        break;case BTN_MIDDLE: io.AddMouseButtonEvent(ImGuiMouseButton_Middle, pressed); handled = true;
    }

    if (!handled) return;

    ui_request_frame(ui);
}

void ui_handle_wheel(Ui* ui, vec2f32 delta)
{
    auto& io = ImGui::GetIO();

    io.AddMouseWheelEvent(delta.x, -delta.y);
    ui_request_frame(ui);
}

void ui_handle_pointer_enter(Ui* ui, SeatPointer* pointer, SeatFocus* focus)
{
    ui->pointer = pointer;

    auto& io = ImGui::GetIO();

    if (focus) {
        io.AddMouseViewportEvent(find_viewport_for_focus(ui, focus)->ID);
    }

    auto pos = seat_pointer_get_position(pointer);
    io.AddMousePosEvent(pos.x, pos.y);

    ui_request_frame(ui);
}

void ui_handle_pointer_leave(Ui* ui)
{
    auto& io = ImGui::GetIO();

    io.AddMouseViewportEvent(0);
    io.AddMousePosEvent(-FLT_MAX,-FLT_MAX);

    for (auto code : seat_pointer_get_pressed(ui->pointer)) {
        ui_handle_button(ui, code, false);
    }

    ui->pointer = nullptr;

    ui_request_frame(ui);
}

void ui_handle_output_layout(Ui* ui)
{
    auto& platform_io = ImGui::GetPlatformIO();

    platform_io.Monitors.clear();
    for (auto* output : wm_list_outputs(ui->wm)) {
        rect2f32 rect = wm_output_get_viewport(output);
        if (rect.extent.x == 0 || rect.extent.y == 0) continue;

        ImGuiPlatformMonitor monitor = {};
        monitor.WorkPos  = monitor.MainPos  = to_imvec(rect.origin);
        monitor.WorkSize = monitor.MainSize = to_imvec(rect.extent);
        platform_io.Monitors.push_back(monitor);
    }

    ui_request_frame(ui);
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

    debug_unreachable();
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
