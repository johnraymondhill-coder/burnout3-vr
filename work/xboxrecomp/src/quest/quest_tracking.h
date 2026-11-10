#ifndef QUEST_TRACKING_H
#define QUEST_TRACKING_H
#include <stdint.h>

/* OpenXR coordinates: metres, +X right, +Y up, -Z forward. Quaternion xyzw.
 * Copies carry the same predicted display time for both eyes. */
struct qvr_pose { float orientation[4], position[3]; };
struct qvr_eye { struct qvr_pose pose; float left, right, up, down; };
struct qvr_tracking {
    struct qvr_eye eye[2];
    struct qvr_pose origin;
    int64_t display_time;
    uint64_t generation;
    int valid;
};
int qvr_tracking_copy(struct qvr_tracking *out);
#endif
