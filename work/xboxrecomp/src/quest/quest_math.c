/* Local Quest camera maths, MIT. No guest addresses or game material. */
#include "quest_math.h"
#include <math.h>
#include <string.h>

static int normalize(float q[4]) {
    float n=sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
    if(!isfinite(n)||n<.0001f)return 0;
    for(unsigned i=0;i<4;++i)q[i]/=n;
    return 1;
}
static void rotate(float out[3],const float q[4],const float v[3]) {
    float t[3]={2*(q[1]*v[2]-q[2]*v[1]),2*(q[2]*v[0]-q[0]*v[2]),2*(q[0]*v[1]-q[1]*v[0])};
    out[0]=v[0]+q[3]*t[0]+q[1]*t[2]-q[2]*t[1];
    out[1]=v[1]+q[3]*t[1]+q[2]*t[0]-q[0]*t[2];
    out[2]=v[2]+q[3]*t[2]+q[0]*t[1]-q[1]*t[0];
}
static void multiply_q(float out[4],const float a[4],const float b[4]) {
    float t[4]={a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],
        a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],
        a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],
        a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]};
    memcpy(out,t,sizeof t);
}
int qvr_camera_compose(float out[16],const float base[16],const struct qvr_pose *origin,
    const struct qvr_pose *eye,float units_per_metre) {
    float inv[4],q[4],m[16],dp[3],p[3];
    if(!isfinite(units_per_metre)||units_per_metre<=0)return 0;
    memcpy(inv,origin->orientation,sizeof inv);memcpy(q,eye->orientation,sizeof q);
    if(!normalize(inv)||!normalize(q))return 0;
    inv[0]=-inv[0];inv[1]=-inv[1];inv[2]=-inv[2];multiply_q(q,inv,q);
    memcpy(m,base,sizeof m);
    /* C=diag(-1,+1,-1): XR right/up/back to RW left/up/forward.
     * New RW basis is base * C * rotation * C. */
    for(unsigned axis=0;axis<3;++axis){
        float v[3]={0,0,0},r[3];v[axis]=axis==1?1:-1;rotate(r,q,v);
        r[0]=-r[0];r[2]=-r[2];
        for(unsigned k=0;k<3;++k)m[axis*4+k]=base[k]*r[0]+base[4+k]*r[1]+base[8+k]*r[2];
    }
    for(unsigned k=0;k<3;++k)dp[k]=eye->position[k]-origin->position[k];
    rotate(p,inv,dp);p[0]=-p[0];p[2]=-p[2];
    for(unsigned k=0;k<3;++k)m[12+k]=base[12+k]+units_per_metre*(base[k]*p[0]+base[4+k]*p[1]+base[8+k]*p[2]);
    for(unsigned axis=0;axis<4;++axis)for(unsigned k=0;k<3;++k)if(!isfinite(m[4*axis+k]))return 0;
    memcpy(out,m,sizeof m);return 1;
}
static void affine(float out[16],const float in[16]) {
    memcpy(out,in,16*sizeof(float));out[3]=out[7]=out[11]=0;out[15]=1;
}
static void multiply_m(float out[16],const float a[16],const float b[16]) {
    float t[16];
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j){
        t[i*4+j]=0;for(unsigned k=0;k<4;++k)t[i*4+j]+=a[i*4+k]*b[k*4+j];
    }
    memcpy(out,t,sizeof t);
}
static int inverse(float out[16],const float in[16]) {
    float a=in[0],b=in[1],c=in[2],d=in[4],e=in[5],f=in[6],g=in[8],h=in[9],i=in[10];
    float det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if(!isfinite(det)||fabsf(det)<1e-8f)return 0;
    det=1/det;memset(out,0,16*sizeof(float));out[15]=1;
    out[0]=(e*i-f*h)*det;out[1]=(c*h-b*i)*det;out[2]=(b*f-c*e)*det;
    out[4]=(f*g-d*i)*det;out[5]=(a*i-c*g)*det;out[6]=(c*d-a*f)*det;
    out[8]=(d*h-e*g)*det;out[9]=(b*g-a*h)*det;out[10]=(a*e-b*d)*det;
    for(unsigned k=0;k<3;++k)out[12+k]=-(in[12]*out[k]+in[13]*out[4+k]+in[14]*out[8+k]);
    return 1;
}
int qvr_view_adjust(float out[16],const float view[16],const float base[16],const float eye[16],float x_ratio,float y_ratio) {
    float m[16],b[16],bi[16],t[16],v[16];
    affine(m,base);affine(b,eye);if(!inverse(bi,b))return 0;
    multiply_m(t,m,view);multiply_m(v,bi,t);
    for(unsigned i=0;i<4;++i){v[4*i]*=x_ratio;v[4*i+1]*=y_ratio;}
    memcpy(out,v,sizeof v);return 1;
}
int qvr_symmetric_fov(struct qvr_eye *eye,float *window_x,float *window_y) {
    float x=fmaxf(fabsf(eye->left),fabsf(eye->right));
    float y=fmaxf(fabsf(eye->down),fabsf(eye->up));
    if(!isfinite(x)||!isfinite(y)||x<.1f||y<.1f||x>1.5f||y>1.5f)return 0;
    eye->left=-x;eye->right=x;eye->down=-y;eye->up=y;
    *window_x=tanf(x);*window_y=tanf(y);return 1;
}
void qvr_hud_transform(const struct qvr_eye *eye,float size,float *sx,float *sy,float *oy){
    *sx=*sy=1;*oy=0;
    if(!eye||!isfinite(size)||size>=.999f)return;
    float tx=tanf(fmaxf(fabsf(eye->left),fabsf(eye->right)));
    float ty=tanf(fmaxf(fabsf(eye->up),fabsf(eye->down)));
    if(!isfinite(tx)||!isfinite(ty)||tx<.1f||ty<.1f)return;
    float half=tanf(.436332313f)*fmaxf(.5f,fminf(1.5f,size/.62f));
    *sx=fminf(1,half/tx);*sy=fminf(1,half*.75f/ty);
    *oy=-tanf(.034906585f)/ty; /* two degrees below straight ahead */
}
void qvr_yaw_origin(struct qvr_pose *out,const struct qvr_tracking *tracking) {
    *out=tracking->eye[0].pose;
    for(unsigned i=0;i<3;++i)out->position[i]=(tracking->eye[0].pose.position[i]+tracking->eye[1].pose.position[i])*.5f;
    float dot=0;
    for(unsigned i=0;i<4;++i)dot+=tracking->eye[0].pose.orientation[i]*tracking->eye[1].pose.orientation[i];
    for(unsigned i=0;i<4;++i)out->orientation[i]+=tracking->eye[1].pose.orientation[i]*(dot<0?-1:1);
    if(!normalize(out->orientation))memcpy(out->orientation,tracking->eye[0].pose.orientation,sizeof out->orientation);
    const float *q=out->orientation;float yaw=atan2f(2*(q[3]*q[1]+q[0]*q[2]),1-2*(q[1]*q[1]+q[0]*q[0]));
    out->orientation[0]=out->orientation[2]=0;out->orientation[1]=sinf(yaw*.5f);out->orientation[3]=cosf(yaw*.5f);
}
