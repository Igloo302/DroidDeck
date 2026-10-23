"""Compile the patched Mesa WSI function against synchronization boundary stubs."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mesa", type=Path)
    args = parser.parse_args()
    source = (args.mesa / "src/vulkan/wsi/wsi_common_drm.c").read_text()
    start = source.index("VkResult\nwsi_signal_dma_buf_from_semaphore(")
    end = source.index("\nstatic const struct vk_sync_type", start)
    function = source[start:end]
    header = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef int VkResult;
enum { VK_SUCCESS=0, VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR=1,
       VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT=16 };
typedef struct { int sType; const void *pNext; uint64_t semaphore; int handleType; } VkSemaphoreGetFdInfoKHR;
static int exported_fd, export_result, import_result, imports, closes;
static VkResult get_fd(uint64_t device, const VkSemaphoreGetFdInfoKHR *info, int *fd) {
    assert(device == 41 && info->semaphore == 42 && info->handleType == 16);
    *fd = exported_fd; return export_result;
}
struct wsi_device { VkResult (*GetSemaphoreFdKHR)(uint64_t, const VkSemaphoreGetFdInfoKHR *, int *); };
struct wsi_swapchain { struct wsi_device *wsi; uint64_t device, dma_buf_semaphore; };
struct wsi_image { int dma_buf_fd; };
static VkResult wsi_dma_buf_import_sync_file(int dma, int fd) {
    assert(dma == 23 && fd == exported_fd); imports++; return import_result;
}
static int close(int fd) { assert(fd == exported_fd && fd >= 0); closes++; return 0; }
'''
    test = r'''
int main(void) {
    struct wsi_device device = {get_fd};
    struct wsi_swapchain chain = {&device, 41, 42};
    struct wsi_image image = {23};
    exported_fd = -1;
    assert(wsi_signal_dma_buf_from_semaphore(&chain, &image) == VK_SUCCESS);
    assert(imports == 0 && closes == 0);
    for (int i = 0; i < 2; i++) {
        exported_fd = i ? 17 : 0;
        assert(wsi_signal_dma_buf_from_semaphore(&chain, &image) == VK_SUCCESS);
        assert(imports == i + 1 && closes == i + 1);
    }
    exported_fd = 19; import_result = -3;
    assert(wsi_signal_dma_buf_from_semaphore(&chain, &image) == -3);
    assert(imports == 3 && closes == 3);
    exported_fd = -1; export_result = -4;
    assert(wsi_signal_dma_buf_from_semaphore(&chain, &image) == -4);
    assert(imports == 3 && closes == 3);
    puts("WSI sync FD boundary regression passed (signaled, fd0, pending, import error, export error)");
}
'''
    with tempfile.TemporaryDirectory(prefix="aura-wsi-test-") as temp:
        c = Path(temp) / "test.c"
        exe = Path(temp) / "test"
        c.write_text(header + function + test)
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(c), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
