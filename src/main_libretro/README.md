# Tsugaru libretro core（FM Towns 模拟器）

把 [TOWNSEMU](https://github.com/captainys/TOWNSEMU)（FM Towns 模拟器 "Tsugaru"）
移植为 libretro core，使其能在 RetroArch / 任意 libretro frontend 中运行。

## 移植要点

- **单线程同步步进**：原 VM 用独立线程 + `sleep` 做实时限速；本 core 改为
  每个 `retro_run` 推进恰好一帧虚拟时间（`TOWNS_RENDERING_FREQUENCY ≈ 1/60 秒`），
  由 frontend 的 VSync 控制节奏，core 内部不 sleep。
- **视频**：在 `retro_run` 内用自有 `TownsRender` 直接
  `BuildImage(GetUsingVRAM(), crtc.GetPalette(), crtc.chaseHQPalette)` + `MoveImage()`
  抓帧，绕开原 VM 的窗口线程与渲染锁，再转换为 XRGB8888 投递。
- **音频**：FM / PCM / CDDA 已在 `TownsSound::ProcessSound` 中混合成同一路
  int16 立体声 @44100，经 `outside_world->FMPCMPlay` 投递；本 core 在
  `Outside_World::Sound` 子类里把样本收集进环形缓冲，每帧由 `retro_audio_sample_batch`
  排空。`CDDAPlay` 等控制函数留空（CDDA 音频已混入 FMPCM 通道）。
- **输入**：键盘由 `RETROK_*` → `TOWNS_JISKEY_*`（见 `keymap.h`）经
  `keyboard.PushFifo(JIS_PRESS/RELEASE, ...)` 注入；手柄经 `SetGamePadState`；
  鼠标经 `SetMouseMotion` + `SetMouseButtonState`。
- **无 SDL 依赖**：仅链接 `towns` + `osinit`，排除 `fssimplewindow_connection`、
  `yssimplesound` 等 SDL 相关实现。

许可：TOWNSEMU 为 BSD 3-clause，libretro.h 为 MIT，二者兼容。

## 构建

### Windows（MSYS2 MinGW-w64，推荐）

```bash
cd TOWNSEMU/src
mkdir -p build && cd build
cmake -G "MinGW Makefiles" -DBUILD_LIBRETRO=ON ..
cmake --build . --target towns_libretro -j4
```

产物：`src/build/main_libretro/libretro.dll`（或直接 `towns_libretro.dll`）。
把它放到 RetroArch 的 `cores/` 目录，并重命名为 `tsugaru_libretro.dll`。

> 用 Visual Studio 也可：`cmake -G "Visual Studio 17 2022" -DBUILD_LIBRETRO=ON ..`，
> 然后构建 `towns_libretro` 目标。Windows 下靠 `libretro.def` 导出 `retro_*` 符号。

### Linux

静态库链接进共享库需要 `-fPIC`，请在配置时全局开启：

```bash
cd TOWNSEMU/src
mkdir -p build && cd build
cmake -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_LIBRETRO=ON ..
cmake --build . --target towns_libretro -j$(nproc)
```

产物：`towns_libretro.so` → 放到 RetroArch `cores/`，命名为 `tsugaru_libretro.so`。

## BIOS（系统 ROM）

core 通过 `RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY` 获取系统目录，并把其中的
ROM 文件作为 `ROMPath` 传给 `FMTownsCommon::Setup`。请把以下文件放到
RetroArch 的 **系统目录**（通常是 `retroarch/system/`）：

- `FMT_SYS.ROM`
- `FMT_DOS.ROM`（可选，DOS 增强）
- `FMT_FNT.ROM`（字体）
- `FMT_F20.ROM`（1200 波特调制解调器固件，可选）
- `FMT_DIC.ROM`（词典 ROM，可选）
- `MYTOWNS.ROM`（可选）
- `MAR_EX0.ROM` ~ `MAR_EX3.ROM`（Marty 扩展 ROM，仅 Marty 机型需要）

或者用合并文件 `FMT_ALL.ROM`（内含分片头标记）替代上述拆分文件。
这些 ROM 来自真实 FM Towns 硬件，需自行提取，本仓库不附带。

CMOS 会被保存到存档目录（`towns_cmos.bin`），跨会话保留。

## 加载内容（游戏/软件）

core 通过文件路径加载镜像（`need_fullpath = true`）。按扩展名判断类型：

| 扩展名 | 类型 |
|--------|------|
| `.cue` `.ccd` `.mds` `.iso` `.toc` `.bin` | CD-ROM 镜像 |
| `.d77` `.dsk` `.imd` `.td0` `.img` | 软盘镜像（FD） |
| `.hdd` `.vhd` | SCSI 硬盘镜像 |

例如在 RetroArch 里「Load Content」选择一个 `.cue` 即可启动 CD 游戏。
未知扩展名按 CD 处理。

## 操作

- **键盘**：几乎全键位直映射（字母/数字/符号为其 ASCII；功能键、方向键、回车、
  退格、ESC、空格等见 `keymap.h`）。
- **手柄（port 0）**：方向键 + A/B；L→RUN、R→PAUSE、START→ZOOM。
- **鼠标（port 1）**：相对移动 + 左/右键。

## Core 选项

- **Machine Model**：选择 FM Towns 机型，默认 `2MX`（最通用）。
  其他可选 `2UX/2CX/2UG/2HG/2HR/2UR/2MA/2ME/2MF/2HC`、初代 `MODEL1_2`、
  `1F_2F`、`10F_20F`、FMR 系列、以及 `MARTY`。

## 已知限制 / TODO

- 鼠标为相对运动注入（best-effort），绝对定位型应用可能需微调。
- 音频缓冲采用简单环形缓冲，未实现 `RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK`。
- 序列化（`retro_serialize`/`retro_unserialize`）已接入 `SaveStateMem`/`LoadStateMem`，
  但音频环形缓冲不在存档内（读档后音频会从静音重新累积，属正常现象）。
- `high-fidelity` CPU 模式（i486DXHighFidelity）暂未作为选项，如需可开启
  `TSUGARU_I486_HIGH_FIDELITY` 并改用 `FMTownsWithHighFidelityCPU`。
