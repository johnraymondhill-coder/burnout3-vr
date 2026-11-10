#ifndef QUEST_FOVEATION_H
#define QUEST_FOVEATION_H
#include "d3d/nv2a_vk_core.h"
/* Immutable map, ready before any command buffer records a rendering use.
 * Only the Vulkan executor calls this. NULL means ordinary full-rate drawing. */
const struct nvk_image *qvr_foveation_map(uint32_t width,uint32_t height);
#endif
