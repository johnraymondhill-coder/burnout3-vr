#ifndef QUEST_GESTURES_H
#define QUEST_GESTURES_H
#include <stdint.h>
#define QVR_GESTURE_START 1u
#define QVR_GESTURE_RECENTER 2u
struct qvr_menu_gesture { uint64_t down_at,start_until; int down,long_press; };
/* Tap left Menu -> a 100 ms Start pulse on release. Hold 650 ms -> one
 * recenter, without pausing. Losing focus cancels an unfinished gesture. */
unsigned qvr_menu_tick(struct qvr_menu_gesture *state,uint64_t now,int focused,int pressed);
#endif
