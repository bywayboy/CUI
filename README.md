# CUI — 即时模式自绘 UI 库（从 RMControl 安装器抽出）

CUI 是安装器界面层（`installer/src/ui` + `gfx` + 自绘 `UiEdit`）的独立工程：
纯 C/C++、零 CRT、Direct2D + DirectComposition 渲染，自带一套"hover/press/focus
平滑 + 转场补间"的轻动画内核和一个支持中文输入法（IMM32）的单行编辑框。

工程包含七个样例，公共代码在 `src/ui`：

- `src/demo.c`：**标准样例（约 340 行）**。基于 `UiApp` 壳 + `UiInput` 表单 +
  `UiButton/Panel/ProgressBar` + `UITheme` + 合成器 `UiToast`（`GfxLayer`），
  只写业务 UI。
- `samples/minimal.c`：最小接入（约 150 行，含注释），一个输入框 + 提交/退出。
- `samples/advanced.c`：完整高级样例，保留手写 WndProc、合成器进度 overlay、
  原生 Toast 层级/重绘隔离与全部输入法细节。
- `samples/multiwindow.c`：多窗口样例。两个 HWND 各自拥有 Gfx / UiTheme /
  UiContext / UiProgress / UiToast，共享一个帧时钟和消息循环；主题全局同步，
  进度/Toast 独立；支持禁用父窗口的模态对话框，关闭主窗会连带退出。
- `samples/mvvm.c`：ButtonGroup + MVVM-lite。普通/单选/多选融合按钮组，
  selected/mask/focus 存在 ViewModel 字段，命令在 after_frame 执行；顶部
  样式条演示 `Ui_ButtonEx` 的 solid/plain/text/dashed/round。
- `samples/scrollview.c`：ScrollView 样例。10,000 行固定高度虚拟列表 +
  40 个变高卡片（measure 缓存/展开收起）；滚轮平滑、甩动惯性、触摸回弹、
  el-scrollbar 风格自动隐藏滚动条。
- `samples/tabs.c`：TabBar + TabPager。LINE/PILL/CARD header、指示条滑动、
  横向/纵向/仅淡入转场、reverse、转场输入锁和键盘导航。

> 来源：RMControl `installer/src`（提交 `1bc6929` 时的实现）。
> 安装器目前仍保留自己的副本；CUI 是**独立可复用**版本，后续可让安装器切到它。
>
> 完整白皮书（架构、接入步骤、API、IME、0CRT、排错与配方）：[docs/CUI-使用白皮书.md](docs/CUI-使用白皮书.md)。

---

## 1. 目录结构

```
CUI/
├─ build.cmd              MSVC x64 构建脚本（0CRT，产物见下）
├─ README.md              本文档
├─ docs/
│  ├─ CUI-使用白皮书.md   详尽的使用/接入/排错文档
│  ├─ CUI-ScrollView-设计方案.md  ScrollView 架构/缓存/物理/取舍
│  └─ CUI-Tab-设计方案.md  TabBar/TabPager 结构与转场设计（含 CARD 计划）
├─ samples/
│  ├─ minimal.c           最小接入样例
│  ├─ advanced.c          高级样例（overlay/Toast/IME 全细节）
│  ├─ multiwindow.c       多窗口样例（全局主题/独立进度/模态对话框）
│  ├─ mvvm.c              ButtonGroup + MVVM-lite 表单
│  ├─ scrollview.c        ScrollView：10k 行 + 变高卡片 + 惯性/回弹/滚动条
│  └─ tabs.c              TabBar/TabPager：LINE/PILL/CARD + 四向转场 + 输入锁
└─ src/
   ├─ demo.c              标准样例（UiApp 壳 + 组件，约 340 行）
   ├─ edit.c / edit.h     自绘单行编辑框 UiEdit（选区/剪贴板/滚动/IME 组合）
   ├─ log.c / log.h       调试日志（OutputDebugString；可选 /DSC_LOG_FILE 落盘）
   ├─ lib/crtstub.c       0CRT 存根（mem*/str*/sprintf/malloc/浮点等）
   ├─ gfx/
   │  ├─ gfx.h            后端无关 2D 接口（DIP 坐标 / 0xAARRGGBB）
   │  ├─ gfx_d2d.cpp      Direct2D + DWrite + D3D11/DComp 实现（ULW 回退）
   │  └─ grain.hlsl       自定义 D2D 颗粒像素着色器（fxc 离线编译）
   └─ ui/
      ├─ ui_types.h       矩形/颜色/稳定 ID（FNV）
      ├─ ui_context.*     即时模式上下文：注册/命中/z-index/拖动/单帧事件
      ├─ ui_layout.*      UiStack / UiFlex 布局：只算矩形，零分配
      ├─ ui_button_group.* 融合按钮组管理器（NORMAL/SINGLE/MULTI + 键盘）
      ├─ ui_scroll.*       滚动容器 + 高度 measure 缓存/可见区间/滚动条
      ├─ ui_tab.*          TabBar（LINE/PILL/CARD）+ TabPager 转场
      ├─ ui_theme.*       颜色/字体令牌（亮/暗 + 自定义 5 语义色）
      ├─ ui_paint.*       Ui_DrawSurface：填充/描边/光晕的统一外观
      ├─ ui_anim.*        动画内核（指数平滑 + 显式补间，固定池零分配）
      ├─ ui_frame.*       vsync 对齐的帧时钟（合成器时钟 / 高精度定时器）
      ├─ ui_ime.c/h       输入法适配（组合串/候选窗/密码框禁用）
      ├─ ui_widgets.*     Button / Panel / ProgressBar 组件
      ├─ ui_toast.*       渐变 Toast（GfxLayer 画一次 + DWM 补间；ULW 回退）
      ├─ ui_input.*       表单输入（焦点/光标/拖选/键盘/IME）
      ├─ ui_progress.*    进度条（6 秒循环 + GfxOverlay + 回退 + 暂停）
      └─ ui_app.*         可选窗口壳（消息/DPI/IME/动画节拍）
```

---

## 2. 构建与运行

要求：Windows 10 1809+，Visual Studio 2019/2022/2026（C++ 桌面工作负载，
含 Windows SDK `fxc.exe`，用于离线编译自定义效果着色器）。

```bat
cd C:\Users\Administrator\CUI
build.cmd
bin\cui_minimal.exe     REM 最小样例
bin\cui_demo.exe        REM 标准样例 (UiApp + 组件)
bin\cui_advanced.exe    REM 高级样例 (overlay/Toast/IME 细节)
bin\cui_multi.exe       REM 多窗口样例
bin\cui_mvvm.exe        REM ButtonGroup + MVVM-lite
bin\cui_scroll.exe      REM ScrollView（10k 行 / 变高卡片 / 惯性/回弹）
bin\cui_tabs.exe        REM TabBar + TabPager（LINE/PILL/CARD / 四向转场）
```

构建是 **0CRT**：`/NODEFAULTLIB` + `/ENTRY:startup`，由 `crtstub.c` 提供
`startup -> WinMain`、/GS/SEH 运行时、SSE2/AVX2/ERMSB 内存与字符串、完整
printf（flags/宽度/精度/length）和分配器；`mem_avx2.c` 单独 `/arch:AVX2`。
脚本用 `/Oi- /GS- /Gs32768`，最后用 `dumpbin` 检查导入表里没有
`msvcrt/ucrtbase/api-ms-win-crt-*`，有就直接判失败。

---

## 3. 分层

```
应用 (demo.c / installer)
   │  布局 + 业务状态 + 消息分发
   ▼
ui_paint / ui_theme        控件表面与设计令牌
ui_context / ui_anim      交互状态机 + 动画
ui_scroll                 视口 / measure 缓存 / 滚动物理 / el-scrollbar
ui_ime + edit             编辑控件与输入法
   ▼
gfx (gfx.h → gfx_d2d.cpp) 形状/文本/裁剪/位图/脏区/透明度
   ▼
Direct2D / DWrite / D3D11 / DirectComposition（失败回退 UpdateLayeredWindow）
```

**即时模式**：没有控件对象树。每帧按顺序"画 + 注册"，`Ui_State()` 用稳定 `UiId`
读取本帧交互；`Ui_EndFrame()` 清空单帧事件（`clicked/released/down`）。

---

## 4. 最小接入（10 步）

```c
#include "gfx/gfx.h"
#include "ui/ui_context.h"
#include "ui/ui_theme.h"
#include "ui/ui_paint.h"
#include "ui/ui_frame.h"
#include "ui/ui_ime.h"
#include "edit.h"

static Gfx *gfx; static UiTheme theme; static UiContext ui;
static UiFrameClock clock; static UiIme ime; static UiEdit ed;

/* WM_CREATE */
Gfx_Init(&gfx, hwnd);
UiTheme_Init(&theme, gfx);
Ui_ContextInit(&ui, hwnd, gfx, &theme);
UiIme_Init(&ime, hwnd);
Edit_Init(&ed, 63, 0, NULL);
UiFrame_Init(&clock, hwnd);

/* WM_PAINT（先 EndPaint，再自己渲染） */
Gfx_Resize(gfx, wPx, hPx, scale);
Ui_SetViewport(&ui, wPx / scale, hPx / scale, scale);
Ui_SetFocusId(&ui, focused_edit_id);
Gfx_SetDirtyRect(gfx, &ps.rcPaint);        /* 脏区，可按帧增量 */
Gfx_Begin(gfx);
Ui_BeginFrame(&ui);
/* ... Uicontrols ... */
Ui_EndFrame(&ui);
Gfx_End(gfx);                              /* Present，内部等 vsync */

/* 鼠标 */
Ui_OnMouseMove(&ui, x / scale, y / scale);
Ui_OnMouseDown(&ui, x / scale, y / scale);
Ui_OnMouseUp(&ui, x / scale, y / scale);
Ui_OnMouseLeave(&ui);  Ui_OnCaptureChanged(&ui);

/* 动画主循环 */
if (Ui_AnimBusy(&ui)) {
    double t0 = UiFrame_Now();
    Ui_AnimAdvance(&ui, (float)UiFrame_Tick(&clock));
    InvalidateRect(hwnd, NULL, FALSE); UpdateWindow(hwnd);
    UiFrame_Delay(&clock, clock.periodMs - (UiFrame_Now() - t0));
} else {
    MsgWaitForMultipleObjectsEx(0, NULL, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
}
```

按钮/关闭键统一用 `Ui_DrawSurface()`：

```c
UiSurfaceStyle ss = {0};
ss.bg = 0xFFFFFFFF; ss.hoverBg = 0xFFF1F3FA; ss.pressBg = 0xFFE4E8F5;
ss.radius = 7; ss.borderW = 1; ss.border = 0xFFDEE5F2;
ss.glow = Ui_ColorAlpha(theme.blue2, 110);
ss.glowW = 4; ss.glowMask = UI_GLOW_HOVER;
UiWidgetState st = Ui_DrawSurface(&ui, Ui_Id("btn", 0, 0), rc, &ss, 1);
if (ui.clicked == Ui_Id("btn", 0, 0)) { /* 动作推迟到帧结束后执行 */ }
```

---

## 5. API 速查

| 模块 | 关键函数 | 说明 |
|---|---|---|
| `Gfx` | `Gfx_Init/Shutdown/Resize` | D2D 设备与绘制表面；resize 按 DPI 重建 |
| | `Gfx_Begin/End/Clear` | 一帧；`End` 提交并在内部等 vsync |
| | `Gfx_FillRoundRect[GradV/H/MultiGrad]` `Gfx_StrokeRoundRect` | 圆角矩形/渐变/描边 |
| | `Gfx_Corners/CornersAll` `Gfx_FillRoundRectCorners[GradV]` `Gfx_StrokeRoundRectCorners[Dashed]` | 每角独立圆角（0.25 DIP 几何缓存）+ 虚线描边 |
| | `Gfx_DrawGlowRect` | 预烘焙柔光纹理 + 一次 DrawBitmap；按尺寸/圆角/颜色/DPI 缓存 |
| | `Gfx_Text/TextWidth/TextHeight` | UTF-8 文本，DirectWrite 排版 |
| | `Gfx_PushClip/PopClip` `Gfx_SetDirtyRect` `Gfx_IsVisible` `Gfx_PushAlpha/PopAlpha` | 裁剪/脏区/可见性/整帧透明度 |
| | `Gfx_CreateOverlay/OverlaySetReveal/OverlaySetRect/OverlaySetOpacity/OverlayAlive/OverlayDestroy` | 合成器动画 overlay（仅 DComp；ULW 返回 NULL） |
| | `Gfx_CreateLayer/LayerDraw/LayerSetOpacity/LayerResize/LayerAlive/LayerDestroy` | 通用合成器图层：独立 swapchain + 绘制回调 + DWM opacity 补间 |
| | `Gfx_CreateBlur/Shadow/ColorMatrix/Grain` `Gfx_DrawWithEffects` | D2D 内置效果 + 自定义 HLSL（离线 fxc）；离屏内容→效果链→合成 |
| | `Gfx_SetFrameEffects` `Gfx_EffectSetColorMatrix` | 整帧后处理（颜色矩阵/无障碍）；只覆盖主窗 uiTarget |
| `UiLayout` | `Ui_StackBegin/Take/TakeFill/TakeRight/TakeAligned` | 游标布局：padding/gap、固定+Fill |
| | `Ui_FlexRow/Ui_FlexCol` | basis+grow 权重、min/max；零分配 |
| `UiContext` | `Ui_BeginFrame/EndFrame` | 一帧的注册周期；EndFrame 清单帧事件 |
| | `Ui_State` `Ui_DrawSurface` | 交互状态与统一表面 |
| | `Ui_PushLayer/Ui_PopLayer` | z-index 注册层；命中取最高 z |
| | `Ui_PushClipRect/Ui_PopClipRect` | 输入命中裁剪（与 Gfx clip 配套） |
| | `Ui_RegisterWheelTarget/Ui_OnMouseWheel` | 滚轮目标路由；`UiApp` 已接 `WM_MOUSEWHEEL` |
| | `Ui_SetModal` `Ui_SetNavId` | 模态 blocker / 非文本控件键盘导航焦点 |
| | `Ui_OnMouse*` `Ui_SetFocusId` | 输入与键盘焦点 |
| `UiButtonGroup` | `Init/SetSelected/SetMask/SetFocusVisible/Draw/Key` | 融合按钮组：NORMAL/SINGLE/MULTI、selected/mask/focus |
| `UiScrollView` | `Init/Begin/End/Tick/Busy/Cull/ContentRect` | 视口/offset/裁剪/滚动条；内容尺寸由应用 measure |
| | `SetPadding/ContentWidth` `UiScrollPadding` | 四向内边距 + 滚动条预留槽，thumb 不遮挡内容 |
| | `ScrollTo/EnsureVisible/Fling/Wheel` | 平滑定位/滚入视口/惯性/手动滚轮 |
| `UiScrollLayout` | `Init/SetFixed/SetCount/SetWidth/ItemNeedsMeasure` | 高度缓存 + 前缀 y + contentH；固定/变高 |
| | `SetItemHeight/Commit/VisibleRange/ItemRect` | 失效项重测、布局提交、可见区间和项矩形 |
| `UiTabBar` | `Init/SetSelected/SetDisabledMask/SetFocusVisible/Draw/DrawActive/ActiveRect/Key` | LINE/PILL/CARD header、指示条滑动、禁用/键盘 |
| `UiTabPager` | `Init/GoTo/Tick/Busy/Draw` | 横/纵/淡入转场、reverse、页面输入锁 |
| | `UiTabStyle_Line/Pill/Card` | header 样式 token（提示：CARD 需 DrawActive 两阶段） |
| `UiTheme` | `UiTheme_SetTheme/SetColors/Touch` | 进程级全局换肤：所有窗口同步并自动重绘；5 语义色自定义 |
| `UiWidgets` | `Ui_Button/Ui_ButtonEx/Ui_Panel/Ui_ProgressBar` | 主题化组件；`Ui_ButtonEx` 支持 type×appearance×shape×dashed（Element-Plus 风格）与 disabled |
| `UiToast` | `UiToast_Init/Attach/Show/Hide/Tick/Draw/EndFrame` | 渐变 Toast；layer 模式主窗零重绘，ULW 回退主窗 |
| | `UiToast_UsingLayer/Shutdown` | 判断当前是否合成器路径 / 释放 layer |
| `UiInput` | `UiInput_Init/Add/Draw/BeginFrame/Key/Char/Mouse*` | 表单焦点/光标/拖选/IME 封装 |
| `UiProgress` | `UiProgress_Init/Tick/Draw/EndFrame/SetPaused` | 循环进度 + GfxOverlay + 回退 |
| `UiApp` | `UiApp_Run/Invalidate/InvalidateRect/Close` | 可选窗口壳：消息/DPI/IME/帧循环；默认 6 DIP 拖边缩放 + `minW/minH` |
| `UiAnim` | `Ui_AnimSmooth/To/Set/Get/Advance/Busy` | 通道式动画（`UI_AN_*`） |
| `UiFrame` | `UiFrame_Init/Tick/Delay/ResetTick` | 合成器时钟优先，失败退高精度定时器 |
| `UiEdit` | `Edit_Init/SetText/TextUtf8/Draw/Key/Char` | 单行编辑框（UTF-16 存储） |
| | `Edit_PlaceCaret/DragTo/StopDrag` | 鼠标定位与拖选 |
| `UiIme` | `UiIme_Init/SetFocus/Apply/Cancel/Start/Comp/End/Char/Pos` | 输入法接线 |

`UiId` 是"作用域字符串 + 两个整数"的稳定哈希；**同一帧内必须唯一**，且跨帧保持
不变（不要用地址/序号做 ID，否则动画与命中都会错位）。

---

## 6. 消息接线（窗口过程）

```c
WM_PAINT          -> BeginPaint/EndPaint; render(&ps.rcPaint)
WM_MOUSEMOVE      -> Ui_OnMouseMove(x/scale, y/scale)   /* 记得 TrackMouseEvent(TME_LEAVE) */
WM_MOUSELEAVE     -> Ui_OnMouseLeave
WM_LBUTTONDOWN/UP -> Ui_OnMouseDown/Up（按下时 SetCapture+SetFocus）
WM_CAPTURECHANGED -> Ui_OnCaptureChanged
WM_KEYDOWN        -> Edit_Key(&ed, hwnd, vk)      /* 组合期间回车/ESC/Tab 先给输入法 */
WM_CHAR           -> Edit_Char(&ed, ch)
WM_SETFOCUS       -> UiIme_Apply(&ime)
WM_KILLFOCUS      -> UiIme_Cancel(&ime)
WM_IME_SETCONTEXT -> UiIme_Pos(); DefWindowProc(lp 去掉 ISC_SHOWUICOMPOSITIONWINDOW)
WM_IME_STARTCOMP. -> UiIme_Start(); UiIme_Pos()
WM_IME_COMPOSITION-> UiIme_Comp(lp); UiIme_Pos()
WM_IME_ENDCOMPOS. -> UiIme_End()
WM_IME_CHAR       -> UiIme_Char(wp)
WM_DPICHANGED     -> 更新 scale；SetWindowPos(建议矩形)；InvalidateRect
WM_TIMER(530ms)   -> 编辑框光标闪烁 caretOn = !caretOn
```

**动作要推迟到帧结束后**：`Ui_EndFrame()` 会清零 `clicked/down/released`。
必须先快照再结束帧（`UiId clicked = ui.clicked; Ui_EndFrame(&ui);`），否则之后
永远读到 0（demo 的关闭键/按钮曾因此全部无响应）。如果在绘制中途弹模态框
（文件选择、MessageBox），模态循环会重入 `WM_PAINT`，此时 `clicked` 仍是同一条
→ 再次弹窗 → 无限嵌套。demo 用 `pending` 标志在 `Gfx_End()` 之后处理。

---

## 7. 编辑框与输入法（重点）

- 文本用 **UTF-16** 存储；绘制时转 UTF-8 交给 DirectWrite。
- 支持选区/拖选、Home/End、Ctrl+A/C/X/V、字符过滤、最大长度、密码掩码、水平滚动。
- **IME 组合串只就地绘制**（光标后 + 下划线），不插入正文；上屏（`GCS_RESULTSTR`）
  才 `Edit_InsertString()`，并清组合串。
- **密码框禁用输入法**：`Edit_SetImeEnabled(&ed, 0)` 之后，应用需在聚焦切换时
  `ImmAssociateContext(hwnd, NULL)`（`UiIme_Apply` 已封装）。组合期间的回车/ESC/Tab
  必须先交给输入法收尾，不能直接当"提交/跳字段"。
- **`WM_IME_CHAR` 不能无条件再插一次**：上屏后部分输入法会补发它，导致双字。
  `UiIme` 用 `seen` 标志规避。
- 候选窗/组合窗跟随插入点：`UiIme_Pos()`（`CANDIDATEFORM` + `COMPOSITIONFORM`）。

---

## 8. 0CRT 与 DPI

- 所有单元统一 `/DNO_CRT_NO_DEFAULTLIB`；入口是 `startup()`（`crtstub.c` +
  `/ENTRY:startup`）。
- `crtstub.c` 的 `vsprintf` 只实现常用格式（宽度/`%x`/`%g`）；**不要**依赖
  `%ls/%S/%f` 等宽字符或浮点格式。
- DPI：demo 用 `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)`（动态取符号，
  失败退 `SetProcessDPIAware`），每帧 `Gfx_Resize(wPx,hPx,scale)`；布局全部写成 DIP。
- 窗口用 `WS_POPUP | WS_EX_NOREDIRECTIONBITMAP`（DComp 直出）；Gfx 失败会自动回退
  `UpdateLayeredWindow`，无需应用改代码。

---

## 9. 已知限制

- 没有布局引擎（行列/滚动容器/自动换行），demo 用绝对坐标布局。
- 编辑框是单行；无上下文菜单、无撤销栈。
- 主题只有一套浅色令牌（`ui_theme.h`），换肤需自行扩展 `UiTheme`。
- 动画通道固定 64 项、16 通道；超出按最近最少使用回收（见 `ui_anim.c`）。
- `UiFrame` 的合成器时钟需要 Win10 1809+，旧系统自动退回定时器（帧率仍可用）。

---

## 10. 与安装器的关系

- 安装器 `installer/src/{ui,gfx,edit.c,log.c,lib}` 是同一套代码的副本，另有大量
  安装业务逻辑（enroll/解包/WJZ1 载荷）。
- 本工程删除了安装器专属依赖（安装流程、资源、`app.c`），新增 `ui_ime.c` 把
  输入法接线从 `app.c` 抽成可复用模块，另加 `demo.c`。
- 后续若让安装器改用 CUI：把 `installer/src/{ui,gfx,edit.*,log.*,lib}` 替换为对
  CUI 的引用即可；业务代码依赖的 `UiContext/UiTheme/Edit` API 未改签名。

---

## 11. 验证记录

- 在 Windows（MSVC 2026，x64）上 `build.cmd` 通过，导入表无 CRT 依赖。
- demo 可执行：标题栏拖动、按钮悬停/按下动画、编辑框选字/拖选/剪贴板、
  中文输入法组合串与候选窗、密码框禁用输入法、进度条自动动画、DPI 缩放。
- 关闭键/按钮回归：高 DPI（200%）下模拟 `WM_LBUTTONDOWN/UP`，`WM_NCHITTEST`
  返回 `HTCLIENT`，关闭键与“退出”按钮均能正常结束进程（exit code 0）。
- 动画脏区回归：状态/按钮等只失效变化矩形；输入、密码掩码、焦点、提交/重置、
  按钮文案、悬停动画均正确，无残影。`Gfx_IsVisible` 跳过脏区外的文本转换/排版。
- 合成器 overlay 回归：进度条填充改为独立小 swapchain + DComp clip 补间；
  前台 1440×1120@60Hz 播放时应用进程 CPU 约 **0.00%**，暂停/恢复/6 秒回卷正常；
  输入/提交时主窗局部重绘；`SCV2_ULW=1` 回退自动改为主窗局部绘制。
- 主题/组件回归：Button/Panel/ProgressBar 只读主题令牌，亮/暗切换即时生效；
  `UiTheme_SetColors` 的品牌色（primary/warning/success/error/default）同步刷新
  主按钮、标题条、进度条、Toast；`themeRev` 让 overlay/layer 自动重建。
  Toast 渐变遮罩 + 底部卡片、点击遮罩/Esc 关闭均通过。
- 采样/壳回归：`cui_minimal.exe` 输入/回车提交/退出正常；`cui_demo.exe` 已改为
  `UiApp` 壳 + `UiInput` + `UiProgress`，**340 行**（原 893 行），前台 60Hz
  无 Toast 时应用进程 CPU 约 **0.00%**；七份 exe 均为 0CRT。
- 合成器 Toast（`GfxLayer`）回归：渐变/卡片只画一次，淡入淡出由 DWM 补间；
  Toast 显示/停留/淡出期间应用进程 CPU **0.00%**、主窗 0 次 `WM_PAINT`；
  下方进度 overlay 持续移动；点击/Esc 关闭、重复显示、Toast 中 resize 均正常。
  本机 Win10 19045 无 `IDCompositionVisual3`，透明度自动走
  `IDCompositionEffectGroup`。`SCV2_ULW=1` 自动回退主窗渐变 + 进度让位。
- 布局回归：`UiStack` 的 padding/gap/固定/Fill/Right/对齐与 `UiFlex`
  的 basis+grow/min-max 通过纯矩形数学测试；demo 表单改用 `UiStack`、按钮行
  改用 `UiFlex`（主题按钮右对齐），100%/200% DPI、Toast/进度 overlay
  均正常，布局零堆分配。
- ButtonGroup/MVVM 回归：`cui_mvvm.exe` 普通/单选/多选三组按钮融合显示；单选组
  点击切全局主题，多选组 mask 状态更新，普通组命令提交/重置/退出；Tab/方向键/
  Space 导航正常；selected/mask/focus 全部在 `FormVM`，无隐藏状态；样式条
  新增 disabled 按钮，灰底灰字、无 hover/click。
- 光晕/disabled 回归：Solid/Plain hover 光晕为预烘焙柔光纹理，一次
  `DrawBitmap`，无 3 圈描边带；尺寸/颜色/DPI 相同则缓存命中，主题切换后
  按新角色色重建；`Ui_ButtonEx(..., enabled=0)` 不注册命中、不发光、
  返回 false；7 个 exe 0CRT。
- ScrollView 回归：`cui_scroll.exe` 10,000 行列表只绘制 16~17 行；滚轮/甩动/
  滚动条拖拽（offset≈172482）与“返回顶部”正常；40 个变高卡片展开/收起只重测
  该项，contentH/可见区间/thumb 同步；触摸回弹出现 offset=-17 后弹回 0；
  四向 padding `{4,4,6,6}` + `reserveSpace=1` 时行尾文字不被 thumb 遮挡；
  底部展开/收起最后卡片导致 contentH 变小时 offset 立即夹到新底部，
  滚轮和 thumb 回滚正常、Busy=0；滚动条离开视口后自动淡出；静止 2 秒 CPU 0ms。
- Tab 回归：`cui_tabs.exe` LINE/PILL/CARD 切换；CARD 活动 tab 与内容面板
  连通、无 2px 双线；指示条滑动、横向/纵向/仅淡入转场、reverse、转场期间
  页面输入锁、Tab+方向键+Space 键盘导航均正常；顶部工具栏按钮 hover 为
  明显浅底 + primary 混合描边，TabBar LINE/PILL/CARD 悬停为 primary.soft
  底 + primary alpha 描边；转场结束 progress=100%，静止 CPU 0；`cui_tabs`
  为 0CRT。
- 按钮样式/每角圆角回归：样式条同时显示 SOLID(primary)、PLAIN(success)、
  TEXT(warning)、PLAIN+DASHED(error)、ROUND(default)；ButtonGroup 首/中/尾段
  为左圆-全直-右圆，选中色铺满到外轮廓、外边框不被覆盖；每角几何按 0.25 DIP
  量化缓存复用，hover 动画不产生新几何。
- ButtonGroup 选中边框/焦点环回归：鼠标选中不再显示额外蓝色边框和相邻边界
  双线；与选中段相邻的分隔线省略；外框内缩 0.5 DIP 保持统一 1px；键盘焦点环
  内缩 2 DIP 且仅键盘可见；多选连续选中无抗锯齿接缝。
- 多窗口回归：`cui_multi.exe` 两个 HWND 各自 Gfx/Theme/UiContext/Progress/Toast；
  切主题时两个窗口同步换肤并自动重绘；二级窗独立弹 Toast 不阻塞主窗；模态对话框
  打开后 `EnableWindow` 禁用两个父窗，关闭后恢复焦点；两窗口同时动画 CPU
  **0.00%**；关闭主窗后二级窗/对话框一并销毁。
- 窗口缩放回归：`UiApp` 默认 `WS_THICKFRAME` + `WM_NCCALCSIZE`；200% DPI 下
  左右/上/右下命中 `HTLEFT/HTTOP/HTBOTTOMRIGHT`，实际拖拽 1440×1120 →
  1600×1220；600×480/800×600/1000×700 三档布局截图正常，客户区=窗口。
- 合成器 z 次序回归：修正 DComp `AddVisual(insertAbove, reference=NULL)` 的反直觉
  语义（FALSE=置顶，TRUE=置底）；最小 600×480 下 Toast 卡片与进度条重叠时卡片
  在上。Toast 遮罩使用 `theme.windowRadius`，底部左右圆角不再被方角遮罩覆盖。
- GPU 效果回归：D2D 内置 Blur/Shadow/ColorMatrix 与本机自定义 `Grain` HLSL
  全部走通；Toast 卡片=软阴影+颗粒，只画一次，CPU 仍 **0.00%**；12 次连续
  显示/关闭无崩溃；`Gfx_DrawWithEffects` 的离屏/恢复/效果链正常。
  整帧 `Gfx_SetFrameEffects` 灰度矩阵对主窗 UI 生效（CPU 0.00%）；进度
  overlay/Toast layer 是独立 DComp visual，不受整帧矩阵影响（已记录限制）。
  DComp `IDCompositionDevice2/3`、`Visual3` 本机 `E_NOINTERFACE`，未依赖。
- 0CRT 自测：printf 格式/边界、SIMD 内存分档（0…10000B）、字符串、分配器、
  qsort/bsearch 全部通过；七份 exe 在新 crtstub 下正常启动/输入/关闭。
- 无桌面环境（SSH/Session 0）不能目视验证，但编译与链接判据（0CRT/无未解析符号）
  可在 CI 里跑。
