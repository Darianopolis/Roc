#pragma once

#include "wm.hpp"

#include "core/types.hpp"
#include "scene/scene.hpp"
#include "way/way.hpp"
#include "io/io.hpp"

enum class WmInteractionMode
{
    none,
    move,
    size,
    zone,
};

struct RocLauncher;

struct WmOutput
{
    IoOutput* output;
    rect2f32  viewport;
};

struct WindowManager
{
    ExecContext* exec;
    Gpu*         gpu;
    Scene*       scene;

    SeatModifier modifier;

    WmInteractionMode mode;

    Ref<RocLauncher> launcher;

    EnumMap<WmLayer, Ref<SceneTree>> layers;

    struct {
        IoContext*            context;
        Ref<GpuImagePool>     pool;
        std::vector<WmOutput> outputs;
    } io;

    struct {
        Ref<Seat>         seat;
        Ref<SeatKeyboard> keyboard;
        Ref<SeatPointer>  pointer;
        vec2f32           position;
        Ref<SceneTexture> cursor;
    } seat;

    // struct {
    //     Ref<SceneEventFilter> filter;
    // } hotkeys;

    // struct {
    //     Ref<SceneEventFilter> filter;
    //     ScenePointer* pointer;

    //     Weak<SceneWindow> window;
    //     vec2f32  grab;
    //     rect2f32 frame;
    //     vec2f32  relative;
    // } movesize;

    // struct {
    //     Ref<SceneEventFilter> filter;
    //     ScenePointer* pointer;
    //     Ref<SceneTexture> texture;

    //     Weak<SceneWindow> window;
    //     aabb2f64 initial_zone;
    //     aabb2f64 final_zone;
    //     bool     selecting = false;
    // } zone;
};

void wm_init_io(      WindowManager*);
void wm_init_seat(    WindowManager*);
void wm_init_hotkeys( WindowManager*);
void wm_init_movesize(WindowManager*);
void wm_init_zone(    WindowManager*);

void wm_seat_handle_io_event(WindowManager*, IoEvent*);