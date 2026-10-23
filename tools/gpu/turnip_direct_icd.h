#include <dlfcn.h>
#ifdef __ANDROID__
#include <hardware/hwvulkan.h>
#endif

#define PROBE_FUNCTIONS(X) \
    X(vkAllocateCommandBuffers) \
    X(vkAllocateDescriptorSets) \
    X(vkAllocateMemory) \
    X(vkBeginCommandBuffer) \
    X(vkBindBufferMemory) \
    X(vkBindImageMemory) \
    X(vkCmdBeginRenderPass) \
    X(vkCmdBindDescriptorSets) \
    X(vkCmdBindPipeline) \
    X(vkCmdCopyImageToBuffer) \
    X(vkCmdDispatch) \
    X(vkCmdDraw) \
    X(vkCmdEndRenderPass) \
    X(vkCmdPipelineBarrier) \
    X(vkCreateBuffer) \
    X(vkCreateCommandPool) \
    X(vkCreateComputePipelines) \
    X(vkCreateDescriptorPool) \
    X(vkCreateDescriptorSetLayout) \
    X(vkCreateDevice) \
    X(vkCreateFence) \
    X(vkCreateFramebuffer) \
    X(vkCreateGraphicsPipelines) \
    X(vkCreateImage) \
    X(vkCreateImageView) \
    X(vkCreatePipelineLayout) \
    X(vkCreateRenderPass) \
    X(vkCreateShaderModule) \
    X(vkDestroyBuffer) \
    X(vkDestroyCommandPool) \
    X(vkDestroyDescriptorPool) \
    X(vkDestroyDescriptorSetLayout) \
    X(vkDestroyDevice) \
    X(vkDestroyFence) \
    X(vkDestroyFramebuffer) \
    X(vkDestroyImage) \
    X(vkDestroyImageView) \
    X(vkDestroyInstance) \
    X(vkDestroyPipeline) \
    X(vkDestroyPipelineLayout) \
    X(vkDestroyRenderPass) \
    X(vkDestroyShaderModule) \
    X(vkEndCommandBuffer) \
    X(vkEnumeratePhysicalDevices) \
    X(vkFreeMemory) \
    X(vkGetBufferMemoryRequirements) \
    X(vkGetDeviceQueue) \
    X(vkGetImageMemoryRequirements) \
    X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkMapMemory) \
    X(vkQueueSubmit) \
    X(vkUnmapMemory) \
    X(vkUpdateDescriptorSets) \
    X(vkWaitForFences)
#define DECLARE(name) static PFN_##name name;
PROBE_FUNCTIONS(DECLARE)
#undef DECLARE
static PFN_vkCreateInstance vkCreateInstance;
static PFN_vkGetInstanceProcAddr probeGipa;

static int open_icd(void) {
    const char *path = getenv("DROIDDECK_PROBE_ICD");
    if (!path || !*path) { fputs("Explicit probe ICD path required\n", stderr); return 1; }
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "ICD dlopen: %s\n", dlerror()); return 1; }
    probeGipa = (PFN_vkGetInstanceProcAddr)dlsym(library, "vk_icdGetInstanceProcAddr");
#ifdef __ANDROID__
    if (!probeGipa) {
        hwvulkan_module_t *module = (hwvulkan_module_t *)dlsym(library, "HMI");
        hw_device_t *device = NULL;
        if (!module || !module->common.methods ||
            module->common.methods->open(&module->common, HWVULKAN_DEVICE_0, &device) != 0 || !device) {
            fputs("Cannot open Android Vulkan HAL\n", stderr); return 1;
        }
        probeGipa = ((hwvulkan_device_t *)device)->GetInstanceProcAddr;
    }
#endif
    if (!probeGipa) { fputs("Missing ICD entry point\n", stderr); return 1; }
    vkCreateInstance = (PFN_vkCreateInstance)probeGipa(VK_NULL_HANDLE, "vkCreateInstance");
    return vkCreateInstance == NULL;
}

static int bind_icd(VkInstance instance) {
#define BIND(name) name = (PFN_##name)probeGipa(instance, #name); if (!name) { fprintf(stderr, "Missing %s\n", #name); return 1; }
    PROBE_FUNCTIONS(BIND)
#undef BIND
    return 0;
}
