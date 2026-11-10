#ifndef QUEST_MATH_H
#define QUEST_MATH_H
#include "quest_tracking.h"
/* RenderWare camera axes are left/up/forward. Preserve the four flag words
 * in its 4x4 RwMatrix storage. Inputs and outputs may alias. */
int qvr_camera_compose(float out[16], const float base[16],
    const struct qvr_pose *origin, const struct qvr_pose *eye, float units_per_metre);
int qvr_view_adjust(float out[16], const float view[16], const float base[16],
    const float eye[16], float x_ratio, float y_ratio);
int qvr_symmetric_fov(struct qvr_eye *eye, float *window_x, float *window_y);
/* HUD covers 50 degrees horizontally at size .62, independent of render FOV. */
void qvr_hud_transform(const struct qvr_eye *eye,float size,float *sx,float *sy,float *oy);
void qvr_yaw_origin(struct qvr_pose *out, const struct qvr_tracking *tracking);
#endif
