#pragma once

#include "scene/scene.hpp"

struct way_server;

auto way_create(exec_context*, gpu_context*, scene_context*) -> ref<way_server>;
