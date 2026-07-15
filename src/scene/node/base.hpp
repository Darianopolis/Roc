#include "../internal.hpp"

#define SCENE_NOISY_NODES 0

#if SCENE_NOISY_NODES
#include <core/log.hpp>
#define NODE_LOG(...) log_error(__VA_ARGS__)
#else
#define NODE_LOG(...)
#endif
