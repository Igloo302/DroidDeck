# AURA Turnip experiment

These tools are separate from the APK build and automatic driver selection.
The experimental table entry matches only chip `0x44050100`. It reuses the
A830 profile as a hardware hypothesis, not as established Adreno 831 support.

## Prototype 3: AURA compositor cache maintenance

On the same APK, the Steam Dashboard right-eye blocks disappear with
`TU_DEBUG=sysmem,flushall` and return after removing `flushall`. Disabling LRZ
does not fix them. Both 2880×900 and 3840×1200 deterministic transfer probes
pass 24 successive frames, including BGRA optimal-image copies, through the
actual Linux Vulkan → DMA-BUF → Android GLES receiver.

Prototype3 retains prototype2's WSI fix and adds conservative cache clean and
invalidate operations at the existing cache-flush boundaries only when the
chip is exactly `0x44050100` and the process is `vrcompositor`. Games and other
chips keep their previous behavior. The underlying missing cache transition
has not been isolated to a single cache; this is an experimental workaround
with possible GPU cost, not a claim of complete 831 support. It does not add
per-draw idle waits or change pending GPU synchronization. The unchanged
Android companion is reused.

```sh
python3 tools/gpu/patch_aura_compositor_cache.py <source>/mesa
python3 tools/gpu/test_aura_compositor_cache.py <source>/mesa
```

Apply after the existing exact-chip and WSI patches. Rebuild Linux using the
same recipe and package with `--revision 3`. The applier verifies complete
source hashes, is idempotent, and refuses drift. The regression compiles the
actual patched guard and checks chip/process isolation and explicit debug
behavior. Keep prototype2 for rollback.

`xr_transfer_probe_linux.cpp` and `xr_transfer_probe_android.cpp` are standalone
diagnostics, not app features. Run the Android receiver with an app-private
socket path; run the Linux sender with that path, width and height. `--image`
adds a BGRA optimal-image round trip before the production shared receiver.
They verify all pixels and rotate all three slots over 24 frames, alternating
RGBA/BGRA. Use a 35-second external timeout and stop VR before running them.
CPU pixel readback is confined to these diagnostics.

## Prototype 2: already-signaled WSI sync FD

Aircar reproduced `DMA_BUF_IOCTL_IMPORT_SYNC_FILE` with `fd=-1`, returning
`EINVAL`, then repeated `VK_ERROR_OUT_OF_HOST_MEMORY` swapchain recreation.
KGSL exports `-1` for an already-signaled sync object. The WSI present path must
return success for that value without importing or closing a nonexistent FD.
Pending sync files, including FD0, still pass through normal import and close;
export and import failures remain errors. This does not skip a pending GPU wait.

Apply the patch to the pinned source after the platform and exact-chip patches:

```sh
python3 tools/gpu/patch_wsi_signaled_fd.py <source>/mesa
python3 tools/gpu/test_wsi_signaled_fd.py <source>/mesa
```

The patch checks the complete original/patched WSI file hash and is idempotent.
Rebuild the Linux library with the existing recipe and package with
`package_aura_turnip.py --revision 2`. The Android library can remain the
byte-identical prototype1 companion: its Android WSI backend does not use this
DRM WSI function. Keep prototype1 available for rollback. No system driver,
CPU frame-copy path, APK helper, or default setting is added.

The connected AURA reports SXR2330P, Adreno 831, 12 MiB GMEM, highest bank bit
16 and UBWC mode 5. Its Android system Vulkan driver passes the 4096-word
compute/readback probe. The unchanged runtime Mesa 26.2.2 rejects this chip.
Evidence is under ignored `build/xr-validation/aura-gpu/`.

## Source and build

Use `DD-Turnip-v0.3.1-source.tar.xz` from
<https://github.com/Droid-Deck/Drivers-CI/releases/tag/DD-Turnip-v0.3.1>.
Its SHA256 must be
`d094072e89d7810537c624b163b3ad40ad0d74f7514a1dd4beaea4b38b25b07a`.
It contains Mesa commit `44fd0fde8187b5bb1e919625f1ba24cb36c18cbc`
and the matching DD patch recipes. Work on an extracted copy outside the
repository; retain the archive as the unchanged source input.

```sh
python3 <source>/drivers/scripts/apply_patches.py linux <source>/mesa
python3 tools/gpu/prepare_aura_turnip.py <source>/mesa
docker build --platform linux/arm64 -f tools/gpu/Dockerfile.turnip \
  -t droiddeck-aura-mesa:27-prototype tools/gpu
```

In that container, mount the source parent as `/work` and configure:

```sh
meson setup /work/build /work/source/DD-Turnip-v0.3.1-source/mesa \
  --buildtype release -Dvulkan-drivers=freedreno -Dfreedreno-kmds=msm,kgsl \
  -Dgallium-drivers= -Dplatforms=wayland,x11 -Dopengl=false -Dgbm=disabled \
  -Dglx=disabled -Degl=disabled -Dllvm=disabled -Dvulkan-layers= -Dtools= \
  -Dvideo-codecs= -Dxmlconfig=disabled
ninja -C /work/build -j 3 src/freedreno/vulkan/libvulkan_freedreno.so
```

## Independent probes

`aura_probe.c` uses read-only KGSL properties and Android system Vulkan
enumeration. Build with the Android NDK and `-lvulkan`.

`turnip_probe.c` uses standard Vulkan and can be built for Android with the
NDK or for Linux ARM64 with the container compiler and `-lvulkan`.
Without arguments it enumerates the chosen device. With a SPIR-V file argument
it creates one 16 KiB storage buffer, dispatches the compute shader and verifies
all 4096 output words. Fence waiting is limited to two seconds; additionally run
the process under a 15-second external timeout.

```sh
glslangValidator -V --target-env vulkan1.1 tools/gpu/turnip_compute.comp -o compute.spv
```

Use a dedicated app-private experiment directory and an explicit ICD JSON via
`VK_DRIVER_FILES` and `VK_ICD_FILENAMES`. The installed Linux loader can run the
Linux probe with `--library-path <linuxfs>/usr/lib`; no proot session or preference
change is needed. Keep the existing runtime driver and device system driver
intact. GPU enumeration alone is not a rendering test; compute readback alone
does not establish graphics, Steam or SteamVR compatibility.

`--triangle triangle.vert.spv triangle.frag.spv` renders a red triangle on a blue
64-by-64 offscreen target, copies its pixels back and checks the center, corner,
color counts and complete coverage. Build the vertex and fragment shaders with
the same `glslangValidator -V --target-env vulkan1.1` command.

## 2026-10-20 AURA result

The exact-chip prototype passes Vulkan enumeration, the compute test (4096
words, no mismatches) and the offscreen triangle (1152 red / 2944 blue pixels,
matching the system driver). These Linux tests use `TU_DEBUG=sysmem`.
The driver SHA256 is
`081132fbdb46461b742e6e8bced4c13f3b5c4f63ec1c71d6360cb1789982f4da`.
Its actual ELF symbol requirements reach glibc 2.38.

Pairing the Linux prototype with Android's system driver initializes gamescope,
Wayland and Xwayland and completes Steam's initial client update, but does not
produce a first displayed frame. The Android system driver lacks
`VK_EXT_image_drm_format_modifier`, required by the existing shared-image path.
It is not a working display-driver pair. The temporary system-driver UI option
used for diagnosis has been removed.

## Android companion build

Extract a fresh source copy; do not apply Android patches on the Linux-patched
copy. Apply the official Android patch set, then the same exact-chip patch:

```sh
python3 <source>/drivers/scripts/apply_patches.py android <source>/mesa
python3 tools/gpu/prepare_aura_turnip.py <source>/mesa
```

Use the ARM64 Docker image above. Mount the NDK 27.3.13750724 host toolchain
folder (containing `sysroot` and `lib/clang/18`) read-only at `/ndk`; its headers
and target libraries work on either host OS. Compilation uses the container's
Linux clang-18/lld-18/llvm-18, not macOS executables. Mount a copy of
`android-cross.ini` at `/work/android-cross.ini` and the fresh source as below:

```sh
CFLAGS="-D__ANDROID__ -Wno-error -Wno-deprecated-declarations -Wno-incompatible-pointer-types" \
CXXFLAGS="-D__ANDROID__ -Wno-error -Wno-deprecated-declarations -Wno-incompatible-pointer-types" \
meson setup /work/android-build /work/android-source/DD-Turnip-v0.3.1-source/mesa \
  --cross-file /work/android-cross.ini -Dbuildtype=release -Dstrip=true \
  -Dplatforms=android -Dplatform-sdk-version=36 -Dandroid-stub=true \
  -Dandroid-libbacktrace=disabled -Dgallium-drivers= -Dvulkan-drivers=freedreno \
  -Dvulkan-beta=true -Dfreedreno-kmds=kgsl -Degl=disabled -Dvideo-codecs=
ninja -C /work/android-build -j3 src/freedreno/vulkan/libvulkan_freedreno.so
```

`-Qunused-arguments` prevents Meson's compile-only checks from rejecting
linker flags. `--rtlib=compiler-rt` selects the NDK target runtime rather than
a nonexistent Android libgcc. Do not suppress missing-library checks.

For an independent Android prototype test, build `turnip_probe.c` using the
NDK with `-DTURNIP_DIRECT_ICD -ldl` and `-I<mesa>/include/android_stub`
(without `-lvulkan`). It opens the Android HAL `HMI` entry. Set
`DROIDDECK_PROBE_ICD` to the absolute Android prototype library path. The probe
loads that exact ICD and rejects any device name other than the experimental
831, preventing an unnoticed fallback to the system driver. This bypasses
Android's Vulkan loader and does not prove the app's display/import path.

## Importable pair

Use `package_aura_turnip.py --help` to package both stripped libraries, the
pinned source archive and extracted source root. Supply NDK `llvm-readelf`
with `--readelf`; the tool verifies ELF ABI, libc, prototype/DD markers, records
hashes and includes licenses and the local recipe. Its ZIP uses the existing
DroidDeck `manifest.json` layout: import via Components → GPU drivers → Import
ZIP, select the pair and fully restart the app. Existing drivers are retained.
The experiment is not bundled into the APK or enabled by Auto.

## Paired integration result

Both Android and Linux prototypes pass enumeration, 4096-word compute and the
64×64 triangle. Android also passes the triangle in its default render mode.
Its stripped SHA256 is
`a133b74db96c923423b54fdf6fab4b2082e98411ac7a2f14738e9fa217c2601c`.

The paired ZIP was imported through AURA's existing app file picker. Both slots
were selected together, and a full app restart loaded the experimental Android
driver through adrenotools. The compositor creates its Vulkan device and screen
swapchain and advertises dmabuf format/modifier feedback. Desktop session
`2026-10-20-27-desktop` reaches `READY`, `firstFrame=true`, `failure=null`.
This validates an actual Linux-to-Android displayed frame, not SteamVR or hands.
Steam's 17-component repair and update completed. Session
`2026-10-20-28-steam` reaches READY with a first frame and Steam app 769 focused.
The display-1 screenshot visibly confirms Big Picture's sign-in page (QR code
and account/password fields). No account login was performed.
The profile remains experimental, with no full GMEM/performance/stability claim.

SteamVR 2.17.10 installation and startup were subsequently verified on AURA's
real Android XR runtime. Stereo source images advance and both submitted eyes
were captured. The right-eye capture contains block artifacts also present in
the incoming source; this is not a visual correctness pass or full driver
validation. See `docs/development/android-xr-runbook.md` section10.3.

The triangle probe now supports compile-time `TRIANGLE_WIDTH` and
`TRIANGLE_HEIGHT` (64..8192) and checks expected interior/background pixels in
both halves, excluding rasterization edges. A 3840×1200 version was compiled,
but its device run was deferred because AURA reported elevated thermal status.
Do not treat compilation as a large-image rendering pass. Temporary `noubwc`
and `nomultipos` trials did not eliminate the right-eye artifacts; overrides
were removed.
