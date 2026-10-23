# 共用 XR 桥接构建与验证手册

配合[实现方案](android-xr.md)，在仓库根目录执行。当前分支 `android-xr`，修改尚未提交。没有分配 Antigravity 任务。

## 1. 工具链

```text
JDK 17 / Gradle wrapper 8.10.2 / AGP 8.8.0 / Kotlin 2.1.0
SDK platforms;android-34 / build-tools;35.0.0
NDK 27.3.13750724 / CMake 3.22.1
OpenXR loader AAR 1.1.49
```

使用 SDK Manager 安装所需组件：

```sh
sdkmanager 'platforms;android-34' 'build-tools;35.0.0' \
  'platform-tools' 'ndk;27.3.13750724' 'cmake;3.22.1'
```

本机实际工具位置均为临时安装，不写入个人全局配置：

```sh
export JAVA_HOME=/Library/Java/JavaVirtualMachines/jdk-17.jdk/Contents/Home
export ANDROID_HOME=/private/tmp/droiddeck-android-sdk
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export GRADLE_USER_HOME=/private/tmp/droiddeck-xr-gradle
```

`/private/tmp` 可能被系统清理，后续机器应设置自己的 SDK / JDK 路径。

## 2. 不依赖 Android 的合约测试

```sh
./gradlew -PxrCoreOnly=true :xr-core:test --no-daemon
cmake -S xr-core/src/main/cpp -B build/xr-contracts -DCMAKE_BUILD_TYPE=Debug
cmake --build build/xr-contracts
ctest --test-dir build/xr-contracts --output-on-failure
python3 -m unittest tools.tests.test_xr_validation \
  tools.tests.test_quest_xr_validation tools.tests.test_runtime_inputs \
  tools.tests.test_windows_vr_build tools.tests.test_prepare_windows_runtime -v
```

C/C++ 合约覆盖字节布局、neutral、唯一 writer、并发快照、跨进程读取、inode 替换、symlink 拒绝、失焦 / 超时 / generation / epoch 释放、SteamVR 输入桥接、grip / aim 坐标转换与 JSON。

## 3. 独立 Smoke APK

```sh
./gradlew -PxrSmokeOnly=true :xr-core:test \
  :xr-smoke:assembleDebug :xr-smoke:assembleRelease :xr-smoke:lintDebug --no-daemon
```

产物：

- `xr-smoke/build/outputs/apk/debug/xr-smoke-debug.apk`：可安装，包含真实 native probe 与2D启动器。
- `xr-smoke/build/outputs/apk/release/xr-smoke-release-unsigned.apk`：未签名；包含同一真实 native probe。
- 两者均含 `arm64-v8a` / `x86_64` 的 native runtime，与主 app 共用源码。

启动器是普通 2D 页面。模拟器上的原生 OpenXR 按钮禁用；模拟能力状态界面已删除。Native probe 使用本地双眼测试场景，不需要 Steam、rootfs 或准备完整 app 的资产。测试应用不是产品控制面板。

## 4. 模拟器启动验证

本机使用私有 API34 google-xr ARM64 revision7 AVD，仅检查安装、2D启动器、前台状态与崩溃日志，不作为头显追踪或 SteamVR 验收。

```sh
adb -s <serial> install -r xr-smoke/build/outputs/apk/debug/xr-smoke-debug.apk
adb -s <serial> shell am start -W -n com.droiddeck.xrsmoke/.XrSmokeActivity
adb -s <serial> logcat -b crash -d
```

原生测试按钮在模拟器中应禁用；不再包含 mock 页面或用模拟能力枚举生成的通过报告。历史预览镜像实验曾提交本地场景，但眼间距为0、手 inactive；该记录不代表真机通过，标准验证器仍拒绝模拟器。

## 5. 主 app 编译与完整 APK

```sh
./gradlew -PskipRust=true :app:testDebugUnitTest \
  :app:compileDebugKotlin :app:externalNativeBuildDebug --no-daemon
```

这些检查不要求提前准备 Linux 运行资产。完整 APK 则必须先执行标准准备流程：

```sh
DROIDDECK_BUILD_VARIANT=debug tools/build_local.sh
```

需要可运行的 Docker Linux engine、工具链和固定组件下载权限；参见根 README。默认包含 Rust 商店引擎；只有明确需要精简包时才设置 `DROIDDECK_SKIP_RUST=1`。准备后可用 `./gradlew :app:assembleDebug` 复用校验过的资产。

2026-10-20 已完成标准准备、主 app `assembleDebug` 和成品校验，完整包包含 Rust 商店引擎、ARM64 XR native 库、Linux SteamVR 驱动与 WSI layer。交付副本：`build/deliverables/DroidDeck-XR-Dev-0.5.0-arm64.apk`，138491892 bytes（132.1MiB），包名 `com.droiddeck.launcher.xr`、versionCode14、AOSP testkey 签名，可与原版共存。SHA256：`5073903b25d875357da6fa95cffda529d9807780957c84893b282bbb39488207`。它仍是开发包，Galaxy XR / Quest 真机验收待完成。

成品检查记录在 `build/xr-validation/full-build/report.json` 及同目录日志：runtime input attestation、session helper 源码一致性、Android JNI 导出与动态依赖闭包、XR NativeActivity 入口、Manifest、ARM64 ELF、zipalign、v1/v2/v3 签名均通过。成品已确认不含额外菜单助手、空框架类与普通像素复制分支；捏合计数采用共用映射规则。旁边的 `.apk.sha256` 文件可用于核对传输后的 APK。

诊断修复前成品已安装到专用 API34 XR AVD，允许空白模拟器的文件权限后显示主界面，再次冷启动 `Status: ok`、前台为 MainActivity、crash buffer 为空。截图 `launcher-after-storage.png` 为 XR 空间面板的宿主视口，存在裁切；不据此声明完整布局验收。首次 Play 仍按上游流程联网下载运行环境，本次未执行该下载或登录 Steam，也未运行 SteamVR 游戏。

本机使用独立 Colima profile `droiddeck-xr`（VZ + Rosetta，4vCPU，首次6GiB内存、缓存重跑3GiB），未启用开机启动或切换默认 Docker context。当前临时工具链和缓存仍在 `/private/tmp`，本机复建可运行：

```sh
source /private/tmp/droiddeck-build-env.sh
colima start --profile droiddeck-xr --activate=false --ssh-config=false
bash tools/build_local.sh
colima stop --profile droiddeck-xr
```

这些临时目录被清理后，需要按根 README 重新准备工具链。`DROIDDECK_COMET_CACHE` 可将 Comet 的 Rust 缓存放在明确目录；`tools/build_local.sh` 的原生库检查已修复 macOS Bash3.2 对空数组的严格模式兼容问题，完整流程已重跑通过。当前 APK 已移除额外菜单助手、菜单状态 IPC 和菜单射线补偿；普通像素复制及强制传输参数已删除；3项 C/C++ 合约、51项 Python 检查及 Linux 驱动 / 传输回归通过。完整手部骨骼透传仍未实现。

Windows 准备入口也已更新：共用 native 源纳入 VR snapshot / 输入哈希，输出清单同步移除菜单助手及其专用依赖。旧 `vr-build.json` 不能继续使用。

## 6. guest 构建与测试

标准 Linux 工具链：

```sh
VK_INCLUDE=/usr/include tools/vr/build.sh --native
python3 -m unittest tools.tests.test_vr_wsi -v
```

macOS 上标准入口使用 Docker：

```sh
ANDROID_HOME=<sdk> tools/vr/build.sh
```

输出应同时包含：

```text
out/droiddeck/bin/linuxarm64/driver_droiddeck.so
out/droiddeck/bin/linuxarm64/xr-display-mode
out/layer/libVkLayer_droiddeck_vr.so
out/layer/libdroiddeck_vr_scheduling.so
```

本机已用标准 Ubuntu24.04 / GCC13.3 Docker 工具链构建 ARM64 glibc driver、WSI layer 与显示模式工具，并运行 optical-controller、scheduling 和 Joy-Con 回归。另在 Linux 容器中运行 `xr_bridge_test.cpp` 与 `tools.tests.test_vr_wsi`：输入释放、坐标转换、原子跨进程 IPC、WSI 同步、raw/shared/GPU 传输、FD 归属与断连隔离均通过。它们验证 Linux 合约与错误处理，不代表驱动已在 SteamVR / 真机中运行。早期 Zig 0.15.2 交叉编译产物 `build/xr-linux-arm64` 仅为工程排错记录。

`.github/workflows/android-xr.yml` 包含 host contracts、双 ABI smoke 编译与 lint、Linux guest 构建和 WSI 测试。尚未推送，不宣称远程 CI 已运行。

## 7. 真机分层验收命令

先安装 debug smoke，实际在头显里伸手并完成一次 捏合按下与释放：

```sh
python3 tools/test_xr.py --platform androidxr --mode smoke --input hands \
  --serial <serial> --seconds 30 --output build/galaxy-smoke
python3 tools/test_xr.py --platform quest --mode smoke --input hands \
  --serial <serial> --seconds 30 --output build/quest-hands-smoke
```

主 app 已准备资产、启动 Steam 会话并安装 SteamVR 后：

```sh
python3 tools/test_xr.py --platform androidxr --mode steamvr --input hands \
  --serial <serial> --seconds 30 --output build/galaxy-auto
python3 tools/test_xr.py --platform quest --mode steamvr --input hands \
  --serial <serial> --output build/quest-hands
```

画面传输沿用上游 GPU / DMA-BUF 路径；已删除强制传输参数和普通像素复制分支。

不额外启动菜单助手，也不通过菜单状态切换输入或补偿射线。SteamVR 负责菜单交互；菜单在裸手下能否正常打开、指向与点击，需要在真机中人工验收。

可以用 `--package com.droiddeck.launcher.xr --activity com.droiddeck.launcher.AndroidXrActivity` 指定并装 app。类名须是 fully qualified。release 主 app 的 Android XR Activity 不对外导出；直接 adb 启动使用受 DUMP 保护的 debug 入口，release 通过现有 app 内菜单进入。

Quest 手柄原工具 `tools/test_quest_xr.py --require-controllers` 保持兼容。不要把它的手柄结果当裸手结果。新工具不发送 Quest 专属 proximity 广播到 Android XR。

新验证器检查：real OpenXR、同 generation / epoch、最后仍 focused + 6DoF tracked、至少72次新 stereo submission。裸手需一只手实际 active、已知 provider、映射后的 pinch press/release。SteamVR 模式还需至少72张新 owned image、source frame id 增长、guest RUNNING。nominal refresh 和实际吞吐分开报告。

`report.json` 的 `status=not_run` 不等于 passed。`opticalHandTrackingVerified=false` 与 `steamVrInputDeliveryVerified=false` 明确表示工具只验证 runtime action 桥；人工移开控制器、核对实际来源、检查游戏骨骼 / 绑定 / 性能仍是单独的验收。

## 8. 日志与定位

| 位置 | 作用 |
| --- | --- |
| app external files `xr-health.json` | native backend、runtime/version、frames、source、手部 provider、计数与错误 |
| app `files/.wayland-rt/droiddeck-xr-input-v2` | 256B 原子输入 sidecar |
| guest Steam logs `droiddeck-vrstartup.log` | SteamVR 启动 |
| `adb logcat -s DroidDeckXR OpenXR-Loader AndroidRuntime` | loader / session / JNI 错误 |

- 无 runtime / 无标准扩展：先跑 smoke；依据原生错误定位，不用设备 feature 假定 runtime 已可用。
- `Hand trackers unavailable`：确认 runtime 扩展和实际 HAND_TRACKING 权限；标准 action provider 不要求关节权限。
- 画面提交增长但 owned images 不增：guest/frame transport 未通过，检查 SteamVR 与 WSI 日志。
- Dashboard 无法操作：核对 runtime 的 aim / pinch / grasp 有效性和 SteamVR 动作绑定，并记录实际射线与点击表现；当前版本没有菜单助手或 tip 补偿。
- 共享 lease lost：当前池隔离，退出重进；不可覆盖尚在使用的数据。
- reference-space change：按当前实现返回 2D，重启 guest XR；不要复用旧图像。

## 9. 2026-10-20 本机验证记录

| 项目 | 结果 |
| --- | --- |
| `xr-core:test` | 本轮3项实际入口与平台路由测试 |
| C/C++ CTest | 3 tests，全部通过 |
| Python validator / runtime / session assets / Windows attestation | 本轮51 tests，全部通过 |
| Linux SteamVR 启动 / WSI 回归 | 本轮18 tests，全部通过；另含共用输入桥测试 |
| 主 app `testDebugUnitTest` | 前轮1834 tests通过；本轮以共用入口、原生与打包回归为主 |
| 主 app Kotlin / ARM64 native | 编译通过 |
| Smoke debug / release / lint | 通过；两 ABI；debug / release 均不含模拟状态页面 |
| Linux ARM64 driver / WSI / 显示模式工具 | 标准 Docker 编译及 Linux IPC / WSI / 输入回归通过；SteamVR 运行未验收 |
| 完整主 app APK | 标准流程通过；包含商店引擎；资产、JNI、ARM64 XR 组件、对齐及 v1/v2/v3 签名检查通过 |
| 完整 APK 的 XR AVD 安装 / 启动 | 安装成功；文件权限授权后主界面显示；再次冷启动正常；crash buffer 为空 |
| XR AVD native 实验 | 场景渲染提交增长；眼间距为0，手 inactive；不算头显验收 |
| XREAL AURA 真机安装 | Android14 / API34、ARM64；主 APK 安装及哈希核对通过；Linux r9 已安装 |
| AURA 首次 Steam 启动 | failed：WN Linux Turnip 0.1.2-b 拒绝 Adreno 831 / `0x44050100`，gamescope 退出，Steam 未启动 |
| AURA / Quest OpenXR、裸手与 SteamVR | not_run；安装及 feature 声明不代表 XR 功能通过 |


### 当前证据与扩展范围

先前完整 APK 的模拟器安装、冷启动、实际安装文件 SHA256、前台界面与 crash buffer 记录在 `build/xr-validation/full-build/`。当前诊断更新复用未变更的 native / guest 产物，不把先前模拟器结果当成新 APK 的安装测试。临时模拟器、adb、Gradle 后台进程和专用构建 VM 在验证后关闭。

早期 `build/xr-validation/avd/` 中的 mock、强制 transport 与 native 实验记录是历史排障证据，不适用于当前接口；本轮未以这些记录宣称当前功能通过。

主包默认仅执行 `:app:assembleDebug` / `:app:assembleRelease`。独立 smoke 只在显式命令或 XR CI 中构建。未接入的会话管理类、通用 runtime / guest 接口、空输入回调、mock 页面及旧 `android_xr_hands` 参数已删除；未来扩展只留在 [技术方案的备注](android-xr.md#10-后续扩展备注未启用)。

完整手部骨骼透传仍未实现，Galaxy XR / Quest 真机裸手、菜单、游戏与性能验收仍未运行。

## 10. AURA 当前连接与安装

当前目标为 XREAL AURA（系统型号 Android XR Puck）。本机通过独立5041端口、已有 ADB35.0.1 native USB 后端连接，APK 及实际安装文件 SHA256 一致。原 SDK ADB37.0.1 的设备发现未成功；未修改全局配置。Linux r9 已安装。安装证据在 `build/xr-validation/aura-install/`；尚未运行原生 OpenXR 或 SteamVR 真机测试。

### 10.1 初次 Adreno 831 驱动阻塞记录

固件 `UXP4.260929.001.A6 / 16499128`、SoC `QTI SXR2330P`，KGSL 报告 `Adreno831`。首次 Steam 会话选中 Android `WN-Turnip-1.19-b_Axxx` 和 Linux `WN Linux Turnip 0.1.2-b`，后者报：

```text
device (chip_id = 44050100, gpu_id = 6851) is unsupported (VK_ERROR_INCOMPATIBLE_DRIVER)
vulkan: failed to find physical device
```

会话未产生第一帧，以状态1退出。后续 Wayland / SDL 错误发生在 GPU 枚举失败之后，不能据此认定手势、OpenXR、Steam 登录或已下载环境损坏。

2026-10-20 核对的 [Mesa 设备表](https://gitlab.freedesktop.org/mesa/mesa/-/blob/fb891a59a94b8791b40ad2ef6b6ae153b882da88/src/freedreno/common/freedreno_devices.py)、[Banners A8xx 补丁](https://github.com/The412Banner/Banners-Turnip/blob/A8xx/patches/apply_a8xx_gpus.py) 未包含 `0x44050100`。已安装的 WN 版本实测拒绝该芯片；DD-Turnip v0.3.1 发布说明列出的新增 GPU 补丁也未覆盖831。未找到可直接替换的831兼容 Linux 驱动，不用相邻型号的设备参数冒充支持。

诊断修复把831从笼统的“受支持”改为“需要兼容驱动、尚未验证”，并从有限日志尾部识别明确的 Turnip 芯片拒绝。仅在未出现第一帧、日志明确拒绝芯片时返回 `GPU_UNSUPPORTED`，状态 API 与界面使用同一原因。真机还验证了运行环境内置 Mesa26.2.2 同样拒绝该芯片；该次进程退出码为0，因此诊断不再只看退出码，保留实际退出码同时标记会话失败。其他 Vulkan 错误继续保留原有处理。该修复不增加831的 GPU 功能支持。

继续 GPU 适配需要确认831的硬件配置、固件与寄存器差异，在 Turnip 中加入准确的设备定义，再构建 ARM64 glibc Linux ICD；Android 合成器若使用自定义 Turnip，也要匹配其 bionic 版本。依次验收 GPU 枚举、简单渲染、gamescope、普通 Steam 会话，最后再测 SteamVR。重装应用、重复下载 rootfs 或改手势桥无法解决当前 GPU 枚举失败。

最终诊断修复版已覆盖安装到 AURA，实际安装 SHA256 与交付文件一致。15项相关 JVM 回归及 APK 资产、依赖、签名、对齐检查通过；native / guest 库哈希保持不变。真机重试的状态为 `FAILED / GPU_UNSUPPORTED`，Linux r9 仍在。UI 采样时前台已回到主页面，未将错误文字的屏幕呈现记为通过；这次只确认结构化错误记录。GPU 运行支持仍未完成。

### 10.2 831 实验驱动真机验证

独立实验的源码、固定输入和构建说明在 [tools/gpu](../../tools/gpu/README.md)。从官方 DD-Turnip v0.3.1 完整源码构建，在 A830 参数组中增加只匹配 `0x44050100` 的831条目；A830参数仍属于待验证假设，不代表已确认全部硬件配置。没有绕过全部型号检查，也没有替换系统驱动。

AURA 的只读 KGSL 探针报告12MiB GMEM、highest bank bit16、UBWC mode5。系统驱动与实验 Linux 驱动的计算探针均校验4096个结果且零错误；两者的64×64离屏三角形均得到1152红色 / 2944蓝色像素，并核对中心和角点。Linux 测试使用 `TU_DEBUG=sysmem`，不证明 GMEM、所有 Vulkan 功能、性能或长时间稳定性。

最终配对包为 `build/deliverables/DD-Turnip-AURA831-prototype1.zip`，包含 Android bionic 和 Linux glibc ARM64 两端库、元数据、固定源码来源、构建补丁及许可证。通过 AURA 上原有“组件 → GPU 驱动 → Import .zip”实际导入，两端驱动已一并选中；冷启动后 adrenotools 确认加载实验831，Vulkan device、交换链和 dmabuf feedback 均初始化成功。未改动系统驱动，也未设为 Auto 默认。

Android 端独立 HAL 探针同样通过4096项计算与1152红 / 2944蓝的离屏三角形；sysmem 和默认渲染模式均通过基础绘图。Android 库 SHA256 为 `a133b74db96c923423b54fdf6fab4b2082e98411ac7a2f14738e9fa217c2601c`。系统 Android 驱动缺少 `VK_EXT_image_drm_format_modifier`，不能用于当前图像共享路径；排查时临时增加的系统驱动选项已删除。没有增加 CPU 图像复制兼容路径。

清理后的 APK 已重新安装到 AURA，11项相关 JVM 测试以及运行资产、依赖、签名和对齐校验通过；4项已记录的 XR/商店原生资产哈希保持不变。配对驱动的桌面会话 `2026-10-20-27-desktop` 达到 `READY`、`firstFrame=true`、`failure=null`，实际 Linux→Android 显示链路通过。Steam 修复下载17项组件并完成更新后，会话 `2026-10-20-28-steam` 达到 `READY`、`firstFrame=true`，焦点为 Steam app769；已通过 display1 截图实际确认 Big Picture 登录页，二维码及账号/密码输入框正常显示。截图为 `steam-ui-builtin.png`。未代用户登录账号。SteamVR、OpenXR、裸手、游戏、性能和长时间稳定性仍需分别验收。

证据保存在忽略目录 `build/xr-validation/aura-gpu/`；`report.json`、`pair-desktop-state.json`、`pair-android-renderer.txt` 与两端探针日志记录本次结果。原驱动保留，之前的选择记录在 `driver-selection-before.json`，可在原有驱动菜单切回。

### 10.3 AURA SteamVR 安装与启动

2026-10-20：通过原有 `start steam --steamvr` 入口从 Valve 固定源下载571MB ARM64 包，安装器 SHA256 校验通过并安装到 `/opt/steamvr`（2.17.10）。无需通过用户账号下载 Steam 库中的另一份运行时。

首次 OpenXR 启动因 STAGE 坐标变化通知主动退出。修复后只消费所用参考空间的通知，到达 `changeTime` 时释放输入、递增 origin epoch 并发送 neutral；拒绝旧 epoch 样本，保留同一会话。3项 C/C++ 合约测试通过。全部已准备 guest 资产哈希核对未变，仅重新构建 Android XR 库；APK 资产、原生依赖、对齐与 v1/v2/v3 签名校验通过并安装。

修复后会话 `2026-10-20-30-steam`：真实 Android XR runtime1.0.0，OpenXR state5、headFlags15、epoch2；SteamVR state4（RUNNING），received/uploaded/sourceFrameId 持续增长，已接收并提交数千张真实图像。成功抓取两眼1920×1200纹理和源图，看到 SteamVR 环境与 Steam 登录 Dashboard，不是 synthetic 场景。证据在 `build/xr-validation/aura-steamvr/`。

安装、启动、持续图像传输通过；右眼截图存在局部块状花屏，源图中也可见，图像正确性不通过，尚未定位原因。用户本轮明确暂不佩戴，未验收佩戴体验、主动头动、裸手捏合/释放、菜单操作或游戏。EXT hand interaction profile 已被 runtime 识别，但 handsActive=false，不能据此声明裸手通过。

### 10.4 后续重复启动与花屏对照

退出 XR 时返回二维 Surface，原 `surfaceCreated` 只检查 stopRequested / ended，但服务已清除 stopRequested、仍处于 STOPPING 的间隙会再次启动 guest。增加 STOPPING 保护后，32、33、34号 Steam 会话均完整退出到 IDLE，日志归档完成；后续33、34、35号会话均能重新进入 OpenXR / SteamVR。没有通过强制结束来冒充这3次正常退出。

临时通过现有 Turnip 调试开关依次测试 `sysmem,noubwc`、`noubwc`、`sysmem,nomultipos`，右眼局部花屏仍可重现。第二项在 Dashboard 初始化完成后补抓晚期画面，不能用只有背景的早期截图宣称修复。所有临时参数已移除，恢复 app 默认 sysmem。源图在双眼投影前已经有花屏，但该源图仍位于 Vulkan→GLES 共享之后，尚不能把原因精确归给原始渲染或共享传输。

计划10分钟的35号会话连续测试中，约3分钟后计数不再增长；日志显示 OpenXR 从5经4/3/6到1，系统前台为 XR Home barrier。设备同时报告全局 thermal status4、部分传感器status5，动态策略报告 app OpenXR rate0。系统保持 Awake，不能简单称为屏幕休眠；尚未证实热状态与 XR 暂停的直接因果。已正常停止负载，观察到热状态下降。不得判为10分钟持续稳定性通过。

验证器补充连续样本新鲜度检查：拥有单调时间戳时，rendered / uploadedFrames / sourceFrameId 任一连续超过5秒未增长即失败，避免健康文件停更后沿用旧的 focused/RUNNING 值误报成功。16项 Quest / Android XR 验证器测试通过。证据位于 `build/xr-validation/aura-followup/`。

本轮长测开始时设备已处于thermal status4，受前面多轮负载影响，不是冷机基线。初始会话约157秒后停止增长；监测记录约473秒，后续包含停止负载及降温读数，不能当作持续渲染时长或据此计算有效帧率。前两分钟 app PSS约303119→295523KiB，随后上下文释放、内存回落；不足以判定长期内存无泄漏。数据按新增新鲜度规则重新核验，结论为失败。

一次返回启动器检查又触发了36号会话，而 `runSteamAtStartup` 未开启；原因尚未确定，不能宣称后台恢复完全通过。已再次正常停止36号会话，最终 `IDLE`、`running=false`、`steamVr=false`。大图逐像素探针只完成编译，因热状态延后执行，不算 GPU 验证通过。后续应在冷机和正常佩戴条件下重新验收。

### 10.5 2026-10-21 佩戴反馈后的修复

用户实测提出：撕裂/卡顿、裸手显示为 Quest 手柄、射线偏高、捏合拖动中断、Dashboard 关闭后无法唤回。此前“启动成功”只证明出图，不能作为交互或画质验收。

本轮修改：

- Android XR 声明并在普通会话页进入 VR 前请求 `android.permission.HAND_TRACKING`。拒绝后仍允许系统动作输入；关节接口按实际权限启用。`XR_EXT_hand_tracking` 不再依赖 Meta 的 `XR_FB_hand_tracking_aim` 才能启用。
- Android XR 与 Quest 共用 `droiddeck-xr-skeleton-v1`，保留现有 input-v2 ABI。两手各 26 个运行时关节，经原子文件传给 Linux 驱动，转为 SteamVR 31 骨骼的父空间姿态。包含 generation、origin epoch、100ms 新鲜度、有效位置/单位四元数检查。失焦、停止和原点变更发布中性数据。
- 裸手模式使用手部渲染模型和兼容 SteamVR Skeletal Input 的配置。Dashboard 的手模型仍是原有开掌/握拳/捏合组件模型，不能将其当作完整关节动画验收；支持 skeletal input 的应用可接收真实关节数据。实体控制器保持原有映射。
- OpenXR aim 是射线坐标，SteamVR 的路径是 `/pose/tip`。射线使用 runtime aim；模型使用真实手掌关节与已有 HandModel 的刚体对齐结果，tip 为模型空间到 runtime aim 的相对变换，grip 和完整骨骼分别转换到模型空间。第一版曾直接用 aim 驱动模型，用户反馈朝向错误，因此已改为分离模型和射线。未添加人工校准或肩部估计。
- 远程指向操作优先映射运行时 `aim_activate_ext`，缺少该能力的提供者继续使用 runtime pinch。按下阈值保持 0.6，松开阈值 0.25，首次必须先观察到松开。按住期间向旧式 SteamVR trigger/value 保持 1。ready/active 丢失、过期或失焦立即释放，禁止用延迟释放掩盖追踪丢失。
- 双手同时捏合并保持 1 秒后，先释放两手 trigger/grip，等待 100ms，再发出一次左手 `/input/system/click` 的 100ms 按键，保持捏合期间继续抑制扳机，再次触发需先松手。第一版同时保持 trigger 与 system，实机在 11:16:15 / 11:16:18 被 compositor 识别为截图快捷键，不能作为 Dashboard 唤回成功。复用运行时 pinch 数值，不读取相机或增加手势识别引擎、Dashboard 助手、独立面板。
- 831 的 Android XR 入口暂以 0.75 线性分辨率运行（本机 1440×900/眼），像素量为原先 56.25%。Quest 和其他 GPU 不受此设置影响。取帧移到 `xrWaitFrame` 后、发布跟踪后，减少提前缓存一帧；新增 `transferMs`、`sourceAgeMs`、`renderScale` 诊断字段。
- 曾仅为 `831 (experimental)` 试验 Vulkan 导出前的额外 GPU blit。安装验证后右眼方块仍在，已撤销该额外转换，避免无效开销。保留了原图像分配路径中 memoryTypeIndex 未随图像内存需求更新的修正。未增加 CPU 像素复制路径。

本机 C/C++ 合约 3 项及 Python 验证/资产测试 27 项通过；Linux ARM64 光学/XR 控制器回归和 WSI 提交同步回归在 AURA 用户态执行通过。回归覆盖捏合迟滞、aim activate 优先、失焦/过期释放、骨骼重建、单次菜单快捷键，以及 Vulkan 提交等待、队列归属和有限在途帧。

佩戴体验仍需用户复测。修复前 compositor 日志出现 `WaitForPendingPresent: failed to wait for present` 和无效 GPU 时间戳；当时设备 Thermal Status 为 4，停止会话后降至 2。不能将降分辨率、自动测试或单张截图等同于解决撕裂、发热或帧时序问题。

实机新增证据：用户佩戴后 `handsActive=[true,true]`、`skeletonActive=[1,1]`，Linux driver 左右手 `runtime skeleton: result=0`；两份 XR 手模型加载成功。一次约14秒采样 sourceFrameId 推进约71.6帧/秒、提交约72.5帧/秒（统计边界受健康文件刷新影响），不代表用户感受到的画面稳定或长时间性能。右眼截图仍有方块异常。

第二轮用户反馈：手已显示模型，但朝向错误；Dashboard 关闭后双手捏合未唤回。相应驱动修复增加掌部刚体对齐和菜单前释放扳机，合约测试与 ARM64 驱动回归均通过，佩戴结果仍待再次确认。删除了尚未验证的 UI 提示。

第二轮修复包已安装：SHA256 `2e26998dabf38f50aefc8442a061209eab031305192af041ac62b38f67096518`，会话 `2026-10-21-05-steam`。签名、APK runtime inputs、session helpers、native exports 检查通过。第一次修复包 `88e8be8c4b32e8a7a844401825cd417f823384614ea806c166d4e8af33e6e8b6` 的截图不能替代第二轮验收。

## 12. 普通游戏手柄接入（2026-10-21）

沉浸式 `GuestXrActivity` 通过 NativeActivity 输入队列接收实体手柄事件，保留设备 ID、source、按键、时间和 12 个常用摇杆轴，再交给现有 `PadBridge` / fake evdev。Android XR 与 Quest 共用此路径，普通按键和轴不转换成 VR 手部姿态。A/B、方向键、摇杆、肩键和扳机继续使用已有映射，Steam 页面是否响应由其游戏手柄支持和焦点决定。

View/Select + Menu/Start 同时按下，或应用收到实体 Guide/Home 键时，请求一次 SteamVR `/input/system/click`。组合键按设备隔离，松开所有菜单键后才能重触发。形成组合后释放已传给 guest 的菜单键，沿用先释放两手 trigger/grip、等待 100ms、菜单按下 100ms 的路径。仍保留单独 Start/Select 的原有行为（包括用户的 Select→Steam 设置）；Home 可能被 Android 系统截获，不能依赖它作为唯一入口。

请求计数使用 input-v2 原保留头字段 offset52 和新增 capability flag，保持 256 字节 ABI。驱动只在同 generation/epoch、前后有效且 focused 的新请求上触发，不重放重启、失焦或过期期间的请求。无有效手姿态时，菜单接收设备保持逻辑连接但 pose 无效，不伪造被跟踪的手；因此拿着游戏手柄、手部未跟踪时仍可发送菜单按键。退出、失焦、暂停和断连会释放输入。头显自身只有 GENERIC_1 的轴不占用游戏手柄玩家槽。

`xr-health.json` 增加 gamepadKeyEvents、gamepadMotionEvents、gamepadMenuSequence；这些只证明 Android 输入路由和请求，不能单独证明 SteamVR 已显示 Dashboard 或界面完成导航。实机验收需确认系统识别了独立实体手柄，佩戴时依次测试 A/B/摇杆、组合键唤回、松开重按、双手不被跟踪、切出和重连。SteamVR 内第三方面板和游戏未必支持游戏手柄，不能据此声明所有 VR 界面兼容。

本轮构建与设备记录：21 项 Android 测试（XR 输入 6、既有路由 9、Joy-Con 6）、3 项 XR 协议测试和 AURA 上 ARM64 驱动回归均通过。完整 APK SHA256 `11dd0908364860ac6c2a972f3b629e82023ef936066a9f9a1b523e13d194eb46`，会话 `2026-10-21-06-steam`；APK/guest 驱动 SHA256 已与设备文件核对一致。系统实际识别 `XREAL Game Controller Consumer Control`（vendor3537/product0932，含 GAMEPAD/JOYSTICK，双摇杆、hat、双扳机），新版本日志确认识别该设备，未将头显通用轴列为游戏手柄。XR state5、SteamVR RUNNING，真实帧持续推进，已抓取包含 Steam 页面与 Dashboard 的双眼纹理；此时 gamepadKeyEvents / gamepadMotionEvents / gamepadMenuSequence 仍为0，真实按键、导航和菜单唤回仍待用户操作确认。证据目录 `build/xr-validation/aura-gamepad/`。

## 13. VRChat 启动失败定位（2026-10-21）

用户已确认 XREAL Game Controller 基本输入可用，随后尝试 PC 版 VRChat（438100）没有进入。会话 `2026-10-21-06-steam` 的 gameprocess 日志确认通过 `Proton Experimental ARM64 (Compatible)` 启动 `VRChat/launch.exe`。EAC setup service.log 记录安装成功，但 `start_protected_game.exe` 留下260×114的错误窗口，VRChat.exe 未启动，LocalLow 还没有 VRChat 游戏日志。切回二维显示后，截图明确显示 **Failed to initialize Wine helper buffer**；这次失败应定位为受保护启动器的 Wine helper 初始化失败，不能宣称完整反作弊已通过。截图保存于 `build/xr-validation/aura-vrchat/launcher-screen.png`。

SteamVR 曾看到 launch.exe 的 overlay 和 OpenXR instance 探测连接后断开，scene-focus 的 scenePid=0，没有游戏场景出图。session.log 还有 proton_vrhelper 的 xrCreateInstance 失败记录；其与 EAC 弹窗是否有直接因果关系未证实，不能仅据此修改 XR 桥接。现有2D可见错误和进程证据足以说明尚未到 VRChat 游戏渲染阶段。没有修改游戏、关闭反作弊、替换 DLL 或切换 Proton；EAC Linux runtime 在当前 common 目录未发现，但未证明补装能解决 ARM64 Wine helper 错误。

上游相同错误：[DroidDeck #903](https://github.com/Droid-Deck/DroidDeck/issues/903)、[#932](https://github.com/Droid-Deck/DroidDeck/issues/932)，查询时均未给出已确认修复。VRChat 官方 [Steam Frame 支持说明](https://help.vrchat.com/hc/en-us/articles/55751011246995-Is-VRChat-supported-on-the-Steam-Frame) 明确独立运行使用 Android 版，PC 版不作为 Frame 独立运行选项，另支持 PC 串流。这不证明 Android 版已经兼容 AURA；该方案需另行验证。普通 Steam/SteamVR 出图和游戏手柄可用，不等于 PC 版 VRChat 兼容。

## 14. 其他免费 VR 游戏实测（2026-10-21）

在同一 AURA、11dd0908 APK 与831实验配对驱动上，通过 Steam 正常安装并启动以下游戏。只下载安装成功或 Steam 显示运行中，不计作游戏兼容通过。本轮三款均未确认进入可玩的 VR 场景。

| 游戏 | 实测结果 |
| --- | --- |
| Aircar（1073390） | 会话07的 OpenVR scene client 接上 server / compositor，12:44:06 记录 Capturing Scene Focus，创建了1440×900远端眼纹理，12:44:30断开。未抓到驾驶舱或游戏画面；低画质重试也未确认持续场景。 |
| Dagon（1481400） | 会话09按官方 RunDagonVR.bat 启动，进程实参 Dagon.exe -VR；Unity2022.3.62f3、DXVK / Direct3D11及1920×1080游戏窗口初始化，但没有确认 OpenVR 场景连接或游戏双眼画面。主体于12:57:07退出，早于12:57:38测试人员返回 Android 窗口。 |
| Poly Runner VR（462910） | 会话10的 Unity5.3.4p1 日志识别 droiddeck:droiddeck-cardboard-hmd，但未抓到游戏 VR 场景。首次12:59:10退出；再次只发送一次启动、随后完全不操作按键，仍于13:00:16退出。最终双眼捕获为 Steam Dashboard。 |

Aircar 日志中的无效 sync-file 导入、Proton VR helper 的 xrCreateInstance 失败，以及 Steam 兼容工具查询重连均只是已观察现象；没有建立它们与各游戏退出的因果。Android crash buffer 未给出对应崩溃报告，不能把退出统一归为内存不足、831驱动或 XR 桥接的某一项。scene-focus.json 的 scenePid 在 Aircar 已记录场景连接时仍为0，不能单独用此字段判定游戏未连接。

Aircar 临时低画质 GameUserSettings.ini 已恢复原文件。游戏及 Steam 自动安装的依赖保留，没有修改 APK、全局 Proton 选择或反作弊。没有完成用户佩戴及各游戏实体手柄验收；测试时观察到 Thermal Status4，不属于冷机性能基线，右眼既有方块异常仍存在。全部测试结束后正常停止到 IDLE。证据与逐项结果位于忽略目录 `build/xr-validation/aura-vr-games/`，总表为 `results.json`。


## 15. Aircar 同步错误修复与 prototype2 驱动（2026-10-21）

第14节记录的是 prototype1 基线。继续诊断后，已在 AURA 上确认 Aircar 的真实双眼城市场景；用户佩戴实测确认能看到城市、能转头且 XREAL 手柄可用。卡顿、花屏、驾驶舱细节和长时间稳定性未得到本次反馈确认，不能视为全部验收通过。

### 原因与修改

Mesa KGSL 在同步对象已完成时可以导出 `sync_file_fd=-1`。临时日志探针捕获到 DRM WSI 把这个值传入 dma-buf 同步导入：`syncfd=-1 flags=3 rc=-1 errno=22`；有效 fd 的导入成功。这个错误随后表现为 `VK_ERROR_OUT_OF_HOST_MEMORY` 和重复重建交换链，并不是已证实的内存耗尽。

在固定 Mesa 源码的 `wsi_signal_dma_buf_from_semaphore()` 中，仅在信号量导出成功且 fd 为 -1 时直接返回成功。fd0 和正数仍按原路径导入并关闭；导出失败和有效 fd 的导入失败继续返回原错误。没有跳过未完成的 GPU 同步，没有 CPU 像素复制、系统驱动替换或游戏启动助手。

补丁工具为 `tools/gpu/patch_wsi_signaled_fd.py`，校验原始/修补后文件 SHA256、支持重复执行并拒绝源码漂移。`tools/gpu/test_wsi_signaled_fd.py` 提取实际 Mesa 函数编译运行，覆盖已完成的 -1、fd0、正数、导入失败及导出失败，全部通过。Linux 驱动增量编译通过。

### 交付与实机验证

配对包 `build/deliverables/DD-Turnip-AURA831-prototype2.zip` 的 SHA256 为 `893e2817824e492bf4a038b0cd5d71fc0dfc7d2024d3816cf49063405f85d28a`。Linux glibc 库 SHA256 为 `a59a888f002dc752820258b88f5bbbcec764197d37c8e43e21c8cb4dab53253c`。Android WSI 不编译该 DRM 函数，因此配对包保留 prototype1 的原始 Android 库，SHA256 仍为 `a133b74db96c923423b54fdf6fab4b2082e98411ac7a2f14738e9fa217c2601c`。

通过原有“组件 → GPU 驱动 → Import .zip”导入并选择两端 prototype2，按原有提示重启。设备已安装 Linux 库哈希与交付库一致；prototype1 保留供回退。APK 没有变化，仍为第12节的 `11dd0908…` 版本。

先用临时探针验证边界处理能恢复实际游戏画面，再删除设备上的探针库，通过正式 prototype2 冷启动会话 `2026-10-21-13-steam` 重试。Aircar 主体和 OpenVR 场景正常启动，抓到2880×900的双眼城市场景；检查时原同步错误为0，恢复 `ENABLE_GAMESCOPE_WSI=1`，未保留游戏专用规避参数或注入库。证据为 `build/xr-validation/aura-vr-repair/aircar-prototype2-scene/source.png`、`aircar-prototype2-proton.log`、`aircar-prototype2-check.json` 与 `prototype2-package.json`。

源图证明实际游戏渲染和传输，不证明光学显示、驾驶输入、游戏帧率或长时间稳定性。XR 提交计数约72Hz也不能作为游戏帧率。此前 Dashboard 的右眼方块异常尚未完成独立复验。831仍借用实验 A830 参数组，不代表完整 Vulkan 或硬件支持。

### 其他游戏与清理状态

Poly Runner VR 的独立故障定位到 Unity5.3.4p1 的 WWWCurl 线程访问0x3c；Experimental 和稳定 Proton、临时 FEX 稳定性参数均复现。已恢复试验前的兼容工具和游戏参数，未宣称修复。Dagon 尚未用 prototype2 复验，VRChat 的受保护启动器错误仍未解决。

用户佩戴后的原话为“能看到城市、能转头、手柄可用”，以上三项据此记为通过。实际游戏场景出图与基本佩戴输入已验证；没有据此认定此前卡顿和花屏已解决。清理结果见本节后续记录。

清理完成：通过设备屏幕实际关闭 Debugging tools，界面状态为 Off，访问 API 为 `commands=false`；最终会话15为 `IDLE`、`running=false`。关闭前临时环境变量的 active/pending/persistent 均为空。专用构建 VM 已关闭。清理期间从会话14返回启动器曾意外产生会话15，随后已正常停止，未将其作为额外稳定性通过；第10.4节的恢复生命周期问题仍需单独处理。清理证据为 `access-after-cleanup.json`、`debug-settings-after-off.json` 与 `state-after-cleanup.json`。


## 16. 卡顿与右眼缺块排查（2026-10-21）

用户补充确认 Aircar 仍有很多卡顿和花屏。本轮把“能运行”与画面正确性/性能分开验收，重新获用户授权临时开启调试工具。设备初始 Thermal Status4；短时测试间正常停机，未关闭系统温控，也不把热机数据当作冷机性能基线。

### 图像链路与对照

系统 Android Vulkan、实验 Android Vulkan 与 prototype2 Linux Vulkan 的3840×1200三角形逐像素检查均通过，左右错误数为0。新增独立探针通过实际 `XrGpuFrames` 与 `XrGpuSender` 传输2880×900、3840×1200的变化图案，各连续24帧、轮换3个缓冲并交替RGBA/BGRA，左右逐像素错误均为0；补充 BGRA optimal image → buffer → Android GLES 路径也通过。探针只在诊断时执行 CPU readback，不加入产品显示路径。基础绘图/传输通过不代表复杂合成器正确。

会话16的 Dashboard 源图复现右眼缺块。会话17临时 `sysmem,nolrz` 的晚期 Dashboard 仍有缺块，参数已撤销。新异步 APK 会话18临时 `sysmem,flushall` 的晚期 Dashboard 未见原缺块；同 APK 会话19去掉该参数后缺块重新出现。两次源图均包含实际 Dashboard 内容，未用空背景代替验证。证据在 `build/xr-validation/aura-visual-repair/` 的 `baseline-dashboard`、`nolrz-late-dashboard`、`flushall-late-dashboard`、`async-without-flush-dashboard`。

### 显示线程同步

原实现每次计算转换后 `glFinish()`，等 GPU 读完共享缓冲才回 ACK，阻塞显示线程。新实现用 `EGL_ANDROID_native_fence_sync` 导出完成 fence，由后台工作线程等信号后向原连接发送同一个 frame id 的 ACK；显示线程继续提交双眼。旧连接的 socket 独立保留，防止重连后把旧 ACK 发给新生产者；等待有界、停止可取消，缓冲仍须完成 GPU 读取才归还。未增加 CPU 图像复制或改动共享协议。缺少原生 fence 能力时保留原有同步等待；缓冲池销毁前仍完整等待 GPU，以保证生命周期安全。Quest 与 Android XR 共用实现。

增加 `gpuAsyncCompletion` 与 `gpuCompletionFailures` 诊断。原生回归验证无提前 ACK、连接归属、取消与描述符无泄漏，在 AURA/Android 和 Linux ARM64 均通过；macOS 的3项既有合约和16项 Python 验证器回归通过。Linux 专用完成通知回归只在 Linux/Android CMake 中注册，不要求 macOS 存在 DMA-BUF 头文件。

稀疏健康采样中，旧同步路径 transferMs 中位数1.77ms、最大4.89ms；新路径中位数0.27ms、最大0.53ms，完成失败0。两次采样分别带 nolrz/flushall 对照参数，不能作为严格同条件整帧性能对比，也不是游戏 FPS。新 APK 无 flush 参数的一次读数为0.25ms。源图年龄仍有波动，不能据此宣称所有卡顿已解决。

### prototype3 驱动

prototype3 保留第15节的已完成同步 fd 修复；在既有 cache flush 边界增加保守的 clean/invalidate，严格限于 chip `0x44050100` 且进程名 `vrcompositor`。其他游戏进程和其他芯片不强制启用。已证实完整缓存刷新能消除本例缺块，但具体缺少哪一项缓存转换尚未隔离；因此仍属于有潜在 GPU 开销的实验规避，不宣称完成831硬件支持。没有增加逐 draw 的 idle wait。补丁工具检查固定源码哈希，作用范围回归编译真实 guard，验证精确芯片/进程与显式调试行为。

配对 ZIP SHA256 `b8be5988c4e2bd7a970be5e283af2f8d03449cbb9615784cedd8386290370925`；Linux 库 `ecb33868039df93bb4ebf7680438bc924f4ad29c5fcf307be7669039b01e9315`，Android 库保留原始 `a133b74d…`。已通过原有组件导入器安装并选择两端 prototype3，实际安装 Linux 文件哈希一致。旧驱动保留可回退。APK `0d049369cd7ec4638ca86f00873433536703404916ee6afd78ace57045e1d924` 已安装；资产、依赖、签名、对齐检查通过，既有3份 guest 库与 OpenXR loader 哈希不变，Android XR 原生库更新。

正式驱动无临时参数的冷启动、Aircar 回归、佩戴体验与本轮权限清理尚待后续记录，不能以诊断参数对照代替正式包验收。

正式包复验：会话20的 Steam 前端未达到 READY，正常停止后重试会话21成功；未清除账号/游戏数据。会话21无 active/pending/persistent 调试环境变量，prototype3 的完整 Dashboard 双眼截图未见原有右眼缺块，随后 Aircar 正常进入实际双眼城市场景。Dashboard 的30个健康样本 transferMs 中位数0.32ms、最大0.78ms；Aircar 的30个样本为0.29ms、最大0.47ms，完成通知错误均为0。这些是显示线程交接耗时，不是游戏帧率。证据为 `prototype3-dashboard/`、`prototype3-aircar/` 及对应 health JSON。canonical APK 已更新，旧11dd版本另存 before-graphics 供回退。专用构建 VM 已关闭，游戏保持运行等用户佩戴复测，临时调试权限尚待复测后关闭。

本轮收尾已完成：等待期间未收到佩戴反馈，因此正常停止会话21，最终 `IDLE / running=false`；通过设备界面关闭 Debugging tools，UI为 Off，访问 API `commands=false`，临时 active/pending/persistent 环境变量为空。本轮新增设备探针已移除，主机源码/证据与旧驱动保留。新的佩戴体验、整体游戏 FPS 和长时间稳定性仍待用户复测，不宣称已解决全部卡顿。最终证据为 `report.json`、`access-after-cleanup.json`、`debug-after-off.json`、`state-after-cleanup.json` 与 `env-before-cleanup.json`。

## 17. 转头时画面滞后与晃动（2026-10-21）

用户佩戴 prototype3 后确认“右眼不撕裂了”，但转头时画面跟不上、来回晃，SteamVR 菜单与 Aircar 均存在。随后明确授权本轮临时开启调试、验证后关闭。正常停止会话23后，通过实际设备诊断界面开启开关，未通过受保护的代理设置接口绕过开关。

### 修复范围

原 Vulkan 显示层按单调时钟零点与整数刷新率计算虚拟 vblank，与 OpenXR 实际预测显示时间没有相位关系。现在仅在 `questwsi` 的 `vrcompositor`、追踪包仍新鲜且预测时间可转换时，使用 OpenXR 的 `predicted_ns / period_ns` 推导上一次显示边界；显示事件等待与 vblank 计数使用同一时间基准。计数不倒退，绝对时间等待被信号打断时继续等待。没有有效 XR 时间时保留原有节拍。

修改位于共享 Vulkan 显示层，Android XR / Quest 共用，不增加设备专用开关或应用界面。prototype3 驱动、游戏画质、分辨率、共享图像协议与帧姿态元数据均未改变。本轮未证明 WSI 提交时读取的最新追踪姿态就是合成画面的原始渲染姿态；该配对问题不能仅凭节拍修正宣称排除。

### 构建与验证

新增回归编译实际显示层，验证72/90/120Hz、预测显示相位、单调计数、被中断等待与非XR退回路径；在 AURA 的现有 Linux 运行库中执行通过。既有 WSI 捕获回归与合成器异步提交回归在 AURA 执行通过；WSI 测试允许指定可写临时目录，避免依赖 Android 不存在的 `/tmp`。24项可在 macOS 运行的设置与 XR 验证器测试通过。Linux 启动助手测试在 macOS 因缺少 `linux/capability.h` 无法执行，不记为通过；该助手源码未改。

离线 Gradle 构建、runtime inputs、session assets、Android ELF依赖、签名与对齐检查通过。只有 `assets/linuxfs/usr/local/lib/droiddeck-vr/layer/libVkLayer_droiddeck_vr.so` 这一份已准备运行资产发生改变；设备文件哈希与产物一致，为 `7b94fcd69394cec16e2ca9d44b66082d4568d83c4d53bfa51bcd9fb59f19d729`。

测试 APK 与更新后的 canonical APK 的 SHA256 为 `69a0354aa4ec339c862a248026ee631bba4418122c47cb8edcbd91c9a86a1076`，旧 APK 保存为 `DroidDeck-XR-Dev-0.5.0-arm64-before-head-motion.apk`。会话24冷启动至 READY，SteamVR持续接收真实图像；日志确认 XR display clock 生效，周期13.889ms，对原始时钟相位的修正约11.8–11.9ms。18个短时健康样本同 generation，提交计数1246→1920、源图937→1594，完成通知错误0，sourceAgeMs中位数20.35、最大37.21。这些数字不等于游戏FPS或受控性能对比。初始全局 Thermal Status2，部分传感器已更高，不作为完全冷机基线。

用户佩戴菜单复测的原话是“明显改善”。据此确认菜单转头体验改善，不将其写成全部抖动消失；Aircar转头与本轮调试清理结果待下文记录。证据位于 `build/xr-validation/aura-head-motion/`。

用户随后更正了佩戴结论：“现在的抖动是时好时坏的，在 launcher 里面不开 dashboard，很丝滑，打开 dashboard 就会抖”。此前“明显改善”不作为稳定修复通过，Aircar头动亦未验收；继续对照 Dashboard 开关时的负载与帧时序。


### Dashboard 开关对照与普通调度优先级

用户进一步明确顺滑的是 SteamVR 背景，打开 SteamVR Dashboard 后抖动。使用 Valve 官方 `openvr_capi.h` 与设备已有 `libopenvr_api.so` 编译一次性只读诊断，作为 Background 客户端读取 `GetFrameTiming`。没有提交图层或输入，不加入 APK；设备上的诊断将在本轮结束移除。

同会话相邻8秒对照：关闭 Dashboard 时576个样本、GPU中位数3.10ms/最大3.60ms、CPU最大2.86ms、报告掉帧合计0；打开 Dashboard 时565个样本、GPU中位数3.66ms/最大4.00ms、CPU最大29.89ms、报告掉帧合计12，出现8个大于20ms的观测间隔。工具是轮询而非完整硬件采样，掉帧和间隔不能替代光学/佩戴验收。

AURA 的 `RLIMIT_RTPRIO=0`、无实时调度权限，但 `RLIMIT_NICE=40`，允许应用提高自己线程的普通调度优先级。实际 vrcompositor 的 VulkanVblankThr、SignalThread、RenderThread 为 nice0，而部分 Steam UI 线程是 nice-1。临时仅将显示线程提高至 nice-8 后，相同8秒菜单采样得到577个样本，CPU最大1.49ms、报告掉帧合计0、无大于20ms的观测间隔；GPU最大4.60ms。该对照不是严格控制温度、场景动画与后台工作的长期性能基准，也不能据此断言所有抖动原因都已排除。

自动处理复用现有 `libdroiddeck_vr_scheduling.so`：仅对进程名为 vrcompositor、无法实时调度、成功为当前线程命名且名称为 RenderThread / SignalThread / VulkanVblankThr 的调用，尝试将普通 nice 提高至-8；保留已有更高优先级，失败则保留原值并记录原因。其他进程、后台线程、命名失败和其他线程目标不受处理。保留真正的 pthread 返回值与实际 SCHED_OTHER 策略，不伪造实时权限或变更系统温控。线程退出后调整自然消失，用户无需操作额外开关。

调度 C 回归已在 AURA 执行通过，包括对应名称、其他进程/后台名称、命名错误、实际非实时策略，以及原有实时预算保护。新 APK 已完成构建与资产/ELF/签名/对齐检查，SHA256 `ca4e2ecce0ad1e91b1a8bdb7ce6ae80ea62795c882cb4871d5ae0ec15778cb48`。相对本轮开始的 APK 仅更新共享显示层和既有调度兼容库两份 guest 资产。会话25全新进程实际日志确认 VulkanVblankThr、SignalThread、主 RenderThread 自动从 nice0 到-8，后台 disk/trace 仍为19，其余进程未提升。用户明确暂不方便继续佩戴，不能将自动生效当作 Dashboard 体感修复通过。


第一次自动版本（会话25）的12秒菜单检查：862个样本，CPU中位数1.01ms、p95 1.34ms、最大4.32ms，报告掉帧合计3。不能将手动8秒0掉帧写成自动版本零掉帧。该轮最新帧 GPU 计时有一次约75724416ms的无效值，原始数据保留，不作为真实GPU耗时；后续诊断改读3帧前的已完成帧，避免把尚未回填的计时混入性能结论。

进一步检查发现 WSI 发送工作线程继承了 RenderThread 名称但没有自行命名，仍为 nice0，与手动对照中被提高的线程范围不完全相同。为它明确命名 XRFrameSender，并将该实际图像传输线程纳入同一普通优先级规则；不提高其他后台线程。对应名称的回归在 AURA 通过。最终 APK SHA256为 `b0c6c616eb6e4a877f407ace300062e2ee4b3c280315ba49cd617b9a623136c3`，替代前述 ca4e 的中间测试包。最终冷启动和清理结果见后续记录。


最终会话26冷启动确认 VulkanVblankThr、主 RenderThread、SignalThread 为 nice-8，XRFrameSender 同样为-8（已继承更高优先级时不重复修改）。菜单打开、等待2秒后读取3帧前的已完成帧，12秒得到861个样本：GPU中位数3.70ms、p95 3.86ms、最大4.05ms，无无效GPU计时；CPU中位数1.00ms、p95 1.49ms、最大3.56ms。仍有4个报告掉帧和4次大于20ms观测间隔，最大28.01ms。仅确认 CPU 尖峰较先前对照减小、自动优先级有效，不宣称稳定零掉帧或全部抖动已解决；也不能跨新旧取样深度作严格整帧比较。

最终 APK 与两份 guest 库的设备哈希均与交付产物一致。canonical APK 更新为 b0c6c616…，原始0d049369…保存在 before-head-motion APK，第一次时序候选69a0354a…另有 head-motion-test APK。prototype3显卡驱动始终未改。本轮不新增应用控制面板或常驻辅助进程；最终测试会话已正常停止到 IDLE，设备临时诊断文件已移除，主机证据和可复现源码保留。调试开关最终状态以 `debug-after-off.json` 与 `access-after-cleanup.json` 为准，最终状态以 `state-after-cleanup.json` 为准。用户暂不方便佩戴，Dashboard头动和Aircar体验仍待下一次人工复测。

收尾确认：实际设备开关为 Off，访问 API `commands=false`，最终会话26为 `IDLE / running=false`；本轮临时调试已关闭。


## 18. Dashboard 头姿与速度对照（2026-10-21，部分改善，仍偶发抖动）

用户再次确认：SteamVR 背景顺滑，Dashboard 打开后转头明显抖动。此前普通显示线程优先级修正不能作为视觉问题已解决的证据。

设备 SteamVR 2.17.10 的有效 `driver_droiddeck/forceSystemLayerUseAppPoses` 与 HMD 属性2096均为 true。通过 Valve 标准设置 API 临时关闭，正常重启后设置与属性均确认 false，合成器新启动日志也不再出现“Driver requests system layer use app poses”。Dashboard 双眼抓取正常，但用户佩戴明确反馈“仍明显抖动”。已用标准 API 删除本轮新增键并正常重启，确认有效设置与属性恢复 true；不将这一项纳入产品修改。

只读 OpenVR 探针发现之前的 HMD 线速度和角速度始终为零。Valve 官方驱动文档要求在驱动世界坐标系提供速度用于预测。现在向 `xrLocateSpace` 链入核心 `XrSpaceVelocity`，优先传递运行时的有效速度。AURA Android XR 1.0.0 实际未返回有效角速度，所以增加连续可靠预测头姿的差分补算：位置差得到米/秒，`q_now * inverse(q_previous)` 的最短旋转得到世界坐标弧度/秒。原点改变、丢失追踪、时间倒退以及小于1ms或大于100ms的采样间隔不会用于补算；原生有效速度优先，非有限分量不传给 SteamVR。

追踪协议同步升至 v2/328B；haptics 的64B布局和帧头264B布局保持不变。Android XR、Quest 共用这一路径，Android Runtime、Linux HMD 驱动、WSI消费者和 xr-display-mode 必须配套更新。未改变 prototype3 GPU 驱动。该补算修复缺失的预测输入，并未证明提交帧头的姿态就是 SteamVR 合成画面的精确渲染姿态；图像与姿态配对仍须继续核实。

4项 CMake 主机回归（含新头部速度数学/原点/追踪/时间测试）、24项设置与XR验证器 Python 回归通过。追踪IPC和WSI同步回归在AURA通过。离线APK构建、runtime inputs、session helpers、ELF依赖、签名及对齐检查通过。测试 APK 为 `build/deliverables/DroidDeck-XR-Dev-0.5.0-arm64-head-velocity-test.apk`，SHA256 `80880200b3d9cbeda4365ae807171c8676447383f5c6d5f2d1385d3521b9902b`，已安装并启动会话31；canonical APK 暂不替换。

设备只读验证中，补算版连续600个样本的角速度有效，SteamVR渲染/原始姿态均读取到非零速度。该次头显基本静止，峰值约0.014rad/s，只证明传递生效，不作为真实转头性能或佩戴顺滑结论。完整证据位于 `build/xr-validation/aura-pose-repair/`。用户已受邀佩戴测试；本轮调试开关当前暂时开启，验收与清理结果在后续追加。


用户对速度修正版的佩戴结论：“好一些了，但是还是会偶尔抖”。因此只记为部分改善，未验收全部抖动消失。随后16秒只读帧时序采样1153条：合成CPU中位数1.021ms/最大1.918ms，GPU中位数3.725ms/最大4.349ms，报告掉帧合计0、无大于20ms的观测间隔；该采样没有逐次标注用户看到抖动的时刻，不能据此排除残余掉帧。现对已含速度数据的版本临时关闭 `forceSystemLayerUseAppPoses` 再做组合对照，属性与设置均确认 false，佩戴结果待记录。标准设置 API 修改前后的键检查仅增加这一项，没有其他新增键。官方依据见 [Valve DriverPose 规范](https://github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md#driverpose_t)。


速度修正与独立菜单头姿的组合对照，用户反馈“差不多，仍偶尔抖”。已再次删除临时设置并正常重启，保留原默认菜单策略。

进一步日志中出现 WSI ACK 等待30.00/33.61ms，超过72Hz两次刷新周期。原发送线程按FIFO等待GPU并发送所有积压帧。队列测试版在当前帧GPU fence完成后检查是否存在更新且已经完成的排队帧；满足时跳过旧帧。尚未完成GPU工作的新帧不导致丢弃，已经向接收端公布的缓冲仍等待原ACK，不改变所有权和同步协议。只有一次性诊断日志记录实际跳过次数。

更新后的WSI回归在AURA通过，新增检查更高frame id、fence未完成、frame未排队和缓冲busy状态保留。队列 APK 的SHA256为 `0a1318299cb64df51dec78ae58f0edb3be25ae46f5ced1d1186fec07ae4dc821`；runtime inputs、helpers、ELF、签名、对齐检查通过。会话32启动，设备WSI库哈希 `b74732356ef1e2ab5f44c386b3fab62e28af9f6e096d22e768a613ba74ead646` 与APK一致，2880×900 GPU捕获正常。该启动短样本尚未出现实际跳过日志，不能把更低的单次sourceAgeMs归因于队列修正；仍等待佩戴结论。


会话32后续真实日志确认跳过已完成旧帧2737（36.93ms）、3041（17.99ms）、4220（12.50ms）。连续10个健康样本 source frame 3777→5124，完成通知失败与错误均0，sourceAgeMs为7.82–20.68ms。只据此确认路径触发和持续传图，视觉改善仍待用户反馈。


### 本轮最终保留与撤回

用户对队列版反馈“仍然差不多，偶尔抖”。因此本轮只保留经用户确认有改善的头部速度修正；独立菜单头姿设置和队列跳帧试验均撤回，源码与运行资产恢复为速度版。队列试验的记录和测试APK保存在验证目录及deliverables，未混入当前源码或默认APK。

设备恢复安装速度版，canonical `DroidDeck-XR-Dev-0.5.0-arm64.apk` 同步为SHA256 `80880200b3d9cbeda4365ae807171c8676447383f5c6d5f2d1385d3521b9902b`；先前canonical备份为 `DroidDeck-XR-Dev-0.5.0-arm64-before-head-velocity.apk`。恢复后的运行资产与该APK的runtime inputs验证通过。正常停止试验会话、保存会话31/32日志，删除设备上的pose/frame-time探针、IPC/WSI测试程序及临时设置备份；实际设置界面“Debugging tools”为Off，控制接口commands=false。

偶发抖动仍未解决。现有WSI帧头从提交时的最新追踪包标记姿态，未证明与SteamVR真正绘制/重投影的相机姿态精确对应；头部速度补齐能改善预测输入，不能代替精确的图像-姿态关联。此为下一步需要验证的疑点，不将其直接认定为剩余问题的已证实根因。


## 19. AURA 普通Steam显示与LIMBO启动（2026-10-23）

通过应用正常显示设置将Steam会话从默认2304×720改为自定义1280×720，重启后状态接口确认READY与1280×720。未改变全局默认代码。

随后用户启动LIMBO（48000，Windows x86）出现 `Pixel shader error: <anonymous>:19:20: E5000: syntax error, unexpected ';'`。ADB指定主显示4630946706660803329抓取到实际对话框；会话仍READY，报错来自游戏。原前缀d3dcompiler_43.dll含Wine builtin DLL/vkd3d标记，游戏导入d3dx9_43.dll。

用户明确同意本轮临时开启调试、验证后关闭。使用游戏自身 `Limbo/DirectX_Redist/Jun2010_D3DCompiler_43_x86.cab` 和 `Jun2010_d3dx9_43_x86.cab` 中的32位微软DLL，分别安装到limbo.exe同目录；仅在该游戏Wine前缀user.reg的 `Software\\Wine\\AppDefaults\\limbo.exe\\DllOverrides` 中设置d3dcompiler_43、d3dx9_43为native,builtin。未覆盖共享兼容层文件。原user.reg在设备应用私有目录 `files/compat-fixes/48000-directx-20261023/user.reg.before` 备份；本次新增两个游戏目录DLL，回滚时只需移除这两个新增文件和该AppDefaults覆盖键，保留其他后续注册表变化。

正常重启Steam后，游戏从原着色器错误进入LIMBO标题界面，发送一次ENTER后森林场景可渲染；进程maps确认加载游戏目录里的d3dx9_43.dll。截图、DLL哈希与报告位于 `build/xr-validation/aura-limbo-20261023/`。这是当前设备游戏安装的局部修复，不包含在APK或源代码默认兼容配置中；未验证完整流程、声音和实际手柄操作。临时调试关闭与再次正常启动结果见报告最终状态。
