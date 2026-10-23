#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

struct kgsl_devinfo {
    unsigned device_id, chip_id, mmu_enabled;
    unsigned long gmem_gpubaseaddr;
    unsigned gpu_id;
    size_t gmem_sizebytes;
};

struct kgsl_property {
    unsigned type;
    void *value;
    size_t size;
};

static int property(int fd, unsigned type, void *value, size_t size) {
    struct kgsl_property query = {type, value, size};
    return ioctl(fd, _IOWR(0x09, 0x02, struct kgsl_property), &query);
}

int main(void) {
    int fd = open("/dev/kgsl-3d0", O_RDWR | O_CLOEXEC);
    printf("kgsl.open errno=%d\n", fd < 0 ? errno : 0);
    if (fd >= 0) {
        struct kgsl_devinfo info = {0};
        int rc = property(fd, 1, &info, sizeof(info));
        printf("kgsl.device rc=%d errno=%d chip=0x%08x gpu=%u gmem=%zu mmu=%u\n",
               rc, rc ? errno : 0, info.chip_id, info.gpu_id, info.gmem_sizebytes, info.mmu_enabled);
        const unsigned properties[] = {0x17, 0x18, 0x1a, 0x1b, 0x25, 0x2a, 0x2b};
        for (unsigned i = 0; i < sizeof(properties) / sizeof(properties[0]); ++i) {
            uint32_t value = 0;
            rc = property(fd, properties[i], &value, sizeof(value));
            printf("kgsl.property=0x%x rc=%d errno=%d value=0x%x\n",
                   properties[i], rc, rc ? errno : 0, value);
        }
        close(fd);
    }
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "DroidDeck GPU probe", .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo create = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance instance;
    VkResult result = vkCreateInstance(&create, NULL, &instance);
    printf("vulkan.instance result=%d\n", result);
    if (result != VK_SUCCESS) return 1;
    uint32_t count = 8;
    VkPhysicalDevice devices[8];
    result = vkEnumeratePhysicalDevices(instance, &count, devices);
    printf("vulkan.devices result=%d count=%u\n", result, count);
    for (uint32_t i = 0; result == VK_SUCCESS && i < count; ++i) {
        VkPhysicalDeviceProperties props;
        VkPhysicalDeviceMemoryProperties memory;
        vkGetPhysicalDeviceProperties(devices[i], &props);
        vkGetPhysicalDeviceMemoryProperties(devices[i], &memory);
        printf("vulkan.device=%s vendor=0x%x device=0x%x driver=0x%x api=%u.%u.%u\n",
               props.deviceName, props.vendorID, props.deviceID, props.driverVersion,
               VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion));
        printf("vulkan.compute shared=%u invocations=%u\n", props.limits.maxComputeSharedMemorySize, props.limits.maxComputeWorkGroupInvocations);
        for (uint32_t j = 0; j < memory.memoryHeapCount; ++j)
            printf("vulkan.heap=%u size=%llu flags=0x%x\n", j, (unsigned long long)memory.memoryHeaps[j].size, memory.memoryHeaps[j].flags);
    }
    vkDestroyInstance(instance, NULL);
    return result == VK_SUCCESS && count > 0 ? 0 : 1;
}
