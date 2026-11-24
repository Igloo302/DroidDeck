from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CLOCK_SOURCE = ROOT / "app/src/main/cpp/waylandcomp/src/clock_domain.c"
XR_HEADERS = ROOT / "tools/vr/src"

HARNESS = r'''
#include <cassert>
#include <cmath>
#include <string>
#include <sys/syscall.h>
#include "xr_gpu_channel.h"
extern "C" {
#include "clock_domain.h"
}

extern "C" int clock_gettime(clockid_t clock, timespec *value) noexcept {
    int result = static_cast<int>(syscall(SYS_clock_gettime, clock, value));
    // A newly booted test VM must still have positive time after a 30-second pause offset.
    if (result == 0 && clock == CLOCK_MONOTONIC) value->tv_sec += 60;
    return result;
}

static int guest_clock_gettime(clockid_t clock, timespec *value) {
    int result = clock_gettime(clock, value);
    if (result == 0 && clock == CLOCK_MONOTONIC) {
        int64_t now = droiddeck_clock_domain_adjust_ns(
            int64_t(value->tv_sec) * 1000000000LL + value->tv_nsec);
        value->tv_sec = now / 1000000000LL;
        value->tv_nsec = now % 1000000000LL;
    }
    return result;
}

#define clock_gettime guest_clock_gettime
#include "xr_tracking_file.h"
#undef clock_gettime

static void set_shift(int descriptor, int64_t shift) {
    assert(pwrite(descriptor, &shift, sizeof(shift), 0) == sizeof(shift));
}

static DroidDeckXrFrameHeader frame(uint64_t id, int64_t captured) {
    DroidDeckXrFrameHeader header{};
    header.magic = DROIDDECK_XR_FRAME_MAGIC;
    header.version = DROIDDECK_XR_FRAME_VERSION;
    header.width = 8;
    header.height = 4;
    header.bytes = header.width * header.height * 4;
    header.color_space = DROIDDECK_XR_COLOR_SRGB_RGBA8;
    header.frame_id = id;
    header.captured_ns = captured;
    for (int eye = 0; eye < 2; eye++) {
        for (int axis = 0; axis < 3; axis++) header.eye_pose[eye][axis][axis] = 1;
        header.projection[eye][0][0] = header.projection[eye][1][1] = 1;
        header.projection[eye][3][2] = -1;
    }
    assert(xr_frame_valid(header));
    return header;
}

static void tracking_and_gpu(int control) {
    for (int64_t shift : {0LL, 10000000000LL, 30000000000LL}) {
        set_shift(control, shift);
        const int64_t host_now = xr_frame_now();
        const int64_t guest_now = droiddeck_clock_domain_adjust_ns(host_now);
        assert(guest_now == host_now - shift);
        DroidDeckXrTracking packet{};
        packet.active = 1;
        packet.sampled_ns = guest_now;
        packet.predicted_ns = droiddeck_clock_domain_adjust_ns(host_now + 20000000LL);
        packet.period_ns = 13888889;
        for (auto &eye : packet.eyes) eye.width = eye.height = 1200;
        assert(XrTrackingFile::Fresh(packet));
        assert(std::abs(XrTrackingFile::PoseTimeOffset(packet.predicted_ns, guest_now) - 0.02) < 1e-9);
        if (shift) {
            packet.sampled_ns = host_now;
            assert(!XrTrackingFile::Fresh(packet));
        }
        packet.sampled_ns = guest_now - 1000000000LL;
        assert(!XrTrackingFile::Fresh(packet));
        packet.sampled_ns = guest_now + 1000000000LL;
        assert(!XrTrackingFile::Fresh(packet));

        XrGpuFrame gpu;
        gpu.header = frame(1, guest_now);
        assert(xr_gpu_frame_state(gpu, 8, 4, 0, guest_now) == XrGpuFrameState::Ready);
        if (shift) assert(xr_gpu_frame_state(gpu, 8, 4, 0, host_now) == XrGpuFrameState::Stale);
        gpu.header.captured_ns = guest_now - 1000000000LL;
        assert(xr_gpu_frame_state(gpu, 8, 4, 0, guest_now) == XrGpuFrameState::Stale);
        gpu.header.captured_ns = guest_now + 1000000000LL;
        assert(xr_gpu_frame_state(gpu, 8, 4, 0, guest_now) == XrGpuFrameState::Stale);
        gpu.header.width = 10;
        assert(xr_gpu_frame_state(gpu, 8, 4, 0, guest_now) == XrGpuFrameState::Invalid);
    }
}

static void accepted(XrFrameReceiver &receiver, XrFrameSender &sender,
                     const std::string &socket, uint64_t id, int64_t captured) {
    auto header = frame(id, captured);
    std::vector<unsigned char> pixels(header.bytes, static_cast<unsigned char>(id));
    assert(sender.Send(socket, header, pixels.data()));
    DroidDeckXrPixels received;
    assert(receiver.Take(&received));
    assert(received.header.frame_id == id);
    assert(received.header.captured_ns == captured);
    assert(received.pixels == pixels);
    assert(!receiver.Take(&received));
}

static void receiver_resume(int control, const std::string &socket) {
    set_shift(control, 10000000000LL);
    XrFrameReceiver receiver;
    assert(receiver.Start(socket, droiddeck_clock_domain_now_ns));
    XrFrameSender sender;
    accepted(receiver, sender, socket, 1, droiddeck_clock_domain_now_ns());
    auto in_flight = frame(2, droiddeck_clock_domain_now_ns());
    set_shift(control, 30000000000LL);
    std::vector<unsigned char> pixels(in_flight.bytes, 99);
    assert(sender.Send(socket, in_flight, pixels.data()));
    DroidDeckXrPixels received;
    assert(!receiver.Take(&received));
    assert(receiver.Received() == 1);
    accepted(receiver, sender, socket, 3, droiddeck_clock_domain_now_ns());
    assert(receiver.Received() == 2);
    receiver.Stop();
}

static void receiver_drops_stale(int control, const std::string &socket) {
    set_shift(control, 10000000000LL);
    XrFrameReceiver receiver;
    assert(receiver.Start(socket, droiddeck_clock_domain_now_ns));
    XrFrameSender sender;
    accepted(receiver, sender, socket, 1, droiddeck_clock_domain_now_ns());
    auto stale = frame(2, droiddeck_clock_domain_now_ns() - 1000000000LL);
    std::vector<unsigned char> pixels(stale.bytes, 99);
    assert(sender.Send(socket, stale, pixels.data()));
    assert(receiver.Received() == 1);
    DroidDeckXrPixels received;
    assert(!receiver.Take(&received));
    accepted(receiver, sender, socket, 3, droiddeck_clock_domain_now_ns());
    auto future = frame(4, droiddeck_clock_domain_now_ns() + 1000000000LL);
    assert(sender.Send(socket, future, pixels.data()));
    assert(receiver.Received() == 2);
    assert(!receiver.Take(&received));
    accepted(receiver, sender, socket, 5, droiddeck_clock_domain_now_ns());
    receiver.Stop();
}

static void receiver_default_clock(int control, const std::string &socket) {
    set_shift(control, 10000000000LL);
    XrFrameReceiver receiver;
    assert(receiver.Start(socket));
    XrFrameSender sender;
    accepted(receiver, sender, socket, 1, xr_frame_now());
    assert(receiver.Received() == 1 && receiver.Rejected() == 0);
    receiver.Stop();
}

static void receiver_rejects_malformed(int control, const std::string &path) {
    set_shift(control, 10000000000LL);
    XrFrameReceiver receiver;
    assert(receiver.Start(path, droiddeck_clock_domain_now_ns));
    int client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(client >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strcpy(address.sun_path, path.c_str());
    assert(connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    auto invalid = frame(1, droiddeck_clock_domain_now_ns());
    invalid.width = 3;
    const int64_t deadline = xr_frame_now() + 1000000000LL;
    assert(xr_frame_io(client, &invalid, sizeof(invalid), true, deadline));
    uint64_t acknowledgement{};
    assert(!xr_frame_io(client, &acknowledgement, sizeof(acknowledgement), false, deadline));
    close(client);
    assert(receiver.Received() == 0 && receiver.Rejected() == 1);
    XrFrameSender sender;
    accepted(receiver, sender, path, 2, droiddeck_clock_domain_now_ns());
    receiver.Stop();
}

int main(int argc, char **argv) {
    assert(argc == 4);
    int control = open(argv[2], O_RDWR);
    assert(control >= 0);
    droiddeck_clock_domain_set_control(argv[2]);
    std::string mode = argv[1];
    if (mode == "tracking") tracking_and_gpu(control);
    else if (mode == "resume") receiver_resume(control, argv[3]);
    else if (mode == "stale") receiver_drops_stale(control, argv[3]);
    else if (mode == "default") receiver_default_clock(control, argv[3]);
    else if (mode == "malformed") receiver_rejects_malformed(control, argv[3]);
    else assert(false);
    droiddeck_clock_domain_set_control(nullptr);
    close(control);
}
'''


@unittest.skipUnless(sys.platform.startswith("linux") and shutil.which("cc") and shutil.which("c++"),
                     "needs Linux and C/C++ compilers")
class XrClockDomainTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="dd-xr-clock-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        source = cls.directory / "test.cpp"
        source.write_text(HARNESS)
        clock_object = cls.directory / "clock.o"
        cls.binary = cls.directory / "test"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-pthread",
                        "-c", str(CLOCK_SOURCE), "-o", str(clock_object)], check=True)
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O2", "-pthread",
                        "-I", str(XR_HEADERS), "-I", str(CLOCK_SOURCE.parent),
                        str(source), str(clock_object), "-o", str(cls.binary)], check=True)

    def run_case(self, mode):
        with tempfile.TemporaryDirectory(prefix="dd-xr-case-") as directory:
            root = Path(directory)
            control = root / "clock"
            control.write_bytes(struct.pack("<q", 0))
            subprocess.run([str(self.binary), mode, str(control), str(root / "frames")],
                           check=True, timeout=5)

    def test_tracking_and_gpu_frames_use_guest_time_after_offset_changes(self):
        self.run_case("tracking")

    def test_cpu_receiver_acknowledges_frames_across_resume_clock_shift(self):
        self.run_case("resume")

    def test_cpu_receiver_acknowledges_and_drops_stale_and_future_frames(self):
        self.run_case("stale")

    def test_default_cpu_receiver_keeps_kernel_clock_behavior(self):
        self.run_case("default")

    def test_malformed_cpu_frame_is_not_acknowledged(self):
        self.run_case("malformed")


if __name__ == "__main__":
    unittest.main()
