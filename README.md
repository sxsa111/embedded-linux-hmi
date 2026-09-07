# 嵌入式 Linux HMI 智能终端 · RK1808

> 基于瑞芯微 **RK1808** 开发板的嵌入式 Linux 人机交互（HMI）系统：自研轻量级帧缓冲 UI 框架 + 讯飞语音（TTS/ASR）+ 智谱 AI 语音对话。从零实现了一套不依赖任何 GUI 库的多页面触摸屏应用，覆盖显示、输入、网络、语音、AI 全链路。

---

## ✨ 功能特性

- **帧缓冲直接渲染**：通过 `mmap` 操作 `/dev/fb0` 显存，无第三方 GUI 库依赖，1024×600 全屏 UI。
- **五大交互页面**（状态机切换）：主页 / 语音助手 / 智慧画板 / 我的相册 / 系统设置。
- **自研 UI 组件库**：圆角矩形、实心圆、圆弧、箭头、竖条等几何绘制，统一「浅白蓝」新拟态主题。
- **中英文字库渲染**：集成 `HZK16` 点阵字库（GB2312）与 8×16 英文字模，支持中英混排、2× 放大。
- **讯飞语音能力**：`tts_tool` 文本转语音（合成 WAV）、`asr_tool` 录音文件转文字，封装讯飞 MSC SDK。
- **AI 语音助手**：板端作为 TCP 客户端，PC 端 `as.py`（智谱 GLM）作服务端，打通「语音 → 识别 → 大模型 → 播报」闭环。
- **BMP 图片浏览器**：扫描目录、等比缩放绘制、内存对齐适配（针对 RK1808）。
- **WiFi 管理 + 网络自愈**：WiFi 扫描/连接（`wpa_supplicant`），并针对博通 AP6212 固件崩溃（FW TRAP）实现自动恢复。
- **触摸交互**：解析 Linux input 子系统事件，识别点击 / 上下左右滑动 / 拖拽；画线采用 Bresenham 插值保证连续。
- **DRM 显示抽象层**（可选）：`DRMwrap` 封装 `drmMode` 接口，作为 framebuffer 之外的另一种显示后端。

---

## 🛠 硬件平台

| 项目 | 规格 |
|------|------|
| SoC | Rockchip **RK1808**（Cortex-A53，带 NPU） |
| 显示屏 | 1024×600 LCD，设备节点 `/dev/fb0` |
| 触摸屏 | 电容触摸，设备节点 `/dev/input/event2` |
| 无线 | 博通 **AP6212**（WiFi/BT，接口 `wlan0`） |
| 显示后端 | Framebuffer（默认）/ DRM（可选） |

> 代码中的分辨率、设备节点、字库路径按上述平台硬编码，移植到其他板子时按需修改宏定义即可。

---

## 🧱 系统架构

```
                ┌─────────────────────────────────────────────┐
                │           板端 main_ui 主循环                 │
                │   AppPage 状态机: HOME / VOICE / DRAW /       │
                │                  ALBUM / SETTING / FILE       │
                └───────┬───────────────┬───────────────┬──────┘
                        │               │               │
                  [绘制到显存]      [读取触摸事件]     [页面业务]
                  /dev/fb0           /dev/input/        │
                      │              event2            ├─ draw_ui   : 手写画板 (Bresenham)
                      ▼                  │             ├─ album_ui  : BMP 浏览器
                  LCD 屏显          点击/滑动/拖拽      ├─ settings_ui: WiFi 扫描/连接
                                                      └─ voice_ui  : 语音助手
                                                              │
                                                  TCP 8080/8081/8082
                                                              ▼
                                                ┌──────────────────────────┐
                                                │  PC 端 as.py (智谱 GLM)   │
                                                │  • ASR: 调用 asr_tool     │
                                                │  • 对话: 调用 ZhipuAI     │
                                                │  • TTS: 调用 tts_tool     │
                                                └───────────┬──────────────┘
                                                            ▼
                                                  讯飞 MSC SDK (云端)
```

数据闭环：用户说话 → 板端录音 → 上传 `as.py` → `asr_tool` 识别 → 智谱大模型生成回复 → `tts_tool` 合成语音 → 回传板端播放，对话内容实时显示在屏上。

---

## 📂 目录结构

```
.
├── src/                      # 板端主应用（C）
│   ├── main_ui.c             # 主循环、页面状态机、帧缓冲初始化、WiFi 自愈
│   ├── draw_utils.c/.h       # 像素/英文/汉字(GTK16)绘制、字库加载
│   ├── ui_components.c/.h    # 圆角矩形、圆、圆弧、箭头等 UI 组件 + 主题色
│   ├── touch.h               # 触摸事件解析（点击/滑动/拖拽）
│   ├── voice_ui.c/.h         # 语音助手页面 + TCP 客户端（3 端口）
│   ├── draw_ui.c/.h          # 智慧画板（Bresenham 画线）
│   ├── album_ui.c/.h         # BMP 图片浏览器（等比缩放）
│   ├── settings_ui.c/.h      # 系统设置（WiFi 扫描/连接、文件管理占位）
│   ├── app_stubs.c           # 未完成的文件管理器占位
│   ├── tts_tool.c            # 讯飞 TTS 封装（文本→WAV）
│   ├── asr_tool.c            # 讯飞 ASR 封装（音频→文本）
│   ├── font_8x16.h           # 8×16 英文字模
│   └── include/iflytek/      # 讯飞 MSC SDK 头文件（已随仓库提供）
├── ai_server/
│   └── as.py                 # PC 端 AI 语音对话服务端（智谱 GLM）
├── drm/
│   ├── DRMwrap.h             # DRM 显示抽象层接口
│   ├── color_demo.c          # DRM/帧缓冲 上色示例
│   └── libDRMwrap.so         # 预编译 DRM 封装库
├── Makefile
└── .gitignore
```

---

## 🚀 编译与运行

### 依赖

| 依赖 | 说明 |
|------|------|
| 编译器 | 交叉编译 `arm-linux-gnueabihf-gcc`，或在板端直接用 `gcc` |
| 讯飞 MSC SDK | `libmsc.so`（及配套资源），需从[讯飞开放平台](https://www.xfyun.cn/)下载，放入 `third_party/iflytek-sdk/libs/`（已 gitignore） |
| 点阵字库 | `HZK16`（GB2312，16×16），运行时通过 `hzk_init("/path/HZK16")` 加载 |
| Python 3 | 仅 AI 服务端 `as.py` 需要，依赖 `zhipuai` |

### 编译板端程序

```bash
# 交叉编译（默认）
make

# 或指定交叉工具链前缀
make CROSS=arm-linux-gnueabihf-

# 仅编译主 UI 应用
make hmi_demo

# 仅编译讯飞工具（需要 libmsc.so）
make tts_tool asr_tool IFLYTEK_LIB=/path/to/iflytek/libs
```

产物：`hmi_demo`（主程序）、`tts_tool`、`asr_tool`。

### 运行

```bash
# 板端运行主程序（需 root 访问 /dev/fb0、/dev/input）
./hmi_demo

# 让相册能读到图片，把 BMP 放到 album_ui.c 中 BMP_DIR 指定的目录（默认 /kkk/bmp/）
```

---

## 🤖 AI 语音助手配置

语音助手需要一台 PC（或同网段主机）运行 `as.py` 作为服务端。

**1. 服务端（PC）**

```bash
pip install zhipuai
export ZHIPUAI_API_KEY="你的智谱API_KEY"     # 安全：从环境变量读取，勿硬编码

# 修改 src/voice_ui.c 中的 SERVER_IP 为运行 as.py 的电脑 IPv4
python ai_server/as.py
```

`as.py` 会监听三个端口：
- `8080`：文本对话（板端 ↔ 大模型）
- `8081`：TTS 语音下发（服务端合成后回传板端播放）
- `8082`：录音上传（板端采集后上传识别）

**2. 端到端流程**

```
板端麦克风录音 → (8082) → as.py → asr_tool(讯飞) 识别
                                      ↓
                              智谱 GLM 生成回复文本
                                      ↓
                              tts_tool(讯飞) 合成语音 → (8081) → 板端播放
                              回复文本 → (8080) → 板端屏幕显示
```

`as.py` 内已处理**多线程写 socket 加锁**与**客户端断线重连**，确保长时间稳定运行。

---

## 🔐 安全说明

本项目已**移除所有硬编码密钥**，可安全公开：

- `as.py` 的智谱 API Key 改为从环境变量 `ZHIPUAI_API_KEY` 读取；
- `settings_ui.c` 的 WiFi 密码为占位符 `YOUR_WIFI_PASSWORD`，请自行修改；
- `voice_ui.c` 的 `SERVER_IP` 为占位示例，请改为实际服务端 IP。

---

## 💡 简历亮点 / 工程要点

这个项目完整覆盖了嵌入式 Linux 应用开发的核心能力，可作为简历上的「综合项目」：

1. **底层显示驱动**：`mmap` 帧缓冲显存、像素级绘制、零 GUI 库依赖的轻量 UI 框架。
2. **中文字库渲染**：基于 `HZK16` 点阵 + GB2312 区位码计算，实现中英混排与 2× 放大。
3. **输入子系统**：解析 Linux `input_event`，区分点击/滑动/拖拽；用 Bresenham 算法解决触摸采样率低导致的断线问题。
4. **网络健壮性**：针对博通 AP6212 真实存在的固件崩溃（FW TRAP）写了自动恢复逻辑——体现「在产线环境定位并解决真实问题」的能力。
5. **跨设备通信架构**：板端/PC 端 C+Python 协作，3 端口 TCP 分工，多线程 socket 加锁 + 断线重连。
6. **第三方 SDK 集成**：封装讯飞 MSC SDK 完成 TTS/ASR，包括 WAV 文件头构造、PCM 流式处理。
7. **内存对齐调试**：定位并修复 RK1808 上因指针对强转导致的崩溃，改用 `memcpy` 安全读取 BMP 头——体现底层调试功力。

---

## 📌 已知限制 / 后续

- **文件管理器**（`PAGE_FILE`）尚未实现，仅在 `app_stubs.c` 占位。
- 运行需自备 `HZK16` 字库、讯飞 `libmsc.so` 及对应 `appid` 资源（受许可限制未纳入仓库）。
- `drm/` 为可选显示后端示例，主程序默认走 framebuffer。
- 分辨率、设备节点、字库路径在源码中以宏硬编码，移植时需按需调整。

---

*个人学习/课程设计项目，代码用于展示嵌入式 Linux 应用开发能力。*
