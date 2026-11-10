#include "quest/quest_math.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void close_to(float a,float b){assert(fabsf(a-b)<.00002f);}
int main(void){
    const float base[16]={1,0,0,3,0,1,0,5,0,0,1,7,100,200,300,9};
    struct qvr_pose origin={{0,0,0,1},{0,1.6f,0}},eye=origin;
    float out[16];
    assert(qvr_camera_compose(out,base,&origin,&eye,1));
    for(unsigned i=0;i<16;++i)close_to(out[i],base[i]);
    eye.position[0]=-.032f;assert(qvr_camera_compose(out,base,&origin,&eye,1));
    close_to(out[12],100.032f); /* XR left eye goes along RW camera's left axis. */
    eye.position[0]=.032f;assert(qvr_camera_compose(out,base,&origin,&eye,1));
    close_to(out[12],99.968f);
    eye=origin;eye.position[2]=-.25f;assert(qvr_camera_compose(out,base,&origin,&eye,2));
    close_to(out[14],300.5f); /* Lean forward, two world units per metre. */
    eye=origin;eye.orientation[1]=sinf(.7853981634f);eye.orientation[3]=cosf(.7853981634f);
    assert(qvr_camera_compose(out,base,&origin,&eye,1));close_to(out[8],1);close_to(out[10],0);
    close_to(out[0],0);close_to(out[2],-1); /* Left yaw turns toward RW left, no mirrored yaw. */
    origin=eye;assert(qvr_camera_compose(out,base,&origin,&eye,1));
    for(unsigned i=0;i<16;++i)close_to(out[i],base[i]);
    eye.orientation[3]=NAN;assert(!qvr_camera_compose(out,base,&origin,&eye,1));
    struct qvr_eye fov={.left=-.8f,.right=.9f,.up=.85f,.down=-.7f};float wx,wy;
    assert(qvr_symmetric_fov(&fov,&wx,&wy));close_to(fov.left,-.9f);close_to(fov.down,-.85f);
    close_to(wx,tanf(.9f));close_to(wy,tanf(.85f));
    float sx,sy,oy;
    qvr_hud_transform(&fov,.62f,&sx,&sy,&oy);
    close_to(sx*wx,tanf(.436332313f));close_to(sy*wy,tanf(.436332313f)*.75f);
    close_to(oy*wy,-tanf(.034906585f));
    assert(atanf((sy+oy)*wy)<.35f&&atanf((sy-oy)*wy)<.38f);
    /* A wider render FOV must not move the HUD farther out in vision. */
    struct qvr_eye wide=fov;wide.left=-1.15f;wide.right=1.15f;wide.up=1.05f;wide.down=-1.05f;
    qvr_hud_transform(&wide,.62f,&sx,&sy,&oy);
    close_to(sx*tanf(1.15f),tanf(.436332313f));close_to(sy*tanf(1.05f),tanf(.436332313f)*.75f);
    qvr_hud_transform(&fov,1,&sx,&sy,&oy);close_to(sx,1);close_to(sy,1);close_to(oy,0);
    qvr_hud_transform(NULL,.62f,&sx,&sy,&oy);close_to(sx,1);close_to(sy,1);
    const float view[16]={-1,0,0,0,0,1,0,0,0,0,1,0,100,-200,-300,1};
    assert(qvr_view_adjust(out,view,base,base,1,1));
    for(unsigned i=0;i<16;++i)close_to(out[i],view[i]);
    float shifted[16];memcpy(shifted,base,sizeof base);shifted[12]+=1;
    assert(qvr_view_adjust(out,view,base,shifted,1,1));close_to(out[12],101);
    struct qvr_tracking tracking={0};
    tracking.eye[0].pose.orientation[1]=sinf(.035f);tracking.eye[0].pose.orientation[3]=cosf(.035f);
    tracking.eye[1].pose.orientation[1]=-sinf(.035f);tracking.eye[1].pose.orientation[3]=cosf(.035f);
    tracking.eye[0].pose.position[0]=-.032f;tracking.eye[1].pose.position[0]=.032f;
    qvr_yaw_origin(&origin,&tracking);close_to(origin.orientation[1],0);close_to(origin.orientation[3],1);close_to(origin.position[0],0);
    puts("PASS: camera transforms, angular HUD bounds/FOV independence, recenter, invalid pose");
    return 0;
}
