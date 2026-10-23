"""Scope the verified cache-maintenance workaround to AURA's SteamVR compositor."""
import argparse
import hashlib
from pathlib import Path

ORIGINAL_SHA256 = "3fab32c0f6ff48f9b10085f7d76312fd830bca560d44a61c24bac3fb73b1c012"
PATCHED_SHA256 = "df8f73f2d1022ad916d4d294c1486f22e447f7995036f4dec915695fc27b4665"
ANCHOR = """   if (TU_DEBUG(FLUSHALL))
      flushes |= TU_CMD_FLAG_ALL_CLEAN | TU_CMD_FLAG_ALL_INVALIDATE;
"""
REPLACEMENT = """   static const bool aura_compositor = !strcmp(util_get_process_name(), "vrcompositor");
   if (TU_DEBUG(FLUSHALL) ||
       (CHIP == A8XX &&
        cmd_buffer->device->physical_device->dev_id.chip_id == 0x44050100 &&
        aura_compositor))
      flushes |= TU_CMD_FLAG_ALL_CLEAN | TU_CMD_FLAG_ALL_INVALIDATE;
"""


def patch(text):
    digest = hashlib.sha256(text.encode()).hexdigest()
    if digest == PATCHED_SHA256:
        return text
    if digest != ORIGINAL_SHA256 or text.count(ANCHOR) != 1:
        raise ValueError("Expected the pinned DD-Turnip v0.3.1 command-buffer source")
    return text.replace('#include "tu_cmd_buffer.h"',
                        '#include "tu_cmd_buffer.h"\n#include "util/u_process.h"', 1).replace(ANCHOR, REPLACEMENT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mesa", type=Path)
    args = parser.parse_args()
    target = args.mesa / "src/freedreno/vulkan/tu_cmd_buffer.cc"
    before = target.read_text()
    after = patch(before)
    if after != before:
        target.write_text(after)
    print("AURA compositor cache patch ready:", hashlib.sha256(after.encode()).hexdigest())


if __name__ == "__main__":
    main()
