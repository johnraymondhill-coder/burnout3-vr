#ifndef QUEST_STEREO_H
#define QUEST_STEREO_H
#include "quest_tracking.h"
#include "d3d/nv2a_vk_core.h"
#define QVR_EVENT_BASE 0x80000000u
#define QVR_BEGIN_LEFT 0u
#define QVR_END_LEFT 1u
#define QVR_BEGIN_RIGHT 2u
#define QVR_END_RIGHT 3u
#define QVR_HUD_BEGIN 0xC0000000u
#define QVR_HUD_END   0xC0000001u
#define QVR_WORLD_BEGIN 0xC0000002u
#define QVR_WORLD_END   0xC0000003u
uint32_t qvr_render_queue(const struct qvr_tracking *tracking);
int qvr_render_lookup(uint32_t id, struct qvr_tracking *out);
void qvr_publish_frame(VkCommandBuffer command, const struct nvk_image *slot,
    struct nvk_image *left, const struct qvr_tracking *tracking);
#endif
