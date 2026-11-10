/* Local Quest OpenXR transport, MIT. No game assets or generated code.
 * Initial integration: virtual-screen output and Touch-to-Xbox input.
 * Stereo world rendering is deliberately a separate, unfinished stage.
 */
#ifdef __ANDROID__
#define XR_USE_PLATFORM_ANDROID
#include <jni.h>
#endif
#define XR_USE_GRAPHICS_API_VULKAN
#include <SDL.h>
#include <SDL_system.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quest_vr.h"
#include "quest_stereo.h"
#include "quest_gestures.h"
#include "video/fb_present_vulkan.h"
#include "input/input_backend.h"
#include "d3d/vk_dyn.h"

void xr_vk_adopt_instance(VkInstance instance);
void xr_vk_adopt_device(VkDevice device, int aliases12);

static struct {
    XrInstance instance;
    XrSystemId system;
    XrSession session;
    XrSpace local, view;
    XrSessionState state;
    XrSwapchain swapchain;
    XrSwapchainImageVulkanKHR *images;
    uint32_t image_count, width, height, layers;
    VkFormat format;
    struct nvk_image rgba_copy;
    VkCommandPool pool;
    VkCommandBuffer command;
    VkFence fence;
    const struct nvk_core *gpu;
    int active, running, quit, recenter, panel_valid;
    XrPosef panel;
    XrActionSet actions;
    XrPath hands[2];
    XrAction primary, secondary, menu, trigger, grip, stick, click, haptic;
    XBOX_INPUT_STATE pad;
    XBOX_VIBRATION rumble;
    uint64_t rumble_at, haptic_at;
    WORD haptic_level[2];
    int input_armed;
    int driving;
    unsigned frames;
    struct qvr_tracking tracking;
    int origin_valid;
    XrTime space_change_time;
    struct qvr_menu_gesture menu_gesture;
    struct nvk_buffer capture[2];
    unsigned stereo_frames,capture_mask;
    int capture_done;
    struct { uint32_t id; struct qvr_tracking tracking; } render[64];
    uint32_t render_id;
    struct {
        const struct nvk_image *key;
        struct nvk_image left;
        struct qvr_tracking tracking;
        int stereo;
    } slot[3];
} Q;
static pthread_mutex_t input_lock=PTHREAD_MUTEX_INITIALIZER;

static int check(XrResult r,const char *site) {
    if(XR_FAILED(r)){fprintf(stderr,"[QUEST-VR] %s failed: %d\n",site,r);return 0;}return 1;
}
static void performance_request(void){
    PFN_xrPerfSettingsSetPerformanceLevelEXT performance=NULL;
    if(XR_SUCCEEDED(xrGetInstanceProcAddr(Q.instance,"xrPerfSettingsSetPerformanceLevelEXT",(PFN_xrVoidFunction*)&performance))&&performance){
        XrResult cpu=performance(Q.session,XR_PERF_SETTINGS_DOMAIN_CPU_EXT,XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
        XrResult gpu=performance(Q.session,XR_PERF_SETTINGS_DOMAIN_GPU_EXT,XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
        fprintf(stderr,"[QUEST-VR] sustained performance request CPU=%d GPU=%d\n",cpu,gpu);
    }
    PFN_xrEnumerateDisplayRefreshRatesFB rates=NULL;PFN_xrRequestDisplayRefreshRateFB request=NULL;
    xrGetInstanceProcAddr(Q.instance,"xrEnumerateDisplayRefreshRatesFB",(PFN_xrVoidFunction*)&rates);
    xrGetInstanceProcAddr(Q.instance,"xrRequestDisplayRefreshRateFB",(PFN_xrVoidFunction*)&request);
    if(rates&&request){
        uint32_t count=0;
        if(XR_SUCCEEDED(rates(Q.session,0,&count,NULL))&&count){
            float *values=calloc(count,sizeof *values);
            if(values&&XR_SUCCEEDED(rates(Q.session,count,&count,values))){
#ifdef _WIN32
                const float preferred[]={120,90,72};
#else
                const float preferred[]={72};
#endif
                int chosen=0;
                for(unsigned p=0;p<sizeof preferred/sizeof preferred[0]&&!chosen;++p)
                    for(uint32_t i=0;i<count;++i)if(fabsf(values[i]-preferred[p])<.1f){
                        fprintf(stderr,"[QUEST-VR] display %.0f Hz request=%d\n",preferred[p],request(Q.session,preferred[p]));chosen=1;break;
                    }
            }
            free(values);
        }
    }
}
static XrPath path(const char *text) {
    XrPath p=XR_NULL_PATH;xrStringToPath(Q.instance,text,&p);return p;
}
static XrAction action(const char *name,XrActionType type) {
    XrAction a=XR_NULL_HANDLE;XrActionCreateInfo ci={XR_TYPE_ACTION_CREATE_INFO};
    snprintf(ci.actionName,sizeof ci.actionName,"%s",name);
    snprintf(ci.localizedActionName,sizeof ci.localizedActionName,"%s",name);
    ci.actionType=type;ci.countSubactionPaths=2;ci.subactionPaths=Q.hands;
    if(!check(xrCreateAction(Q.actions,&ci,&a),"xrCreateAction"))return XR_NULL_HANDLE;
    return a;
}
static int make_actions(void) {
    Q.hands[0]=path("/user/hand/left");Q.hands[1]=path("/user/hand/right");
    XrActionSetCreateInfo ci={XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(ci.actionSetName,"burnout");strcpy(ci.localizedActionSetName,"Burnout controls");
    if(!check(xrCreateActionSet(Q.instance,&ci,&Q.actions),"xrCreateActionSet"))return 0;
    Q.primary=action("primary",XR_ACTION_TYPE_BOOLEAN_INPUT);
    Q.secondary=action("secondary",XR_ACTION_TYPE_BOOLEAN_INPUT);
    Q.menu=action("pause",XR_ACTION_TYPE_BOOLEAN_INPUT);
    Q.trigger=action("trigger",XR_ACTION_TYPE_FLOAT_INPUT);
    Q.grip=action("grip",XR_ACTION_TYPE_FLOAT_INPUT);
    Q.stick=action("stick",XR_ACTION_TYPE_VECTOR2F_INPUT);
    Q.click=action("stick_click",XR_ACTION_TYPE_BOOLEAN_INPUT);
    Q.haptic=action("rumble",XR_ACTION_TYPE_VIBRATION_OUTPUT);
    if(!Q.primary||!Q.secondary||!Q.menu||!Q.trigger||!Q.grip||!Q.stick||!Q.click||!Q.haptic)return 0;
    XrActionSuggestedBinding b[20];unsigned n=0;
#define BIND(a,p) b[n++]=(XrActionSuggestedBinding){Q.a,path(p)}
    BIND(primary,"/user/hand/left/input/x/click");BIND(primary,"/user/hand/right/input/a/click");
    BIND(secondary,"/user/hand/left/input/y/click");BIND(secondary,"/user/hand/right/input/b/click");
    BIND(menu,"/user/hand/left/input/menu/click");
    BIND(trigger,"/user/hand/left/input/trigger/value");BIND(trigger,"/user/hand/right/input/trigger/value");
    BIND(grip,"/user/hand/left/input/squeeze/value");BIND(grip,"/user/hand/right/input/squeeze/value");
    BIND(stick,"/user/hand/left/input/thumbstick");BIND(stick,"/user/hand/right/input/thumbstick");
    BIND(click,"/user/hand/left/input/thumbstick/click");BIND(click,"/user/hand/right/input/thumbstick/click");
    BIND(haptic,"/user/hand/left/output/haptic");BIND(haptic,"/user/hand/right/output/haptic");
#undef BIND
    XrInteractionProfileSuggestedBinding bi={XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    bi.interactionProfile=path("/interaction_profiles/oculus/touch_controller");bi.countSuggestedBindings=n;bi.suggestedBindings=b;
    if(!check(xrSuggestInteractionProfileBindings(Q.instance,&bi),"xrSuggestInteractionProfileBindings"))return 0;
    XrSessionActionSetsAttachInfo ai={XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};ai.countActionSets=1;ai.actionSets=&Q.actions;
    return check(xrAttachSessionActionSets(Q.session,&ai),"xrAttachSessionActionSets");
}
static int pressed(XrAction a,unsigned hand) {
    XrActionStateGetInfo gi={XR_TYPE_ACTION_STATE_GET_INFO};gi.action=a;gi.subactionPath=Q.hands[hand];
    XrActionStateBoolean s={XR_TYPE_ACTION_STATE_BOOLEAN};
    return XR_SUCCEEDED(xrGetActionStateBoolean(Q.session,&gi,&s))&&s.isActive&&s.currentState;
}
static float analog(XrAction a,unsigned hand) {
    XrActionStateGetInfo gi={XR_TYPE_ACTION_STATE_GET_INFO};gi.action=a;gi.subactionPath=Q.hands[hand];
    XrActionStateFloat s={XR_TYPE_ACTION_STATE_FLOAT};
    float v=XR_SUCCEEDED(xrGetActionStateFloat(Q.session,&gi,&s))&&s.isActive?fmaxf(0,fminf(1,s.currentState)):0;
    return v<.08f?0:v;
}
static XrVector2f stick(unsigned hand) {
    XrActionStateGetInfo gi={XR_TYPE_ACTION_STATE_GET_INFO};gi.action=Q.stick;gi.subactionPath=Q.hands[hand];
    XrActionStateVector2f s={XR_TYPE_ACTION_STATE_VECTOR2F};
    if(XR_SUCCEEDED(xrGetActionStateVector2f(Q.session,&gi,&s))&&s.isActive){
        float length=hypotf(s.currentState.x,s.currentState.y);
        if(length>.18f){float scale=fminf(1,(length-.18f)/.82f)/length;
            return (XrVector2f){s.currentState.x*scale,s.currentState.y*scale};}
    }
    return (XrVector2f){0,0};
}
static DWORD pad_get(DWORD port,XBOX_INPUT_STATE *out) {
    if(port||!out)return ERROR_DEVICE_NOT_CONNECTED;
    pthread_mutex_lock(&input_lock);*out=Q.pad;pthread_mutex_unlock(&input_lock);return ERROR_SUCCESS;
}
static DWORD pad_rumble(DWORD port,const XBOX_VIBRATION *v) {
    if(port||!v)return ERROR_DEVICE_NOT_CONNECTED;
    pthread_mutex_lock(&input_lock);Q.rumble=*v;Q.rumble_at=SDL_GetTicks64();pthread_mutex_unlock(&input_lock);return ERROR_SUCCESS;
}
static uint64_t now_ms(void){return SDL_GetTicks64();}
static const xbox_input_backend input_backend={NULL,pad_get,pad_rumble,NULL,now_ms,NULL};
void qvr_set_driving(int driving){pthread_mutex_lock(&input_lock);Q.driving=driving;pthread_mutex_unlock(&input_lock);}
static void sync_inputs(void) {
    XBOX_INPUT_STATE out={0};int driving;
    pthread_mutex_lock(&input_lock);driving=Q.driving;out.dwPacketNumber=Q.pad.dwPacketNumber+1;pthread_mutex_unlock(&input_lock);
    if(Q.state==XR_SESSION_STATE_FOCUSED){
        XrActiveActionSet active={Q.actions,XR_NULL_PATH};XrActionsSyncInfo si={XR_TYPE_ACTIONS_SYNC_INFO};
        si.countActiveActionSets=1;si.activeActionSets=&active;
        if(XR_SUCCEEDED(xrSyncActions(Q.session,&si))){
            XBOX_GAMEPAD *p=&out.Gamepad;XrVector2f l=stick(0),r=stick(1);
            p->bAnalogButtons[XBOX_BUTTON_A]=pressed(Q.primary,1)?255:0;
            p->bAnalogButtons[XBOX_BUTTON_B]=pressed(Q.secondary,1)?255:0;
            p->bAnalogButtons[XBOX_BUTTON_X]=(pressed(Q.primary,0)||analog(Q.grip,0)>.65f)?255:0;
            p->bAnalogButtons[XBOX_BUTTON_Y]=pressed(Q.secondary,0)?255:0;
            p->bAnalogButtons[XBOX_BUTTON_LTRIGGER]=(unsigned char)(analog(Q.trigger,0)*255);
            p->bAnalogButtons[XBOX_BUTTON_RTRIGGER]=(unsigned char)(analog(Q.trigger,1)*255);
            unsigned menu=qvr_menu_tick(&Q.menu_gesture,SDL_GetTicks64(),1,pressed(Q.menu,0));
            if(menu&QVR_GESTURE_START)p->wButtons|=XBOX_GAMEPAD_START;
            if(menu&QVR_GESTURE_RECENTER){Q.panel_valid=0;Q.origin_valid=0;fprintf(stderr,"[QUEST-VR] recenter: held left Menu\n");}
            if(pressed(Q.click,0)&&!pressed(Q.menu,0)&&!(menu&QVR_GESTURE_START))p->wButtons|=XBOX_GAMEPAD_BACK;
            if(driving){
                p->sThumbLX=(short)(fmaxf(-1,fminf(1,l.x))*32767);
                p->sThumbLY=(short)(fmaxf(-1,fminf(1,l.y))*32767);
                if(analog(Q.grip,1)>.65f)p->bAnalogButtons[XBOX_BUTTON_A]=255;
                p->bAnalogButtons[XBOX_BUTTON_BLACK]=r.y>.65f?255:0;
                p->bAnalogButtons[XBOX_BUTTON_WHITE]=r.y<-.65f?255:0;
                p->sThumbRX=(short)(fmaxf(-1,fminf(1,r.x))*32767);
                p->sThumbRY=(short)(fmaxf(-1,fminf(1,r.y))*32767);
            }else{
                if(l.x<-.6f)p->wButtons|=XBOX_GAMEPAD_DPAD_LEFT;
                if(l.x>.6f)p->wButtons|=XBOX_GAMEPAD_DPAD_RIGHT;
                if(l.y<-.6f)p->wButtons|=XBOX_GAMEPAD_DPAD_DOWN;
                if(l.y>.6f)p->wButtons|=XBOX_GAMEPAD_DPAD_UP;
            }
            int click=pressed(Q.click,1);if(click&&!Q.recenter){Q.panel_valid=0;Q.origin_valid=0;fprintf(stderr,"[QUEST-VR] recenter: right-stick click\n");}
            Q.recenter=click;
        }
    }
    /* Do not carry a held accelerator or menu button across headset focus. */
    if(Q.state!=XR_SESSION_STATE_FOCUSED){Q.input_armed=0;Q.recenter=0;qvr_menu_tick(&Q.menu_gesture,SDL_GetTicks64(),0,0);}
    if(!Q.input_armed){
        XBOX_GAMEPAD neutral={0};
        if(Q.state==XR_SESSION_STATE_FOCUSED&&!memcmp(&out.Gamepad,&neutral,sizeof neutral))Q.input_armed=1;
        out.Gamepad=neutral;
    }
    pthread_mutex_lock(&input_lock);Q.pad=out;pthread_mutex_unlock(&input_lock);
    uint64_t now=SDL_GetTicks64();XBOX_VIBRATION rumble;uint64_t at;
    pthread_mutex_lock(&input_lock);rumble=Q.rumble;at=Q.rumble_at;pthread_mutex_unlock(&input_lock);
    WORD levels[2]={rumble.wLeftMotorSpeed,rumble.wRightMotorSpeed};
    if(Q.state!=XR_SESSION_STATE_FOCUSED||now-at>XBOX_RUMBLE_EFFECT_MS)levels[0]=levels[1]=0;
    for(unsigned hand=0;hand<2;++hand){
        if(levels[hand]!=Q.haptic_level[hand]||(levels[hand]&&now-Q.haptic_at>=XBOX_RUMBLE_REFRESH_MS)){
            XrHapticActionInfo hi={XR_TYPE_HAPTIC_ACTION_INFO};hi.action=Q.haptic;hi.subactionPath=Q.hands[hand];
            if(levels[hand]){
                XrHapticVibration vibration={XR_TYPE_HAPTIC_VIBRATION};vibration.duration=XBOX_RUMBLE_EFFECT_MS*1000000ll;
                vibration.frequency=XR_FREQUENCY_UNSPECIFIED;vibration.amplitude=levels[hand]/65535.0f;
                xrApplyHapticFeedback(Q.session,&hi,(const XrHapticBaseHeader*)&vibration);
            }else xrStopHapticFeedback(Q.session,&hi);
            Q.haptic_level[hand]=levels[hand];
        }
    }
    if(now-Q.haptic_at>=XBOX_RUMBLE_REFRESH_MS)Q.haptic_at=now;
}
int qvr_active(void){return Q.active;}

int qvr_tracking_copy(struct qvr_tracking *out) {
    if(!out)return 0;
    pthread_mutex_lock(&input_lock);*out=Q.tracking;pthread_mutex_unlock(&input_lock);
    return out->valid;
}
uint32_t qvr_render_queue(const struct qvr_tracking *tracking){
    pthread_mutex_lock(&input_lock);
    uint32_t id=++Q.render_id;if(id>0x0fffffffu)id=Q.render_id=1;
    Q.render[id%64].tracking=*tracking;Q.render[id%64].id=id;
    pthread_mutex_unlock(&input_lock);return id;
}
int qvr_render_lookup(uint32_t id,struct qvr_tracking *out){
    pthread_mutex_lock(&input_lock);int valid=Q.render[id%64].id==id;
    if(valid)*out=Q.render[id%64].tracking;pthread_mutex_unlock(&input_lock);return valid;
}
void qvr_publish_frame(VkCommandBuffer command,const struct nvk_image *slot,
        struct nvk_image *left,const struct qvr_tracking *tracking){
    unsigned i=0;
    pthread_mutex_lock(&input_lock);
    for(;i<3;++i)if(Q.slot[i].key==slot)break;
    if(i==3)for(i=0;i<3;++i)if(!Q.slot[i].key)break;
    if(i==3){pthread_mutex_unlock(&input_lock);return;}
    Q.slot[i].key=slot;Q.slot[i].stereo=0;
    pthread_mutex_unlock(&input_lock);
    if(!left||!tracking||!tracking->valid||left->w!=slot->w||left->h!=slot->h)return;
    struct nvk_image *dest=&Q.slot[i].left;
    if(dest->img&&(dest->w!=slot->w||dest->h!=slot->h)){
        /* Resize only; the present broker has released this back slot. */
        nvk_queue_lock();VkResult result=vkDeviceWaitIdle(Q.gpu->dev);nvk_queue_unlock();
        if(result!=VK_SUCCESS){nvk_device_lost("Stereo slot resize wait failed");return;}
        nvk_image_free(dest);
    }
    if(!dest->img&&!nvk_image_new(dest,slot->format,slot->w,slot->h,1,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT))return;
    nvk_image_to(command,left,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    nvk_image_to(command,dest,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy copy={0};copy.srcSubresource.aspectMask=copy.dstSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    copy.srcSubresource.layerCount=copy.dstSubresource.layerCount=1;copy.extent=(VkExtent3D){slot->w,slot->h,1};
    vkCmdCopyImage(command,left->img,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dest->img,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    nvk_image_to(command,dest,VK_IMAGE_LAYOUT_GENERAL);
    pthread_mutex_lock(&input_lock);Q.slot[i].tracking=*tracking;Q.slot[i].stereo=1;pthread_mutex_unlock(&input_lock);
}
static void pose_copy(struct qvr_pose *out,const XrPosef *pose){
    out->orientation[0]=pose->orientation.x;out->orientation[1]=pose->orientation.y;
    out->orientation[2]=pose->orientation.z;out->orientation[3]=pose->orientation.w;
    out->position[0]=pose->position.x;out->position[1]=pose->position.y;out->position[2]=pose->position.z;
}
static void sample_tracking(XrTime time){
    if(Q.space_change_time&&time>=Q.space_change_time){
        Q.space_change_time=0;Q.origin_valid=0;Q.panel_valid=0;
        fprintf(stderr,"[QUEST-VR] recenter: system reference-space change\n");
    }
    struct qvr_tracking sample;
    pthread_mutex_lock(&input_lock);sample=Q.tracking;pthread_mutex_unlock(&input_lock);
    sample.valid=0;
    XrViewLocateInfo locate={XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime=time;locate.space=Q.local;
    XrViewState state={XR_TYPE_VIEW_STATE};XrView views[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};uint32_t count=0;
    const XrViewStateFlags valid=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
    if(Q.state==XR_SESSION_STATE_FOCUSED&&XR_SUCCEEDED(xrLocateViews(Q.session,&locate,&state,2,&count,views))
            &&count==2&&(state.viewStateFlags&valid)==valid){
        for(unsigned i=0;i<2;++i){
            pose_copy(&sample.eye[i].pose,&views[i].pose);
            sample.eye[i].left=views[i].fov.angleLeft;sample.eye[i].right=views[i].fov.angleRight;
            sample.eye[i].up=views[i].fov.angleUp;sample.eye[i].down=views[i].fov.angleDown;
        }
        static int fov_said;
        if(!fov_said++)for(unsigned i=0;i<2;++i)fprintf(stderr,"[QUEST-VR] runtime eye %u FOV degrees L/R/U/D %.2f %.2f %.2f %.2f\n",i,
            sample.eye[i].left*57.29578f,sample.eye[i].right*57.29578f,sample.eye[i].up*57.29578f,sample.eye[i].down*57.29578f);
        if(!Q.origin_valid){
            XrSpaceLocation head={XR_TYPE_SPACE_LOCATION};
            XrSpaceLocationFlags hv=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
            if(XR_SUCCEEDED(xrLocateSpace(Q.view,Q.local,time,&head))&&(head.locationFlags&hv)==hv){
                XrQuaternionf q=head.pose.orientation;float yaw=atan2f(2*(q.w*q.y+q.x*q.z),1-2*(q.y*q.y+q.x*q.x));
                head.pose.orientation=(XrQuaternionf){0,sinf(yaw*.5f),0,cosf(yaw*.5f)};
                pose_copy(&sample.origin,&head.pose);++sample.generation;Q.origin_valid=1;
            }
        }
        sample.display_time=time;sample.valid=Q.origin_valid;
    }
    pthread_mutex_lock(&input_lock);Q.tracking=sample;pthread_mutex_unlock(&input_lock);
}

int qvr_create(struct nvk_core *c,char *error,size_t error_size) {
    Q.gpu=c;
#ifdef __ANDROID__
    JNIEnv *env=SDL_AndroidGetJNIEnv();jobject activity=SDL_AndroidGetActivity();JavaVM *vm=NULL;
    if(!env||!activity||(*env)->GetJavaVM(env,&vm)!=JNI_OK)goto fail;
    PFN_xrInitializeLoaderKHR init=NULL;
    xrGetInstanceProcAddr(XR_NULL_HANDLE,"xrInitializeLoaderKHR",(PFN_xrVoidFunction*)&init);
    if(!init)goto fail;
    XrLoaderInitInfoAndroidKHR li={XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};li.applicationVM=vm;li.applicationContext=activity;
    if(!check(init((const XrLoaderInitInfoBaseHeaderKHR*)&li),"xrInitializeLoaderKHR"))goto fail;
    XrInstanceCreateInfoAndroidKHR ai={XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};ai.applicationVM=vm;ai.applicationActivity=activity;
    const char *extensions[4]={XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
    uint32_t extension_count=2;
#else
    const char *extensions[4]={XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
    uint32_t extension_count=1;
#endif
    uint32_t available_count=0;
    if(XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(NULL,0,&available_count,NULL))&&available_count){
        XrExtensionProperties *available=calloc(available_count,sizeof *available);
        if(available){
            for(uint32_t i=0;i<available_count;++i)available[i].type=XR_TYPE_EXTENSION_PROPERTIES;
            if(XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(NULL,available_count,&available_count,available)))
                for(uint32_t i=0;i<available_count;++i){
                    if(!strcmp(available[i].extensionName,XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME))extensions[extension_count++]=XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME;
                    if(!strcmp(available[i].extensionName,XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))extensions[extension_count++]=XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
                }
            free(available);
        }
    }
    XrInstanceCreateInfo ci={XR_TYPE_INSTANCE_CREATE_INFO};
#ifdef __ANDROID__
    ci.next=&ai;
#endif
    ci.enabledExtensionCount=extension_count;ci.enabledExtensionNames=extensions;
    strcpy(ci.applicationInfo.applicationName,"Burnout 3 VR Development");ci.applicationInfo.apiVersion=XR_CURRENT_API_VERSION;
    if(!check(xrCreateInstance(&ci,&Q.instance),"xrCreateInstance"))goto fail;
    XrInstanceProperties runtime_properties={XR_TYPE_INSTANCE_PROPERTIES};
    if(!check(xrGetInstanceProperties(Q.instance,&runtime_properties),"xrGetInstanceProperties"))goto fail;
    fprintf(stderr,"[QUEST-VR] runtime: %s\n",runtime_properties.runtimeName);
    XrSystemGetInfo sg={XR_TYPE_SYSTEM_GET_INFO};sg.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if(!check(xrGetSystem(Q.instance,&sg,&Q.system),"xrGetSystem"))goto fail;
    PFN_xrGetVulkanGraphicsRequirements2KHR reqfn=NULL;PFN_xrCreateVulkanInstanceKHR vinst=NULL;
    PFN_xrGetVulkanGraphicsDevice2KHR gpu=NULL;PFN_xrCreateVulkanDeviceKHR devfn=NULL;
#define GET(name,ptr) if(!check(xrGetInstanceProcAddr(Q.instance,name,(PFN_xrVoidFunction*)&ptr),name)||!ptr)goto fail
    GET("xrGetVulkanGraphicsRequirements2KHR",reqfn);GET("xrCreateVulkanInstanceKHR",vinst);
    GET("xrGetVulkanGraphicsDevice2KHR",gpu);GET("xrCreateVulkanDeviceKHR",devfn);
#undef GET
    XrGraphicsRequirementsVulkan2KHR req={XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    if(!check(reqfn(Q.instance,Q.system,&req),"Vulkan requirements")||req.maxApiVersionSupported<XR_MAKE_VERSION(1,2,0))goto fail;
    if(!xr_vk_loader_ok())goto fail;
    VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="Burnout 3 VR";app.apiVersion=VK_API_VERSION_1_2;
    VkInstanceCreateInfo vi={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};vi.pApplicationInfo=&app;
    XrVulkanInstanceCreateInfoKHR xvi={XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};xvi.systemId=Q.system;xvi.pfnGetInstanceProcAddr=vkGetInstanceProcAddr;xvi.vulkanCreateInfo=&vi;
    VkResult vr=VK_SUCCESS;
    if(!check(vinst(Q.instance,&xvi,&c->inst,&vr),"XR Vulkan instance")||vr!=VK_SUCCESS)goto fail;
    xr_vk_adopt_instance(c->inst);
    XrVulkanGraphicsDeviceGetInfoKHR gi={XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};gi.systemId=Q.system;gi.vulkanInstance=c->inst;
    if(!check(gpu(Q.instance,&gi,&c->phys),"XR GPU"))goto fail;
    vkGetPhysicalDeviceProperties(c->phys,&c->props);vkGetPhysicalDeviceMemoryProperties(c->phys,&c->mem);
    snprintf(c->name,sizeof c->name,"%s",c->props.deviceName);
    uint32_t n=0;vkGetPhysicalDeviceQueueFamilyProperties(c->phys,&n,NULL);
    VkQueueFamilyProperties *qs=calloc(n,sizeof *qs);if(!qs)goto fail;
    vkGetPhysicalDeviceQueueFamilyProperties(c->phys,&n,qs);uint32_t q=0;
    for(;q<n;++q)if(qs[q].queueFlags&VK_QUEUE_GRAPHICS_BIT)break;
    free(qs);if(q==n)goto fail;c->qfam=q;
    uint32_t ext_count=0;vkEnumerateDeviceExtensionProperties(c->phys,NULL,&ext_count,NULL);
    VkExtensionProperties *device_exts=calloc(ext_count,sizeof *device_exts);int has_fsr=0;
    if(device_exts){vkEnumerateDeviceExtensionProperties(c->phys,NULL,&ext_count,device_exts);
        for(uint32_t i=0;i<ext_count;++i)if(!strcmp(device_exts[i].extensionName,VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME))has_fsr=1;
        free(device_exts);}
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR density={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
    VkPhysicalDeviceVulkan12Features have12={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    if(has_fsr)have12.pNext=&density;
    VkPhysicalDeviceFeatures2 have={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};have.pNext=&have12;vkGetPhysicalDeviceFeatures2(c->phys,&have);
    if(!have12.timelineSemaphore)goto fail;
    if(has_fsr&&density.pipelineFragmentShadingRate&&density.attachmentFragmentShadingRate){
        VkFormatProperties fp;vkGetPhysicalDeviceFormatProperties(c->phys,VK_FORMAT_R8_UINT,&fp);
        VkPhysicalDeviceFragmentShadingRatePropertiesKHR dp={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR};
        VkPhysicalDeviceProperties2 p={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};p.pNext=&dp;vkGetPhysicalDeviceProperties2(c->phys,&p);
        uint32_t texel=8;while(texel<dp.minFragmentShadingRateAttachmentTexelSize.width||texel<dp.minFragmentShadingRateAttachmentTexelSize.height)texel*=2;
        int rate2=0;uint32_t nr=0;
        PFN_vkGetPhysicalDeviceFragmentShadingRatesKHR rates=(PFN_vkGetPhysicalDeviceFragmentShadingRatesKHR)vkGetInstanceProcAddr(c->inst,"vkGetPhysicalDeviceFragmentShadingRatesKHR");
        if(rates&&rates(c->phys,&nr,NULL)==VK_SUCCESS&&nr){
            VkPhysicalDeviceFragmentShadingRateKHR *rs=calloc(nr,sizeof *rs);
            if(rs){for(uint32_t i=0;i<nr;++i)rs[i].sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_KHR;
                if(rates(c->phys,&nr,rs)==VK_SUCCESS)for(uint32_t i=0;i<nr;++i)
                    if(rs[i].fragmentSize.width==2&&rs[i].fragmentSize.height==2&&(rs[i].sampleCounts&VK_SAMPLE_COUNT_1_BIT))rate2=1;
                free(rs);}
        }
        if(rate2&&(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR)
                &&texel<=dp.maxFragmentShadingRateAttachmentTexelSize.width&&texel<=dp.maxFragmentShadingRateAttachmentTexelSize.height){c->quest_fsr=1;c->quest_fsr_texel=texel;}
    }
    density.pNext=NULL;density.pipelineFragmentShadingRate=c->quest_fsr;density.primitiveFragmentShadingRate=VK_FALSE;
    density.attachmentFragmentShadingRate=c->quest_fsr;
    VkPhysicalDeviceExtendedDynamicState2FeaturesEXT ed2={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT};ed2.extendedDynamicState2=VK_TRUE;
    if(c->quest_fsr)ed2.pNext=&density;
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT ed={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};ed.extendedDynamicState=VK_TRUE;ed.pNext=&ed2;
    VkPhysicalDeviceSynchronization2FeaturesKHR sync={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR};sync.synchronization2=VK_TRUE;sync.pNext=&ed;
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dyn={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};dyn.dynamicRendering=VK_TRUE;dyn.pNext=&sync;
    VkPhysicalDeviceVulkan12Features f12={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};f12.pNext=&dyn;f12.timelineSemaphore=VK_TRUE;f12.hostQueryReset=have12.hostQueryReset;
    VkPhysicalDeviceFeatures2 f2={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};f2.pNext=&f12;
#ifdef _WIN32
    /* VDXR adds VkPhysicalDeviceTimelineSemaphoreFeatures itself. A Vulkan12
     * aggregate in our chain would duplicate that promoted feature structure,
     * which Vulkan forbids. Use individual feature nodes on Windows. */
    VkPhysicalDeviceHostQueryResetFeatures query_reset={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES};
    query_reset.hostQueryReset=have12.hostQueryReset;query_reset.pNext=&dyn;
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    timeline.timelineSemaphore=VK_TRUE;timeline.pNext=&query_reset;
    f2.pNext=strstr(runtime_properties.runtimeName,"VirtualDesktop")?(void*)&query_reset:(void*)&timeline;
#endif
    f2.features.occlusionQueryPrecise=have.features.occlusionQueryPrecise;f2.features.samplerAnisotropy=have.features.samplerAnisotropy;
    f2.features.textureCompressionBC=have.features.textureCompressionBC;c->bc=have.features.textureCompressionBC;
    c->max_aniso=have.features.samplerAnisotropy?c->props.limits.maxSamplerAnisotropy:0;
    if(have.features.depthClamp&&have.features.shaderClipDistance){f2.features.depthClamp=VK_TRUE;f2.features.shaderClipDistance=VK_TRUE;c->depth_clamp=1;}
    const char *de[]={VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME,VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME};
    float priority=1;VkDeviceQueueCreateInfo qi={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=q;qi.queueCount=1;qi.pQueuePriorities=&priority;
    VkDeviceCreateInfo di={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.pNext=&f2;di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;di.enabledExtensionCount=4+c->quest_fsr;di.ppEnabledExtensionNames=de;
    XrVulkanDeviceCreateInfoKHR xd={XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};xd.systemId=Q.system;xd.pfnGetInstanceProcAddr=vkGetInstanceProcAddr;xd.vulkanPhysicalDevice=c->phys;xd.vulkanCreateInfo=&di;
    if(!check(devfn(Q.instance,&xd,&c->dev,&vr),"XR Vulkan device")||vr!=VK_SUCCESS)goto fail;
    xr_vk_adopt_device(c->dev,1);if(xr_vk_missing_core13())goto fail;
    vkGetDeviceQueue(c->dev,q,0,&c->queue);
    fprintf(stderr,"[QUEST-VR] world foveation capability: %d, shading-rate tile %u\n",c->quest_fsr,c->quest_fsr_texel);
    VkFormatProperties fp;vkGetPhysicalDeviceFormatProperties(c->phys,VK_FORMAT_D32_SFLOAT_S8_UINT,&fp);
    c->depth_format=(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)?VK_FORMAT_D32_SFLOAT_S8_UINT:VK_FORMAT_D24_UNORM_S8_UINT;
    XrGraphicsBindingVulkan2KHR binding={XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};binding.instance=c->inst;binding.physicalDevice=c->phys;binding.device=c->dev;binding.queueFamilyIndex=q;
    XrSessionCreateInfo sc={XR_TYPE_SESSION_CREATE_INFO};sc.next=&binding;sc.systemId=Q.system;
    if(!check(xrCreateSession(Q.instance,&sc,&Q.session),"xrCreateSession"))goto fail;
    XrReferenceSpaceCreateInfo rs={XR_TYPE_REFERENCE_SPACE_CREATE_INFO};rs.poseInReferenceSpace.orientation.w=1;rs.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
    if(!check(xrCreateReferenceSpace(Q.session,&rs,&Q.local),"local space"))goto fail;
    rs.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;if(!check(xrCreateReferenceSpace(Q.session,&rs,&Q.view),"view space"))goto fail;
    if(!make_actions())goto fail;
    VkCommandPoolCreateInfo pool={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=q;pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if(vkCreateCommandPool(c->dev,&pool,NULL,&Q.pool)!=VK_SUCCESS)goto fail;
    VkCommandBufferAllocateInfo cb={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cb.commandPool=Q.pool;cb.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cb.commandBufferCount=1;
    if(vkAllocateCommandBuffers(c->dev,&cb,&Q.command)!=VK_SUCCESS)goto fail;
    VkFenceCreateInfo fence={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};fence.flags=VK_FENCE_CREATE_SIGNALED_BIT;
    if(vkCreateFence(c->dev,&fence,NULL,&Q.fence)!=VK_SUCCESS)goto fail;
    Q.active=1;xbox_InputSetBackend(&input_backend);
    fprintf(stderr,"[QUEST-VR] OpenXR + Vulkan 1.2/KHR device ready: %s\n",c->name);return 1;
fail:
    snprintf(error,error_size,"Quest OpenXR/Vulkan initialization failed; inspect [QUEST-VR] log");return 0;
}

static void xr_image_to(VkImage image,VkImageLayout old,VkImageLayout next){
    VkImageMemoryBarrier2 barrier={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask=barrier.dstStageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask=VK_ACCESS_2_MEMORY_READ_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_2_MEMORY_READ_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.oldLayout=old;barrier.newLayout=next;barrier.image=image;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,Q.layers};
    VkDependencyInfo dependency={VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dependency.imageMemoryBarrierCount=1;dependency.pImageMemoryBarriers=&barrier;
    vkCmdPipelineBarrier2(Q.command,&dependency);
}
static void capture_record(VkImage image,unsigned eye,uint32_t width,uint32_t height){
    VkDeviceSize size=(VkDeviceSize)width*height*4u;
    if(!nvk_buffer_new(&Q.capture[eye],size,VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT))return;
    VkBufferImageCopy copy={0};copy.imageSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
    copy.imageExtent=(VkExtent3D){width,height,1};
    vkCmdCopyImageToBuffer(Q.command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,Q.capture[eye].buf,1,&copy);
    VkMemoryBarrier2 barrier={VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask=VK_PIPELINE_STAGE_2_TRANSFER_BIT;barrier.srcAccessMask=VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask=VK_PIPELINE_STAGE_2_HOST_BIT;barrier.dstAccessMask=VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo dependency={VK_STRUCTURE_TYPE_DEPENDENCY_INFO};dependency.memoryBarrierCount=1;dependency.pMemoryBarriers=&barrier;
    vkCmdPipelineBarrier2(Q.command,&dependency);Q.capture_mask|=1u<<eye;
}
static void capture_write(uint32_t width,uint32_t height){
    const char *prefix=getenv("BO3_QUEST_STEREO_CAPTURE");
    for(unsigned eye=0;eye<2;++eye){
        if((Q.capture_mask&(1u<<eye))&&prefix){
            uint8_t header[54]={0};uint32_t values[6]={54u+width*height*4u,54,40,width,(uint32_t)-(int32_t)height,width*height*4u};
            const unsigned offsets[6]={2,10,14,18,22,34};header[0]='B';header[1]='M';header[26]=1;header[28]=32;
            for(unsigned i=0;i<6;++i)for(unsigned byte=0;byte<4;++byte)header[offsets[i]+byte]=(uint8_t)(values[i]>>(8*byte));
            char path[1200];snprintf(path,sizeof path,"%s-post-%s.bmp",prefix,eye?"right":"left");
            FILE *file=fopen(path,"wb");if(file){fwrite(header,1,sizeof header,file);fwrite(Q.capture[eye].map,4,(size_t)width*height,file);fclose(file);
                fprintf(stderr,"[QUEST-VR] post-processed eye captured: %s (%ux%u)\n",path,width,height);}
        }
        nvk_buffer_free(&Q.capture[eye]);
    }
    Q.capture_mask=0;Q.capture_done=1;
}
static int make_swapchain(uint32_t width,uint32_t height,uint32_t layers) {
    if(Q.swapchain&&Q.width==width&&Q.height==height&&Q.layers==layers)return 1;
    if(Q.swapchain){xrDestroySwapchain(Q.swapchain);Q.swapchain=XR_NULL_HANDLE;free(Q.images);Q.images=NULL;}
    nvk_image_free(&Q.rgba_copy);
    uint32_t n=0;xrEnumerateSwapchainFormats(Q.session,0,&n,NULL);int64_t *formats=calloc(n,sizeof *formats);if(!formats)return 0;
    xrEnumerateSwapchainFormats(Q.session,n,&n,formats);Q.format=VK_FORMAT_UNDEFINED;
    /* Copy game-encoded BGRA bytes into an sRGB image, without a second gamma conversion. */
    for(uint32_t i=0;i<n;++i)if(formats[i]==VK_FORMAT_B8G8R8A8_SRGB)Q.format=VK_FORMAT_B8G8R8A8_SRGB;
    if(Q.format==VK_FORMAT_UNDEFINED)
        for(uint32_t i=0;i<n;++i)if(formats[i]==VK_FORMAT_R8G8B8A8_SRGB)Q.format=VK_FORMAT_R8G8B8A8_SRGB;
    free(formats);if(Q.format==VK_FORMAT_UNDEFINED){nvk_device_lost("XR runtime has no supported sRGB swapchain format");return 0;}
    if(Q.format==VK_FORMAT_R8G8B8A8_SRGB){
        /* Reorder BGRA to RGBA in UNORM first. Blitting directly to sRGB would
         * encode already-encoded game pixels a second time. The final compatible
         * UNORM-to-sRGB copy preserves their values. */
        if(!nvk_image_new(&Q.rgba_copy,VK_FORMAT_R8G8B8A8_UNORM,width,height,1,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT))return 0;
    }
    XrSwapchainCreateInfo sc={XR_TYPE_SWAPCHAIN_CREATE_INFO};sc.usageFlags=XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    sc.format=Q.format;sc.sampleCount=1;sc.width=width;sc.height=height;sc.faceCount=1;sc.arraySize=layers;sc.mipCount=1;
    if(!check(xrCreateSwapchain(Q.session,&sc,&Q.swapchain),"xrCreateSwapchain"))return 0;
    xrEnumerateSwapchainImages(Q.swapchain,0,&Q.image_count,NULL);Q.images=calloc(Q.image_count,sizeof *Q.images);if(!Q.images)return 0;
    for(uint32_t i=0;i<Q.image_count;++i)Q.images[i].type=XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
    if(!check(xrEnumerateSwapchainImages(Q.swapchain,Q.image_count,&Q.image_count,(XrSwapchainImageBaseHeader*)Q.images),"XR images"))return 0;
    Q.width=width;Q.height=height;Q.layers=layers;
    fprintf(stderr,"[QUEST-VR] swapchain %ux%u layers=%u format=%d, channel reorder=%d\n",width,height,layers,Q.format,Q.rgba_copy.img!=VK_NULL_HANDLE);
    return 1;
}
int qvr_present(const struct nvk_image *source_slot,uint32_t width,uint32_t height) {
    VkImage source=source_slot->img,left_image=VK_NULL_HANDLE;
    struct nvk_image left_source={0};
    struct qvr_tracking rendered={0};
    pthread_mutex_lock(&input_lock);
    for(unsigned i=0;i<3;++i)if(Q.slot[i].key==source_slot&&Q.slot[i].stereo){
        rendered=Q.slot[i].tracking;left_source=Q.slot[i].left;left_image=left_source.img;break;
    }
    pthread_mutex_unlock(&input_lock);
    int stereo=left_image&&rendered.valid;
    const char *capture_prefix=getenv("BO3_QUEST_STEREO_CAPTURE");
    int capture=stereo&&!Q.capture_done&&capture_prefix&&*capture_prefix&&++Q.stereo_frames>=30;
    if(!Q.active||Q.quit||nvk_core_lost())return 0;
    XrEventDataBuffer ev={XR_TYPE_EVENT_DATA_BUFFER};
    while(xrPollEvent(Q.instance,&ev)==XR_SUCCESS){
        if(ev.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){XrEventDataSessionStateChanged *s=(void*)&ev;Q.state=s->state;
            fprintf(stderr,"[QUEST-VR] session state %d\n",Q.state);
            if(Q.state!=XR_SESSION_STATE_FOCUSED){
                pthread_mutex_lock(&input_lock);memset(&Q.pad.Gamepad,0,sizeof Q.pad.Gamepad);memset(&Q.rumble,0,sizeof Q.rumble);pthread_mutex_unlock(&input_lock);
                Q.input_armed=0;
                Q.recenter=0;qvr_menu_tick(&Q.menu_gesture,SDL_GetTicks64(),0,0);
                pthread_mutex_lock(&input_lock);Q.tracking.valid=0;pthread_mutex_unlock(&input_lock);
            }
            if(Q.state==XR_SESSION_STATE_READY){XrSessionBeginInfo bi={XR_TYPE_SESSION_BEGIN_INFO};bi.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;Q.running=check(xrBeginSession(Q.session,&bi),"xrBeginSession");if(Q.running)performance_request();}
            if(Q.state==XR_SESSION_STATE_STOPPING&&Q.running){xrEndSession(Q.session);Q.running=0;}
            if(Q.state==XR_SESSION_STATE_EXITING||Q.state==XR_SESSION_STATE_LOSS_PENDING)Q.quit=1;
        }else if(ev.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING){
            const XrEventDataReferenceSpaceChangePending *change=(const void*)&ev;
            if(change->referenceSpaceType==XR_REFERENCE_SPACE_TYPE_LOCAL)Q.space_change_time=change->changeTime;
        }ev=(XrEventDataBuffer){XR_TYPE_EVENT_DATA_BUFFER};
    }
    if(!Q.running){SDL_Delay(10);return 1;}
    XrFrameState state={XR_TYPE_FRAME_STATE};XrFrameWaitInfo wi={XR_TYPE_FRAME_WAIT_INFO};XrFrameBeginInfo begin={XR_TYPE_FRAME_BEGIN_INFO};
    if(!check(xrWaitFrame(Q.session,&wi,&state),"xrWaitFrame"))return 0;
    /* VDXR's app GPU timer submits commands during xrBeginFrame too. */
    nvk_queue_lock();XrResult begun=xrBeginFrame(Q.session,&begin);nvk_queue_unlock();
    if(!check(begun,"xrBeginFrame"))return 0;
    sync_inputs();
    sample_tracking(state.predictedDisplayTime);
    XrCompositionLayerQuad quad={XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerProjection projection={XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerProjectionView eyes[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    const XrCompositionLayerBaseHeader *layers[1]={stereo?(const XrCompositionLayerBaseHeader*)&projection:(const XrCompositionLayerBaseHeader*)&quad};
    unsigned layer_count=0;
    int swapchain_ready=0;
    if(state.shouldRender&&source){
        /* Runtime image enumeration can submit layout/interop work to the
         * graphics queue too (VDXR). Serialize it with the executor. */
        nvk_queue_lock();swapchain_ready=make_swapchain(width,height,stereo?2:1);nvk_queue_unlock();
    }
    if(swapchain_ready){
        uint32_t idx=0;XrSwapchainImageAcquireInfo ac={XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if(check(xrAcquireSwapchainImage(Q.swapchain,&ac,&idx),"xrAcquireSwapchainImage")){
            XrSwapchainImageWaitInfo wait={XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;
            if(check(xrWaitSwapchainImage(Q.swapchain,&wait),"xrWaitSwapchainImage")){
                VkResult r=vkWaitForFences(Q.gpu->dev,1,&Q.fence,VK_TRUE,5000000000ull);
                if(r!=VK_SUCCESS){nvk_device_lost("Quest frame fence wait failed");return 0;}
                vkResetFences(Q.gpu->dev,1,&Q.fence);vkResetCommandBuffer(Q.command,0);
                VkCommandBufferBeginInfo cb={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};cb.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;vkBeginCommandBuffer(Q.command,&cb);
                xr_image_to(Q.images[idx].image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                /* The XR array has one independently rendered game image per eye. */
                for(unsigned eye=0;eye<(stereo?2u:1u);++eye){
                const struct nvk_image *input_image=stereo&&eye==0?&left_source:source_slot;
                VkImage input=input_image->img;VkImageLayout input_layout=VK_IMAGE_LAYOUT_GENERAL;
                fbv_post_eye(Q.command,input_image,(int)eye,&input,&input_layout);
                nvk_image_barrier(Q.command,input,VK_IMAGE_ASPECT_COLOR_BIT,input_layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                if(capture)capture_record(input,eye,width,height);
                VkImageCopy copy={0};copy.srcSubresource.aspectMask=copy.dstSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;copy.srcSubresource.layerCount=copy.dstSubresource.layerCount=1;copy.extent=(VkExtent3D){width,height,1};
                copy.dstSubresource.baseArrayLayer=eye;
                VkImage copy_source=input;
                if(Q.rgba_copy.img){
                    nvk_image_to(Q.command,&Q.rgba_copy,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                    VkImageBlit blit={0};blit.srcSubresource=copy.srcSubresource;blit.dstSubresource=copy.srcSubresource;
                    blit.srcOffsets[1]=blit.dstOffsets[1]=(VkOffset3D){(int32_t)width,(int32_t)height,1};
                    vkCmdBlitImage(Q.command,input,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,Q.rgba_copy.img,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
                    nvk_image_to(Q.command,&Q.rgba_copy,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                    copy_source=Q.rgba_copy.img;
                }
                vkCmdCopyImage(Q.command,copy_source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,Q.images[idx].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
                nvk_image_barrier(Q.command,input,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,input_layout);
                }
                xr_image_to(Q.images[idx].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                vkEndCommandBuffer(Q.command);VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&Q.command;
                nvk_queue_lock();r=vkQueueSubmit(Q.gpu->queue,1,&submit,Q.fence);nvk_queue_unlock();
                if(r!=VK_SUCCESS||vkWaitForFences(Q.gpu->dev,1,&Q.fence,VK_TRUE,5000000000ull)!=VK_SUCCESS){nvk_device_lost("Quest image copy failed");return 0;}
                if(Q.capture_mask)capture_write(width,height);
                XrSwapchainImageReleaseInfo release={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                if(check(xrReleaseSwapchainImage(Q.swapchain,&release),"xrReleaseSwapchainImage")){
                    if(!Q.panel_valid){
                        XrSpaceLocation head={XR_TYPE_SPACE_LOCATION};
                        if(XR_SUCCEEDED(xrLocateSpace(Q.view,Q.local,state.predictedDisplayTime,&head))&&(head.locationFlags&XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)&&(head.locationFlags&XR_SPACE_LOCATION_POSITION_VALID_BIT)){
                            XrQuaternionf q=head.pose.orientation;float yaw=atan2f(2*(q.w*q.y+q.x*q.z),1-2*(q.y*q.y+q.x*q.x));
                            Q.panel.orientation=(XrQuaternionf){0,sinf(yaw*.5f),0,cosf(yaw*.5f)};
                            Q.panel.position=(XrVector3f){head.pose.position.x-2.5f*sinf(yaw),head.pose.position.y,head.pose.position.z-2.5f*cosf(yaw)};
                            Q.panel_valid=1;
                        }
                    }
                    quad.space=Q.local;quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;quad.pose=Q.panel;
                    quad.size=(XrExtent2Df){3.0f,3.0f*(float)height/(float)width};quad.subImage.swapchain=Q.swapchain;
                    quad.subImage.imageRect.extent=(XrExtent2Di){(int32_t)width,(int32_t)height};layer_count=Q.panel_valid;
                    if(stereo){
                        for(unsigned eye=0;eye<2;++eye){
                            const struct qvr_eye *e=&rendered.eye[eye];
                            eyes[eye].pose.orientation=(XrQuaternionf){e->pose.orientation[0],e->pose.orientation[1],e->pose.orientation[2],e->pose.orientation[3]};
                            eyes[eye].pose.position=(XrVector3f){e->pose.position[0],e->pose.position[1],e->pose.position[2]};
                            eyes[eye].fov=(XrFovf){e->left,e->right,e->up,e->down};
                            eyes[eye].subImage.swapchain=Q.swapchain;eyes[eye].subImage.imageArrayIndex=eye;
                            eyes[eye].subImage.imageRect.extent=(XrExtent2Di){(int32_t)width,(int32_t)height};
                        }
                        projection.space=Q.local;projection.viewCount=2;projection.views=eyes;layer_count=1;
                    }
                }
            }
        }
    }
    XrFrameEndInfo end={XR_TYPE_FRAME_END_INFO};end.displayTime=state.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=layer_count;end.layers=layers;
    /* OpenXR Vulkan requires external synchronization of runtime operations
     * that access the bound VkQueue. VDXR submits its D3D interop fence here. */
    nvk_queue_lock();XrResult ended=xrEndFrame(Q.session,&end);nvk_queue_unlock();
    if(!check(ended,"xrEndFrame"))return 0;
    static uint64_t rate_start;
    uint64_t now=SDL_GetTicks64();if(!rate_start)rate_start=now;
    if(++Q.frames%300==0){
        fprintf(stderr,"[QUEST-VR] submitted %u frames (%ux%u), stereo=%d focused=%d; submission %.1f Hz, runtime %.1f Hz\n",Q.frames,width,height,stereo,Q.state==XR_SESSION_STATE_FOCUSED,
            now>rate_start?300000.0/(double)(now-rate_start):0,state.predictedDisplayPeriod>0?1e9/(double)state.predictedDisplayPeriod:0);
        rate_start=now;
    }
    return 1;
}
void qvr_destroy(void) {
    if(!Q.active)return;Q.active=0;
    if(Q.fence)vkWaitForFences(Q.gpu->dev,1,&Q.fence,VK_TRUE,5000000000ull);
    if(Q.swapchain)xrDestroySwapchain(Q.swapchain);free(Q.images);
    nvk_image_free(&Q.rgba_copy);
    for(unsigned i=0;i<3;++i)nvk_image_free(&Q.slot[i].left);
    for(unsigned i=0;i<2;++i)nvk_buffer_free(&Q.capture[i]);
    if(Q.fence)vkDestroyFence(Q.gpu->dev,Q.fence,NULL);if(Q.pool)vkDestroyCommandPool(Q.gpu->dev,Q.pool,NULL);
    if(Q.local)xrDestroySpace(Q.local);if(Q.view)xrDestroySpace(Q.view);
    if(Q.session)xrDestroySession(Q.session);if(Q.actions)xrDestroyActionSet(Q.actions);if(Q.instance)xrDestroyInstance(Q.instance);
}
