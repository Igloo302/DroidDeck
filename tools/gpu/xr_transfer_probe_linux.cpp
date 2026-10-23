#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <cstdio>
#include <vector>
#include "../../tools/vr/src/xr_gpu_channel.h"

#define FUNCTIONS(X) X(CreateInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) X(GetPhysicalDeviceQueueFamilyProperties) X(GetPhysicalDeviceMemoryProperties) X(CreateDevice) X(GetDeviceQueue) X(CreateBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(BindBufferMemory) X(CreateCommandPool) X(AllocateCommandBuffers) X(ResetCommandBuffer) X(BeginCommandBuffer) X(CmdPipelineBarrier) X(CmdUpdateBuffer) X(CreateImage) X(GetImageMemoryRequirements) X(BindImageMemory) X(CmdCopyBufferToImage) X(CmdCopyImageToBuffer) X(DestroyImage) X(EndCommandBuffer) X(CreateFence) X(ResetFences) X(QueueSubmit) X(WaitForFences) X(DestroyFence) X(DestroyCommandPool) X(DestroyBuffer) X(FreeMemory) X(DestroyDevice) X(DestroyInstance)
#define DECLARE(n) PFN_vk##n vk##n{};
FUNCTIONS(DECLARE)
PFN_vkGetMemoryFdPropertiesKHR vkGetMemoryFdPropertiesKHR{};
#undef DECLARE
#define CHECK(call) do { auto r=(call); if(r) {std::printf("FAILED %s result=%d\n",#call,int(r)); return 1;} } while(0)
int main(int argc,char **argv) {
    if(argc!=4 && argc!=5) return 2;
    bool imageCopy=argc==5 && std::string(argv[4])=="--image";
    unsigned width=std::stoul(argv[2]),height=std::stoul(argv[3]);
    if(!xr_gpu_dimensions(width,height)) return 2;
    void *loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL); if(!loader) return 3;
#define LOAD(n) vk##n=reinterpret_cast<PFN_vk##n>(dlsym(loader,"vk" #n)); if(!vk##n) {std::printf("Missing vk%s\n",#n);return 4;}
    FUNCTIONS(LOAD)
#undef LOAD
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ic.pApplicationInfo=&app;
    VkInstance instance{}; CHECK(vkCreateInstance(&ic,nullptr,&instance));
    unsigned count=1; VkPhysicalDevice gpu{}; CHECK(vkEnumeratePhysicalDevices(instance,&count,&gpu));
    VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(gpu,&props); std::printf("device=%s\n",props.deviceName);
    count=0; vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,nullptr);
    std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,families.data());
    unsigned family=0; while(family<count && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)) family++; if(family==count)return 5;
    float priority=.5f; VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qc.queueFamilyIndex=family;qc.queueCount=1;qc.pQueuePriorities=&priority;
    const char *extensions[]{"VK_KHR_external_memory_fd","VK_EXT_external_memory_dma_buf","VK_EXT_queue_family_foreign"};
    VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&qc;dc.enabledExtensionCount=3;dc.ppEnabledExtensionNames=extensions;
    VkDevice device{};CHECK(vkCreateDevice(gpu,&dc,nullptr,&device));
    auto gdpa=reinterpret_cast<PFN_vkGetDeviceProcAddr>(dlsym(loader,"vkGetDeviceProcAddr"));
    vkGetMemoryFdPropertiesKHR=reinterpret_cast<PFN_vkGetMemoryFdPropertiesKHR>(gdpa(device,"vkGetMemoryFdPropertiesKHR")); if(!vkGetMemoryFdPropertiesKHR)return 4;
    VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);
    XrGpuSender sender;XrGpuPoolInfo pool;std::array<int,3> fds{{-1,-1,-1}};
    if(!sender.Connect(argv[1],width,height,pool,fds))return 6;
    VkPhysicalDeviceMemoryProperties mp{};vkGetPhysicalDeviceMemoryProperties(gpu,&mp);
    VkBuffer buffers[3]{};VkDeviceMemory memories[3]{};
    for(unsigned i=0;i<3;i++) {
        VkExternalMemoryBufferCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};external.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.pNext=&external;bc.size=uint64_t(width)*height*4;bc.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        CHECK(vkCreateBuffer(device,&bc,nullptr,&buffers[i]));VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,buffers[i],&req);
        VkMemoryFdPropertiesKHR fp{VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};CHECK(vkGetMemoryFdPropertiesKHR(device,VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,fds[i],&fp));
        unsigned type=0;while(type<mp.memoryTypeCount && !((req.memoryTypeBits&fp.memoryTypeBits)&(1u<<type)))type++;if(type==mp.memoryTypeCount)return 7;
        VkImportMemoryFdInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};import.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;import.fd=fds[i];
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.pNext=&import;alloc.allocationSize=pool.sizes[i];alloc.memoryTypeIndex=type;
        CHECK(vkAllocateMemory(device,&alloc,nullptr,&memories[i]));CHECK(vkBindBufferMemory(device,buffers[i],memories[i],0));
    }
    VkBuffer source{}; VkDeviceMemory sourceMemory{},imageMemory{}; VkImage image{};
    if(imageCopy) {
        VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=uint64_t(width)*height*4;bc.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        CHECK(vkCreateBuffer(device,&bc,nullptr,&source));VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,source,&req);
        unsigned type=0;while(type<mp.memoryTypeCount && !(req.memoryTypeBits&(1u<<type)))type++;
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=req.size;alloc.memoryTypeIndex=type;
        CHECK(vkAllocateMemory(device,&alloc,nullptr,&sourceMemory));CHECK(vkBindBufferMemory(device,source,sourceMemory,0));
        VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ic.imageType=VK_IMAGE_TYPE_2D;ic.format=VK_FORMAT_B8G8R8A8_UNORM;ic.extent={width,height,1};ic.mipLevels=ic.arrayLayers=1;ic.samples=VK_SAMPLE_COUNT_1_BIT;ic.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        CHECK(vkCreateImage(device,&ic,nullptr,&image));vkGetImageMemoryRequirements(device,image,&req);
        type=0;while(type<mp.memoryTypeCount && !(req.memoryTypeBits&(1u<<type)))type++;
        alloc.allocationSize=req.size;alloc.memoryTypeIndex=type;CHECK(vkAllocateMemory(device,&alloc,nullptr,&imageMemory));CHECK(vkBindImageMemory(device,image,imageMemory,0));
    }
    VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pc.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pc.queueFamilyIndex=family;
    VkCommandPool cp{};CHECK(vkCreateCommandPool(device,&pc,nullptr,&cp));
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=cp;ca.commandBufferCount=1;
    VkCommandBuffer cmd{};CHECK(vkAllocateCommandBuffers(device,&ca,&cmd));VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence{};CHECK(vkCreateFence(device,&fc,nullptr,&fence));
    std::vector<uint32_t> pixels(width*height);
    for(unsigned frame=1;frame<=24;frame++) {
        unsigned slot=(frame-1)%3;bool bgra=frame%2==0;
        for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++) {
            uint32_t n=x*13+y*37+frame*17;
            uint32_t word=0xff000000u|((n&255u)<<16)|(((n>>3)&255u)<<8)|((n>>7)&255u);
            pixels[y*width+x]=bgra ? (word&0xff00ff00u)|((word&255u)<<16)|((word>>16)&255u) : word;
        }
        CHECK(vkResetCommandBuffer(cmd,0));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&begin));
        VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};b.srcQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT;b.dstQueueFamilyIndex=family;b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.buffer=buffers[slot];b.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&b,0,nullptr);
        for(size_t offset=0;offset<pixels.size();offset+=16384) {
            size_t words=std::min<size_t>(16384,pixels.size()-offset);vkCmdUpdateBuffer(cmd,imageCopy?source:buffers[slot],offset*4,words*4,pixels.data()+offset);
        }
        if(imageCopy) {
            VkBufferMemoryBarrier src{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};src.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;src.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;src.srcQueueFamilyIndex=src.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;src.buffer=source;src.size=VK_WHOLE_SIZE;
            VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};ib.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;ib.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib.srcQueueFamilyIndex=ib.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;ib.image=image;ib.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&src,1,&ib);
            VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};
            vkCmdCopyBufferToImage(cmd,source,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
            ib.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;ib.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;ib.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&ib);
            vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffers[slot],1,&copy);
        }
        b.srcQueueFamilyIndex=family;b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=0;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,1,&b,0,nullptr);
        CHECK(vkEndCommandBuffer(cmd));CHECK(vkResetFences(device,1,&fence));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
        CHECK(vkQueueSubmit(queue,1,&submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,2000000000LL));
        DroidDeckXrFrameHeader h{};h.magic=DROIDDECK_XR_FRAME_MAGIC;h.version=DROIDDECK_XR_FRAME_VERSION;h.width=width;h.height=height;h.bytes=width*height*4;h.color_space=DROIDDECK_XR_COLOR_LINEAR_RGBA8;h.frame_id=frame;h.captured_ns=xr_frame_now();
        for(unsigned eye=0;eye<2;eye++){for(unsigned axis=0;axis<3;axis++)h.eye_pose[eye][axis][axis]=1;h.projection[eye][0][0]=h.projection[eye][1][1]=1;h.projection[eye][3][2]=-1;}
        if(!sender.Send(h,slot,bgra?XR_GPU_BGRA:0))return 8;
    }
    sender.Close();vkDestroyFence(device,fence,nullptr);vkDestroyCommandPool(device,cp,nullptr);
    for(unsigned i=0;i<3;i++){vkDestroyBuffer(device,buffers[i],nullptr);vkFreeMemory(device,memories[i],nullptr);}
    if(imageCopy){vkDestroyBuffer(device,source,nullptr);vkFreeMemory(device,sourceMemory,nullptr);vkDestroyImage(device,image,nullptr);vkFreeMemory(device,imageMemory,nullptr);}
    vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);dlclose(loader);std::puts("SENDER_RESULT passed=1");return 0;
}
