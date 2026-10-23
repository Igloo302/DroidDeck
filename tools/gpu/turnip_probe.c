#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef TURNIP_DIRECT_ICD
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#ifdef TURNIP_DIRECT_ICD
#include "turnip_direct_icd.h"
#endif

#define CHECK(call) do { VkResult r = (call); printf("%s: %d\n", #call, r); fflush(stdout); if (r != VK_SUCCESS) return 1; } while (0)
#define WORDS 4096
#include "turnip_triangle.h"

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
#ifdef TURNIP_DIRECT_ICD
    if (open_icd()) return 1;
#endif
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "DroidDeck Turnip probe", .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo create = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance instance;
    CHECK(vkCreateInstance(&create, NULL, &instance));
    #ifdef TURNIP_DIRECT_ICD
    if (bind_icd(instance)) return 1;
    #endif
    uint32_t count = 1;
    VkPhysicalDevice gpu;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, &gpu));
    if (count != 1) return 1;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(gpu, &props);
    printf("device=%s id=0x%x driver=0x%x\n", props.deviceName, props.deviceID, props.driverVersion);
    #ifdef TURNIP_DIRECT_ICD
    if (strcmp(props.deviceName, "Adreno (TM) 831 (experimental)") != 0) return 1;
    #endif
    if (argc == 1) { vkDestroyInstance(instance, NULL); return 0; }
    if (argc != 2 && !(argc == 4 && strcmp(argv[1], "--triangle") == 0)) return 2;

    VkQueueFamilyProperties queues[32];
    count = 32;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, queues);
    uint32_t family;
    for (family = 0; family < count; ++family)
        if (queues[family].queueFlags & VK_QUEUE_COMPUTE_BIT) break;
    if (family == count) return 1;
    float priority = 0.5f;
    VkDeviceQueueCreateInfo queueInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    VkDeviceCreateInfo deviceInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &queueInfo};
    VkDevice device;
    CHECK(vkCreateDevice(gpu, &deviceInfo, NULL, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    VkDeviceSize readbackBytes = argc == 4 ? (VkDeviceSize)TRIANGLE_WIDTH * TRIANGLE_HEIGHT * 4 : WORDS * sizeof(uint32_t);
    VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = readbackBytes, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buffer;
    CHECK(vkCreateBuffer(device, &bufferInfo, NULL, &buffer));
    VkMemoryRequirements requirements;
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    vkGetPhysicalDeviceMemoryProperties(gpu, &memoryProperties);
    uint32_t memoryType;
    for (memoryType = 0; memoryType < memoryProperties.memoryTypeCount; ++memoryType) {
        VkMemoryPropertyFlags needed = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((requirements.memoryTypeBits & (1u << memoryType)) && (memoryProperties.memoryTypes[memoryType].propertyFlags & needed) == needed) break;
    }
    if (memoryType == memoryProperties.memoryTypeCount) return 1;
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = requirements.size, .memoryTypeIndex = memoryType};
    VkDeviceMemory memory;
    CHECK(vkAllocateMemory(device, &allocation, NULL, &memory));
    CHECK(vkBindBufferMemory(device, buffer, memory, 0));
    uint32_t *mapped;
    CHECK(vkMapMemory(device, memory, 0, requirements.size, 0, (void **)&mapped));
    memset(mapped, 0, (size_t)readbackBytes);
    if (argc == 4) {
        int result = triangle(device, gpu, queue, family, buffer, mapped, argv[2], argv[3]);
        vkUnmapMemory(device, memory);
        vkDestroyBuffer(device, buffer, NULL);
        vkFreeMemory(device, memory, NULL);
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        return result;
    }

    FILE *file = fopen(argv[1], "rb");
    if (!file) { perror("shader"); return 1; }
    if (fseek(file, 0, SEEK_END) != 0) return 1;
    long length = ftell(file);
    if (length <= 0 || length > 1024 * 1024 || length % 4 != 0) return 1;
    rewind(file);
    uint32_t *code = malloc((size_t)length);
    if (!code || fread(code, 1, (size_t)length, file) != (size_t)length) return 1;
    fclose(file);
    VkShaderModuleCreateInfo shaderInfo = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = (size_t)length, .pCode = code};
    VkShaderModule shader;
    CHECK(vkCreateShaderModule(device, &shaderInfo, NULL, &shader));
    free(code);
    VkDescriptorSetLayoutBinding binding = {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
    VkDescriptorSetLayoutCreateInfo layoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &binding};
    VkDescriptorSetLayout layout;
    CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, NULL, &layout));
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &layout};
    VkPipelineLayout pipelineLayout;
    CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, NULL, &pipelineLayout));
    VkComputePipelineCreateInfo pipelineInfo = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = shader, .pName = "main"}, .layout = pipelineLayout};
    VkPipeline pipeline;
    CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline));
    VkDescriptorPoolSize poolSize = {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1};
    VkDescriptorPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &poolSize};
    VkDescriptorPool pool;
    CHECK(vkCreateDescriptorPool(device, &poolInfo, NULL, &pool));
    VkDescriptorSetAllocateInfo setInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &layout};
    VkDescriptorSet set;
    CHECK(vkAllocateDescriptorSets(device, &setInfo, &set));
    VkDescriptorBufferInfo descriptor = {.buffer = buffer, .offset = 0, .range = bufferInfo.size};
    VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &descriptor};
    vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
    VkCommandPoolCreateInfo commandsInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
    VkCommandPool commands;
    CHECK(vkCreateCommandPool(device, &commandsInfo, NULL, &commands));
    VkCommandBufferAllocateInfo commandInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = commands, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer command;
    CHECK(vkAllocateCommandBuffers(device, &commandInfo, &command));
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(command, &begin));
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, NULL);
    vkCmdDispatch(command, WORDS / 64, 1, 1);
    VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    CHECK(vkEndCommandBuffer(command));
    VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    CHECK(vkCreateFence(device, &fenceInfo, NULL, &fence));
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command};
    CHECK(vkQueueSubmit(queue, 1, &submit, fence));
    VkResult waited = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(2000000000));
    printf("fence=%d\n", waited);
    if (waited != VK_SUCCESS) _Exit(124);
    uint32_t errors = 0;
    for (uint32_t i = 0; i < WORDS; ++i) {
        uint32_t expected = i * UINT32_C(1664525) + UINT32_C(1013904223);
        if (mapped[i] != expected) {
            if (errors < 4) printf("mismatch[%u]: got=0x%x expected=0x%x\n", i, mapped[i], expected);
            ++errors;
        }
    }
    printf("COMPUTE_READBACK words=%u errors=%u\n", WORDS, errors);
    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, commands, NULL);
    vkDestroyDescriptorPool(device, pool, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, pipelineLayout, NULL);
    vkDestroyDescriptorSetLayout(device, layout, NULL);
    vkDestroyShaderModule(device, shader, NULL);
    vkUnmapMemory(device, memory);
    vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, memory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return errors ? 1 : 0;
}
