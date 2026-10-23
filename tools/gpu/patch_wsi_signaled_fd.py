"""Handle KGSL's already-signaled sync FD in the pinned Mesa WSI source."""
import argparse
import hashlib
from pathlib import Path

ORIGINAL_SHA256 = "7cc381755a3aa52a307d3570b664f14f09082ccb7e99f1587862c82a6ce78779"
PATCHED_SHA256 = "4b7b6eb40331b896e0226f3587c08861eb7d4ec70badbe9c7c5fcaca9e7995be"
ANCHOR = """   result = wsi_dma_buf_import_sync_file(image->dma_buf_fd, sync_file_fd);
   close(sync_file_fd);
   return result;
"""
REPLACEMENT = """   if (sync_file_fd == -1)
      return VK_SUCCESS;

""" + ANCHOR


def patch(text):
    digest = hashlib.sha256(text.encode()).hexdigest()
    if digest == PATCHED_SHA256:
        return text
    if digest != ORIGINAL_SHA256 or text.count(ANCHOR) != 1:
        raise ValueError("Expected the pinned DD-Turnip v0.3.1 WSI source")
    return text.replace(ANCHOR, REPLACEMENT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mesa", type=Path)
    args = parser.parse_args()
    target = args.mesa / "src/vulkan/wsi/wsi_common_drm.c"
    before = target.read_text()
    after = patch(before)
    if after != before:
        target.write_text(after)
    print("WSI signaled-sync-FD patch ready:", hashlib.sha256(after.encode()).hexdigest())


if __name__ == "__main__":
    main()
