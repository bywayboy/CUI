# CUI 使用白皮书

> 面向对象：需要在 Windows 桌面程序中接入 CUI 的 C/C++ 开发者，以及需要维护 CUI 本身的工程师。
>
> 代码基线：本仓库 `src/`（从 RMControl 安装器 UI 层抽出的独立版本）。
>
> 文档目标：读完能独立完成构建、接入、调试与扩展；照着代码片段即可跑起来。
>
> 最近变更：
> 1. 修复 `cui_demo` 关闭键/按钮点击无响应（`Ui_EndFrame()` 会清零单帧事件，
>    必须在它之前快照 `ui.clicked`）。
> 2. 动画循环改为脏区驱动：状态文字/按钮悬停等注册 `Ui_AnimSetRect`，主循环用
>    `Ui_AnimTakeDirty` 只失效变化区域；输入/焦点/光标/IME/离散动作各自精确失效，
>    不再每帧整窗重绘。
> 3. 新增 `Gfx_IsVisible`，脏区外跳过 UTF-8 转换与文本排版；ULW 回退路径按
>    `prcDirty` 局部上传。
> 4. demo 的进度条改为**合成器动画 overlay**（`GfxOverlay`）：静态主窗 + 独立小
>    swapchain + DComp clip 补间。播放时应用进程 CPU 约 0.00%（前台 1440×1120@60Hz），
>    暂停/恢复/回卷仍可用；ULW 回退自动退回主窗局部绘制。
> 5. 新增 z-index（`Ui_PushLayer/PopLayer` + `UiWidget.z`）、主题化组件
>    （Button/Panel/ProgressBar）、亮/暗主题切换（`UiTheme_SetTheme`）和带渐变
>    overlay 的 Toast（最高 z、默认阻断输入、自动超时）。
> 6. 新增 `UiApp` 窗口壳、`UiInput` 表单输入、`UiProgress` 进度组件；标准 demo
>    重写为壳版本（893 → 340 行），新增 `samples/minimal.c`（最小样例）和
>    `samples/advanced.c`（高级样例）。构建产出 `cui_demo` / `cui_minimal` /
>    `cui_advanced` 三个 0CRT exe。
> 7. 参照 `RemoteCtrl/agent` 增强 0CRT：/GS/SEH 运行时、SSE2/AVX2/ERMSB 内存与
>    字符串、完整 printf（flags/宽度/精度/length）、calloc/qsort/strtoul/
>    localeconv；`crtstub.c` + `mem_avx2.c`。
> 8. 新增通用合成器图层 `GfxLayer`（独立 swapchain + 绘制回调 + DComp opacity
>    补间 + 固定置顶 z 次序）；Toast 改为 layer 版本：渐变/卡片只画一次，淡入淡出
>    由 DWM 完成，主窗只保留一个不可见全窗输入 blocker（`Ui_SetModal`），不再整窗
>    重绘，也不再隐藏进度 overlay。Toast 显示/停留/淡出期间应用 CPU 实测 **0.00%**。
>    opacity 优先用 `IDCompositionVisual3`，本机 Win10 19045 无此接口时自动退回
>    `IDCompositionEffectGroup`（Win8+）。ULW/图层创建失败仍走原主窗回退。
> 9. 新增自定义语义色主题：`UiThemeColors` + `UiTheme_SetColors` 覆盖
>    `primary/warning/success/error/neutral(default)` 五个角色，自动派生
>    hover/press/soft/对比文字；`UiColorRole` 同时开放给组件。新增
>    `UI_BTN_WARNING/SUCCESS/ERROR/DEFAULT` 变体；`themeRev` 让进度 overlay 与
>    Toast layer 在任意换肤后自动重建/重画。demo 增加“品牌主题”循环。
> 10. 新增 `UiStack` / `UiFlex` 布局：只计算矩形，容器 padding/gap；
>    `UiStack` 固定+Fill，`UiFlex` basis+grow 权重（不做百分比归一化）。
>    支持 min/max 夹取与右/底对齐，零分配；demo 表单和按钮行已迁移。
> 11. 新增 `GfxEffect`：D2D 内置 Blur/Shadow/ColorMatrix 包装 + 自定义 HLSL
>    `Grain`（离线 fxc、内嵌 cso、注册 `ID2D1EffectImpl` + `ID2D1DrawTransform`）。
>    提供 `Gfx_DrawWithEffects`（离屏内容 → 效果链 → 合成回目标）。Toast 卡片使用
>    软阴影 + 颗粒，仍只绘制一次，显示/停留期间应用 CPU 0.00%。
>    **本机探测**：DComp `IDCompositionDevice2/3`、`Visual3` 均 `E_NOINTERFACE`，
>    DComp 内置滤镜不可用；D2D `GaussianBlur/ColorMatrix/Shadow/Turbulence`
>    全部 `S_OK`，因此效果走 D2D 路径。

---

## 目录

1. [一页速览](#1-一页速览)
2. [项目定位](#2-项目定位)
3. [快速开始](#3-快速开始)
4. [架构与运行模型](#4-架构与运行模型)
5. [接入指南](#5-接入指南)
6. [Gfx 渲染层](#6-gfx-渲染层)
7. [UiContext 交互层](#7-uicontext-交互层)
8. [UiPaint 与 UiTheme](#8-uipaint-与-uitheme)
9. [UiAnim 动画内核](#9-uianim-动画内核)
10. [UiFrame 帧时钟](#10-uiframe-帧时钟)
11. [UiEdit 编辑框](#11-uiedit-编辑框)
12. [UiIme 输入法](#12-uiime-输入法)
13. [DPI 与多显示器](#13-dpi-与多显示器)
14. [0CRT 构建](#14-0crt-构建)
15. [日志与性能诊断](#15-日志与性能诊断)
16. [常用任务配方](#16-常用任务配方)
17. [已知限制与设计取舍](#17-已知限制与设计取舍)
18. [故障排查](#18-故障排查)
19. [附录](#19-附录)

---

## 1. 一页速览

CUI 是一套 **纯 C/C++、零 CRT、即时模式（Immediate Mode）** 的 Windows 自绘 UI 库，
底层使用 Direct2D + DirectWrite + D3D11 + DirectComposition，失败时自动回退到
`UpdateLayeredWindow`。它专门解决三类问题：

1. **无边框、透明、GPU 合成的窗口**：`WS_EX_NOREDIRECTIONBITMAP` + DComp 直出。
2. **需要轻量交互动画**：hover / press / focus / glow 指数平滑，转场补间，帧率无关。
3. **自绘控件也要能输入中文**：IMM32 组合串、候选窗定位、密码框禁用输入法。

它 **不提供** 布局引擎、控件对象树、数据绑定、MVVM、样式表解析。界面就是每帧按顺序
“画 + 注册”，交互状态从稳定 ID 读取。

| 维度 | 说明 |
|---|---|
| 语言 | C11 + 单个 C++17 翻译单元（`gfx_d2d.cpp`） |
| 依赖 | Windows 10 1809+；系统 DLL（D2D/DWrite/D3D11/DXGI/DComp/IMM32 等） |
| 运行库 | 0CRT：`/NODEFAULTLIB` + `/ENTRY:startup`，自带最小 CRT 存根 |
| 坐标 | DIP（float），后端按 DPI 自动缩放 |
| 颜色 | `0xAARRGGBB` |
| 文本 | UTF-8 输入，DirectWrite 排版，微软雅黑 |
| 渲染 | DComp 翻转链 + 离屏位图；回退 ULW（分层窗口） |
| 交互 | 即时模式，稳定 `UiId`，单帧事件 |
| 动画 | 固定池：64 条目 × 16 通道，零分配 |
| 输入法 | IMM32，组合串就地绘制，候选窗跟随插入点 |
| 产物 | `bin/cui_demo.exe`（约 47 KB） |

---

## 2. 项目定位

### 2.1 它解决什么问题

传统的 Win32 自绘界面通常要在以下方案里做取舍：

- **原生控件 + 子类化**：外观受系统主题限制，透明/圆角/动画很难做。
- **Qt/DuiLib/WebView**：依赖重、体积大、启动慢，不适合安装器这类“小窗口、短生命周期”程序。
- **纯 GDI**：透明窗口、抗锯齿、GPU 合成、DPI 支持都要自己补。

CUI 的取舍是：**只做安装器/工具类窗口需要的那一层**。体积小、启动快、无 CRT、
无第三方依赖，同时把透明合成、动画、中文输入法这些“真机坑”封装好。

### 2.2 它不是什么

- 不是通用桌面 UI 框架，不追求覆盖所有控件。
- 不是声明式框架，没有“改状态 → 自动 diff → 更新 DOM”的机制。
- 不是跨平台库，只支持 Windows。
- 不提供布局引擎。demo 全部使用绝对坐标（DIP）。

### 2.3 能力矩阵

| 能力 | 状态 | 说明 |
|---|---|---|
| 圆角/描边/渐变/光晕 | ✅ | `Gfx_*` + `Ui_DrawSurface` |
| 透明窗口 | ✅ | DComp 预乘 alpha，或 ULW 回退 |
| 合成器图层 | ✅ | `GfxLayer`：独立 swapchain，画一次 + DWM 透明度补间 |
| 模态 Toast | ✅ | 合成器 layer 优先（主窗零重绘），ULW 回退主窗 |
| 脏区局部重绘 | ✅ | `Gfx_SetDirtyRect` + 动画脏区 |
| hover/press/focus 动画 | ✅ | 指数平滑，帧率无关 |
| 转场补间 | ✅ | `Ui_AnimTo`，线性/缓出/缓入缓出 |
| 单行编辑框 | ✅ | 选区、拖选、剪贴板、滚动 |
| 中文输入法 | ✅ | 组合串、候选窗、上屏 |
| 密码框禁用 IME | ✅ | `Edit_SetImeEnabled` + `ImmAssociateContext` |
| 多行文本/富文本 | ❌ | 编辑框仅单行 |
| 布局系统 | ❌ | 应用自己算矩形 |
| 滚动容器 | ❌ | 应用自己维护偏移与裁剪 |
| 撤销/重做 | ❌ | 编辑框无撤销栈 |
| 原生文件对话框 | 由应用调用 | 注意动作延后，避免渲染重入 |
| 多主题/换肤 | 自行扩展 | `UiTheme` 是一组令牌 |
| 托盘/通知 | ❌ | 不属于 UI 层 |

---

## 3. 快速开始

### 3.1 环境要求

- Windows 10 1809 或更高（DComp 与高精度定时器）。
- Visual Studio 2019 / 2022 / 2026，安装 **使用 C++ 的桌面开发** 工作负载
  （含 Windows SDK 的 `fxc.exe`，用于离线编译自定义效果着色器）。
- x64 工具链。
- 不需要 CMake、vcpkg、NuGet 或任何第三方库。

### 3.2 构建

在项目根目录执行：

```bat
build.cmd
```

脚本会：

1. 在 `C:\Program Files\Microsoft Visual Studio\{18,2022,2019}\{Community,Professional,Enterprise}`
   下寻找 `vcvarsall.bat`，并加载 x64 环境。
2. 创建 `build\obj`、`bin`。
3. 用 `%WindowsSdkVerBinPath%x64\fxc.exe` 把 `src\gfx\grain.hlsl` 编译成
   `build\grain_cso.h`（内嵌字节数组，运行时不依赖 d3dcompiler）。
4. 用 `/std:c11 /O1 /GS- /utf-8 /DNO_CRT_NO_DEFAULTLIB` 编译 C 文件。
5. 用 `/std:c++17 /EHs-c- /GR- /D_HAS_EXCEPTIONS=0` 编译 `gfx_d2d.cpp`。
6. 链接 `/SUBSYSTEM:WINDOWS /NODEFAULTLIB /ENTRY:startup /MANIFEST:NO`。
7. 用 `dumpbin /imports` 检查导入表，若出现 `msvcrt / ucrtbase / api-ms-win-crt-*`
   就判定 0CRT 构建失败。

产物：

```text
bin\cui_demo.exe
bin\cui_minimal.exe
bin\cui_advanced.exe
```

成功输出示例：

```text
[OK] no CRT dependency
=== BUILD OK ===
Output: ...\bin\cui_demo.exe (47104 bytes)
```

> 注意：`build.cmd` 必须保持 **CRLF** 行尾；改成 LF 后 cmd 在多行括号块/标签处
> 可能直接卡住（无输出、无子进程）。编辑后可用 `file build.cmd` 检查。
>
> 注意：`build.cmd` 使用 `del /q build\obj\*.obj` 清理旧对象，属于正常构建步骤；
> 它只删除构建目录中的中间文件，不碰源码。

### 3.3 运行 demo

```bat
bin\cui_demo.exe
```

窗口为 720×560 DIP（高 DPI 下按比例放大），无系统标题栏，顶部是自绘标题带。

| 操作 | 预期 |
|---|---|
| 拖动顶部标题带 | 移动窗口（`WM_NCHITTEST` 返回 `HTCAPTION`） |
| 点击右上角 ✕ | 关闭窗口 |
| 点击“退出” | 关闭窗口 |
| 在“名称”里输入中文 | 组合串就地显示，候选窗跟随光标 |
| 在“密码”里输入中文 | 输入法被禁用，只接受普通字符 |
| Tab | 在名称/密码之间切换 |
| 回车 | 提交（名称写入状态行） |
| Esc | 取消焦点 |
| 点击“暂停动画” | 进度条停止自动播放 |
| 重置 | 清空两个输入框 |

demo 还演示了：窗口进场淡入、按钮悬停光晕、进度条平滑、状态文字上滑淡入、
编辑框光标 530 ms 闪烁、DPI 缩放。

### 3.4 五分钟接入清单

如果你只想知道“接进来最少要做什么”，按下面这项清单走：

- [ ] 窗口创建时加 `WS_EX_NOREDIRECTIONBITMAP`；程序启动时设置 Per-Monitor V2 DPI 感知。
- [ ] `WinMain` 里 `CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)`。
- [ ] `WM_CREATE`：`Gfx_Init` → `UiTheme_Init` → `Ui_ContextInit` → `UiFrame_Init`。
- [ ] `WM_PAINT`：`BeginPaint/EndPaint` 后调用自己的 `render(ps.rcPaint)`。
- [ ] 每帧：`Gfx_Resize` → `Ui_SetViewport` → `Ui_SetFocusId` → `Gfx_SetDirtyRect`。
- [ ] 每帧：`Gfx_Begin` → `Ui_BeginFrame` → 绘制控件 → `Ui_EndFrame` → `Gfx_End`。
- [ ] 鼠标：`WM_MOUSEMOVE/DOWN/UP` 转到 `Ui_OnMouse*`，坐标除以 `scale`；
      `WM_MOUSELEAVE`、`WM_CAPTURECHANGED` 也要接。
- [ ] 绘制控件时用 `Ui_DrawSurface` 或 `Ui_State`，ID 用 `Ui_Id("作用域", a, b)`。
- [ ] **在 `Ui_EndFrame()` 之前读取 `ui.clicked/released/down`**，动作放到 `Gfx_End()` 之后执行。
- [ ] 动画期间主循环调用 `Ui_AnimAdvance`，并驱动重绘；空闲时阻塞等消息。

---

## 4. 架构与运行模型

### 4.1 分层

```text
应用（demo.c / 你的业务代码）
   │  布局 + 业务状态 + 消息分发 + 动作派发
   ▼
ui_paint / ui_theme          控件表面与设计令牌
ui_context / ui_anim         交互状态机 + 动画通道
ui_ime + edit                编辑控件与输入法
   ▼
gfx (gfx.h → gfx_d2d.cpp)    形状/文本/裁剪/位图/脏区/透明度
   ▼
Direct2D / DirectWrite / D3D11 / DirectComposition
（失败回退 UpdateLayeredWindow）
```

### 4.2 模块职责

| 文件 | 职责 | 关键约束 |
|---|---|---|
| `src/demo.c` | 演示程序：窗口、消息、布局、状态 | 业务动作延后到帧结束 |
| `src/edit.c/.h` | 自绘单行编辑框 | UTF-16 存储，单行，最大 511 字符 |
| `src/log.c/.h` | 调试日志 | `OutputDebugStringA`，可选文件 |
| `src/lib/crtstub.c` | 0CRT 存根 | /GS/SEH、SIMD mem/str、完整 printf、分配器 |
| `src/lib/mem_avx2.c` | AVX2 大块内存操作 | 单独 `/arch:AVX2`，运行时 CPUID 检测 |
| `src/gfx/gfx.h` | 后端无关 2D 接口 | DIP 坐标、`0xAARRGGBB`、UTF-8 |
| `src/gfx/gfx_d2d.cpp` | D2D/DWrite/D3D/DComp 实现 | 唯一的 C++ 单元，无 STL/异常/RTTI |
| `src/ui/ui_types.h` | 矩形/颜色/稳定 ID | `UiId` 必须跨帧稳定 |
| `src/ui/ui_context.*` | 注册、命中、拖动、单帧事件 | 每帧 Begin/End |
| `src/ui/ui_layout.*` | UiStack / UiFlex 布局 | 只算矩形；padding/gap；固定+Fill / basis+grow |
| `src/ui/ui_theme.*` | 字体/颜色令牌 | 亮暗预设 + 5 语义色自定义；`themeRev` 通知重建 |
| `src/ui/ui_paint.*` | 统一交互表面 | 填充/描边/光晕/渐变叠加 |
| `src/ui/ui_anim.*` | 动画通道 | 64 条目 × 16 通道，零分配 |
| `src/ui/ui_frame.*` | 帧时钟 | 刷新率检测 + 高精度等待 |
| `src/ui/ui_ime.*` | IMM32 适配 | 组合串/候选窗/禁用输入法 |
| `src/ui/ui_widgets.*` | 主题化 Button/Panel/ProgressBar | UTF-8 标签；进度条不注册交互 |
| `src/ui/ui_toast.*` | 渐变 Toast | 优先 GfxLayer（画一次 + DWM 补间）；ULW 回退主窗 |
| `src/ui/ui_input.*` | 表单输入封装 | 焦点/光标/拖选/键盘/IME/失效 |
| `src/ui/ui_progress.*` | 循环进度组件 | 轨道 + GfxOverlay + 回退 + 暂停 |
| `src/ui/ui_app.*` | 可选窗口壳 | 消息/DPI/IME/帧循环/关闭 |

### 4.3 一次点击的完整链路

以 demo 的关闭键为例：

```text
用户按下鼠标
  WM_LBUTTONDOWN
    SetCapture / SetFocus
    demo_mouse_down -> Ui_OnMouseDown
      Ui_HitTest 找到 "demo.close"
      ctx->pressed = id; ctx->down = id; ctx->mouseDown = 1
      失效该控件矩形 -> WM_PAINT
        demo_render -> draw_close -> Ui_DrawSurface -> Ui_State
          st.pressed = 1（press 动画开始）
用户抬起鼠标
  WM_LBUTTONUP
    demo_mouse_up -> Ui_OnMouseUp
      hit == pressed -> ctx->clicked = id
      失效控件 -> WM_PAINT
        demo_render
          Ui_BeginFrame
          绘制（此时 ui.clicked 仍然是 id）
          UiId clicked = ui.clicked;      ← 必须先快照
          Ui_EndFrame                     ← 清零 clicked/down/released
          Gfx_End
          if (clicked == close) PostMessage(WM_CLOSE)
  WM_CLOSE -> DestroyWindow
  WM_DESTROY -> 释放资源 -> PostQuitMessage
```

关键点：**`Ui_EndFrame()` 会清零单帧事件**。任何在它之后读取
`ui.clicked / ui.down / ui.released / ui.clickedOutside` 的代码都永远读到 0。
这是本次修复的 demo 关闭键问题的根因。

### 4.4 即时模式的四条铁律

1. **每帧重新注册**：`Ui_BeginFrame()` 清空上一帧的控件表，控件必须在绘制时重新
   `Ui_DrawSurface` / `Ui_State` / `Ui_RegisterWidget`。
2. **ID 稳定且同帧唯一**：用 `Ui_Id("作用域", a, b)` 或 `Ui_IdIndex`。不要用指针、
   数组下标、循环序号做 ID；列表项要用业务键。
3. **单帧事件尽早读**：`clicked/down/released/clickedOutside` 只在“鼠标消息 → 下一次
   绘制”之间有效；`EndFrame` 清零。
4. **动作延后**：不要在绘制过程中弹模态框、打开文件对话框或销毁窗口。先把动作记到
   `pending`，等 `Gfx_End()` 之后再做。

---

## 5. 接入指南

### 5.0 推荐路径：UiApp 壳 + 组件

如果只是普通单窗口工具/表单，**不需要**手写 WndProc、DPI、IME 和主循环。
`UiApp` 壳接管这些，应用只提供业务回调：

```c
#include "ui/ui_app.h"
#include "ui/ui_input.h"
#include "ui/ui_widgets.h"

typedef struct App { UiInput form; int name; } App;

static void on_init(UiApp *app, void *user) {
    App *a = user;
    UiInput_Init(&a->form, &app->ui, &app->ime);
    a->name = UiInput_Add(&a->form, Ui_Id("name", 0, 0), 63, 0, NULL, L"名称");
}

static void on_frame(UiApp *app, void *user) {
    App *a = user;
    Ui_Panel(&app->ui, Ui_Rect(24, 24, app->w - 48, app->h - 48));
    UiInput_BeginFrame(&a->form);
    UiInput_SetRect(&a->form, a->name, Ui_Rect(48, 80, app->w - 96, 40));
    UiInput_Draw(&a->form, a->name);
    if (Ui_Button(&app->ui, Ui_Id("ok", 0, 0),
            Ui_Rect(48, 140, 110, 38), "确定", UI_BTN_PRIMARY, 1)) {
        /* 业务动作: 建议放到 after_frame (Gfx_End 之后) 执行 */
    }
}

static void on_mouse(UiApp *app, int msg, float x, float y, void *user) {
    App *a = user;
    if (msg == UI_APP_MOUSE_DOWN)
        UiInput_MouseDown(&a->form, Ui_HitTest(&app->ui, x, y), x);
    else if (msg == UI_APP_MOUSE_MOVE)
        UiInput_MouseMove(&a->form, x);
    else if (msg == UI_APP_MOUSE_UP)
        UiInput_MouseUp(&a->form);
}

int WINAPI WinMain(HINSTANCE i, HINSTANCE p, LPSTR c, int s) {
    static App a;
    UiAppCallbacks cb;
    ZeroMemory(&cb, sizeof(cb));
    cb.init = on_init;
    cb.frame = on_frame;
    cb.mouse = on_mouse;
    /* key/chr/ime/timer/tick/after_frame/animating 按需接 */
    return UiApp_Run(L"App", 520, 360, UI_THEME_LIGHT, &cb, &a);
}
```

- 壳负责：窗口创建、Per-Monitor DPI、鼠标捕获、键盘/字符、IME 消息与候选窗、
  530ms 光标闪烁定时器、动画节拍、Gfx/Theme/UiContext/UiIme/UiFrame 生命周期、关闭。
- **可拖边缩放**：`UiApp` 默认带 `WS_THICKFRAME`，用 `WM_NCCALCSIZE` 把客户区
  铺满窗口，`WM_NCHITTEST` 在 6 DIP 边缘返回 `HTLEFT/HTTOP/...`；应用的
  `nchittest` 只负责标题栏/客户区，不处理边缘。`app->minW/minH`（DIP）控制
  最小尺寸，默认 320×240，可在 `init` 里改；demo 设为 600×480。
- 应用负责：布局、控件、状态、业务动作。
- 动作请在 `after_frame`（`Gfx_End` 之后）执行，避免绘制中重入。
- 低层 API 完全保留；非常规窗口（多窗口/自定义命中/特殊循环）仍可按 5.1–5.6 手写。
- 完整可运行代码：[samples/minimal.c](../samples/minimal.c)（约 150 行）；
  标准样例：[src/demo.c](../src/demo.c)（约 340 行）。

### 5.1 窗口与进程准备

#### 5.1.1 窗口样式

```c
HWND hwnd = CreateWindowExW(
    WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP,
    L"MyCuiWnd", L"My App",
    WS_POPUP,
    0, 0, 0, 0,
    NULL, NULL, inst, NULL);
```

- `WS_EX_NOREDIRECTIONBITMAP` **必须在创建时指定**。运行时用
  `SetWindowLongPtr` 增删无效。它是 DComp 直出的前提。
- `WS_POPUP` 表示无系统边框；标题栏、关闭键由你自绘。
- 手写窗口要可缩放时，加 `WS_THICKFRAME`，在 `WM_NCCALCSIZE` 返回 0 让客户区
  铺满，并在 `WM_NCHITTEST` 返回 `HTLEFT/HTRIGHT/HTTOP/HTBOTTOM` 及四角；
  `UiApp` 已内置这套逻辑（6 DIP 边缘）和 `WM_GETMINMAXINFO` 最小尺寸。
- 如果希望任务栏显示图标，保留 `WS_EX_APPWINDOW`。
- 回退到 ULW 时，`Gfx` 会自动追加 `WS_EX_LAYERED` 并调用
  `SetWindowPos(... SWP_FRAMECHANGED)`，应用无需改代码。

#### 5.1.2 DPI 感知

推荐 Per-Monitor V2：

```c
static void enable_dpi_awareness(void)
{
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        typedef BOOL (WINAPI *FnSetCtx)(HANDLE);
        FnSetCtx f = (FnSetCtx)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (f && f((HANDLE)(INT_PTR)-4)) return;   /* PER_MONITOR_AWARE_V2 */
    }
    SetProcessDPIAware();                           /* 旧系统回退 */
}
```

> 为什么动态取符号：这样同一份二进制在较旧的 Win10 上也能启动。

#### 5.1.3 COM

D2D/DWrite/DComp 需要 COM。`WinMain` 开头：

```c
CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
```

退出时 `CoUninitialize()`。

### 5.2 初始化

推荐的初始化顺序：

```c
/* WM_CREATE */
Gfx_Init(&gfx, hwnd);                 /* 1. D2D/DWrite 工厂 */
UiTheme_Init(&theme, gfx);            /* 2. 字体 + 颜色令牌 */
Ui_ContextInit(&ui, hwnd, gfx, &theme);/* 3. 交互上下文 */
UiIme_Init(&ime, hwnd);               /* 4. 输入法（有编辑框时） */
Edit_Init(&edit, 63, 0, NULL);        /* 5. 编辑框（可选） */
UiFrame_Init(&clock, hwnd);           /* 6. 帧时钟 */
```

注意：

- `Gfx_Init` 只创建工厂，**真正的渲染表面在第一次 `Gfx_Resize` 时创建**。
  因此 `WM_CREATE` 里窗口尺寸为 0 也没关系。
- `UiTheme_Init` 创建的字体必须在 `Gfx_Shutdown` 之前销毁：
  `UiTheme_Shutdown(&theme, gfx)`。
- `UiIme_Init` 只是保存 HWND；真正的输入上下文在聚焦时装卸。

### 5.3 一帧的顺序

`WM_PAINT` 是唯一入口。推荐顺序：

```c
case WM_PAINT: {
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    EndPaint(hwnd, &ps);              /* 先结束系统绘制，再自己渲染 */

    render(&ps.rcPaint);
    return 0;
}
```

`render` 内部：

```c
static void render(const RECT *dirtyPx)
{
    RECT rc;
    float w, h;

    GetClientRect(hwnd, &rc);
    Gfx_Resize(gfx, rc.right, rc.bottom, scale);   /* 物理像素 + 缩放 */
    w = (float)rc.right / scale;
    h = (float)rc.bottom / scale;
    Ui_SetViewport(&ui, w, h, scale);              /* DIP 视口 */
    Ui_SetFocusId(&ui, focusedId);                 /* 键盘焦点 */

    Gfx_SetDirtyRect(gfx, dirtyPx);                /* 物理像素脏区 */
    Gfx_Begin(gfx);
    Gfx_Clear(gfx, 0x00000000);
    /* 背景与内容 ... */

    Ui_BeginFrame(&ui);
    /* 控件绘制 + 注册 ... */
    UiId clicked = ui.clicked;                     /* 必须先快照 */
    Ui_EndFrame(&ui);

    Gfx_End(gfx);                                  /* Present，内部等 vsync */

    if (clicked == someId) { /* 延后动作 */ }
}
```

顺序要点：

1. `Gfx_Resize` 在 `Gfx_SetDirtyRect` 之前；尺寸变化会强制整帧。
2. `Ui_SetViewport` 在命中/失效之前；`scale` 必须与 `Gfx_Resize` 一致。
3. `Ui_SetFocusId` 每帧设置，告诉控件“键盘焦点在谁身上”。
4. `Gfx_SetDirtyRect` 在 `Gfx_Begin` 之前。
5. `Ui_BeginFrame` 在 `Gfx_Begin` 之后（绘制期间注册）。
6. `Ui_EndFrame` 在 `Gfx_End` 之前。
7. 模态/关闭类动作在 `Gfx_End` 之后。

### 5.4 主循环

demo 的模式是“有动画就按 vsync 跑，没动画就阻塞等消息”：

```c
while (!quit) {
    if (app_animating()) {
        double t0 = UiFrame_Now();
        double dt = UiFrame_Tick(&clock);
        app_advance(dt);                       /* Ui_AnimAdvance + 业务动画 */
        if (hwnd) {
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateWindow(hwnd);                /* Present 自己挡到 vsync */
        }
        UiFrame_Delay(&clock, clock.periodMs - (UiFrame_Now() - t0));
    } else {
        MsgWaitForMultipleObjectsEx(0, NULL, INFINITE, QS_ALLINPUT,
            MWMO_INPUTAVAILABLE);
    }

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { quit = 1; break; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}
```

`app_animating()` 至少要包含 `Ui_AnimBusy(&ui)`；demo 还包含自动进度条和最小化判断。

> 轮询式 `InvalidateRect + UpdateWindow` 看起来“浪费”，但 `Gfx_End` 里的
> `Present(1, 0)` 会阻塞到 vsync，实际是稳定帧率，不会空转烧 CPU。

### 5.5 完整最小骨架

下面是一份可直接改名的单按钮程序。为了聚焦主干，省略了 DPI 变化、输入法等
可选部分；完整版见 `src/demo.c`。

```c
#include <windows.h>
#include "gfx/gfx.h"
#include "ui/ui_context.h"
#include "ui/ui_theme.h"
#include "ui/ui_paint.h"
#include "ui/ui_frame.h"
#include "ui/ui_anim.h"

typedef struct App {
    HWND hwnd;
    Gfx *gfx;
    UiTheme theme;
    UiContext ui;
    UiFrameClock clock;
    float scale;
    int tracking;
    int pending;
} App;

static App app;

static void app_render(HWND hwnd, const RECT *dirtyPx)
{
    RECT rc;
    float w, h;

    if (!app.gfx) return;
    GetClientRect(hwnd, &rc);
    Gfx_Resize(app.gfx, rc.right, rc.bottom, app.scale);
    w = (float)rc.right / app.scale;
    h = (float)rc.bottom / app.scale;
    Ui_SetViewport(&app.ui, w, h, app.scale);
    Ui_SetFocusId(&app.ui, UI_ID_NONE);

    Gfx_SetDirtyRect(app.gfx, dirtyPx);
    Gfx_Begin(app.gfx);
    Gfx_Clear(app.gfx, 0x00000000);
    Gfx_FillRoundRectGradV(app.gfx, Ui_Rect(0, 0, w, h), 12.0f,
        0xFFF9FBFF, 0xFFECF1FB);

    Ui_BeginFrame(&app.ui);
    {
        UiId id = Ui_Id("app.hello", 0, 0);
        UiRect r = Ui_Rect(24, 24, 180, 42);
        UiSurfaceStyle ss;
        ZeroMemory(&ss, sizeof(ss));
        ss.bg = 0xFF4C6FFF;
        ss.bg2 = 0xFF3F5BE0;
        ss.hoverBg = 0xFF7E9BFF;
        ss.pressBg = 0xFF3550C8;
        ss.radius = 8.0f;
        ss.glow = Ui_ColorAlpha(app.theme.blue2, 110);
        ss.glowW = 4.0f;
        ss.glowMask = UI_GLOW_HOVER;
        ss.overlayTint = 1;

        Ui_DrawSurface(&app.ui, id, r, &ss, 1);
        Gfx_Text(app.gfx, app.theme.fBtn, "Hello CUI", r, 0xFFFFFFFF,
            GFX_TEXT_CENTER | GFX_TEXT_VCENTER);

        if (app.ui.clicked == id) app.pending = 1;   /* EndFrame 前读 */
    }
    Ui_EndFrame(&app.ui);
    Gfx_End(app.gfx);

    if (app.pending) {
        app.pending = 0;
        MessageBoxW(hwnd, L"CUI works.", L"CUI", MB_OK);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        app.hwnd = hwnd;
        app.scale = 1.0f;
        if (!Gfx_Init(&app.gfx, hwnd)) return -1;
        UiTheme_Init(&app.theme, app.gfx);
        Ui_ContextInit(&app.ui, hwnd, app.gfx, &app.theme);
        UiFrame_Init(&app.clock, hwnd);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        app_render(hwnd, &ps.rcPaint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE: {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        if (!app.tracking) {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = 0;
            if (TrackMouseEvent(&tme)) app.tracking = 1;
        }
        Ui_OnMouseMove(&app.ui, pt.x / app.scale, pt.y / app.scale);
        return 0;
    }
    case WM_MOUSELEAVE:
        app.tracking = 0;
        Ui_OnMouseLeave(&app.ui);
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        SetCapture(hwnd);
        SetFocus(hwnd);
        Ui_OnMouseDown(&app.ui, pt.x / app.scale, pt.y / app.scale);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        Ui_OnMouseUp(&app.ui, pt.x / app.scale, pt.y / app.scale);
        ReleaseCapture();
        return 0;
    }
    case WM_CAPTURECHANGED:
        Ui_OnCaptureChanged(&app.ui);
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        UiFrame_Shutdown(&app.clock);
        UiTheme_Shutdown(&app.theme, app.gfx);
        Gfx_Shutdown(app.gfx);
        app.gfx = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
```

`WinMain` 里把消息循环按 5.4 节的模板补上即可。

### 5.6 关闭窗口

关闭有两条路径：

1. **自绘关闭键**：`Ui_DrawSurface` 注册 `demo.close`，在 `EndFrame` 前快照
   `clicked`，`Gfx_End` 后 `PostMessage(hwnd, WM_CLOSE, 0, 0)`。
2. **系统关闭**（Alt+F4 / 任务栏关闭）：直接走 `WM_CLOSE` → `DestroyWindow`。

不要在自己的绘制函数里直接 `DestroyWindow`：`WM_DESTROY` 会释放 `Gfx`，而当前栈
还在 `Gfx_End`/`Ui_EndFrame` 里，容易出现 use-after-free。用 `PostMessage` 把关闭
推到消息队列，是最稳的做法。

---

## 6. Gfx 渲染层

### 6.1 坐标、颜色、文本

- **坐标**：DIP（Device Independent Pixel），`float`。96 DPI 下 1 DIP = 1 物理像素。
- **颜色**：`0xAARRGGBB`。例如 `0xFF4C6FFF` 是不透明白底上的靛蓝。
- **文本**：`const char *` UTF-8。后端转 UTF-16 交给 DirectWrite。
- **字体**：`Gfx_CreateFont(gfx, sizeDip, weight)`，固定微软雅黑、`zh-CN`；
  `weight >= 600` 映射为 Bold。

### 6.2 生命周期

```c
Gfx *gfx = NULL;
Gfx_Init(&gfx, hwnd);          /* 工厂 + 文本格式 + 描边样式 */
/* ... 使用 ... */
Gfx_Shutdown(gfx);
```

`Gfx_Init` 不创建渲染目标；第一次 `Gfx_Resize` 才创建 DComp 交换链或 DIB。
这样应用可以先创建窗口、再决定初始尺寸。

### 6.3 帧、脏区与呈现

```c
Gfx_SetDirtyRect(gfx, &ps.rcPaint);   /* 物理像素；NULL/空 = 整帧 */
Gfx_Begin(gfx);
/* 绘制... */
Gfx_End(gfx);                          /* EndDraw + Present / ULW */
```

- **脏区是物理像素**。`Gfx` 内部按 `scale` 转成 DIP，并裁剪掉脏区外的绘制调用。
- `Gfx_End` 之后会把 `dirtyFull` 置回 1，所以**下一帧必须重新调用
  `Gfx_SetDirtyRect`**；否则默认整帧。
- 尺寸/设备恢复/DPI 变化会自动强制整帧，不需要应用操心。
- `Gfx_Begin` 会 `SetTransform(scale)`，所以组件始终用 DIP。

DComp 路径的内部结构：

```text
uiTarget (离屏 ID2D1Bitmap1，持久层)
   │  每帧只画脏区
   ▼
swapTarget (交换链后缓冲)
   │  DrawBitmap 整幅拷贝（GPU）
   ▼
Present(1, 0) 阻塞到 vsync
```

ULW 回退路径：

```text
DIB (32bpp top-down) ← D2D DC 渲染目标
   │
   ▼
UpdateLayeredWindow(hwnd, ..., ULW_ALPHA)
```

### 6.4 绘制原语

| 函数 | 用途 |
|---|---|
| `Gfx_FillRect` | 实心矩形 |
| `Gfx_FillRoundRect` | 圆角矩形 |
| `Gfx_FillRoundRectGradV/H` | 垂直/水平渐变圆角矩形 |
| `Gfx_FillRoundRectMultiGrad` | 多停靠点渐变（最多 16 个） |
| `Gfx_FillEllipse` / `Gfx_FillEllipseGradV` | 圆/椭圆 |
| `Gfx_StrokeRoundRect` / `Gfx_StrokeEllipse` | 描边 |
| `Gfx_Line` | 线段（圆头线帽） |
| `Gfx_StrokeArc` | 圆弧（角度，屏幕坐标系顺时针） |
| `Gfx_FillTriangle` | 三角形 |
| `Gfx_PushClip` / `Gfx_PopClip` | 轴对齐裁剪，必须成对 |
| `Gfx_PushAlpha` / `Gfx_PopAlpha` | 全帧透明度图层，必须成对 |
| `Gfx_IsVisible` | 当前帧脏区是否与矩形相交；用于跳过脏区外的排版/转换 |
| `Gfx_DrawBitmap` | 预乘 alpha 的 BGRA 位图 |

### 6.5 文本

```c
GfxFont *f = Gfx_CreateFont(gfx, 14.0f, 400);
float w = Gfx_TextWidth(gfx, f, "你好");
float h = Gfx_TextHeight(gfx, f);
Gfx_Text(gfx, f, "你好", Ui_Rect(10, 10, 200, 30), 0xFF1F2637,
    GFX_TEXT_VCENTER | GFX_TEXT_ELLIPSIS);
```

- `Gfx_Text` 的矩形同时是布局宽度与裁剪范围；文本不会自动换行
  （`WORD_WRAPPING_NO_WRAP`）。
- 对齐标志：`GFX_TEXT_CENTER`、`GFX_TEXT_RIGHT`、`GFX_TEXT_VCENTER`、
  `GFX_TEXT_ELLIPSIS`。
- **文本布局有缓存**：同一字体 + 文本 + 尺寸 + 标志会复用 `IDWriteTextLayout`。
  缓存上限 128 条，单条文本 < 160 字节；超长文本走临时布局，绘制后释放。
- `Gfx_TextHeight` 用“国”字测量，作为行高参考。
- 文本抗锯齿固定 **GRAYSCALE**。不能用 ClearType：它会写入不透明像素，破坏
  透明窗口的边缘 alpha。
- 局部重绘时，先用 `Gfx_IsVisible(gfx, rc)` 判断再转换 UTF-8/排版，可以省掉
  脏区外大量静态文本的 CPU；demo 的 `draw_wtext` 与 `Edit_Draw` 都这样做。

### 6.6 渐变与画刷缓存

- 实心画刷缓存 128 种颜色，满了按轮转淘汰。颜色来自动画插值时，缓存会频繁轮转，
  这是预期行为，不会“画不出来”。
- 渐变画刷缓存 24 个，按“颜色 + 位置 + 方向”匹配；最多缓存 4 个停靠点，
  超过 4 个的渐变走临时画刷，绘制后释放。
- 渐变画刷按单位空间创建（0,0 → 1,0 / 0,1），绘制时用矩阵映射到目标矩形。
  因此进度条逐帧变宽不会重建 stop collection。
- `Gfx_GradientCreates(gfx)` 返回并清零“累计新建渐变画刷次数”，可用于验证
  动画期间没有反复建画刷。

### 6.7 位图

```c
GfxBitmap *bmp = Gfx_CreateBitmap(gfx, w, h, bgra);
Gfx_DrawBitmap(gfx, bmp, Ui_Rect(10, 10, w, h));
Gfx_DestroyBitmap(gfx, bmp);
```

- 像素格式：`DXGI_FORMAT_B8G8R8A8_UNORM`。
- alpha 模式：**预乘 alpha**。如果你从 PNG 解码，记得先做 `rgb *= a / 255`。
- 设备/表面重建后，位图可能失效；用 `Gfx_Generation(gfx)` 判断是否要重建缓存。

### 6.8 DComp / ULW 双路径

```text
Gfx_Resize(第一次)
   ├─ SCV2_ULW=1 ? ────────────────► ULW
   ├─ InitDcomp 成功 ? ────────────► DComp
   └─ 失败 ────────────────────────► ULW
```

- DComp 失败的原因会写日志：`[gfx] DComp ... failed`。
- 运行时 `EndDraw`/`Present` 失败也会自动回退 ULW，应用无需改代码。
- 环境变量：
  - `SCV2_ULW=1`：强制 ULW，便于对比/排查。
  - `SCV2_NOVSYNC=1`：`Present(0, 0)`，不等 vsync（会撕裂，仅诊断用）。

> DComp 路径要求窗口带 `WS_EX_NOREDIRECTIONBITMAP`；ULW 路径要求 `WS_EX_LAYERED`。
> `Gfx` 会自动切换 `WS_EX_LAYERED`，但 `WS_EX_NOREDIRECTIONBITMAP` 必须在创建时给。

---

## 7. UiContext 交互层

### 7.1 一帧的注册周期

```c
Ui_BeginFrame(&ui);        /* widgetCount = 0; layer = BASE */
/* 绘制并注册控件 */
Ui_EndFrame(&ui);          /* 清空单帧事件；结束拖动 */
```

控件表容量 `UI_MAX_WIDGETS = 256`。超出后 `Ui_RegisterWidget` 静默丢弃；
页面级控件数量要控制在这个量级内。

### 7.2 注册与命中

```c
Ui_RegisterWidget(&ui, id, rc);           /* 只注册，不绘制 */
UiId hit = Ui_HitTest(&ui, x, y);         /* 最高 z; 同 z 后画的优先 */
UiWidgetState st = Ui_State(&ui, id, rc, enabled);

Ui_PushLayer(&ui, UI_Z_TOAST);            /* 切换注册层 z-index */
/* ... 画/注册弹层 ... */
Ui_PopLayer(&ui);
```

- `Ui_HitTest` 先选**最高 z** 的控件；同 z 时后注册（后画）的优先。
- `Ui_PushLayer(ctx, z)` / `Ui_PopLayer` 成对使用；`Ui_BeginFrame/EndFrame` 会重置。
- **z-index 只影响命中**。即时模式的绘制顺序仍是调用顺序，应用要按 z 从低到高画。
  预置常量：`UI_Z_BASE=0`、`UI_Z_OVERLAY=1000`、`UI_Z_TOAST=2000`。
- 旧的 `popupOpen + UI_LAYER_OVERLAY` 仍保留：`popupOpen=1` 时只命中 overlay 层，
  可与 z-index 共存。
- **不可见模态 blocker**：`Ui_SetModal(&ui, id)` 之后所有命中都返回 `id`，但
  `Ui_OnMouseMove/Down/Up` 不会失效任何控件（不会触发重绘）。合成器 Toast 的遮罩
  在 DComp layer 上，主窗用这个接口阻断输入；`Ui_SetModal(&ui, UI_ID_NONE)` 解除。
- 鼠标位置是 DIP；调用 `Ui_OnMouse*` 前记得除以 `scale`。

### 7.3 状态语义

`UiWidgetState` 同时给出离散状态和平滑强度：

| 字段 | 含义 |
|---|---|
| `hovered` | 当前鼠标是否命中 |
| `pressed` | 鼠标按下且按在这个控件上 |
| `active` | 正在拖动（`ctx->drag.active`） |
| `focused` | `Ui_SetFocusId` 设置的焦点 ID 与它相同 |
| `hover/press/focus/glow` | 平滑后的 0..1 强度，可直接用于插值 |

`Ui_State` 会自动对 `UI_AN_HOVER/PRESS/FOCUS/GLOW` 调用 `Ui_AnimSmooth`。
`Ui_DrawSurface` 还会根据 `glowMask` 重新计算 glow 目标。

禁用态（`enabled = 0`）：

- 不注册命中，因此点不到；
- 四个交互通道目标归零，视觉上会淡出；
- 你仍然需要自己画禁用配色。

### 7.4 单帧事件

```c
UiId clicked;         /* 同一控件内按下并抬起 */
UiId down;            /* 本次按下的控件（编辑框用于立即定位光标） */
UiId released;        /* 按下过的控件被抬起（滑块提交用） */
int  clickedOutside;  /* 抬起时未命中任何控件 */
```

**这些字段只在一次鼠标消息与下一次绘制之间有效**，`Ui_EndFrame` 清零。

正确写法：

```c
Ui_BeginFrame(&ui);
draw_everything();
UiId clicked = ui.clicked;    /* 快照 */
Ui_EndFrame(&ui);
Gfx_End(gfx);
if (clicked == myButtonId) do_action();
```

错误写法（本次修复前的 demo）：

```c
Ui_EndFrame(&ui);
if (ui.clicked == myButtonId) do_action();   /* 永远为 0 */
```

如果你在绘制中途就处理动作，注意不要弹模态框：模态循环会重入 `WM_PAINT`，
此时 `clicked` 还没有被清零，会再次弹窗，形成嵌套。

### 7.5 鼠标接线细节

```c
WM_MOUSEMOVE -> Ui_OnMouseMove(ctx, x/scale, y/scale)
WM_LBUTTONDOWN -> SetCapture; SetFocus; Ui_OnMouseDown(...)
WM_LBUTTONUP -> Ui_OnMouseUp(...); ReleaseCapture();
WM_MOUSELEAVE -> Ui_OnMouseLeave(ctx)
WM_CAPTURECHANGED -> Ui_OnCaptureChanged(ctx)
```

- `WM_MOUSEMOVE` 第一次进入窗口时要 `TrackMouseEvent(TME_LEAVE)`，否则收不到
  `WM_MOUSELEAVE`，hover 会卡住。
- 按下期间 `Ui_OnMouseMove` 会清空 hover，保证按下态稳定；`Ui_OnMouseUp` 会按抬起
  位置恢复 hover。随后 `ReleaseCapture()` 触发的 `WM_CAPTURECHANGED` 会再次清空
  hover，鼠标下一次移动时恢复——这是正常现象。
- `WM_CAPTURECHANGED` 要清 pressed/active/drag，否则丢失捕获后控件会卡在按下态。

### 7.6 焦点

`UiContext` 不自己管理键盘焦点；应用每帧调用：

```c
Ui_SetFocusId(&ui, focusedEdit ? editId : UI_ID_NONE);
```

这样 `Ui_State` 才能给出 `focused`，`Ui_DrawSurface` 才能画焦点描边/光晕。
编辑框的 `focused` 字段也要同步设置，用于光标闪烁。

### 7.7 稳定 ID 规范

```c
UiId id = Ui_Id("page.settings.name", 0, 0);
UiId item = Ui_IdIndex("list.row", 0, 0, rowIndex);  /* 列表项用业务键 */
```

- 作用域建议用“页面/模块.控件”命名，避免碰撞。
- 同帧内必须唯一；跨帧必须稳定。
- 不要用指针地址、动态序号、`__LINE__` 做 ID。
- 列表项不要用行号：插入/删除行会导致动画和命中错位；用数据 ID。

### 7.8 拖动与滑块

`UiContext` 内置了一组拖动字段：

```c
ui.drag.id / ui.drag.v1 / ui.drag.v2 / ui.drag.active
```

`Ui_State` 的 `active` 直接来自它。滑块/色板类控件可以这样用：

```c
if (ui.down == id) { ui.drag.id = id; ui.drag.active = 1; }
if (ui.drag.active && ui.drag.id == id) { /* 用鼠标位置更新 v1 */ }
if (ui.released == id) { /* 提交 */ ui.drag.active = 0; }
```

`Ui_EndFrame` 会在 `released` 非零时自动把 `drag.active` 置 0。

---

## 8. UiPaint 与 UiTheme

### 8.1 UiSurfaceStyle

`Ui_DrawSurface` 用一份样式描述一个交互表面：

```c
typedef struct {
    UiColor bg, bg2;                  /* bg2 非 0 = 垂直渐变 */
    UiColor hoverBg, pressBg;
    UiColor border, hoverBorder, focusBorder;
    UiColor glow;                     /* 含 alpha；0 = 不发光 */
    float   radius;
    float   borderW;
    float   glowW;
    unsigned glowMask;                /* UI_GLOW_HOVER / UI_GLOW_FOCUS */
    int     overlayTint;
} UiSurfaceStyle;
```

绘制规则：

1. 根据 `hover/press` 平滑值插值填充色。若有 `bg2`，两个端点分别插值。
2. 若有 `border`，按 `hover/focus` 插值描边色；焦点时线宽增加 `0.6`。
3. 若 `glow` 非 0 且 `glowMask` 匹配当前状态，画 3 圈渐隐描边作为光晕。

### 8.2 overlayTint 优化

当满足以下条件时，CUI 会走“固定渐变 + 半透明纯色叠加”：

- `bg2 != 0`；
- `overlayTint = 1`；
- `hoverBg` 和 `pressBg` 要么为 0，要么完全不透明（alpha = 0xFF）。

好处：渐变画刷的颜色恒定，悬停动画期间不会每帧重建 stop collection。
demo 的主按钮就是这种用法。

### 8.3 光晕

光晕不是高斯模糊，而是 3 圈逐渐外扩、alpha 递减的描边：

```text
k = 2, 1, 0
infl = glowW * (k+1)/3 * (0.35 + 0.65 * intensity)
alpha = glowBase * intensity * (0.42 - 0.12*k)
```

成本远低于模糊，适合“同时最多一两个悬停控件”的安装器场景。

### 8.4 UiTheme

主题是 **一组颜色令牌 + 一组字体**，组件不写死颜色。

```c
UiTheme theme;
UiTheme_Init(&theme, gfx);       /* 创建 11 个字体 + 填颜色 */
/* ... */
UiTheme_Shutdown(&theme, gfx);
```

字体令牌：

| 字段 | 字号/字重 | 用途 |
|---|---|---|
| `fTitle` | 16 bold | 标题 |
| `fHead` | 15 bold | 小标题 |
| `fBody` | 13 normal | 正文 |
| `fSmall` | 11 normal | 辅助文字 |
| `fTemp` | 26 bold | 温度等大数字 |
| `fBtn` | 12 bold | 按钮 |
| `fMini` | 11 normal | 极小字 |
| `fTempBig` | 46 bold | 超大数字 |
| `fInput` | 14 normal | 编辑框 |
| `fLabel` | 12 bold | 字段标签 |
| `fBig` | 22 bold | 大标题 |

颜色令牌按用途分组：

- 通用：`page/page2/card/border/frame/frameHi/text/text2/text3/text4`、
  `green/gold/red/orange/blue/blue2`。
- 设备面板：`panelTop/panelBottom/hdrLine/tab1/tab2/btn1/btn2/iconBlue/...`。
- 卡片/控件：`panelBg/panelBorder`、`controlBox/controlBorder/rowOn/rowOff/...`。
- 按钮：`btnPrimary/btnPrimary2/btnPrimaryHover/btnPrimaryPress/btnPrimaryText`、
  `btnBg/btnHoverBg/btnPressBg/btnText/btnBorder/btnHoverBorder`、
  `btnDisabledBg/btnDisabledText`。
- 进度条：`progressTrack/progressFill/progressFill2`。
- Toast：`toastBg/toastBorder/toastText/toastOverlay`。
- 安装器：`inputBg/inputBorder/inputFocus/hint/danger/dangerBg/okBg`。
- 圆角：`windowRadius/panelRadius/buttonRadius/progressRadius/toastRadius`。
  `windowRadius` 同时用于主窗背景和 Toast 整窗遮罩，保证底部圆角不被遮罩填方。

**语义色角色**：

每种状态都有一组 `UiColorRole`（组件只读这里）：

| 角色 | 用途 | 字段 |
|---|---|---|
| `primary` | 主按钮、焦点、进度、开关 | `base/hover/press/soft/text/border` |
| `warning` | 警告 | 同上 |
| `success` | 成功 | 同上 |
| `error` | 错误/危险 | 同上 |
| `neutral` | default：中性/次要按钮 | 同上 |

**亮/暗预设**：

```c
UiTheme_SetTheme(&theme, UI_THEME_LIGHT);   /* 或 UI_THEME_DARK */
UiApp_Invalidate(app);                      /* 整窗重绘 */
```

**自定义 5 个语义色**：

```c
static const UiThemeColors brand = {
    0xFFE4572E,   /* primary */
    0xFFF2A93B,   /* warning */
    0xFF2FA56B,   /* success */
    0xFFD64545,   /* error */
    0xFFF3F5F9    /* neutral = default */
};
UiTheme_SetColors(&theme, UI_THEME_LIGHT, &brand);  /* 深色外壳传 DARK */
UiApp_Invalidate(app);
```

- `UiTheme_SetColors` 先应用 light/dark 外壳（页面/卡片/文字），再覆盖 5 个语义色；
  hover/press/soft/对比文字/描边全部自动派生，不需要手算。
- `UiThemeColors` 字段传 `0` 表示保留该角色的预设；整个指针传 `NULL` 等价于
  `UiTheme_SetTheme(t, base)`。
- `neutral` 就是组件库里的 `default`（C 关键字不能作字段名）。
- 这 5 个色会同步写入旧令牌（`blue/blue2/btnPrimary*/progressFill*/green/orange/
  danger/dangerBg/btnBg/...`），所以老代码和所有内置组件一起换肤。
- `UiTheme_SetTheme/SetColors` 都会让 `themeRev+1`；`UiProgress` 用 `themeRev`
  判断重建 overlay，`UiToast` 用它判断重画 layer。**任意换肤都不需要手动销毁**。
- 只想要完整自定义（页面/卡片/文字也改）时：直接改 `UiTheme` 字段，再调用
  `UiTheme_Touch(&theme)` 通知 overlay/Toast。
- 字体字号属于主题初始化的一部分；换字体需要自行重建 `UiTheme` 或另行管理
  `Gfx_CreateFont`。
- demo 的按钮按 浅色 → 品牌色 → 深色 循环，品牌色就是上面这段代码。

### 8.5 自绘关闭键示例

```c
static void draw_close(UiRect rc)
{
    UiSurfaceStyle ss;
    UiWidgetState st;
    UiColor c;

    ZeroMemory(&ss, sizeof(ss));
    ss.bg = 0xFFFFFFFF;
    ss.hoverBg = 0xFFF1F3FA;
    ss.pressBg = 0xFFE4E8F5;
    ss.radius = rc.w * 0.5f;

    st = Ui_DrawSurface(&ui, Ui_Id("demo.close", 0, 0), rc, &ss, 1);
    c = st.pressed ? theme.blue2 : theme.text2;

    float cx = rc.x + rc.w * 0.5f;
    float cy = rc.y + rc.h * 0.5f;
    float d = 5.0f;
    Gfx_Line(gfx, cx - d, cy - d, cx + d, cy + d, c, 1.6f);
    Gfx_Line(gfx, cx - d, cy + d, cx + d, cy - d, c, 1.6f);
}
```

关闭键本身不需要特殊逻辑，点击判定与其他按钮一致。

### 8.6 组件库（Button / Panel / ProgressBar）

```c
#include "ui/ui_widgets.h"

int clicked = Ui_Button(&ui, Ui_Id("form.submit", 0, 0),
    Ui_Rect(52, 300, 110, 38), "提交", UI_BTN_PRIMARY, 1);
Ui_Panel(&ui, cardRc);
Ui_ProgressBar(&ui, barRc, progress, drawFill);
```

- `Ui_Button` 变体：`UI_BTN_PRIMARY / UI_BTN_SECONDARY / UI_BTN_DANGER`，
  以及 `UI_BTN_WARNING / UI_BTN_SUCCESS`；`UI_BTN_DEFAULT` 是 `SECONDARY` 别名，
  `UI_BTN_ERROR` 是 `DANGER` 别名。标签 UTF-8；返回 `ui->clicked == id`。
  禁用时自动用 `neutral.soft` 与 `btnDisabledText`。
- `Ui_Panel` 画 `panelBg/panelBorder/panelRadius` 的卡片。
- `Ui_ProgressBar` 画 `progressTrack` + 可选填充
  `progressFill -> progressFill2`；`drawFill=0` 时只画轨道，填充交给
  `GfxOverlay`（demo 就是这么做的）。
- 组件不持有布局：矩形由应用按 DIP 计算；没有自动换行/流式布局。
- 组件只读 `ui->theme`，不写死颜色；换主题下一帧生效。
- 组件身份仍是调用方的 `UiId`；同一帧内必须唯一。

**UiProgress（自带 6 秒循环与合成器 overlay）**

```c
#include "ui/ui_progress.h"

UiProgress_Init(&p, &ui, 6000);

/* 帧内 */
UiProgress_Draw(&p, barRc);

/* Gfx_End 之后 */
UiProgress_EndFrame(&p);

/* 动画节拍 (UiApp 的 on_tick 或手写循环) */
UiProgress_Tick(&p, dtMs);
if (!UiProgress_OverlayAlive(&p) && p.rc.w > 0)
    InvalidateRect(hwnd, &dirtyBarRect, FALSE);  /* ULW/回退才需要 */

/* 暂停/恢复 */
UiProgress_SetPaused(&p, paused);

/* 只有"主窗内模态遮罩"才需要让位; 合成器 Toast 不用 (见 8.7) */
UiProgress_SetHidden(&p, mainWindowModalVisible);
```

- 组件内部：6 秒循环值、轨道、`GfxOverlay` 补间、ULW 回退、暂停、主题重建。
- DComp 可用时主窗不需要为进度重绘（demo 实测 0.00% 应用 CPU）；
  overlay 不可用/隐藏时自动退回主窗填充。
- `SetHidden(1)` 会让 overlay 透明并在主窗回退绘制，**只适用于主窗内绘制的模态
  遮罩**（例如 advanced 样例的 Toast）。合成器 Toast 在 overlay 之上另有图层，
  进度条可以继续在遮罩下面由 DWM 更新，不要调用 `SetHidden`。
- 一个 `UiProgress` 对应一条进度条；周期可在 Init 中改。

### 8.7 Toast（合成器 layer 版）

Toast 有两种模式，API 相同：

| 模式 | 触发 | 遮罩/卡片画在哪 | 主窗重绘 | 进度 overlay |
|---|---|---|---|---|
| 合成器（推荐） | `UiToast_Attach` 且 `GfxLayer` 创建成功 | 独立 DComp layer | 只在创建/改尺寸/换主题时重画 layer，主窗 0 次 | 保持可见，继续更新 |
| 主窗回退 | 未 Attach，或 DComp/图层创建失败 | 主窗 UI（z-index） | 淡入/停留/淡出需整窗重绘 | 需 `SetHidden(1)` 让位 |

**推荐接法（clean demo 同款）**

```c
#include "ui/ui_toast.h"

/* init: 绑定上下文后优先走合成器 */
UiToast_Init(&toast);
UiToast_Attach(&toast, &ui);

/* 任意消息里显示 */
UiToast_Show(&toast, "已保存", UiFrame_Now(), 2600, UI_TOAST_SUCCESS);

/* 一帧内, 基础 UI 之后: layer 模式只登记不可见全窗 blocker, 不画像素 */
UiToast_Draw(&ui, &toast, UiFrame_Now(), w, h);

/* 点击遮罩/卡片 (Ui_SetModal 让 Ui_HitTest 返回 backdrop) */
if (UiToast_IsVisible(&toast) &&
    (ui.clicked == UiToast_BackdropId() || ui.clicked == UiToast_CardId()))
    UiToast_Hide(&toast, UiFrame_Now());

/* Gfx_End 之后: 创建/改尺寸 layer, 把渐变+卡片画一次, 启动 DWM 淡入 */
UiToast_EndFrame(&toast, UiFrame_Now(), w, h);

/* 动画节拍: layer 模式返回 0, 不需要主窗失效 */
int redraw = UiToast_Tick(&toast, UiFrame_Now());
if (!UiToast_UsingLayer(&toast)) {
    /* 回退: 主窗画遮罩, 进度让位, 按需整窗重绘 */
    UiProgress_SetHidden(&progress, UiToast_IsVisible(&toast));
    if (UiToast_IsVisible(&toast) || redraw)
        InvalidateRect(hwnd, NULL, FALSE);
}

/* WM_DESTROY, 在 Gfx_Shutdown 之前 */
UiToast_Shutdown(&toast);
```

- 视觉不变：全窗**竖向渐变遮罩**（顶部透明 → 底部 `toastOverlay`）+ 底部居中卡片；
  卡片按 `UI_TOAST_INFO/SUCCESS/ERROR` 用蓝/绿/红竖条强调。
- 遮罩使用 `theme.windowRadius` 圆角，不能画成直角矩形；否则底部 alpha 较高时会
  盖住主窗的左右下圆角，看起来窗口变成方角。ULW 回退的主窗渐变同样用该半径。
- **只画一次**：渐变/卡片/文本在 `UiToast_EndFrame` 中画进 layer 的 swapchain；
  淡入、停留、淡出都不再重绘。透明度补间由 DWM 完成（`Gfx_LayerSetOpacity`）。
- **卡片效果**：卡片轮廓过一次 D2D Shadow 得到软阴影，底板+强调条过一次自定义
  Grain（HLSL），文字最后直接画保持清晰。效果只在显示时计算一次，所以停留期间
  没有额外 GPU/CPU 工作。
- **z 次序固定**：DComp 的 `AddVisual(insertAbove, reference=NULL)` 语义反直觉：
  `FALSE` 才是置顶，`TRUE` 是置底。所以 layer 用 `AddVisual(FALSE, NULL)`，
  `GfxOverlay` 用 `AddVisual(TRUE, NULL)`。Toast 永远在进度 overlay 上面，
  进度条可以继续在遮罩下面更新，不需要 `UiProgress_SetHidden`。
- **输入隔离**：layer 模式只调用 `Ui_SetModal(&ui, UiToast_BackdropId())`；它让
  命中返回 backdrop，但鼠标移动/按下**不会失效控件**，因此主窗不会因为 Toast 重绘。
  `UiToast_Tick` 在 Toast 结束（淡出完成）时自动解除 modal。
- **窗口 resize / 主题切换**：`UiToast_EndFrame` 检测尺寸与 `themeId`，resize layer
  或重画一次；停留期间仍然零主窗重绘。
- **回退模式**：`UiToast_UsingLayer()` 变 0 后按上表走主窗渐变 + 整窗重绘；
  这条路径给 ULW 和极老系统兜底。`UiToast_Tick` 在这种情况下恢复旧语义
  （返回 1 = 需要重绘）。
- 时间用调用方传入的单调毫秒（`UiFrame_Now()`）；默认淡入/淡出各 200 ms。
- 一次只显示一条 Toast；需要排队/堆叠时在应用层扩展。
- 实测：前台 1440×1120@60Hz，Toast 淡入/停留/淡出期间应用进程 CPU **0.00%**；
  Toast 下进度条持续移动；点击遮罩/Esc 关闭、resize/重复显示均正常。

### 8.8 布局：UiStack / UiFlex

布局只计算矩形，不绘制、不注册控件、不持有控件状态。组件仍按 `UiRect` 工作，
因此布局是**可选**的：Toast/进度 overlay 继续绝对定位。

**UiStack（游标式）**

```c
UiStack col;
Ui_StackBeginPad(&col, card, UI_COLUMN, 0.0f, 28, 26, 28, 26);
UiRect title = Ui_StackTake(&col, 18);
Ui_StackSkip(&col, 6);
UiRect input = Ui_StackTake(&col, 40);
Ui_StackSkip(&col, 20);
UiRect row   = Ui_StackTake(&col, 38);
UiRect status = Ui_StackTakeFill(&col);
/* 不需要 End: UiStack 在栈上, 用完即可丢弃 */
```

- `Ui_StackBegin` 统一 padding；`Ui_StackBeginPad` 四边不同。
- `Take` 固定 DIP；`TakeFill` 吃掉剩余（**每个 stack 最多一个 Fill**）；
  `TakeRight` 主轴末端对齐；`Skip` 显式留白。
- `TakeAligned(main, cross, align)` 控制交叉轴 start/center/end。
- **没有 per-item margin**：用 gap + Skip；避免 margin 折叠/叠加规则。
- `Ui_StackInner` 返回 padding 后的内容区；背景仍用 `Ui_Panel(ui, outer)`。

**UiFlex（声明式，basis + grow）**

```c
UiFlexItem items[5] = {
    { 110, 0, 0, 0 },   /* 固定 110 */
    { 90,  0, 0, 0 },   /* 固定 90 */
    { 110, 0, 0, 0 },   /* 固定 110 */
    { 0,   1, 0, 0 },   /* grow=1, 空白弹簧 */
    { 110, 0, 0, 0 }    /* 主题按钮靠右 */
};
UiRect out[5];
Ui_FlexRow(rowRc, 12, items, 5, out);
```

- `basis` 是固定基础尺寸（DIP）；`grow` 是**剩余空间权重**，不是百分比。
- 固定 200 + 两个 `{0, 50}` / `{0, 80}`：先扣 200 和 gap，剩余按 50:80 分——
  这才等同于 flex-grow；不存在“50% 再归一化”的第二种语义。
- `minSize/maxSize` 会夹取；触到 max 的项冻结，余量最多再分 4 轮。
- **不做 shrink**：basis 总和超过容器就是溢出，由应用决定换行/滚动/裁剪。
- 交叉轴默认铺满；百分比固定宽由应用自己算 `basis = pct * inner.w/h`。
- `Ui_FlexRow/Col` 纯函数、零分配，`out` 由调用方提供。

**规则总结**

| 项 | 规则 |
|---|---|
| padding | 容器四边，默认统一 |
| margin | 不支持；gap + `Ui_StackSkip` |
| 背景/边框 | 布局不画；外层 `Ui_Panel` 或自绘 |
| 固定尺寸 | `Ui_StackTake` / `basis` |
| 剩余空间 | `TakeFill` / `grow` 权重 |
| 百分比 | 不作为独立模式；需要就换算成 basis |
| 溢出 | 不压缩，调用方处理 |
| 分配 | 栈上结构/调用方数组，无堆分配 |

demo 的卡片表单已改为 `UiStack` 纵向排布，按钮行用 `UiFlex`（中间 grow 弹簧，
主题按钮靠右）；窗口可拖边缩放，600×480 到 1000×700 之间布局自动重算，最小
尺寸在 demo 初始化为 600×480（再小按钮行会按“无 shrink”规则溢出）。

---

## 9. UiAnim 动画内核

### 9.1 两种运动模型

| 模型 | API | 行为 | 用途 |
|---|---|---|---|
| 指数平滑 | `Ui_AnimSmooth` | 帧率无关地追目标，可中途反向 | hover/press/focus/glow |
| 显式补间 | `Ui_AnimTo` | 固定时长，从当前值走到目标 | 淡入、上滑、状态切换 |
| 直接设值 | `Ui_AnimSet` | 立即到位 | 初始化/重置 |

平滑公式：

```text
v += (target - v) * dt / (tau + dt)
```

不使用 `exp()`，在 0CRT 下也没有精度/性能问题。

### 9.2 通道

```c
enum {
    UI_AN_HOVER = 0,
    UI_AN_PRESS,
    UI_AN_FOCUS,
    UI_AN_GLOW,
    UI_AN_ALPHA,      /* 0..1 透明度 */
    UI_AN_OFFSET,     /* DIP 位移 */
    UI_AN_PROGRESS,   /* 0..1 进度 */
    UI_AN_USER = 8,   /* 自定义通道从 8 开始 */
    UI_AN_CH = 16
};
```

每个控件 ID 最多 16 个通道。池容量 `UI_AN_MAX_ENTRIES = 64`，满了会忽略新动画，
不会崩。

平滑时间常数：

| 通道 | tau (ms) |
|---|---|
| HOVER | 90 |
| PRESS | 55 |
| FOCUS | 120 |
| GLOW | 160 |
| 其他 | 140 |

收敛阈值：

- `UI_AN_PROGRESS`：0.0015（进度条收尾不跳步）；
- 其他：0.008（约 2 个 8bit 色阶，肉眼不可辨）。

### 9.3 缓动

```c
Ui_AnimTo(&ui, id, UI_AN_ALPHA, 1.0f, 220, UI_AN_EASE_OUT);
```

| 常量 | 曲线 |
|---|---|
| `UI_AN_EASE_LINEAR` | 线性 |
| `UI_AN_EASE_OUT` | 三次缓出 `1-(1-p)^3` |
| `UI_AN_EASE_INOUT` | 三次缓入缓出 |

### 9.4 读取与推进

```c
/* 绘制时读取（不推进） */
float a = Ui_AnimGet(&ui, id, UI_AN_ALPHA, 1.0f);

/* 主循环每帧推进一次；dt 会被夹到 0..100 ms */
Ui_AnimAdvance(&ui, (float)dtMs);

/* 是否还有通道在动 */
int busy = Ui_AnimBusy(&ui);
```

- `Ui_AnimGet` 在条目不存在或通道未初始化时返回默认值（并创建条目）。
- 只有 `Ui_AnimAdvance` 会改变值；绘制函数里的 `Ui_AnimGet` 是只读的。
- `dt` 夹到 100 ms：拖动窗口/断点后不会瞬移，而是“慢半拍”继续。
- `Ui_AnimBusy` 为真时主循环应持续出帧，否则动画会停在中间。
- 应用若检测到系统“减少动态”，可调用 `Ui_AnimSetReduced(&ui, 1)`：所有通道立即到位。
  CUI 不会自动读取系统设置。

### 9.5 动画脏区

```c
UiRect dirty;
int kind = Ui_AnimTakeDirty(&ui, &dirty);
/* kind: 0 = 无变化, 1 = dirty 有效, 2 = 需要整窗 */
```

- 每个条目记录最近绘制矩形（`Ui_AnimSetRect`，`Ui_State`/`Ui_DrawSurface` 自动调用）。
- 动画值变化时把矩形外扩 10 DIP 合并到 `animDirty`。
- 无法定位矩形的动画（如窗口整体淡入）会设置 `animDirtyFull`，需要整窗重绘。
- 应用可以在 `InvalidateRect` 时使用这个区域做精细的局部重绘；demo 的主循环
  现在就是这么做的：状态文字/按钮悬停等注册 `Ui_AnimSetRect`，每帧只失效真正
  变化的矩形；持续进度条则进一步交给 9.7 的 `GfxOverlay` 由合成器补间。
- 注意：**每个会动的通道都必须有矩形**（`Ui_AnimSetRect`）。没有矩形的通道会
  置 `animDirtyFull`，导致整窗重绘；这也是常见的“动画一开 CPU 就高”的原因。
- 脏区只减少 D2D 绘制。DComp 交换链仍会每帧 Present 整窗；如果性能瓶颈在
  Present/DWM 合成（RDP、虚拟 GPU 上尤其明显），需要把持续变化的控件拆到独立
  visual/surface，或交给合成器动画，而不是继续扩脏区。下一节就是后者。

### 9.6 自定义动画

```c
#define MY_AN_SCALE UI_AN_USER

Ui_AnimSmooth(&ui, id, MY_AN_SCALE, hovered ? 1.08f : 1.0f);
float s = Ui_AnimGet(&ui, id, MY_AN_SCALE, 1.0f);
```

自定义通道同样受 16 通道上限与固定池限制。

### 9.7 合成器动画 overlay（持续动画）

当一条动画需要长时间连续播放（进度条、扫描线），即使脏区很小，DComp 交换链仍要
每帧 Present 整窗。`GfxOverlay` 把这条动画拆到**独立的小 swapchain**，作为根 visual
的子 visual；reveal 比例通过 `IDCompositionAnimation` 交给 DWM 补间——应用只 Commit
一次，不再每帧提交主窗口。

```c
/* 必须在 Gfx_End 之后创建 (DComp 已初始化) */
GfxOverlay *ov = Gfx_CreateOverlay(gfx, barRc, 5.0f, 0xFF7C97FF, 0xFF4C6FFF);
if (ov) {
    /* 先把当前比例同步过去, 再做 0 -> 1 的 6 秒线性补间 */
    Gfx_OverlaySetReveal(ov, 0.0f, 0);
    Gfx_OverlaySetReveal(ov, 1.0f, 6000);
}

/* 暂停: 立即停在当前逻辑比例 */
Gfx_OverlaySetReveal(ov, progress, 0);

/* 窗口/DPI 变化: 重建后恢复比例 (返回 2 表示已重建) */
if (Gfx_OverlaySetRect(ov, newRect) == 2)
    Gfx_OverlaySetReveal(ov, progress, 0);

/* 只有主窗内绘制的模态遮罩才需要让位 (合成器 Toast 在 overlay 上方, 不用) */
Gfx_OverlaySetOpacity(ov, 0.0f);   /* 0 = 隐藏, 1 = 恢复 */

/* 运行期 DComp 回退: 销毁并退回主窗局部绘制 */
if (!Gfx_OverlayAlive(ov)) {
    Gfx_OverlayDestroy(ov);
    ov = NULL;
}
```

- `rect` 是窗口客户区 **DIP**；内部按 DPI 转物理像素，offset/clip 都按物理像素提交。
- 底层是“一条静态渐变条 + 一个可动画的矩形 clip”；reveal 只改 clip 右边界，
  圆角/渐变在 DWM 合成时保留。
- **仅 DComp 路径可用**：ULW 回退时 `Gfx_CreateOverlay` 返回 `NULL`，应用应退回
  主窗局部绘制（demo 用 `barP` 回退）。
- 创建/改尺寸必须在 `Gfx_End` 之后；销毁必须在 `Gfx_Shutdown` 之前。
- **z 次序**：注意 `AddVisual(insertAbove, reference=NULL)` 的反直觉语义：
  `TRUE` 表示放到所有兄弟之下，`FALSE` 表示放到所有兄弟之上。因此 overlay 用
  `AddVisual(TRUE)` 置底，`GfxLayer` 用 `AddVisual(FALSE)` 置顶。合成器 Toast
  （9.8）在 overlay 上面，进度条可以继续在它下面跑，**不需要** `SetOpacity(0)`。
- 只有**主窗内绘制**的模态遮罩才需要 overlay 让位：`Gfx_OverlaySetOpacity(ov, 0)`。
  透明度属性在新系统走 `IDCompositionVisual3::SetOpacity`，老系统自动退回
  `IDCompositionEffectGroup::SetOpacity`（Win8+）。
- 一个 overlay 对应一条动态条带。多个动态元素可以各建一个，或合并成一条动态层。
- demo 实测：前台 1440×1120@60Hz，播放时应用进程 CPU 约 **0.00%**，暂停/恢复/
  6 秒回卷/输入均正常；DComp 失败时自动退回主窗绘制。

### 9.8 通用合成器图层 GfxLayer

`GfxOverlay` 只解决“一条静态渐变 + 一个 clip 动画”。需要把**任意一块 UI**
（渐变遮罩、卡片、复杂自绘）整体放到主窗之上、并且只画一次时，用 `GfxLayer`：

```c
/* Gfx_End 之后 (DComp 已初始化) */
GfxLayer *layer = Gfx_CreateLayer(gfx, pixelW, pixelH);   /* 物理像素 */
if (layer) {
    /* 画内容: 回调里用普通 Gfx_* (DIP 坐标), 不要 Gfx_Begin/End */
    Gfx_LayerDraw(layer, draw_toast, &toast);
    /* 交给 DWM 在 200ms 内把整体透明度补到 1 */
    Gfx_LayerSetOpacity(layer, 1.0f, 200);
}

/* 窗口/DPI 变化 */
Gfx_LayerResize(layer, newPixelW, newPixelH);
Gfx_LayerDraw(layer, draw_toast, &toast);

/* 淡出; 补间结束后销毁 */
Gfx_LayerSetOpacity(layer, 0.0f, 200);
Gfx_LayerDestroy(layer);

static void draw_toast(Gfx *g, void *user)
{
    /* 用普通 Gfx_* 绘制; 内容只画一次, 不要在里面读实时动画值 */
}
```

- **独立 swapchain + DComp 子 visual**，与主窗口的 swapchain 完全解耦；主窗不
  Present 也能更新。
- **稳定置顶**：创建时 `AddVisual(FALSE, NULL)`（reference=NULL 时 FALSE 才是
  置顶；TRUE 是置底），永远在已有 overlay/layer 之上；
  多个 layer 按创建顺序叠放。
- **绘制约定**：`Gfx_LayerDraw` 在 `Gfx_End` 之后调用；内部临时把 `Gfx` 目标切到
  layer 的 back buffer、清透明、按 DPI 设置缩放，回调返回后 `EndDraw + Present`
  一次并恢复原目标/脏区状态。回调里可以正常用 `Gfx_Text` / 渐变缓存 / 裁剪。
- **透明度**：`Gfx_LayerSetOpacity(l, target, ms)`；`ms=0` 立即生效，`>0` 由
  `IDCompositionAnimation` 补间。优先 `IDCompositionVisual3`，本机没有时自动用
  `IDCompositionEffectGroup`；两者都没有才创建失败。
- **失败/回退**：ULW 或 DComp 设备不可用时 `Gfx_CreateLayer` 返回 `NULL`；应用应
  退回主窗绘制（Toast 的 `fallback` 字段就是这么做的）。
- **生命周期**：layer 不参与命中测试；输入隔离由主窗 `Ui_SetModal` 完成。
  销毁必须在 `Gfx_Shutdown` 之前；`Gfx_Shutdown` 也会兜底释放。
- **不要每帧重画**：layer 的价值是“画一次 + DWM 补间”。如果内容每帧都变，
  应改用主窗脏区绘制或 `GfxOverlay`。
- 实测：Toast 全窗 layer（1440×1120）显示 2.2 秒 + 淡入淡出，应用进程 CPU
  **0.00%**；期间主窗零次 `WM_PAINT`。

### 9.9 GPU 效果与自定义着色器

`GfxEffect` 把一块内容先画到离屏位图，再经过 D2D 效果链，最后合成回当前目标：

```c
GfxEffect *shadow = Gfx_CreateShadow(gfx, 10.0f, 0x66000000u);
GfxEffect *grain  = Gfx_CreateGrain(gfx, 0.055f);

/* 单效果 */
Gfx_DrawWithEffects(gfx, &shadow, 1, draw_shape, user, cardRc);

/* 线性效果链: 前一个效果的输出作为下一个的输入 */
GfxEffect *chain[2] = { blur, colorMatrix };
Gfx_DrawWithEffects(gfx, chain, 2, draw_content, user, contentRc);

Gfx_EffectDestroy(shadow);
Gfx_EffectDestroy(grain);
```

可用工厂：

| API | 底层 | 外扩 pad |
|---|---|---|
| `Gfx_CreateBlur(g, stdDev, hardBorder)` | `CLSID_D2D1GaussianBlur` | `3*stdDev` |
| `Gfx_CreateShadow(g, stdDev, color)` | `CLSID_D2D1Shadow` | `3*stdDev+4` |
| `Gfx_CreateColorMatrix(g, matrix20)` | `CLSID_D2D1ColorMatrix` | 0 |
| `Gfx_CreateGrain(g, intensity)` | 自定义 `CuiGrain` HLSL | 0 |

- **调用位置**：只能在 `Gfx_Begin/End` 或 `Gfx_LayerDraw` 回调内，且调用点不能有
  未闭合的 `Gfx_PushClip` / `Gfx_PushAlpha`。内部会结束当前帧、画离屏内容、
  恢复目标并重新 `BeginDraw`，调用方可以继续画（例如 Toast 在效果之后画文字）。
- **效果链是线性的**：每个效果输出替换内容。Shadow 的输出只有阴影，所以
  “卡片 + 阴影”要像 Toast 一样分两次：先画轮廓过 shadow，再画卡片本体。
- **仅 DComp 路径**：ULW 没有 D3D 设备，所有 `Gfx_Create*Effect` 返回 `NULL`；
  应用应跳过效果直接绘制。
- 效果对象可复用；必须在 `Gfx_Shutdown` 之前 `Gfx_EffectDestroy`。设备丢失后
  `Gfx_EffectAlive` 变 0，销毁后重建即可。
- **创建时机**：DComp 在首次 `Gfx_Resize` 才初始化，所以 `Gfx_Create*Effect`
  必须在首帧之后调用（demo 的 Toast 是在 layer 绘制时懒创建）。

**整帧后处理（颜色矩阵/无障碍/整窗滤镜）**：

```c
/* D2D 5x4 是 column-major: 每 4 个数 = 输入 R/G/B/A 对输出 RGBA 的贡献 */
static const float mono[20] = {
    0.299f, 0.299f, 0.299f, 0.0f,   /* from R */
    0.587f, 0.587f, 0.587f, 0.0f,   /* from G */
    0.114f, 0.114f, 0.114f, 0.0f,   /* from B */
    0.0f,   0.0f,   0.0f,   1.0f,   /* from A */
    0.0f,   0.0f,   0.0f,   0.0f    /* offset */
};

GfxEffect *monoFx = Gfx_CreateColorMatrix(gfx, mono);
GfxEffect *chain[1] = { monoFx };
Gfx_SetFrameEffects(gfx, chain, 1);   /* 0 = 清除 */
...
Gfx_EffectSetColorMatrix(monoFx, newMatrix20);  /* 逐帧动画矩阵 */
Gfx_EffectDestroy(monoFx);                       /* 会自动从 frame 链移除 */
```

- `Gfx_SetFrameEffects` 在 `Gfx_End` 把 UI 已画好的 `uiTarget` 作为输入，跑效果链后
  再合成到 swapchain；不需要把 UI 绘制包进额外回调。
- **只作用于主窗 UI 内容**。进度 `GfxOverlay`、Toast `GfxLayer` 是独立的 DComp
  子 visual，不经过 `uiTarget`，所以整帧颜色矩阵不会改变它们。需要全局一致
  滤镜时，应把动态内容改画到主窗，或在图层内容上也应用同一效果。
- 每帧应用一次整窗 pass；静态滤镜可常驻，颜色过渡/无障碍切换按需开。
- 本机已验证灰度矩阵对主窗 UI 生效（按钮/文字/背景变灰），应用 CPU 仍 0.00%
  （GPU 合成）；进度 overlay 因是独立 visual 保持原色，符合上述限制。

**自定义 HLSL 的硬性要求**（`src/gfx/grain.hlsl` 是完整样例）：

1. 像素着色器签名必须是：
   ```hlsl
   float4 main(float4 clip : SV_POSITION,
               float4 scene : SCENE_POSITION,
               float4 tex : TEXCOORD0) : SV_TARGET
   ```
   缺 `SCENE_POSITION` 会导致效果输出空白。
2. `MapInputRectsToOutputRect` 里 `outputRect = inputRects[0]`，
   `outputOpaqueSubRect = inputOpaqueSubRects[0]`。opaque 区写错会把圆角切成方角
   或直接剔除输出。
3. 在 `SetDrawInfo`/`PrepareForRender` 里 `SetPixelShader(shaderGuid)` 并
   `SetPixelShaderConstantBuffer`（16 字节对齐）；shader 先 `LoadPixelShader`。
4. 用 XML + `D2D1_PROPERTY_BINDING` 注册 `ID2D1EffectImpl`，`Initialize` 里
   `SetSingleTransformNode` 连接 1 个输入。
5. 着色器**离线编译**：`fxc /T ps_4_0 /E main /Fh build\grain_cso.h /Vn g_grainCso`，
   代码内嵌 cso；运行时不需要 `d3dcompiler.dll`，0CRT 判据不受影响。

**本机能力探测（Windows 10 19045）**：

| 接口 | 结果 |
|---|---|
| `IDCompositionDevice2/3`、`IDCompositionVisual3` | `E_NOINTERFACE` |
| `DCompositionCreateDevice2/3` | 失败 |
| D2D `GaussianBlur/ColorMatrix/Shadow/Turbulence` | 全部 `S_OK` |

所以 DComp 内置滤镜（零应用 CPU 的 blur/shadow）在本机不可用；当前效果统一走
D2D。Toast 卡片 = 软阴影 + 颗粒，只画一次，显示/停留期间应用 CPU 仍 **0.00%**。

---

## 10. UiFrame 帧时钟

### 10.1 为什么需要它

`WM_TIMER` 的粒度约 15.6 ms，且会被消息队列合并，做 60/120 Hz 动画会抖。
`UiFrameClock` 负责：

- 用 `EnumDisplaySettings` 检测刷新率（夹到 24..240 Hz）；
- 用 `CreateWaitableTimerEx(..., CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, ...)`
  创建高精度可等待定时器，失败退普通定时器；
- 记录帧间隔、掉帧、平均/最大帧时间、渲染耗时、绘制面积；
- 动画期间 `timeBeginPeriod(1)`，退出时 `timeEndPeriod(1)`。

> 当前版本会探测 `DCompositionWaitForCompositorClock` 并记录在 `useComp` 中，
> 但实际节拍由 `Present(vsync)` 与可等待定时器完成；合成器时钟作为预留字段。
> 这不影响 60/120 Hz 的稳定性。

### 10.2 API

```c
UiFrame_Init(&clock, hwnd);
double now = UiFrame_Now();          /* QPC 毫秒 */
double dt  = UiFrame_Tick(&clock);   /* 记录帧起点，返回夹到 0..100 的 dt */
UiFrame_Delay(&clock, remainMs);     /* 补足节拍；期间消息仍可达 */
UiFrame_ResetTick(&clock);           /* 一批动画开始/结束时重置 */
UiFrame_AddPaint(&clock, px, full);  /* 统计绘制面积 */
UiFrame_LogStats(&clock);            /* 输出统计日志 */
UiFrame_Shutdown(&clock);
```

`UiFrame_Delay` 用 `MsgWaitForMultipleObjectsEx(..., QS_ALLINPUT, MWMO_INPUTAVAILABLE)`
等待定时器，所以等待期间不会卡住消息循环。

---

## 11. UiEdit 编辑框

### 11.1 数据与约束

```c
#define EDIT_MAX 512
#define EDIT_COMP_MAX 64
```

- 文本用 `wchar_t text[512]` 存储，UTF-16；`len` 是字符数（不是字节）。
- 单行；没有换行、自动折行、垂直滚动。
- 最大长度由 `Edit_Init(e, maxLen, ...)` 决定；`maxLen <= 0` 或 `>= EDIT_MAX`
  会被改成 `EDIT_MAX - 1`。
- 组合串单独存 `comp[64]`，不进入正文，直到输入法上屏。

### 11.2 初始化与读写

```c
UiEdit ed;
Edit_Init(&ed, 63, 0, NULL);            /* 最多 63 字，普通文本 */
Edit_Init(&edPass, 63, 1, NULL);        /* 密码模式 */
Edit_SetText(&ed, L"默认值");
Edit_SetTextUtf8(&ed, "默认值");
Edit_TextUtf8(&ed, buf, sizeof(buf));
Edit_TextW(&ed, wbuf, 64);
int empty = Edit_Empty(&ed);
```

- `allow` 参数：非 NULL 时只接受这些 ASCII 字符；**非 ASCII 字符（>127）始终允许**，
  所以中文输入不受影响。
- 控制字符（<32、127）始终拒绝。
- `Edit_InsertString` 返回实际插入的字符数，常用于 IME 上屏。

### 11.3 绘制

```c
UiId id = Ui_Id("form.name", 0, 0);
ed.focused = (focus == 0);
ed.caretOn = blinkOn;                  /* 由 530ms 定时器翻转 */
Edit_Draw(&ui, id, rc, &ed, L"请输入名称");
```

`Edit_Draw` 负责：

1. 调 `Ui_DrawSurface` 画输入框表面（悬停/焦点光晕）。
2. 计算内边距：左右各 10 DIP，上下各 1 DIP。
3. 密码模式把正文替换为 `*`。
4. 画选区（`0x553D8BFD`）。
5. 画文本；有组合串时把组合串画在插入点，原文向右让位，下方画下划线。
6. 画光标（1.4 DIP 竖线），并更新 `caretDipX/textTopDip/textHDip` 供 IME 定位。
7. 自动调整 `scrollX`，保证光标与组合串在可视区内。

### 11.4 鼠标定位与拖选

```c
/* 按下 */
Edit_PlaceCaret(&ed, gfx, theme.fInput, rc, mouseX);
/* 拖动 */
Edit_DragTo(&ed, gfx, theme.fInput, rc, mouseX);
/* 抬起 */
Edit_StopDrag(&ed);
```

- `PlaceCaret` 会设置 `dragging = 1`，并选择离鼠标最近的字符边界。
- `DragTo` 只在 `dragging` 为真时生效，更新 `caret`，`anchor` 保持按下时的位置。
- 坐标是 DIP；内部会加上 `scrollX` 再测量。

### 11.5 键盘与剪贴板

`Edit_Key(e, hwnd, vk)` 返回 1 表示已处理：

| 按键 | 行为 |
|---|---|
| `←/→` | 移动光标；`Ctrl+←/→` 按词移动 |
| `Home/End` | 行首/行尾 |
| `Shift+方向` | 扩展选区 |
| `Backspace/Delete` | 删除选区或单字符 |
| `Ctrl+A` | 全选 |
| `Ctrl+C` | 复制到 `CF_UNICODETEXT` |
| `Ctrl+X` | 剪切 |
| `Ctrl+V` | 粘贴（过滤 `\r\n\t` 与控制字符） |

`Edit_Char(e, ch)` 处理 `WM_CHAR` 的普通字符输入。

demo 的键处理顺序：

```c
/* 组合期间，回车/ESC/Tab 先交给输入法收尾 */
if (e && Edit_HasComposition(e) &&
    (vk == VK_RETURN || vk == VK_ESCAPE || vk == VK_TAB))
    return;

if (vk == VK_TAB)    { /* 切换字段 */ return; }
if (vk == VK_RETURN) { /* 提交 */ return; }
if (vk == VK_ESCAPE) { /* 取消焦点 */ return; }
if (e) Edit_Key(e, hwnd, vk);
```

### 11.6 密码模式与 IME

```c
Edit_Init(&edPass, 63, 1, NULL);
Edit_SetImeEnabled(&edPass, 0);    /* 标记禁用输入法 */
```

注意：`Edit_SetImeEnabled` **只打标记**，真正摘除/装回输入上下文由应用在聚焦切换时
调用 `UiIme_Apply` 完成（见第 12 节）。同一时刻只有聚焦字段的 IME 状态生效。

密码框仍然支持鼠标定位、选区、剪贴板；只是显示为 `*`，并且输入法被禁用。

### 11.7 光标闪烁

demo 用 `SetTimer(hwnd, 2, 530, NULL)`，`WM_TIMER` 翻转 `blinkOn`，并在有焦点时
`InvalidateRect`。编辑框本身不拥有定时器，`caretOn` 由应用驱动。

输入/点击/提交/重置后应把 `blinkOn` 置 1，让光标立即出现。

---

## 12. UiIme 输入法

### 12.1 为什么需要单独一层

自绘编辑框没有原生 `EDIT` 控件的输入上下文。中文/日文输入法的组合串、候选窗、
“密码框禁用输入法”都要应用自己接线。`UiIme` 把安装器里验证过的逻辑抽成了模块。

### 12.2 完整消息接线

```c
case WM_SETFOCUS:
    UiIme_Apply(&ime);
    return 0;
case WM_KILLFOCUS:
    UiIme_Cancel(&ime);
    return 0;

case WM_IME_SETCONTEXT:
    UiIme_Pos(&ime, g.scale);
    return DefWindowProcW(hwnd, msg, wp,
        (LPARAM)((DWORD)lp & ~(DWORD)ISC_SHOWUICOMPOSITIONWINDOW));

case WM_IME_STARTCOMPOSITION:
    UiIme_Start(&ime);
    UiIme_Pos(&ime, g.scale);
    return 0;
case WM_IME_COMPOSITION:
    UiIme_Comp(&ime, lp);
    UiIme_Pos(&ime, g.scale);
    return 0;
case WM_IME_ENDCOMPOSITION:
    UiIme_End(&ime);
    return 0;
case WM_IME_CHAR:
    UiIme_Char(&ime, (wchar_t)wp);
    return 0;
```

聚焦字段变化时：

```c
UiIme_SetFocus(&ime, newEdit);   /* NULL = 没有字段 */
```

### 12.3 组合串与上屏

- `WM_IME_STARTCOMPOSITION`：标记 `seen = 1`，清空旧组合串。
- `WM_IME_COMPOSITION`：
  - `GCS_RESULTSTR`：取上屏字符串，`Edit_InsertString`，清空组合串。
  - `GCS_COMPSTR`：取组合串，`Edit_SetComposition`，只做就地绘制。
- `WM_IME_ENDCOMPOSITION`：清空组合串。

组合串绘制在插入点，原文向右让位；组合串带下划线，表示“尚未上屏”。
密码模式下组合串也显示为 `*`。

### 12.4 双字防护

部分输入法在上屏后仍会补发 `WM_IME_CHAR`。如果无条件把它当普通字符插入，就会
出现“双字”。`UiIme` 用 `seen` 标志规避：

```c
void UiIme_Char(UiIme *m, wchar_t ch)
{
    if (!m || m->seen) return;      /* 已经走组合路径，忽略补发 */
    if (m->edit) Edit_Char(m->edit, ch);
}
```

`UiIme_Apply` / `UiIme_SetFocus` 会把 `seen` 重置为 0，使新字段重新探测。

### 12.5 密码框禁用输入法

```c
Edit_SetImeEnabled(&edPass, 0);
```

真正生效在 `UiIme_Apply`：

```c
if (!want) {
    HIMC old = ImmAssociateContext(hwnd, NULL);
    if (old) ctx = old;
    off = 1;
} else if (off) {
    ImmAssociateContext(hwnd, ctx);
    ctx = NULL;
    off = 0;
}
```

只改转换模式（`ImmSetConversionStatus`）各家输入法未必听；摘掉输入上下文是密码框的
标准做法。

### 12.6 候选窗/组合窗定位

`UiIme_Pos` 用 `Edit_GetImePoint` 取插入点（DIP → 物理像素），同时设置：

- `COMPOSITIONFORM`（组合窗）；
- `CANDIDATEFORM`（候选窗）。

如果不设置，候选窗默认落在窗口左上角。每次 `WM_IME_COMPOSITION`、
`WM_IME_STARTCOMPOSITION`、`WM_IME_SETCONTEXT` 后都应调用一次。

### 12.7 排查清单

| 现象 | 检查 |
|---|---|
| 中文输不进去 | 是否处理了 `WM_CHAR` 并转 `Edit_Char`；编辑框是否 `focused` |
| 组合串不显示 | 是否调用 `UiIme_Comp`；`Edit_Draw` 是否在聚焦时绘制 |
| 上屏后多一个字 | `WM_IME_CHAR` 是否绕过 `UiIme_Char` 直接 `Edit_Char` |
| 候选窗在左上角 | 是否调用 `UiIme_Pos`；`Edit_GetImePoint` 的 `hasRect` 是否为 1 |
| 密码框还能打中文 | 是否在切到密码框时 `UiIme_SetFocus` + `UiIme_Apply` |
| 组合期间回车被当提交 | 键处理前先判断 `Edit_HasComposition` |

---

## 13. DPI 与多显示器

### 13.1 原则

- 所有布局写 **DIP**，不要写物理像素。
- 鼠标坐标除以 `scale` 再交给 `Ui_OnMouse*`。
- `Gfx_Resize(gfx, pixelW, pixelH, scale)` 同时告诉后端物理尺寸与缩放。
- `Ui_SetViewport(ui, dipW, dipH, scale)` 告诉交互层视口与缩放。
- 每帧调用 `Gfx_Resize`；尺寸/缩放变化由后端处理。

### 13.2 窗口尺寸与位置

demo 的 `place_window`：

```c
int w = (int)(DEMO_W * scale);
int h = (int)(DEMO_H * scale);
SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
SetWindowPos(hwnd, NULL,
    wa.left + ((wa.right - wa.left) - w) / 2,
    wa.top + ((wa.bottom - wa.top) - h) / 2,
    w, h, SWP_NOZORDER | SWP_NOACTIVATE);
```

### 13.3 WM_DPICHANGED

```c
case WM_DPICHANGED: {
    const RECT *sugg = (const RECT *)lp;
    apply_dpi(hwnd);                       /* 更新 scale */
    SetWindowPos(hwnd, NULL, sugg->left, sugg->top,
        sugg->right - sugg->left, sugg->bottom - sugg->top,
        SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
}
```

`Gfx_Resize` 检测到 `scale` 变化但物理尺寸不变时，会强制整帧重画（DIB 里的旧内容
是按旧缩放画的）。

### 13.4 自绘标题栏的命中

demo 的 `WM_NCHITTEST`：

```c
if (pt.y >= 0 && pt.y < (LONG)(92.0f * g.scale) &&
    pt.x >= 0 && pt.x < (LONG)(rc.right - 56.0f * g.scale))
    return HTCAPTION;
return DefWindowProcW(hwnd, msg, wp, lp);
```

- 顶部 92 DIP 的带状区域可拖动；
- 右侧 56 DIP 留给关闭键，不参与拖动；
- 关闭键本身在 `x = w - 46 .. w - 18`，完全落在排除区内。

如果你的关闭键位置不同，记得同步调整排除区，否则点击会变成拖动窗口。

---

## 14. 0CRT 构建

### 14.1 入口

`build.cmd`：

```bat
link /SUBSYSTEM:WINDOWS /NODEFAULTLIB /ENTRY:startup ...
```

`src/demo.c` 末尾：

```c
#ifdef NO_CRT_NO_DEFAULTLIB
void __security_init_cookie(void);
void __stdcall startup(void)
{
    __security_init_cookie();          /* /GS cookie, 见 crtstub.c */
    ExitProcess(WinMain(GetModuleHandleA(NULL), NULL,
        GetCommandLineA(), SW_SHOWDEFAULT));
}
#else
int main(void) { ... }
#endif
```

`crtstub.c` 只在 `NO_CRT_NO_DEFAULTLIB` 下编译出存根。

> 本版 0CRT 参考 `RemoteCtrl/agent` 的 `helpers.c`/`mem_avx2.c`，
> 保留了与 UI 库相关的部分（安全运行时、SIMD 内存、完整 printf、分配器），
> 去掉了 agent 的日志/泄漏追踪/业务逻辑。

### 14.2 crtstub 提供什么

| 类别 | 符号 |
|---|---|
| 安全 | `__security_cookie/init_cookie/check_cookie`、`__GSHandlerCheck`、`__GSHandlerCheck_SEH`、`__C_specific_handler`、`__report_rangecheckfailure` |
| 内存 | `memset/memcpy/memmove/memcmp/malloc/free/calloc/realloc`；SSE2 → AVX2 → ERMSB/FSRM 分档 |
| 字符串 | `strlen/strcmp/strncmp/strcpy/strncpy/strcat/strncat/strchr/strrchr/strstr/atoi/strtol/strtoul` |
| 浮点/locale | `fabs/sqrt/strtod`、`_fltused`、`_dclass`、`localeconv` |
| 格式化 | `vsprintf/vsnprintf/sprintf/snprintf/sscanf` |
| 排序 | `qsort/bsearch` |
| 退出 | `exit/abort` |
| RTC | `_RTC_CheckEsp/_RTC_InitBase/_RTC_Shutdown/_RTC_CheckStackVars` |

- `memcpy/memset/memmove` 按大小分档：<512B SSE2、512B–4KB AVX2、
  ≥4KB 且 CPU 支持 ERMSB/FSRM 时 `rep movsb`；AVX2 用 CPUID+XCR0 检测。
- `strlen/strcmp/strncmp/memcmp` 有 SSE2/AVX2 路径。
- `malloc/free` 使用**独立堆** + 16 字节块头（`realloc` 用头里的旧 size）；
  不是进程默认堆上的薄封装。
- 以上实现集中在 `crtstub.c` + `mem_avx2.c`（后者必须 `/arch:AVX2` 单独编译）。

### 14.3 格式化能力与限制

现在的 printf 比旧版完整很多：

- 支持 flags：`- + 空格 # 0`；宽度/精度（含 `*`）；长度：`l/ll/z/I64`。
- 支持：`%s %d %i %u %x %X %c %p %n %f %e %g %%`。
- `snprintf/vsnprintf` 有界，超长截断并保证 NUL；`sprintf/vsprintf` 由调用方保证缓冲。
- 浮点 `%e/%g` 按**定点**输出（不输出指数形式），日志/UI 足够，且不会错位。
- **不支持** `%ls/%S` 宽字符格式；宽字符请先 `WideCharToMultiByte` 转 UTF-8。
- `sscanf` 仅实现 `%lg`（cJSON 兼容），其余格式返回 -1。
- `log.c` 仍手动补零时间戳（不依赖 `%02d`，也更省）。

### 14.4 依赖检查

构建最后的检查：

```bat
dumpbin /imports bin\cui_demo.exe | findstr /i "msvcrt ucrtbase api-ms-win-crt"
```

返回 1（没找到）才算通过。若失败，通常是：

- 某个文件漏加 `/DNO_CRT_NO_DEFAULTLIB`；
- 链接了带入 CRT 的第三方 `.lib`；
- 新增了使用 CRT 的 C++ 运行库特性（异常/STL 静态初始化等）。

### 14.5 新增源文件/编译标志

如果你新增 `.c/.cpp`：

1. 加到 `build.cmd` 的编译列表；
2. C 文件沿用 `CFLAGS`，C++ 文件沿用 `CXXFLAGS`；
3. `mem_avx2.c` 必须单独 `cl ... /arch:AVX2`；
4. 链接所需系统库（如 `ole32.lib`）；
5. 重新跑 `dumpbin` 检查。

本版 `CFLAGS` 关键项：

```text
/Oi-      关闭编译器自动内建，避免它把 memcpy/memset 实现替换成对自身的调用
/GS-      关闭栈 cookie；crtstub 自带 __security_check_cookie，必须叶子
/Gs32768  栈帧 <32KB 不插 __chkstk（0CRT 不链它；大对象请放堆/static）
```

新增的 `__security_init_cookie()` 已在 demo/minimal/advanced 的 `startup` 调用；
自定义入口也要在进入 WinMain 前调用一次。

### 14.6 不使用 0CRT 时

去掉 `/DNO_CRT_NO_DEFAULTLIB`、`/NODEFAULTLIB`、`/ENTRY:startup`，并把
`crtstub.c` 从编译列表移除即可切回普通 CRT。`startup` 分支会用 `main` 分支代替。

---

## 15. 日志与性能诊断

### 15.1 日志

```c
Log_Init();                          /* 初始化锁 */
Log_Write("[app] ready %d", 1);      /* 任意线程 */
Log_Close();
```

- 始终输出到 `OutputDebugStringA`，用 DebugView / VS 输出窗口查看。
- 开发构建加 `/DSC_LOG_FILE` 后，会写 `exe 同目录 .log`。
- 日志前缀 `[HH:MM:SS.mmm]` 由代码手动补零。

### 15.2 渲染诊断

| API | 用途 |
|---|---|
| `Gfx_GradientCreates(gfx)` | 取走并清零渐变画刷新建次数；动画期间应接近 0 |
| `Gfx_Generation(gfx)` | 表面重建代数；变化时位图缓存需重建 |
| `UiFrame_LogStats(&clock)` | 帧数、掉帧、平均/最大帧时间、渲染耗时、绘制面积 |
| `UiFrame_AddPaint(&clock, px, full)` | 手动累计绘制面积 |

demo 的启动日志会输出：

```text
[ui] dcomp=1 proc=1
[ui] 帧时钟 60 Hz (16666 us/帧) compositor=1 hiRes=1
[gfx] D3D hw=1 fl=b000 hr=0
[gfx] DComp swapchain 720x560
```

如果看到 `[gfx] DComp failed, fallback to ULW`，说明当前环境走了 GDI 分层窗口，
性能会略低，但功能正常。

### 15.3 性能建议

- 动画期间用 `Ui_AnimTakeDirty` 做局部失效，而不是整窗。
- 每个持续动画的控件都要 `Ui_AnimSetRect`；漏一个就会退化成整窗重绘。
- 脏区外用 `Gfx_IsVisible` 跳过 UTF-8 转换与文本排版。
- 避免在绘制里创建字体/位图；初始化时创建，退出时销毁。
- 渐变停靠点尽量 ≤ 4，命中缓存。
- 文本尽量 < 160 字节，命中布局缓存。
- 控件数量控制在 256 以内。
- 不要在 `WM_PAINT` 里做耗时 I/O 或弹模态框。
- 脏区只减少绘制；DComp 每帧仍 Present 整窗。若 CPU 卡在 Present/DWM 合成
  （RDP、虚拟 GPU 常见），把持续变化的控件拆到 `GfxOverlay`（9.7 节），让
  DWM 做补间；不要靠降低帧率掩盖问题。
- 静态主窗 + 一条动态 overlay 的结构，能把连续动画的进程 CPU 从 ~10% 降到 ~0%
  （demo 实测），代价是多一个小 swapchain 和一次性的 DComp 子 visual 设置。

---

## 16. 常用任务配方

### 16.1 新增一个按钮

```c
static int button(UiId id, UiRect rc, const wchar_t *text,
    UiColor bg, UiColor fg, UiColor border, int enabled)
{
    UiSurfaceStyle ss;
    UiWidgetState st;

    ZeroMemory(&ss, sizeof(ss));
    ss.bg = enabled ? bg : 0xFFF2F4F9;
    ss.hoverBg = Ui_ColorShift(bg, 8);
    ss.pressBg = Ui_ColorShift(bg, -18);
    ss.border = border;
    ss.hoverBorder = border ? Ui_ColorShift(border, -8) : 0;
    ss.focusBorder = border;
    ss.radius = 7.0f;
    ss.borderW = 1.0f;
    ss.glowW = 4.0f;
    ss.glowMask = UI_GLOW_HOVER;
    ss.glow = enabled ? Ui_ColorAlpha(theme.blue2, 110) : 0;

    st = Ui_DrawSurface(&ui, id, rc, &ss, enabled);
    draw_wtext(gfx, theme.fBtn, text, rc,
        enabled ? fg : theme.panelDisabled,
        GFX_TEXT_CENTER | GFX_TEXT_VCENTER);
    return st.hovered;
}
```

调用方在 `EndFrame` 前读 `ui.clicked`，在 `Gfx_End` 后处理。

### 16.2 新增一个输入字段

1. `UiEdit ed; Edit_Init(&ed, 63, 0, NULL);`
2. 在 `render` 里分配矩形，`ed.focused = ...; ed.caretOn = blinkOn;`
   `Edit_Draw(&ui, id, rc, &ed, L"提示");`
3. 在 `WM_LBUTTONDOWN` 里 `hit_test` 字段矩形 → `set_focus` → `Edit_PlaceCaret`。
4. 在 `WM_MOUSEMOVE` 里拖动 → `Edit_DragTo`。
5. 在 `WM_LBUTTONUP` 里 `Edit_StopDrag`。
6. 在 `WM_KEYDOWN/WM_CHAR` 里转 `Edit_Key/Edit_Char`。
7. 如果有输入法，按第 12 节接线，并在聚焦变化时 `UiIme_SetFocus`。

### 16.3 新增一个自绘控件（开关/滑块）

```c
UiId id = Ui_Id("settings.switch", 0, 0);
UiWidgetState st = Ui_State(&ui, id, rc, 1);

float t = st.press > 0.5f ? 0.85f : 1.0f;
UiColor track = on ? theme.switchOn : theme.switchOff;
Gfx_FillRoundRect(gfx, Ui_Rect(rc.x, rc.y, rc.w, rc.h), rc.h * 0.5f, track);

float knob = on ? rc.x + rc.w - rc.h * 0.5f : rc.x + rc.h * 0.5f;
Gfx_FillEllipse(gfx, knob, rc.y + rc.h * 0.5f,
    rc.h * 0.5f - 3.0f, rc.h * 0.5f - 3.0f, 0xFFFFFFFF);

if (ui.clicked == id) on = !on;
```

`Ui_State` 已经处理了命中、悬停、按下、焦点和动画；你只需要画。

### 16.4 弹层 / 模态

```c
/* 打开 */
ui.popupOpen = 1;

/* 绘制弹层 */
ui.layer = UI_LAYER_OVERLAY;
/* ... Ui_DrawSurface / Ui_RegisterWidget ... */
ui.layer = UI_LAYER_BASE;

/* 关闭 */
ui.popupOpen = 0;
```

`popupOpen = 1` 时 `Ui_HitTest` 只命中 overlay 层，底层控件不会被误触。
`Ui_BeginFrame/EndFrame` 会把 layer 重置为 BASE，所以每帧都要重新设置。

### 16.5 窗口淡入 / 转场

```c
/* 启动时 */
Ui_AnimSet(&ui, id, UI_AN_ALPHA, 0.0f);
Ui_AnimTo(&ui, id, UI_AN_ALPHA, 1.0f, 220, UI_AN_EASE_OUT);

/* 每帧 */
float a = Ui_AnimGet(&ui, id, UI_AN_ALPHA, 1.0f);
int pushed = Gfx_PushAlpha(gfx, a);
/* 绘制整窗 */
if (pushed) Gfx_PopAlpha(gfx);
```

demo 还演示了状态文字的上滑淡入：`UI_AN_ALPHA` + `UI_AN_OFFSET` 同时补间。

### 16.6 换肤

1. 复制 `ui_theme.c` 的颜色赋值，改成你的色板。
2. 字体大小/字重按需调整。
3. 组件代码不动。

如果要运行时切换主题，把 `UiTheme_Init` 拆成 `Theme_Load(&theme, gfx, palette)`
并重建字体（或复用字体，只换颜色）。

### 16.7 让安装器切换到 CUI

README 第 10 节给出了路线。核心步骤：

1. 用 CUI 的 `src/{ui,gfx,edit.*,log.*,lib}` 替换安装器里的副本。
2. 保留安装器业务代码对 `UiContext/UiTheme/Edit` 的调用；API 签名未变。
3. 新增 `ui_ime.c` 的接线（从 `app.c` 的 `App_Ime*` 迁移）。
4. 把 `app.c` 里的绘制/消息处理按 CUI 的帧顺序调整。
5. 重点检查单帧事件的读取时机：必须在 `Ui_EndFrame` 之前快照。

### 16.8 CI / 无桌面环境

没有桌面会话时无法目视验证，但可以：

- 编译 + 链接通过；
- `dumpbin` 确认 0CRT；
- 静态检查 API 用法；
- 在带桌面的机器上跑 demo 做交互回归。

---

## 17. 已知限制与设计取舍

1. **没有布局引擎**：绝对坐标，窗口缩放/动态内容需要应用自己算。
2. **编辑框单行**：无多行、自动折行、垂直滚动、撤销栈、右键菜单。
3. **内置亮/暗两套主题**：更多主题需扩展 `UiTheme_SetTheme`；切换不重建字体。
4. **控件表 256 项**：超出静默丢弃。大列表用虚拟化/分页。
5. **动画池 64 条目 × 16 通道**：超出忽略新动画；长列表不要给每行都开独立动画。
6. **文本缓存 128 条 / 单条 <160 字节**：超长文本每帧临时排版。
7. **渐变缓存 24 个 / 最多 4 停靠点**：更多停靠点走临时画刷。
8. **编辑框最大 511 字符**：`EDIT_MAX - 1`。
9. **组合串最大 63 字符**：`EDIT_COMP_MAX - 1`。
10. **DComp 需要 Win10 1809+**：旧系统自动回退 ULW。
11. **仅 Windows**：无跨平台计划。
12. **`UiResult` 当前未使用**：为滑块/色板预留，demo 不使用。
13. **`GfxOverlay` / `GfxLayer` 仅 DComp 路径**：ULW 回退返回 `NULL`，应用需退回
    主窗绘制。一个 overlay 对应一条静态渐变条；layer 适合“画一次 + DWM 补间”的
    整块内容。创建/绘制/改尺寸必须在 `Gfx_End` 之后。
14. **z-index 只影响命中**：绘制顺序仍是调用顺序，应用必须按 z 从低到高画。
15. **Toast 默认走合成器 layer**：`UiToast_Attach` 后渐变/卡片画在 `GfxLayer`，
    主窗只登记 `Ui_SetModal` 的不可见 blocker；显示/停留/淡出期间主窗零重绘。
    只有 ULW 或 layer 创建失败时才退回主窗渐变（需要整窗重绘 + 进度让位）。
16. **图层透明度有两条路径**：优先 `IDCompositionVisual3`；没有时退
    `IDCompositionEffectGroup`（Win8+）。两者都不可用则 layer 创建失败，应用回退。
17. **`UiApp` 是可选单窗口壳**：多窗口/特殊消息循环仍用低层 API；壳不限制
    应用直接访问 `app->hwnd/gfx/ui/theme`。
18. **`UiInput` 最多 8 个字段**（`UI_INPUT_MAX`），单行；需要动态表单/多行时
    直接使用 `UiEdit`。
19. **`UiProgress` 一个实例对应一条进度条**；周期在初始化时设置。合成器 Toast
    在它上方，不需要 `UiProgress_SetHidden`；只有主窗内模态遮罩才需要让位。
20. **layer 内容不应每帧重画**：它的收益来自“一次绘制 + DWM 补间”；持续变化的
    内容请用主窗脏区绘制或 `GfxOverlay`。
21. **自定义主题只覆盖 5 个语义角色**：页面/卡片/文字/输入框外壳仍由
    light/dark 基座决定。要连外壳一起改，直接改 `UiTheme` 字段后调用
    `UiTheme_Touch`。
22. **没有 per-component 主题覆盖**：同一 `UiTheme*` 下所有组件共享令牌；
    需要局部差异时，在调用组件前临时改字段并 `UiTheme_Touch`，或复制一份
    `UiTheme` 给子树使用。
23. **`GfxEffect` 仅 DComp 路径**：ULW 回退没有 D3D 设备，效果创建返回 `NULL`；
    应用必须直接绘制。效果内容会额外占用一张离屏位图，尽量只用于“画一次”的
    图层，不要每帧对每个控件使用。
24. **自定义 D2D effect 样板较重**：需要 `ID2D1EffectImpl` +
    `ID2D1DrawTransform`、XML/binding 注册、离线 fxc；目前只内置 Grain 一个
    自定义效果。DComp 内置滤镜在本机不可用，不要把它当作必然能力。
25. **`Gfx_DrawWithEffects` 不能在有未闭合 clip/alpha 层时调用**：内部会
    EndDraw/重新 BeginDraw；调用方应在干净的绘制点使用。
26. **整帧效果只覆盖主窗 `uiTarget`**：`GfxOverlay`（进度）和 `GfxLayer`
    （Toast）是独立 DComp visual，不经过整帧效果；需要全局一致颜色时，应把
    动态内容并入主窗或在图层内容上再次应用同一效果。
27. **布局没有 margin / 百分比 / shrink**：间距用 gap + `Ui_StackSkip`；
    剩余空间用 `TakeFill` / `grow`；溢出由应用处理。`UiStack` 每个 stack
    最多一个 Fill。
28. **布局不绘制背景/边框**：需要卡片时先 `Ui_Panel(ui, outer)`，再把同一
    outer 交给 `Ui_StackBegin`；布局不参与 z-index 或命中测试。

---

## 18. 故障排查

### 18.1 窗口能创建但全透明/什么都不显示

- 检查是否在 `CreateWindowEx` 时加了 `WS_EX_NOREDIRECTIONBITMAP`。
- 检查 `Gfx_Init` 返回值；失败时 demo 会弹 MessageBox。
- 检查 `WM_PAINT` 是否真的调用了 `Gfx_Begin/End`。
- 查看日志是否有 `[gfx] DComp failed, fallback to ULW`。
- 检查是否 `Gfx_Resize` 传了 0 尺寸（最小化时）。

### 18.2 点击没有反应

按优先级检查：

1. **是否在 `Ui_EndFrame` 之后读 `ui.clicked`**。这是本次 demo 关闭键的根因。
   必须在 `EndFrame` 前快照。
2. 控件是否真的注册了：`Ui_DrawSurface` 的 `enabled` 是否为 0？
3. 命中是否被其他控件遮挡：`Ui_HitTest` 从后往前，后画的优先。
4. 坐标是否除过 `scale`：`Ui_OnMouse*` 要 DIP。
5. 是否接了 `WM_LBUTTONDOWN/UP` 和 `SetCapture`。
6. 自绘标题栏的 `WM_NCHITTEST` 是否把按钮区域也判成了 `HTCAPTION`。

### 18.3 hover 卡住 / 点一次闪一下

- 是否接了 `WM_MOUSELEAVE` 并在第一次 `WM_MOUSEMOVE` 时 `TrackMouseEvent`。
- 是否接了 `WM_CAPTURECHANGED` 并调用 `Ui_OnCaptureChanged`。
- 按下期间 hover 被清空是正常的；抬起时 `Ui_OnMouseUp` 会恢复。

### 18.4 控件整块消失

- 画刷缓存满了？当前实现会轮转淘汰，不会再静默返回 NULL。
- 脏区太小？动画脏区外扩 10 DIP；`Ui_OnMouseMove` 失效也是 10 DIP。
- 局部重绘时是否画到了持久层？`Gfx` 内部已处理（`uiTarget`）。
- 是否在 `Gfx_End` 之后继续绘制？必须在 `Begin/End` 之间。

### 18.5 中文输入法异常

- 组合串不显示：检查 `Edit_Draw` 是否在 `compLen > 0` 时绘制。
- 上屏双字：检查 `WM_IME_CHAR` 是否经过 `UiIme_Char` 的 `seen` 防护。
- 候选窗位置不对：检查 `UiIme_Pos` 和 `Edit_GetImePoint`。
- 密码框还能输入中文：检查 `UiIme_SetFocus` + `UiIme_Apply` 是否在聚焦切换时调用。
- 组合期间回车被当提交：键处理前判断 `Edit_HasComposition`。

### 18.6 0CRT 构建失败

- `dumpbin` 报 `msvcrt/ucrtbase/api-ms-win-crt-*`：
  - 是否漏了 `/DNO_CRT_NO_DEFAULTLIB`；
  - 是否用了 `%f/%ls/%02d` 等不支持的格式；
  - 是否链接了带入 CRT 的库；
  - 是否新增了 C++ 文件但用了 STL/异常/RTTI。
- 链接报 `_fltused`：`crtstub.c` 是否编译进来。
- 链接报 RTC 符号：`crtstub.c` 已提供空实现。

### 18.7 高 DPI 下模糊/错位

- 是否设置了 Per-Monitor V2 DPI 感知。
- 是否每帧 `Gfx_Resize` + `Ui_SetViewport` 使用同一个 `scale`。
- 是否在 `WM_DPICHANGED` 里更新 `scale` 并 `SetWindowPos` 到建议矩形。
- 鼠标坐标是否除以 `scale`。

### 18.8 调试时弹窗无限嵌套

- 不要在绘制中途弹 `MessageBox`/文件对话框；用 `pending` 标志延后到
  `Gfx_End` 之后。
- 本次修复的 demo 也是“快照 clicked → EndFrame → Gfx_End → 处理动作”。

---

## 19. 附录

### 19.1 常量速查

| 常量 | 值 | 含义 |
|---|---|---|
| `UI_MAX_WIDGETS` | 256 | 单帧控件表容量 |
| `UI_AN_MAX_ENTRIES` | 64 | 动画条目池 |
| `UI_AN_CH` | 16 | 每控件动画通道数 |
| `UI_AN_USER` | 8 | 自定义通道起点 |
| `EDIT_MAX` | 512 | 编辑框文本缓冲（字符） |
| `EDIT_COMP_MAX` | 64 | IME 组合串缓冲（字符） |
| `GFX_TEXT_CACHE_MAX` | 128 | 文本布局缓存条目 |
| `GFX_TEXT_CACHE_CHARS` | 160 | 可缓存文本最大字节数 |
| `GFX_GRAD_CACHE_MAX` | 24 | 渐变画刷缓存条目 |
| `GFX_GRAD_MAX_STOPS` | 4 | 缓存渐变最大停靠点 |
| `UI_WIN_W` / `UI_WIN_H` | 420 / 480 | 主题默认窗口尺寸（demo 用 720×560） |
| `UI_HEADER_H` | 52 | 主题默认标题栏高度 |

### 19.2 API 索引

**Gfx**

```c
Gfx_Init / Gfx_Shutdown / Gfx_Resize / Gfx_Generation / Gfx_GradientCreates
Gfx_Begin / Gfx_End / Gfx_Clear
Gfx_PushAlpha / Gfx_PopAlpha
Gfx_SetDirtyRect
Gfx_FillRect / Gfx_FillRoundRect / Gfx_FillRoundRectGradV / Gfx_FillRoundRectGradH
Gfx_FillRoundRectMultiGrad / Gfx_FillEllipse / Gfx_FillEllipseGradV
Gfx_StrokeRoundRect / Gfx_StrokeEllipse / Gfx_Line / Gfx_StrokeArc / Gfx_FillTriangle
Gfx_PushClip / Gfx_PopClip
Gfx_IsVisible
Gfx_CreateFont / Gfx_DestroyFont / Gfx_TextWidth / Gfx_TextHeight / Gfx_Text
Gfx_CreateBitmap / Gfx_DestroyBitmap / Gfx_DrawBitmap
Gfx_CreateOverlay / Gfx_OverlaySetReveal / Gfx_OverlaySetRect
Gfx_OverlaySetOpacity / Gfx_OverlayAlive / Gfx_OverlayDestroy
Gfx_CreateLayer / Gfx_LayerDraw / Gfx_LayerSetOpacity
Gfx_LayerResize / Gfx_LayerAlive / Gfx_LayerDestroy
Gfx_CreateBlur / Gfx_CreateShadow / Gfx_CreateColorMatrix / Gfx_CreateGrain
Gfx_DrawWithEffects / Gfx_EffectSetColorMatrix / Gfx_EffectAlive / Gfx_EffectDestroy
Gfx_SetFrameEffects
```

**UiContext**

```c
Ui_ContextInit / Ui_SetViewport
Ui_BeginFrame / Ui_EndFrame
Ui_RegisterWidget / Ui_HitTest / Ui_State / Ui_SetFocusId / Ui_SetModal
Ui_PushLayer / Ui_PopLayer
Ui_OnMouseMove / Ui_OnMouseDown / Ui_OnMouseUp
Ui_OnMouseLeave / Ui_OnCaptureChanged
```

**UiLayout**

```c
Ui_StackBegin / Ui_StackBeginPad / Ui_StackInner / Ui_StackRemaining
Ui_StackTake / Ui_StackTakeFill / Ui_StackTakeRight / Ui_StackTakeAligned
Ui_StackSkip
Ui_FlexRow / Ui_FlexCol
```

**UiPaint / UiTheme**

```c
Ui_DrawSurface
UiTheme_Init / UiTheme_Shutdown
UiTheme_SetTheme / UiTheme_SetColors / UiTheme_Touch
```

**UiWidgets / UiToast / UiInput / UiProgress**

```c
Ui_Button / Ui_Panel / Ui_ProgressBar
UiToast_Init / UiToast_Attach / UiToast_Show / UiToast_Hide
UiToast_IsVisible / UiToast_Tick / UiToast_Draw / UiToast_EndFrame
UiToast_UsingLayer / UiToast_Shutdown
UiToast_BackdropId / UiToast_CardId
UiInput_Init / UiInput_Add / UiInput_BeginFrame / UiInput_SetRect
UiInput_Draw / UiInput_MouseDown / UiInput_MouseMove / UiInput_MouseUp
UiInput_Key / UiInput_Char / UiInput_OnIme / UiInput_Blink
UiProgress_Init / UiProgress_Tick / UiProgress_Draw / UiProgress_EndFrame
UiProgress_SetPaused / UiProgress_SetHidden / UiProgress_OverlayAlive
UiProgress_NeedsMainRedraw / UiProgress_Value / UiProgress_Shutdown
```

**UiAnim**

```c
Ui_AnimSetReduced / Ui_AnimSet / Ui_AnimSmooth / Ui_AnimTo
Ui_AnimGet / Ui_AnimAdvance / Ui_AnimSetRect
Ui_AnimTakeDirty / Ui_AnimBusy
```

**UiFrame**

```c
UiFrame_Init / UiFrame_Shutdown / UiFrame_Now
UiFrame_Tick / UiFrame_Delay / UiFrame_ResetTick
UiFrame_AddPaint / UiFrame_LogStats
```

**UiApp（可选窗口壳）**

```c
UiApp_Run / UiApp_Invalidate / UiApp_InvalidateRect / UiApp_Close
/* callbacks: init/frame/after_frame/animating/tick/mouse/key/chr/ime/timer/nchittest/shutdown */
```

**UiEdit**

```c
Edit_Init / Edit_SetText / Edit_SetTextUtf8 / Edit_TextUtf8 / Edit_TextW / Edit_Empty
Edit_Draw / Edit_PlaceCaret / Edit_DragTo / Edit_StopDrag
Edit_Key / Edit_Char / Edit_InsertString
Edit_SetImeEnabled / Edit_ImeEnabled
Edit_SetComposition / Edit_ClearComposition / Edit_HasComposition
Edit_GetImePoint
```

**UiIme**

```c
UiIme_Init / UiIme_SetFocus / UiIme_Apply / UiIme_Cancel
UiIme_Start / UiIme_Comp / UiIme_End / UiIme_Char / UiIme_Pos
```

### 19.3 窗口消息接线表

| 消息 | 处理 | 备注 |
|---|---|---|
| `WM_CREATE` | 初始化 Gfx/Theme/Context/Ime/Frame | 失败返回 -1 |
| `WM_PAINT` | `BeginPaint/EndPaint` + `render(ps.rcPaint)` | 先结束系统绘制 |
| `WM_ERASEBKGND` | `return 1` | 避免闪烁 |
| `WM_NCHITTEST` | 自绘标题带返回 `HTCAPTION` | 排除关闭键区域 |
| `WM_MOUSEMOVE` | `TrackMouseEvent` + `Ui_OnMouseMove` | 坐标 / scale |
| `WM_MOUSELEAVE` | `Ui_OnMouseLeave` | |
| `WM_LBUTTONDOWN` | `SetCapture` + `SetFocus` + `Ui_OnMouseDown` | |
| `WM_LBUTTONUP` | `Ui_OnMouseUp` + `ReleaseCapture` | |
| `WM_CAPTURECHANGED` | `Ui_OnCaptureChanged` | |
| `WM_KEYDOWN` | `Edit_Key` / 字段切换 / 提交 | 组合期间先让 IME 收尾 |
| `WM_CHAR` | `Edit_Char` | |
| `WM_SETFOCUS` | `UiIme_Apply` | |
| `WM_KILLFOCUS` | `UiIme_Cancel` | |
| `WM_IME_SETCONTEXT` | `UiIme_Pos` + `DefWindowProc` 去掉组合窗 | |
| `WM_IME_STARTCOMPOSITION` | `UiIme_Start` + `UiIme_Pos` | |
| `WM_IME_COMPOSITION` | `UiIme_Comp` + `UiIme_Pos` | |
| `WM_IME_ENDCOMPOSITION` | `UiIme_End` | |
| `WM_IME_CHAR` | `UiIme_Char` | |
| `WM_SIZE` | 重置帧时钟 + 失效 | 最小化时暂停动画 |
| `WM_TIMER` | 光标闪烁 | 530 ms |
| `WM_DPICHANGED` | 更新 scale + `SetWindowPos` + 失效 | |
| `WM_CLOSE` | `DestroyWindow` | |
| `WM_DESTROY` | 释放资源 + `PostQuitMessage` | |

### 19.4 关键源码索引

| 主题 | 位置 |
|---|---|
| 单帧事件清零 | [src/ui/ui_context.c:54](../src/ui/ui_context.c#L54) |
| demo 快照 clicked | [src/demo.c:333](../src/demo.c#L333) |
| 鼠标抬起与 hover 恢复 | [src/ui/ui_context.c:157](../src/ui/ui_context.c#L157) |
| 控件表容量 | [src/ui/ui_context.h:17](../src/ui/ui_context.h#L17) |
| 动画推进 | [src/ui/ui_anim.c:204](../src/ui/ui_anim.c#L204) |
| 帧时钟等待 | [src/ui/ui_frame.c:109](../src/ui/ui_frame.c#L109) |
| DComp 初始化 | [src/gfx/gfx_d2d.cpp:357](../src/gfx/gfx_d2d.cpp#L357) |
| 合成器动画 overlay | [src/gfx/gfx_d2d.cpp:773](../src/gfx/gfx_d2d.cpp#L773) |
| 合成器图层 GfxLayer | [src/gfx/gfx_d2d.cpp:1016](../src/gfx/gfx_d2d.cpp#L1016) |
| GPU 效果 GfxEffect | [src/gfx/gfx_d2d.cpp:1448](../src/gfx/gfx_d2d.cpp#L1448) |
| 整帧效果 | [src/gfx/gfx_d2d.cpp:1547](../src/gfx/gfx_d2d.cpp#L1547) |
| 效果合成 DrawWithEffects | [src/gfx/gfx_d2d.cpp:1590](../src/gfx/gfx_d2d.cpp#L1590) |
| Grain 着色器 | [src/gfx/grain.hlsl](../src/gfx/grain.hlsl) |
| demo overlay 接线 | [src/demo.c:122](../src/demo.c#L122) |
| 布局 UiStack/UiFlex | [src/ui/ui_layout.c](../src/ui/ui_layout.c) |
| 窗口拖边缩放 | [src/ui/ui_app.c:102](../src/ui/ui_app.c#L102) |
| z-index / 命中 | [src/ui/ui_context.c:116](../src/ui/ui_context.c#L116) |
| 模态 blocker | [src/ui/ui_context.c:41](../src/ui/ui_context.c#L41) |
| 主题切换 | [src/ui/ui_theme.c:21](../src/ui/ui_theme.c#L21) |
| 组件库 | [src/ui/ui_widgets.c:4](../src/ui/ui_widgets.c#L4) |
| Toast layer 绘制 | [src/ui/ui_toast.c:96](../src/ui/ui_toast.c#L96) |
| Toast 主窗回退 | [src/ui/ui_toast.c:201](../src/ui/ui_toast.c#L201) |
| 脏区转换 | [src/gfx/gfx_d2d.cpp:622](../src/gfx/gfx_d2d.cpp#L622) |
| 呈现 | [src/gfx/gfx_d2d.cpp:680](../src/gfx/gfx_d2d.cpp#L680) |
| 编辑框绘制 | [src/edit.c:249](../src/edit.c#L249) |
| 编辑框按键 | [src/edit.c:381](../src/edit.c#L381) |
| IME 摘除/装回 | [src/ui/ui_ime.c:24](../src/ui/ui_ime.c#L24) |
| IME 组合/上屏 | [src/ui/ui_ime.c:71](../src/ui/ui_ime.c#L71) |

### 19.5 术语表

| 术语 | 含义 |
|---|---|
| DIP | Device Independent Pixel，96 DPI 下的逻辑像素 |
| DComp | DirectComposition，DWM 合成 |
| ULW | `UpdateLayeredWindow`，GDI 分层窗口回退路径 |
| 即时模式 | 每帧按顺序绘制并注册，不保留控件对象树 |
| 单帧事件 | 只在一次输入消息与下一次绘制之间有效的事件 |
| 稳定 ID | 跨帧不变的 `UiId`，用于命中/动画/焦点 |
| 组合串 | 输入法尚未上屏的预编辑字符串 |
| 上屏 | 输入法把组合串提交为最终文字 |
| 0CRT | 不链接 C 运行时，自带最小存根 |
| 脏区 | 本次需要重绘的矩形区域 |
| z-index | 命中优先级；本库不改变绘制顺序 |
| Toast | 带渐变遮罩的临时通知卡片 |
| overlay | 本库中的 DComp 子 visual，永远在主窗之上 |

### 19.6 验证记录

- 在 Windows（MSVC 2026，x64）上 `build.cmd` 通过，导入表无 CRT 依赖。
- demo 可执行：标题栏拖动、按钮悬停/按下动画、编辑框选字/拖选/剪贴板、
  中文输入法组合串与候选窗、密码框禁用输入法、进度条自动动画、DPI 缩放。
- 关闭键回归：在高 DPI（200%）环境下模拟 `WM_LBUTTONDOWN/UP`，
  `WM_NCHITTEST` 返回 `HTCLIENT`，窗口正常退出（exit code 0）。
- 脏区回归：状态/按钮等只失效变化区域；输入/焦点/密码/提交/重置/暂停/悬停均正确，无残影。
- 合成器 overlay 回归：前台 1440×1120@60Hz，播放时应用进程 CPU 约 **0.00%**；
  暂停后画面冻结、恢复后继续补间、6 秒回卷正常；输入/提交时主窗局部重绘；
  `SCV2_ULW=1` 回退时自动改为主窗局部绘制，功能正常。
- 组件/主题回归：Button/Panel/ProgressBar 走主题令牌；亮/暗切换即时生效，
  进度 overlay 按新令牌重建；浅色/深色截图均正常。
- 壳/输入/进度回归：`cui_minimal.exe` 输入、回车提交、退出正常；`cui_demo.exe`
  由 `UiApp + UiInput + UiProgress` 重写为 **340 行**（原 893 行），前台 60Hz
  无 Toast 时应用进程 CPU 约 **0.00%**；三份 exe 均为 0CRT。
- 0CRT 增强回归：独立自测程序覆盖 `%d/%u/%x/%s/%c/%lld/%zu/%08x/%.2f/%-5d`
  与边界截断、内存 0/7/15/16/31/32/63/64/65/511/512/513/4095/4096/4097/
  8192/10000 字节、字符串、malloc/calloc/realloc/qsort/bsearch，退出码 0；
  三份 exe 在新 crtstub 下均正常启动/输入/关闭。
- **布局回归**：纯矩形自测覆盖 `UiStack` padding/gap/固定/Fill/Right/对齐、
  `UiFlex` basis+grow（50:80）、max 夹取重分、无 shrink 溢出；demo 表单
  迁移为 `UiStack`，按钮行用 `UiFlex` 弹簧把主题按钮推到右侧；200% DPI、
  Toast/进度 overlay 正常，布局零堆分配。
- **窗口缩放回归**：200% DPI 下 `WM_NCHITTEST` 左上/上/右下分别返回
  `HTLEFT/HTTOP/HTBOTTOMRIGHT`；实际鼠标拖右下角 1440×1120 → 1600×1220；
  `WM_NCCALCSIZE` 后客户区=窗口；MoveWindow 600×480 / 800×600 / 1000×700
  三档布局截图正常，`UiFlex` 弹簧与卡片宽度自适应。
- **合成器 z 次序/圆角回归**：修正 DComp `AddVisual(insertAbove, reference=NULL)`
  的语义（FALSE 置顶、TRUE 置底）后，最小 600×480 下 Toast 卡片与进度条重叠时
  卡片正确盖住进度条；Toast 遮罩按 `windowRadius` 圆角裁剪，底部左右圆角保持。
- **自定义主题回归**：demo 的“品牌主题”用 `UiTheme_SetColors` 设置
  `primary=#E4572E / warning=#F2A93B / success=#2FA56B / error=#D64545 /
  neutral=#F3F5F9`；主按钮、标题条、进度条、焦点色同步变橙，中性按钮变浅灰，
  切回深色/浅色预设正常；Toast layer 通过 `themeRev` 自动重画。
  `UI_BTN_WARNING/SUCCESS` 在浅色下分别呈琥珀/绿色描边与文字，悬停底板正确。
- **合成器 Toast（GfxLayer）回归**：`cui_demo.exe` 提交后 Toast 由独立
  layer 显示，渐变/卡片只绘制一次；淡入淡出由 DWM 补间；Toast 的 2.2 秒停留
  与淡出期间应用进程 CPU **0.00%**，主窗 `WM_PAINT` 次数为 0；下方的进度
  overlay 持续按 6 秒周期移动。点击任意位置/卡片、Esc 均可关闭；关闭后输入
  恢复正常；重复显示、Toast 期间 resize 到 620×500 DIP 后 layer/卡片跟随。
- **图层透明度兼容**：本机 Windows 10 19045 查询 `IDCompositionVisual3` 返回
  `E_NOINTERFACE`，已自动退回 `IDCompositionEffectGroup::SetOpacity`；同一构建
  在两条路径下均通过。
- **ULW 回退回归**：`SCV2_ULW=1` 时 `Gfx_CreateLayer` 返回 NULL，Toast 自动
  改主窗渐变 + 进度让位 + 整窗重绘，点击/Esc/自动超时均正常（回退路径 CPU
  高于合成器路径，属预期）。`samples/advanced.c` 未 Attach，保持原主窗行为。
- 无桌面环境（SSH/Session 0）不能目视验证，但编译、链接与 0CRT 判据可在 CI 跑。

---

## 结语

CUI 的核心只有一句话：**每帧重新画、用稳定 ID 读交互、把动作推迟到帧结束**。
把这三件事做对，剩下的就是画矩形、写字和调动画曲线。

如果你准备把它接进真实产品，建议先按第 5 节的骨架跑通一个按钮，再逐项加编辑框、
输入法、动画和 DPI；每加一项，用 `src/demo.c` 对照行为。这样出问题时能快速定位是
“接线顺序”还是“绘制逻辑”。

祝接入顺利。
