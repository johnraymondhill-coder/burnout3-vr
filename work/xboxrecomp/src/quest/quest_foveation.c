/* Fixed foveation of world draws, not the final XR copy. MIT. */
#include "d3d/vk_dyn.h"
#include "quest_foveation.h"
#include <stdio.h>
#include <stdlib.h>

const struct nvk_image *qvr_foveation_map(uint32_t width,uint32_t height){
    const struct nvk_core *c=nvk_core_get();
    static struct {uint32_t w,h;struct nvk_image image;} maps[8];
    static unsigned count;static int enabled=-1;
    if(enabled<0){const char *v=getenv("BO3_QUEST_FOVEATION");enabled=!v||atoi(v)!=0;}
    if(!enabled||!c||!c->quest_fsr||!c->quest_fsr_texel)return NULL;
    for(unsigned i=0;i<count;++i)if(maps[i].w==width&&maps[i].h==height)return maps[i].image.img?&maps[i].image:NULL;
    if(count==8)return NULL;
    unsigned at=count++;maps[at].w=width;maps[at].h=height;
    struct nvk_image *im=&maps[at].image;
    uint32_t w=(width+c->quest_fsr_texel-1)/c->quest_fsr_texel;
    uint32_t h=(height+c->quest_fsr_texel-1)/c->quest_fsr_texel;
    struct nvk_buffer upload={0};VkCommandPool pool=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;
    VkCommandBuffer cb=VK_NULL_HANDLE;int submitted=0;
    if(!nvk_image_new(im,VK_FORMAT_R8_UINT,w,h,1,
            VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_ASPECT_COLOR_BIT))goto fail;
    if(!nvk_buffer_new(&upload,(VkDeviceSize)w*h,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,0))goto fail;
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
        float nx=(2*((x+.5f)*c->quest_fsr_texel)/width-1)/.62f;
        float ny=(2*((y+.5f)*c->quest_fsr_texel)/height-1)/.70f;
        /* A broad full-rate central ellipse; 2x2 only outside it. The HUD,
         * menus, reflections and AA/CAS passes never use this attachment. */
        /* R8_UINT: log2(width) in bits3:2, log2(height) in bits1:0.
         * 0=1x1, 5=2x2. This is exact, rather than a density hint. */
        ((unsigned char*)upload.map)[y*w+x]=nx*nx+ny*ny<=1?0:5;
    }
    VkCommandPoolCreateInfo pi={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=c->qfam;
    if(vkCreateCommandPool(c->dev,&pi,NULL,&pool)!=VK_SUCCESS)goto fail;
    VkCommandBufferAllocateInfo ai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
    if(vkAllocateCommandBuffers(c->dev,&ai,&cb)!=VK_SUCCESS)goto fail;
    VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if(vkBeginCommandBuffer(cb,&bi)!=VK_SUCCESS)goto fail;
    nvk_image_to(cb,im,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy copy={0};copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;copy.imageSubresource.layerCount=1;
    copy.imageExtent=(VkExtent3D){w,h,1};
    vkCmdCopyBufferToImage(cb,upload.buf,im->img,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    nvk_image_to(cb,im,VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR);
    if(vkEndCommandBuffer(cb)!=VK_SUCCESS)goto fail;
    VkFenceCreateInfo fi={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};if(vkCreateFence(c->dev,&fi,NULL,&fence)!=VK_SUCCESS)goto fail;
    VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cb;
    nvk_queue_lock();VkResult r=vkQueueSubmit(c->queue,1,&si,fence);nvk_queue_unlock();
    if(r!=VK_SUCCESS)goto fail;submitted=1;
    int outcome=nvk_wait_fence(NVK_WAIT_UPLOAD,"immutable Quest shading-rate map",fence,&r);
    if(outcome!=NVK_WAIT_OK){nvk_wait_fatal(outcome,r,"Quest shading-rate map upload");return NULL;}
    /* Immutable attachment, uploaded and made visible before rendering. */
    vkDestroyFence(c->dev,fence,NULL);vkDestroyCommandPool(c->dev,pool,NULL);nvk_buffer_free(&upload);
    fprintf(stderr,"[QUEST-FFR] ready %ux%u map for %ux%u world, full center / 2x2 edges; HUD full rate\n",w,h,width,height);
    return im;
fail:
    if(!submitted){if(fence)vkDestroyFence(c->dev,fence,NULL);if(pool)vkDestroyCommandPool(c->dev,pool,NULL);
        nvk_buffer_free(&upload);nvk_image_free(im);}
    fprintf(stderr,"[QUEST-FFR] map unavailable for %ux%u: using full rate\n",width,height);
    return NULL;
}
