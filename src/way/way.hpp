#pragma once

#include "scene/scene.hpp"

struct way_server;

auto way_create(core_event_loop*, gpu_context*, scene_context*) -> ref<way_server>;
