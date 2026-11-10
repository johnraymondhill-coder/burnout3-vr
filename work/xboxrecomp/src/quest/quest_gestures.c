#include "quest_gestures.h"
#include <string.h>
unsigned qvr_menu_tick(struct qvr_menu_gesture *state,uint64_t now,int focused,int pressed){
    unsigned result=0;
    if(!focused){memset(state,0,sizeof *state);return 0;}
    if(pressed&&!state->down){state->down_at=now;state->long_press=0;state->down=1;}
    if(pressed&&!state->long_press&&now-state->down_at>=650){
        state->long_press=1;state->start_until=0;result|=QVR_GESTURE_RECENTER;
    }
    if(!pressed&&state->down){
        if(!state->long_press)state->start_until=now+100;
        state->down=0;state->long_press=0;
    }
    if(now<state->start_until)result|=QVR_GESTURE_START;
    return result;
}
