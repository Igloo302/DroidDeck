"""Compile the real patched guard and verify chip/process scope and patch integrity."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from patch_aura_compositor_cache import patch


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mesa", type=Path)
    args = parser.parse_args()
    text = (args.mesa / "src/freedreno/vulkan/tu_cmd_buffer.cc").read_text()
    assert patch(text) == text
    try:
        patch(text + "\n")
        raise AssertionError("Source drift was accepted")
    except ValueError:
        pass
    start = text.index("   static const bool aura_compositor =")
    end = text.index("   if (TU_DEBUG(SYNCDRAW))", start)
    guard = text[start:end]
    harness = r'''
#include <cassert>
#include <cstring>
#include <cstdint>
static const char *process;
static bool debug;
static const char *util_get_process_name() { return process; }
#define TU_DEBUG(x) debug
constexpr int A8XX=8;
constexpr unsigned TU_CMD_FLAG_ALL_CLEAN=0x13, TU_CMD_FLAG_ALL_INVALIDATE=0x24;
struct physical { struct { uint64_t chip_id; } dev_id; };
struct device { physical *physical_device; };
struct command { device *device; };
template<int CHIP> unsigned test(command *cmd_buffer) {
    unsigned flushes=0x80;
''' + guard + r'''
    return flushes;
}
int main(int argc, char **argv) {
    assert(argc==2); process=argv[1]; physical p{{0x44050100}}; device d{&p}; command c{&d};
    unsigned expected=!strcmp(process,"vrcompositor") ? 0xb7 : 0x80;
    assert(test<8>(&c)==expected);
    p.dev_id.chip_id=0x44050001;assert(test<8>(&c)==0x80);
    p.dev_id.chip_id=0x44050100;assert(test<7>(&c)==0x80);
    debug=true;assert(test<8>(&c)==0xb7);assert(test<7>(&c)==0xb7);
}
'''
    with tempfile.TemporaryDirectory(prefix="aura-cache-scope-") as folder:
        source = Path(folder) / "test.cpp"
        exe = Path(folder) / "test"
        source.write_text(harness.replace('device *device;', 'struct device *device;'))
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(exe)], check=True)
        for process in ["vrcompositor", "AirCar-Win64-Shipping.exe", "vrserver", "steamwebhelper"]:
            subprocess.run([str(exe), process], check=True)
    print("Cache scope passed: exact chip/compositor only; explicit debug behavior retained; source drift rejected")


if __name__ == "__main__":
    main()
