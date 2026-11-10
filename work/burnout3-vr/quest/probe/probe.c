/* Local compatibility probe. No game assets, network, telemetry, or settings changes. */
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_VULKAN
#include <jni.h>
#include <android/native_activity.h>
#include <android/log.h>
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

static ANativeActivity *activity;
static FILE *report;
static void note(const char *fmt, ...) {
    char b[1024]; va_list a; va_start(a,fmt); vsnprintf(b,sizeof b,fmt,a); va_end(a);
    __android_log_print(ANDROID_LOG_INFO,"BO3VR-PROBE","%s",b);
    if(report){fprintf(report,"%s\n",b);fflush(report);}
}
static int xr_ok(XrResult r,const char *at){note("%s: %d",at,r);return XR_SUCCEEDED(r);}
static void *probe(void *unused) {
    (void)unused;
    JNIEnv *env=NULL; (*activity->vm)->AttachCurrentThread(activity->vm,&env,NULL);
    char path[1024]; snprintf(path,sizeof path,"%s/graphics-check.txt",activity->externalDataPath);
    report=fopen(path,"w");note("Burnout 3 VR preflight; game runtime has not been started.");
    XrInstance xr=XR_NULL_HANDLE; XrSession session=XR_NULL_HANDLE;
    VkInstance vk=VK_NULL_HANDLE; VkDevice device=VK_NULL_HANDLE;
    PFN_xrInitializeLoaderKHR init=NULL;
    xrGetInstanceProcAddr(XR_NULL_HANDLE,"xrInitializeLoaderKHR",(PFN_xrVoidFunction*)&init);
    if(!init){note("FAIL: Android OpenXR loader entry missing");goto done;}
    XrLoaderInitInfoAndroidKHR li={XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    li.applicationVM=activity->vm;li.applicationContext=activity->clazz;
    if(!xr_ok(init((const XrLoaderInitInfoBaseHeaderKHR*)&li),"xrInitializeLoaderKHR"))goto done;
    const char *exts[]={XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
    XrInstanceCreateInfoAndroidKHR android={XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    android.applicationVM=activity->vm;android.applicationActivity=activity->clazz;
    XrInstanceCreateInfo xi={XR_TYPE_INSTANCE_CREATE_INFO};xi.next=&android;
    strcpy(xi.applicationInfo.applicationName,"Burnout VR Graphics Check");
    xi.applicationInfo.apiVersion=XR_CURRENT_API_VERSION;xi.enabledExtensionCount=2;xi.enabledExtensionNames=exts;
    if(!xr_ok(xrCreateInstance(&xi,&xr),"xrCreateInstance"))goto done;
    XrSystemGetInfo si={XR_TYPE_SYSTEM_GET_INFO};si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system=0;if(!xr_ok(xrGetSystem(xr,&si,&system),"xrGetSystem"))goto done;
    PFN_xrGetVulkanGraphicsRequirements2KHR get_req=NULL;
    PFN_xrCreateVulkanInstanceKHR make_vk=NULL;
    PFN_xrGetVulkanGraphicsDevice2KHR get_gpu=NULL;
    PFN_xrCreateVulkanDeviceKHR make_device=NULL;
    xrGetInstanceProcAddr(xr,"xrGetVulkanGraphicsRequirements2KHR",(PFN_xrVoidFunction*)&get_req);
    xrGetInstanceProcAddr(xr,"xrCreateVulkanInstanceKHR",(PFN_xrVoidFunction*)&make_vk);
    xrGetInstanceProcAddr(xr,"xrGetVulkanGraphicsDevice2KHR",(PFN_xrVoidFunction*)&get_gpu);
    xrGetInstanceProcAddr(xr,"xrCreateVulkanDeviceKHR",(PFN_xrVoidFunction*)&make_device);
    if(!get_req||!make_vk||!get_gpu||!make_device){note("FAIL: Vulkan-enable2 entry points missing");goto done;}
    XrGraphicsRequirementsVulkan2KHR req={XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    if(!xr_ok(get_req(xr,system,&req),"xrGetVulkanGraphicsRequirements2KHR"))goto done;
    note("OpenXR Vulkan interval %u.%u - %u.%u",(unsigned)XR_VERSION_MAJOR(req.minApiVersionSupported),(unsigned)XR_VERSION_MINOR(req.minApiVersionSupported),(unsigned)XR_VERSION_MAJOR(req.maxApiVersionSupported),(unsigned)XR_VERSION_MINOR(req.maxApiVersionSupported));
    /* Respect the XR runtime's advertised maximum even when the GPU is 1.3.
     * Use promoted KHR features through their 1.2 extension contracts. */
    VkApplicationInfo ai={VK_STRUCTURE_TYPE_APPLICATION_INFO};ai.pApplicationName="BO3VR probe";ai.apiVersion=VK_API_VERSION_1_2;
    if(req.maxApiVersionSupported<XR_MAKE_VERSION(1,2,0)){note("FAIL: XR Vulkan ceiling below 1.2");goto done;}
    VkInstanceCreateInfo vi={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};vi.pApplicationInfo=&ai;
    XrVulkanInstanceCreateInfoKHR xvi={XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    xvi.systemId=system;xvi.pfnGetInstanceProcAddr=vkGetInstanceProcAddr;xvi.vulkanCreateInfo=&vi;
    VkResult vr=VK_SUCCESS;
    if(!xr_ok(make_vk(xr,&xvi,&vk,&vr),"xrCreateVulkanInstanceKHR")||vr!=VK_SUCCESS){note("FAIL: VkInstance result %d",vr);goto done;}
    XrVulkanGraphicsDeviceGetInfoKHR gi={XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};gi.systemId=system;gi.vulkanInstance=vk;
    VkPhysicalDevice gpu=VK_NULL_HANDLE;if(!xr_ok(get_gpu(xr,&gi,&gpu),"xrGetVulkanGraphicsDevice2KHR"))goto done;
    VkPhysicalDeviceProperties prop;vkGetPhysicalDeviceProperties(gpu,&prop);
    note("Runtime-selected GPU: %s Vulkan %u.%u.%u",prop.deviceName,VK_VERSION_MAJOR(prop.apiVersion),VK_VERSION_MINOR(prop.apiVersion),VK_VERSION_PATCH(prop.apiVersion));
    VkPhysicalDeviceVulkan13Features f13={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features f12={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};f12.pNext=&f13;
    VkPhysicalDeviceFeatures2 f={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};f.pNext=&f12;vkGetPhysicalDeviceFeatures2(gpu,&f);
    note("Required features: dynamicRendering=%u synchronization2=%u timelineSemaphore=%u",f13.dynamicRendering,f13.synchronization2,f12.timelineSemaphore);
    if(prop.apiVersion<VK_API_VERSION_1_3||!f13.dynamicRendering||!f13.synchronization2||!f12.timelineSemaphore){note("FAIL: stock OpenXR graphics path cannot satisfy upstream Takedawn requirements");goto done;}
    uint32_t count=0;vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,NULL);
    VkQueueFamilyProperties *queues=calloc(count,sizeof *queues);if(!queues)goto done;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,queues);uint32_t queue=0;
    for(;queue<count;++queue)if(queues[queue].queueFlags&VK_QUEUE_GRAPHICS_BIT)break;
    free(queues);if(queue==count){note("FAIL: no graphics queue");goto done;}
    float priority=1;VkDeviceQueueCreateInfo qi={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=queue;qi.queueCount=1;qi.pQueuePriorities=&priority;
    const char *device_exts[]={VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME};
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT ed={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};ed.extendedDynamicState=VK_TRUE;
    VkPhysicalDeviceSynchronization2FeaturesKHR sync={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR};sync.synchronization2=VK_TRUE;sync.pNext=&ed;
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dyn={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};dyn.dynamicRendering=VK_TRUE;dyn.pNext=&sync;
    VkPhysicalDeviceVulkan12Features enabled12={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};enabled12.timelineSemaphore=VK_TRUE;enabled12.pNext=&dyn;
    VkDeviceCreateInfo di={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.pNext=&enabled12;di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;
    di.enabledExtensionCount=3;di.ppEnabledExtensionNames=device_exts;
    XrVulkanDeviceCreateInfoKHR xd={XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};xd.systemId=system;xd.pfnGetInstanceProcAddr=vkGetInstanceProcAddr;xd.vulkanPhysicalDevice=gpu;xd.vulkanCreateInfo=&di;
    if(!xr_ok(make_device(xr,&xd,&device,&vr),"xrCreateVulkanDeviceKHR")||vr!=VK_SUCCESS){note("FAIL: VkDevice result %d",vr);goto done;}
    XrGraphicsBindingVulkan2KHR binding={XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};binding.instance=vk;binding.physicalDevice=gpu;binding.device=device;binding.queueFamilyIndex=queue;binding.queueIndex=0;
    XrSessionCreateInfo sci={XR_TYPE_SESSION_CREATE_INFO};sci.next=&binding;sci.systemId=system;
    if(!xr_ok(xrCreateSession(xr,&sci,&session),"xrCreateSession"))goto done;
    note("PASS: Vulkan 1.2 + required KHR features and OpenXR session compatible. Stereo game rendering remains unimplemented and untested.");
done:
    if(session)xrDestroySession(session);
    if(device)vkDestroyDevice(device,NULL);
    if(vk)vkDestroyInstance(vk,NULL);
    if(xr)xrDestroyInstance(xr);
    note("Probe finished. This check does not certify performance or controller mappings.");
    if(report)fclose(report);report=NULL;
    (*activity->vm)->DetachCurrentThread(activity->vm);
    ANativeActivity_finish(activity);return NULL;
}
void ANativeActivity_onCreate(ANativeActivity *a, void *saved, size_t n) {
    (void)saved;(void)n;activity=a;pthread_t thread;
    if(pthread_create(&thread,NULL,probe,NULL)==0)pthread_detach(thread);
    else ANativeActivity_finish(activity);
}
