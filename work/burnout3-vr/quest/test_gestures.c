#include "quest/quest_gestures.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    struct qvr_menu_gesture state={0};
    assert(qvr_menu_tick(&state,0,1,1)==0);
    assert(qvr_menu_tick(&state,649,1,1)==0);
    assert(qvr_menu_tick(&state,650,1,1)==QVR_GESTURE_RECENTER);
    assert(qvr_menu_tick(&state,900,1,1)==0);
    assert(qvr_menu_tick(&state,1000,1,0)==0);
    assert(qvr_menu_tick(&state,1100,1,1)==0);
    assert(qvr_menu_tick(&state,1150,1,0)==QVR_GESTURE_START);
    assert(qvr_menu_tick(&state,1230,1,0)==QVR_GESTURE_START);
    assert(qvr_menu_tick(&state,1250,1,0)==0);
    assert(qvr_menu_tick(&state,1300,1,1)==0);
    assert(qvr_menu_tick(&state,1350,0,1)==0);
    assert(qvr_menu_tick(&state,1400,1,0)==0);
    assert(qvr_menu_tick(&state,1500,1,1)==0);
    assert(qvr_menu_tick(&state,2200,0,0)==0);
    assert(qvr_menu_tick(&state,2250,1,0)==0);
    puts("PASS: tap Start pulse, held Menu recenter once, no pause after hold, focus cancellation");
}
