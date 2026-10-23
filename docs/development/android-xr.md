# DroidDeck 共用 OpenXR / SteamVR 桥接方案

> 当前状态（2026-10-23）：后续实现和真机结果以 runbook 第10.5、12、16、18、19节为准。已加入完整关节桥接、普通手柄和头部速度传递；Aircar 的场景、转头和手柄通过用户测试，右眼撕裂已获用户确认消失，Dashboard 偶发抖动与裸手完整验收仍未完成。本文下面较早的“未实现骨骼”等描述保留为阶段背景，不能代表当前实现。

版本 v4 · 2026-10-20 · 分支 `android-xr` · 上游基线 `ffa40a15d5655fb7fb2aa22cdf51d13f7ba95af1`（2026-10-23迁移；历史验收对应旧测试APK）

当前真机适配目标是 XREAL AURA，设备系统型号为 Android XR Puck（Android14 / API34、arm64-v8a）。通过 Android XR 标准能力接通现有 DroidDeck SteamVR 链路，同时让 Quest 使用同一套裸手桥接；不增加按 AURA 型号分支的输入实现。DroidDeck 提供 Android 上的 Linux / Steam / Proton 环境，并非完整 SteamOS 虚拟机。

本版已经实现共用原生运行时、Android XR Activity、Quest 输入兼容、SteamVR 驱动消费和独立 smoke 应用。编译与自动化结果见[操作手册](android-xr-runbook.md)。AURA 已完成主 APK 安装与 Linux r9 运行环境准备，初次选用的通用 Linux Turnip 驱动拒绝 Adreno 831（芯片 `0x44050100`）。现已构建 Android/Linux 实验驱动配对包，通过原有 ZIP 导入器安装；两端计算与离屏绘图通过，AURA 的 Linux 桌面已 READY 且确认首帧。Steam Big Picture 登录页已实际显示并截图确认；SteamVR 安装、启动与持续双眼图像传输已通过，右眼仍有局部花屏；账号登录、佩戴体验和裸手仍待单独验收。仍不能把代码完成、模拟器通过或设备 feature 声明解释为 OpenXR、裸手或 SteamVR 真机验收通过。

## 1. 产品边界

- 首版必须可走裸手路径，不要求两个物理手柄；Quest 默认自动选择手或 Touch，Android XR 入口要求裸手。
- 复用现有 2D 设置、SteamVR Dashboard、SteamVR 输入组件及动作绑定。不增加独立控制面板，不开发新的手势识别系统。
- OpenXR runtime 提供 aim / grip / pinch / grasp；桥接负责传递、坐标转换、能力缺失与失效释放。
- Meta 扩展和标准扩展的差异仅在采集侧。输入协议、原子 IPC、SteamVR 映射、释放策略、验证工具全部共用。
- 无摇杆、ABXY、触觉或骨骼能力时不伪造。能进入 Dashboard 不代表全部 VR 游戏兼容。
- 系统导航与保留手势由头显系统处理。退出沉浸会停止当前 SteamVR，可能中断游戏；不承诺无损往返。
- 官方文档仍将 Android XR Emulator 的 OpenXR 应用列为不支持。模拟器只验收 2D 启动器；本机预览镜像的额外实验观察见操作手册，不能作为真机通过。[官方说明](https://developer.android.com/develop/xr/openxr)

## 2. 工程结构与调用链

```text
现有 SessionActivity
  ├─ QuestXrActivity ──────┐
  └─ AndroidXrActivity ────┤ GuestXrActivity（guest 生命周期 / owner）
                          └─ NativeActivity: droiddeckxr
                             └─ xr-core/OpenXrRuntime.cpp
                                ├─ loader / GLES / stereo / lifecycle
                                ├─ EXT hand actions 或 EXT joints + FB aim
                                ├─ v1 tracking + v2 input
                                └─ 既有 GPU / DMA-BUF frame receiver

guest vrserver: driver_droiddeck
  ├─ v1 HMD / Touch 保持兼容
  └─ v2 hands → 现有 trigger / grip / pose / aim 组件

xr-smoke（独立构建）
  └─ RuntimeProbeActivity → 同一 native runtime，独立双眼测试场景
```

`app/src/main/cpp/questxr/quest_xr.cpp` 仅保留 native entry。共用实现和 GPU 帧接收器位于 `xr-core/src/main/cpp`，没有复制 Android XR / Quest 两份渲染循环。

Kotlin `xr-core` 是 JVM 模块，仅保留平台识别与主应用实际使用的入口检查；native 使用同目录独立 CMake 构建。未接入的会话 owner、通用 backend 接口、模拟能力状态机和空回调已删除。`-PxrCoreOnly=true` 不配置 Android 工程；`-PxrSmokeOnly=true` 不配置完整 app，二者互斥。

## 3. 启动、探测与退出

2D 入口只检查设备 feature、native 接线与现有 Steam guest 状态。返回 `READY_TO_PROBE` 只表示可以启动 Full Space Activity，不能证明 OpenXR 可用。真实探测在 NativeActivity worker 中执行：

1. 初始化 Android OpenXR loader，枚举扩展。
2. 创建 instance / system，取得实际 runtime 名称与版本。
3. 检查 GLES 要求，创建 EGL context，并验证实际 GL 版本。
4. 创建 session、空间、actions、双眼 swapchain。
5. 非 smoke 模式建立 tracking / input / 图像通道。
6. session VISIBLE / FOCUSED 时调用 Android callback，接上 compositor 并启动 SteamVR。
7. guest 状态监控启动限时 30 秒；运行后连续失去 RUNNING 5 秒返回 2D。不可见时暂停此监控。
8. 不额外启动菜单助手；SteamVR 自行管理 Dashboard，XR 启动与输入不依赖菜单状态。

同一时刻的 SteamVR owner 由共用 `GuestXrActivity` 维护。旧 Activity 退出不能停止替代 Activity 的 guest。会话身份还必须匹配当前 Steam session id。

失败写入 `xr-health.json`，经 JNI 显示错误并结束 Activity。Native 析构顺序为释放输入、停止接收器、解除 guest、解除映射、销毁 swapchain / spaces / trackers / session / action set / instance / EGL。JNI 在 native worker attach / detach 一次。原生退出后继续处理 glue 消息直至 destroyRequested，确保初始化早期失败也能确认 UI 线程的 START / RESUME / DESTROY，避免启动画面死锁。

## 4. 输入能力选择

| 能力 | 采集实现 | 下游行为 |
| --- | --- | --- |
| `XR_EXT_hand_interaction` | 标准动作值与 ready / active 标记 | 任意支持 runtime 共用，包括 Android XR / Quest |
| `XR_EXT_hand_tracking` + `XR_FB_hand_tracking_aim` | runtime aim pose、index pinch strength、palm pose | Meta/Quest 兼容采集适配器 |
| `XR_EXT_hand_tracking_data_source` | 请求并验证 UNOBSTRUCTED 来源 | 避免把控制器生成的关节当裸手 |
| Touch controller profile | 原有 grip / trigger / squeeze / stick / ABXY / haptic | 保留现有 Quest 手柄映射 |

Android XR 当前通过标准 action provider 工作，不申请高精度关节权限。FB provider 按实际 HAND_TRACKING 权限、扩展与 tracker 创建结果启用，provider 选择不检查头显型号。Quest manifest 声明可选 handtracking feature 及 `com.oculus.permission.HAND_TRACKING`，与 Meta 官方示例一致。[Meta 示例](https://github.com/meta-quest/Meta-OpenXR-SDK/blob/main/Samples/XrSamples/XrHandsFB/Projects/Android/AndroidManifest.xml)

标准 profile 为 `/interaction_profiles/ext/hand_interaction_ext`，左右子路径 `/user/hand/left`、`/user/hand/right`：

| 语义 | 动作路径 | 类型 |
| --- | --- | --- |
| aim | `/input/aim/pose` | pose |
| grip | `/input/grip/pose` | pose |
| pinch | `/input/pinch_ext/value`、`ready_ext` | float / bool |
| grasp | `/input/grasp_ext/value`、`ready_ext` | float / bool |

所有 actions 属于同一个 action set，每帧只 sync 一次。Touch bindings 在不支持的 runtime 上允许 PATH_UNSUPPORTED；标准手 profile 绑定不可用但 FB provider 可用时允许使用后者。

FB provider 将 `XrHandTrackingAimStateFB` 链接到 `XrHandJointLocationsEXT`。仅接收 active、aim valid、非 SYSTEM_GESTURE 的结果。没有 data-source 扩展时保守拒绝当前 Touch profile 的关节输入。palm 用作兼容 grip；pinch 来自 runtime 的 index pinch strength。**此适配器没有标准 grasp 信号，grasp capability 保持关闭，不自行计算握拳。** [Khronos 扩展](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrHandTrackingAimStateFB.html)

`xr_input=hands|controllers|auto`：

- hands：只允许裸手；缺少可用 provider 明确失败。
- controllers：保留 Touch。
- auto：实际采集到手时切为 hands；手跟踪暂时丢失保持 hands 并输出 neutral。只有明确恢复 Touch profile 才切回手柄。
- 来源 / profile 切换产生 neutral 帧，不沿用捏合。单手可用，另一只保持 neutral。

## 5. v2 协议与原子 IPC

tracking 协议 v2 为328B，追加头部线速度、世界坐标角速度及有效标志；Android 端与 Linux 消费者必须配套更新。haptics 64B、frame header 264B 布局保持不变。独立 sidecar `droiddeck-xr-input-v2` 固定256B，小端格式。

| 偏移 | 大小 | 字段 |
| --- | --- | --- |
| 0 | 4 | magic `DXI2` |
| 4 / 6 | 2 / 2 | version=2、header_bytes=64 |
| 8 / 12 | 4 / 4 | byte_size=256、sequence |
| 16 / 24 | 8 / 8 | session generation、origin epoch |
| 32 / 40 | 8 / 8 | sample、predicted monotonic ns |
| 48 / 52 | 4 / 12 | flags、reserved |
| 64 / 160 | 96 / 96 | 左 / 右手 |

每只手包含 aim 32B、grip 32B、保留的 select 槽与 pinch/grasp 共12B、ready/status/source/capability/reserved 20B。select 槽保持零，保留字段偏移以兼容现有256B协议；当前不创建或轮询 select actions。pose 为 position[3]、quaternion xyzw[4]、OpenXR flags。所有字段偏移均由 C/C++ static assertion 检查。

flags 区分 FOCUSED、PREDICTED_TIME_CONVERTIBLE、HAND_MODE。generation / epoch 必须非零。姿态必须 finite、有效位置与方向、单位四元数；交互还要求 grip 的位置和方向均 TRACKED，仅 VALID 的推断姿态不允许继续按住按钮。sample 最长有效 100ms，未来时间拒绝。

`XrAtomicFile` 对每个 32-bit word 原子读取 / 写入，以 sequence seqlock 验证快照，最多重试三次。writer 有非阻塞 flock 唯一所有权，文件必须是当前 UID 的普通文件，0600，禁止 symlink、错误大小；reader 识别 inode 替换并重新映射。reader 失败不能回退到旧的裸手或猜测输入。

宿主目录是 app `files/.wayland-rt`，guest 映射为 `/run/droiddeck`。运行时创建 generation，origin epoch 从 1 开始；不复用先前进程的包。

## 6. SteamVR 现有输入的复用

`CardboardController::UpdateXrHand` 是桥接入口，不承担手势识别。它使用现有 `XrTouchInput` 与 OpenVR driver 组件，将标准 runtime 信号投递给 SteamVR：

| 用途 | 输入 | SteamVR 组件 |
| --- | --- | --- |
| 共用动作桥 | grip + pinch | raw pose、trigger value / click |
| runtime 有 grasp 时 | grasp | grip value / click |
| 支持单独 aim binding 的应用 | aim 相对 raw 的刚体变换 | `/pose/aim` |

浮点点击阈值沿用既有 Touch 桥的 0.6。重新获得焦点、切换会话 / origin 后必须先观察释放值，才重新允许按钮；不是重新识别手势，也没有驻留、双击、掌心菜单、虚拟摇杆等自定义系统。

Android XR 与 Quest 统一使用 runtime 提供的 grip / aim 与 pinch / grasp，不按菜单或游戏场景切换映射，不查询 SteamVR 控制器模型 tip，也不额外补偿菜单射线。菜单显示与应用交互由 SteamVR 及其绑定管理；裸手在 Dashboard 中的实际指向与点击仍需真机验收。

```text
Device pose: T_world_device = T_world_grip
Aim offset:  T_device_aim   = inverse(T_world_device) × T_world_aim
```

驱动保留 Touch profile / 模型作为 SteamVR 动作绑定兼容层，设备制造商与型号标为 DroidDeck。该兼容层不代表完整 SteamVR 手部骨骼已实现。

失焦、姿态无效、输入过期、epoch/generation 变化、source 不允许时清零输入。裸手 v2 过期时不能恢复 v1 Touch 的旧按键。`ClearInputs` 同时释放 trigger/grip、摇杆、ABXY/touch 等既有组件。

完整 SteamVR Skeletal Input 骨骼仍未实现；没有把 26 个 OpenXR joints 伪装成 SteamVR 31 骨骼。现有 SteamVR 动作绑定可以使用姿态和 trigger 桥接，Dashboard 裸手操作及要求完整骨骼、摇杆或 ABXY 的游戏另行验证。

## 7. 渲染、坐标、时间与图像所有权

- 世界空间依次选择 STAGE、LOCAL_FLOOR、LOCAL。不为 LOCAL 注入假地面高度。`floorKnown` 单独记录，游戏不得据此假定房间边界已测量。
- `XR_KHR_convert_timespec_time` 可用时才产生 predicted monotonic 时间；否则 predicted 为零，不能直接把 XrTime 当 Android/Linux monotonic。
- eyes 的有效 flags 来自 `XrViewState`。`shouldRender=false` 或视图无效时提交零层，但继续处理生命周期和 neutral 输入。
- 只接受两眼 PRIMARY_STEREO、sRGB swapchain；按 runtime 推荐尺寸创建，检查 GL 版本及可用 blend mode。
- 每帧 wait/begin/end 用 scope guard 配对；图像 acquire 后最多 wait 500ms，成功 wait 后必须 release。异常时提交空层并清理会话。
- reference-space change 到达生效时刻后释放、返回 2D，要求重启整个 guest XR 会话。v1 frame 协议没有 epoch，不能混合重定位前后的 guest 图像。
- 非 smoke 显示已有 SteamVR owned frame，使用该帧保存的每眼 pose/FOV；拒绝过期 / 非递增 frame id。没有把 Android 屏幕截图冒充双眼。

画面传输复用既有共享 GPU buffer 与 DMA-BUF CPU staging。新增的普通像素复制分支、强制 transport 参数及跨进程配置文件已删除。帧同步、ACK、FD 归属与断连隔离继续使用上游实现。

## 8. 构建、资产与验证层级

固定工具链：JDK17、Gradle8.10.2、AGP8.8.0、Kotlin2.1.0、NDK27.3.13750724、SDK CMake3.22.1、OpenXR loader1.1.49。主 app ARM64；smoke ARM64 / x86_64。native Activity 库声明16KiB页对齐。

`tools/vr/build.sh` 编译驱动、显示模式工具与传输层，`SessionFiles` 在每次 guest session 启动时部署当前产物。`tools/build_local.sh` 的 VR cache key 已纳入共用 native 源；runtime input attestation 同时覆盖共用源和 guest 必需输出，避免新源码配旧二进制。菜单助手、专用 IPC 和仅供该助手使用的 OpenVR client SDK 已移除。

| 验证 | 可以证明 | 不能证明 |
| --- | --- | --- |
| Kotlin / C/C++ host tests | 平台路由、实际入口检查、ABI、原子包、释放与转换 | runtime / GPU / 游戏可用 |
| 主 app native / JVM 编译 | 源码集成正确 | 完整 APK guest 资产完备 |
| smoke Debug / Release 构建、lint | 两 ABI 原生入口可编译、真实 smoke 可独立构建 | 模拟器支持原生 OpenXR |
| XR AVD 2D 启动器 | 安装、界面、主应用冷启动 | 头显追踪或 SteamVR |
| 本机预览镜像 native 实验 | 可以初始化并持续提交本地测试场景 | 两眼姿态相同、手 inactive；不能证明立体深度 / 裸手 / guest |
| Linux ARM64 交叉编译 | guest 库与显示模式工具可生成 ELF | 它们已在 SteamVR 中运行 |
| Linux Docker IPC / WSI 测试 | 输入释放、帧同步、FD 归属、跨进程原子包、断连隔离 | 完整 SteamVR 或头显游戏运行 |
| 完整主 APK 成品校验 | 运行资产、JNI 依赖、XR native 入口、ARM64 payload、签名完备 | 真机兼容性、裸手或游戏体验 |
| 真机 smoke | 本地 stereo / runtime actions | 完整 guest 或全部游戏兼容 |
| 真机 steamvr | owned frames、RUNNING、runtime 输入 | 光学来源与全游戏覆盖自动证明 |

`tools/test_xr.py` 为 Quest / Android XR 共用验证器，拒绝模拟数据、模拟器、跨 generation、未跟踪头姿态及无实际 pinch press/release 的手输入。原生 `xr-health.json` schemaVersion=3，按与驱动相同的 `SteamVrHandBridge` 判断 readiness、active、来源、跟踪与释放后重置，累计 `handPinchPresses` / `handPinchReleases`；不再用 select 计数验收 pinch 映射。工具只接受最后仍 active 且 provider 有效的同一只手完成操作，报告 `steamVrInputDeliveryVerified=false`，不把 Android 侧计数当作 SteamVR 消费确认。smoke 允许本地合成图像源；steamvr 必须有 advancing owned images。Dashboard 的实际指向与点击由真机人工验收。真实立体验收还必须有非零眼间距；两个 swapchain 不足以证明立体深度。报告区分 runtimeRefreshHz 与实际 frame rate。运行时 action 测试通过仍不自动声明光学来源或完整骨骼已验收。

## 9. 当前真机验收

完整主 APK 已于2026-10-20通过标准 Docker 资产准备、Gradle 构建、资产清单、JNI 依赖及签名检查，包含商店引擎。交付文件为 `build/deliverables/DroidDeck-XR-Dev-0.5.0-arm64.apk`，完整记录见 runbook 和 `build/xr-validation/full-build/report.json`。

1. AURA 已连接，主 APK 安装及文件哈希核对通过；系统声明 OpenXR 与 hand_tracking feature，Linux r9 已安装。实际 SoC 为 SXR2330P、GPU 为 Adreno 831；选中的 WN Linux Turnip 0.1.2-b 报芯片不受支持，Steam 尚未启动。精确芯片匹配的 Android/Linux 实验配对驱动已通过独立计算与离屏绘图，原有导入器安装及选择通过，Linux 桌面已 READY 且 firstFrame=true；Steam Big Picture 登录页也已显示；SteamVR 安装、启动与图像传输通过，右眼花屏待修；账号登录和裸手验收仍待完成。本地 smoke 使用系统图形栈，可以单独验收 OpenXR，但不能绕过 guest 驱动阻塞。设备信息与日志位于 `build/xr-validation/aura-install/`，该目录不纳入源码。
2. Quest 必须分别实际验收裸手、Touch、手柄切手、失焦恢复。移开 / 禁用物理控制器来人工确认裸手输入来源。
3. 验收双眼差异、左右眼次序、6DoF、系统手势、返回 2D、Dashboard 点击、跟踪丢失释放、guest 崩溃、重定位重启、20分钟稳定性及温控。
4. 依游戏记录缺失摇杆、ABXY、骨骼、触觉的影响。只有完成这些真机记录后，才可称为 AURA 可用版本；其他 Android XR 设备另行验收。

## 10. 后续扩展备注（未启用）

- 完整手部骨骼透传：这是尚未完成的能力，需要定义 OpenXR joints 到 SteamVR Skeletal Input 的骨骼层级、坐标与协议扩展；当前不预留空实现，不声称已支持。
- 第二种运行时或 IPC 实现：实际出现替代实现后再抽象接口；当前直接使用 OpenXR 与映射文件通道。
- 标准 select 绑定：只有实际应用绑定需要时再启用；v2 select 槽暂为零，不创建动作或模拟系统选择手势。
- 普通像素复制、菜单射线补偿：只有真机确认现有传输不可用或菜单射线偏移后再评估，不保留休眠代码。
- 模拟能力状态界面：当前没有真实 native 探测输入，不保留仅验证自身枚举的页面。保留 JVM 入口测试、原生协议测试和独立真实 smoke。
