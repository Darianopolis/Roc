#include "shell.hpp"

#include "renderdoc_app.h"

struct ShellRenderDocPlugin : ShellPlugin
{
    Shell* shell;

    bool loaded = false;
    RENDERDOC_API_1_7_0* renderdoc;

    u32 capture = 0;

    Ref<WmHotkey> hotkey;
};

static
void load_renderdoc(ShellRenderDocPlugin* plugin)
{
    if (plugin->loaded) return;
    plugin->loaded = true;

    log_debug("Loading RenderDoc API");

    void* mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
    if (!mod) {
        log_error("Failed to load shared object: [librenderdoc.so]");
        return;
    }

    auto RENDERDOC_GetAPI = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(mod, "RENDERDOC_GetAPI"));
    if (!RENDERDOC_GetAPI) {
        log_error("Failed to load symbol: [RENDERDOC_GetAPI]");
        return;
    }

    RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_7_0, (void**)&plugin->renderdoc);

    int major, minor, patch;
    plugin->renderdoc->GetAPIVersion(&major, &minor, &patch);

    log_debug("RenderDoc API loaded: {}.{}.{}", major, minor, patch);
}

static
void renderdoc_capture(ShellRenderDocPlugin* plugin)
{
    load_renderdoc(plugin);

    auto* gpu = plugin->shell->gpu.get();
    auto* wm = plugin->shell->wm.get();

    if (!plugin->renderdoc) {
        log_warn("RenderDoc isn't attached, can't capture!");
        return;
    }

    plugin->renderdoc->StartFrameCapture(nullptr, nullptr);
    plugin->renderdoc->SetCaptureTitle(std::format("Shell capture {}", ++plugin->capture).c_str());
    auto* output = wm_find_output_at(wm, seat_pointer_get_position(seat_get_pointer(wm_get_seat(wm)))).output;
    if (output) {
        auto viewport = wm_output_get_viewport(output);
        auto texture = gpu_image_create(gpu, {
            .extent = vec_cast<u32>(viewport.extent),
            .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
            .usage = GpuImageUsage::storage,
        });
        scene_render(wm_get_scene_renderer(wm), {
            .root = wm_get_scene(wm),
            .target = texture.get(),
            .viewport = viewport,
        });
        gpu_flush(gpu);
    }
    plugin->renderdoc->EndFrameCapture(nullptr, nullptr);
}

void shell_init_renderdoc(Shell* shell)
{
    auto plugin = ref_create<ShellRenderDocPlugin>();
    shell->plugins.emplace_back(plugin);
    plugin->shell = shell;

    plugin->hotkey = wm_bind_hotkey(shell->wm.get(), shell->main_mod, KEY_J, [plugin = plugin.get()](auto...) {
        renderdoc_capture(plugin);
    });
}
