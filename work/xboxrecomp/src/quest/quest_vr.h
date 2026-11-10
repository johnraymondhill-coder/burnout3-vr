#ifndef BO3_QUEST_VR_H
#define BO3_QUEST_VR_H
#include <stddef.h>
#include <stdint.h>
#include "d3d/nv2a_vk_core.h"
#include "quest_tracking.h"
/* Vulkan/OpenXR ownership stays on the SDL presentation thread. */
int qvr_create(struct nvk_core *core, char *error, size_t error_size);
int qvr_active(void);
int qvr_present(const struct nvk_image *source, uint32_t width, uint32_t height);
void qvr_destroy(void);
void qvr_set_driving(int driving);
#endif
