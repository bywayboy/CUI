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
> 12. 主题改为**进程级全局同步**：所有 UiTheme 注册到内部链，
>    `UiTheme_SetTheme/SetColors` 广播到全部窗口并按 `notifyHwnd` 自动重绘；
>    新窗口继承当前全局主题。
> 13. 新增 `samples/multiwindow.c`：两个 HWND 各自 Gfx/UiContext/Progress/Toast，
>    共享全局主题、帧时钟与消息循环；新增禁用父窗口的模态对话框模式；关闭主窗
>    连带退出。构建产出增加 `cui_multi.exe`。
> 14. 修正 DComp `AddVisual(insertAbove, reference=NULL)` 的反直觉次序（FALSE
>    置顶、TRUE 置底）：Toast layer 现在正确盖住进度 overlay；新增
>    `theme.windowRadius`，Toast 整窗遮罩按主窗圆角绘制，不再吃掉底部圆角。
> 15. 新增 `UiButtonGroup`：普通/单选/多选融合按钮组，管理器保存
>    selected/mask/focus；`Ui_SetNavId` + 方向键/Home/End/Space 键盘导航。
>    新增 `samples/mvvm.c`（ButtonGroup + MVVM-lite + 显式命令）；构建产出
>    `cui_mvvm.exe`。
> 16. 新增每角独立圆角几何 `GfxCorners` 与虚线描边：`Gfx_FillRoundRectCorners
>    [GradV]` / `Gfx_StrokeRoundRectCorners[Dashed]`，路径几何按 0.25 DIP
>    量化缓存（32 条，LRU 淘汰）。`UiSurfaceStyle` 支持 `corners/useCorners/
>    dashed`。新增 Element-Plus 风格 `UiButtonStyle`（type × appearance ×
>    shape × dashed）与 `Ui_ButtonEx`；`UiButtonGroup` 选中/hover 改为一像素
>    铺满到外轮廓的每角圆角填充。
> 17. 修正 ButtonGroup 选中态边框：组外框内缩 0.5 DIP；选中段不再额外描边；
>    与选中段相邻的分隔线跳过；键盘焦点环改为 `focusVisible` 控制并内缩
>    2 DIP，鼠标点击不显示，消除外框/相邻边界的双线与半像素外溢。
> 18. 新增 `UiScrollView` + `UiScrollLayout`：视口裁剪、输入命中裁剪、
>    可见区间、高度 measure 缓存/前缀和、滚轮平滑、惯性甩动、触摸回弹、
>    el-scrollbar 风格自动隐藏滚动条；`UiApp` 增加 `WM_MOUSEWHEEL` 与
>    `wheel` 回调。新增 `samples/scrollview.c`，构建产出 `cui_scroll.exe`。
> 19. 修正 ScrollView 在“滚动到底部 → 展开/收起最后一项”后无法回滚：
>    内容变矮导致 `maxOffset` 变小时立即夹取 offset 并清速度；惯性不再
>    每帧覆盖滚轮 target；小越界直接吸附，消除无法落到边界的死区。
> 20. 按钮光晕改为预烘焙柔光纹理 + 一次 `DrawBitmap`（按尺寸/圆角/颜色/
>    DPI 的 24 条 LRU 缓存），失败回退 3 圈描边；MVVM 样式条增加 disabled
>    按钮，验证禁用底色/文字/无 hover/无点击。
> 21. 新增 `UiTabBar` + `UiTabPager`：LINE/PILL/CARD header、指示条滑动、
>    禁用项/键盘导航；横向/纵向/仅淡入转场、reverse、转场输入锁。
>    新增 `samples/tabs.c`，构建产出 `cui_tabs.exe`。
> 22. 完成 CARD 样式：`Gfx_StrokeRoundRectOpen` 缺边描边、
>    `UiTabBar_ActiveRect` + `UiTabBar_DrawActive` 两阶段接缝、
>    `cardBg/contentBg/mergeContent` 样式字段。
> 23. 图标 GLYPH 后端：`Gfx_CreateFontFamily`、`UiTheme.fIcon`（Segoe MDL2
>    Assets）、`UiIcon_Glyph/UiIcon_Draw`；tabs 概览页加入 MDL2 图标示例。
> 24. 图标组件接入：`Ui_ButtonExIcon`、`UiTabBar_DrawEx/DrawActiveEx`，
>    Toast 按 kind 自动绘制 MDL2 图标；新增 `samples/icons.c`，构建产出
>    `cui_icons.exe`。
> 25. 新增 Checkbox/CheckboxGroup、RadioGroup、Switch、Slider；Popup 定位
>    基础与 Tooltip、IconButton；新增 `samples/controls.c`，构建产出
>    `cui_controls.exe`。
> 26. 键盘焦点改为 hover 风格的 `primary.soft` 行底色 + 控件 primary 描边，
>    移除覆盖复选框的蓝色圆角外框；`Ui_SetNavId` 置 `navFocusVisible`，
>    鼠标按下清 0。
> 27. 新增弹层交互管理器 `UiPopupManager`（点击外部/Esc 关闭、单弹层、
>    焦点回归、弹层打开时底层不参与命中、滚轮只走 overlay 层）与
>    `UiSelect`（下拉单选、长列表滚动、禁用项）、`UiMenu`（下拉/右键菜单、
>    禁用/分隔/危险项/快捷键提示）；`UiApp` 增加右键回调；
>    新增 `samples/overlays.c`，构建产出 `cui_overlays.exe`。
> 28. Select 列表滚动区改为复用 `UiScrollView`：滚轮、thumb 拖动、轨道点击翻页、
>    滚动条预留槽全部复用；修复按住拖动时 `WM_MOUSELEAVE` 把鼠标坐标清成
>    -10000 导致滚动条 thumb / 滑块瞬间跳回顶部的问题。
> 29. Select 增加多选（32 位 mask、触发器摘要 `A, B +N`）与可搜索过滤
>    （内嵌 `UiEdit` + IME；ASCII 大小写不敏感 / 中文精确匹配）；多选点击/
>    Enter 切换并保持展开，关闭时提交；样例增加可搜索多选标签。
> 30. Select 增加持久 placeholder（`SetPlaceholder`，`Ui_Select` 参数优先）
>    与可配置搜索提示；右侧箭头改为矢量 chevron，展开/收起做 180ms 旋转补间，
>    新增 `Gfx_PushRotate/PopTransform`；动画期间自行失效触发器，不依赖应用
>    持续 `animating` 回调。
> 31. 修复 CheckboxGroup/RadioGroup 左侧少 1px：组内复选框描边中心落在组
>    矩形边界上，按 rc 绘制裁剪会切掉左半边抗锯齿；绘制裁剪左右各放宽 1 DIP，
>    命中裁剪不变。
> 32. Checkbox / CheckboxGroup / RadioGroup 状态切换加入淡入淡出：
>    选中底色、边框、对勾、横线、单选圆点按 0..1 平滑值过渡；
>    indeterminate→checked 时横线淡出、对勾淡入；每项用独立动画通道。
> 33. 修复 indeterminate 与 checked 同时为真时对勾与横线重叠：横线优先，
>    对勾动画目标改为 `checked && !indeterminate`，初始“部分应用”只显示横线。
> 34. 下拉列表项/菜单项改用 `Ui_StateInstant`：hover 直接取 0/1，不走 90ms
>    平滑淡变，消除指针移动时的滞后/卡顿感；触发器与表单控件仍用 `Ui_State`。
> 35. 新增 `UiTextArea` 多行编辑器：软换行、垂直滚动、选区、剪贴板、
>    readonly/disabled、IME 代理；新增 `samples/textarea.c` 与
>    `cui_textarea.exe`（11 个 exe）。
> 36. `UiEdit` / `UiTextArea` 容量改为动态倍增，`maxLen<=0` 表示不限长；
>    TextArea 行索引动态增长，末尾追加 O(新增字符)、段落内编辑只重排当前段，
>    换行编辑/宽度变化才全文重排；新增 `Edit_Shutdown` / `UiInput_Shutdown` /
>    `UiSelect_Shutdown` / `UiTextArea_Shutdown`。
> 37. 修复 TextArea 选择跳动/行末选不全：`Gfx_TextWidth` 改用
>    `widthIncludingTrailingWhitespace`（行尾空格不再被裁掉）；鼠标列命中补上
>    “跨过目标的字符之后”候选列；拖选不再触发 `ensureCaret` 自动滚动。
> 38. 恢复 TextArea 拖选边缘自动滚动：鼠标靠近/越出文本视口时按住期间每帧
>    滚动 6–40 DIP，选择随滚动继续扩展；只在拖动时触发，不影响行内选择。
> 39. Slider 增加 hover/press 视觉（轨道加粗染色、拇指放大、百分比染色），
>    拖动改为抓取偏移 1:1 跟手（抓拇指不跳位、抓轨道直接定位）；快速单击在
>    按下帧即落值；命中区收窄到轨道，映射内缩使 0/100% 拇指不越界。
> 40. 新增鼠标光标声明：`Ui_SetCursor/Ui_CursorAt/Ui_CursorHandle`，控件在
>    绘制时声明形状，`UiApp` 在 `WM_SETCURSOR` 与帧末应用；`UiEdit` 与可编辑
>    `UiTextArea` 显示 I 形光标，禁用 TextArea/按钮/空白区保持箭头，
>    TextArea 滚动条上仍为箭头。
> 41. 新增 `UiDialog` / `Ui_ConfirmDialog`（8.18）：窗内模态、开合动画、焦点循环、
>    嵌套 Popup 与背景输入隔离。body 仅供布局，不裁剪按钮描边、光晕或下拉列表。
>    `samples/overlays.c` 增加通用 Dialog、危险操作确认框及 TextArea 备注。
> 42. 修复含不可滚动 ScrollView（TextArea / 短 Select）的 Dialog 首次打开卡顿：
>    滚动条初始透明度为 1，内容不滚动时仍逐帧淡出并在 `WM_PAINT` 内
>    `InvalidateRect`，排队出下一次 `WM_PAINT` 占住消息泵，开合 Tick 被饿死
>    约 800ms；现在不可滚动时直接归零，不再请求重绘。真实鼠标首开由 814ms
>    降到 76ms（第二次 95ms），静态 Dialog 仍为 0ms CPU/1.5s。
> 43. 修复任务栏点击不能最小化 / 还原、且无过渡动画：`UiApp` 创建样式补
>    `WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU`。实测缺
>    boxes 时 Explorer 不下发 `SC_MINIMIZE`（只收到 `WM_ACTIVATE`）；缺
>    `WS_CAPTION` 时 DWM 不播放最小化 / 还原过渡。`WM_NCCALCSIZE` 仍返回 0，
>    非客户区不占像素、标题栏继续自绘；动画由 DWM 原生接管，壳零动画代码、
>    过渡期间应用 0 CPU。见
>    [CUI-窗口最小化还原-设计方案.md](CUI-窗口最小化还原-设计方案.md)。
> 44. 新增主题编辑器 `cui_theme_editor.exe`（8.23）：全量 68 色令牌 + 5 圆角
>    可视化编辑，Light/Dark 独立调色与对照色块，HSV 取色板 + HEX/滑块 +
>    搜索过滤，实时预览；基础视图默认只列 15 个常用令牌并配中文短名，可切
>    高级视图看全量，点选右侧预览元素即可定位令牌（高级令牌自动展开）。
>    新增"抽卡"随机配色：3 套候选各带 Light/Dark 缩略图与 seed，可按主色/
>    中性色/语义色分组锁定后重抽，使用后支持单步撤销；生成器为模板受控变换，
>    自动校验对比度。启动恢复 `theme_editor.palette`，导出完整
>    `theme_apply_preset()` 函数体（8 位十六进制、两模式各自圆角）写回
>    `ui_theme.c`；新增 `src/ui/ui_theme_edit.*` 与 `ui_theme_edit_test`
>    （7574 断言）。见
>    [CUI-主题编辑器-设计方案.md](CUI-主题编辑器-设计方案.md)。

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
   - [8.8 布局：UiStack / UiFlex / UiGrid](#88-布局uistack--uiflex--uigrid)
   - [8.9 ButtonGroup 与键盘导航](#89-buttongroup-与键盘导航)
   - [8.10 MVVM-lite](#810-mvvm-lite)
   - [8.11 按钮样式与每角圆角](#811-按钮样式与每角圆角)
   - [8.12 ScrollView 滚动容器](#812-scrollview-滚动容器)
   - [8.13 TabBar 与 TabPager](#813-tabbar-与-tabpager)
   - [8.14 图标 (GLYPH 后端)](#814-图标-glyph-后端)
   - [8.15 表单控件 / Popup / Tooltip](#815-表单控件--popup--tooltip)
   - [8.16 弹层管理器 / Select / Menu](#816-弹层管理器--select--menu)
   - [8.17 TextArea 多行编辑器](#817-textarea-多行编辑器)
   - [8.18 Dialog / ConfirmDialog](#818-dialog--confirmdialog)
   - [8.19 DatePicker 日期选择](#819-datepicker-日期选择)
   - [8.20 Image 图片](#820-image-图片)
   - [8.21 NavBar 多级导航](#821-navbar-多级导航)
   - [8.22 UiPagination 行内分页](#822-uipagination-行内分页)
   - [8.23 主题编辑器与令牌工具](#823-主题编辑器与令牌工具)
9. [UiAnim 动画内核](#9-uianim-动画内核)
10. [网络层与定时器](#10-网络层与定时器uinet--uihttp--uiws--uiwsserver--uitimer)
11. [UiFrame 帧时钟](#11-uiframe-帧时钟)
12. [UiEdit 编辑框](#12-uiedit-编辑框)
13. [UiIme 输入法](#13-uiime-输入法)
14. [DPI 与多显示器](#14-dpi-与多显示器)
15. [0CRT 构建](#15-0crt-构建)
16. [日志与性能诊断](#16-日志与性能诊断)
17. [常用任务配方](#17-常用任务配方)
18. [已知限制与设计取舍](#18-已知限制与设计取舍)
19. [故障排查](#19-故障排查)
20. [附录](#20-附录)

---

## 1. 一页速览

CUI 是一套 **纯 C/C++、零 CRT、即时模式（Immediate Mode）** 的 Windows 自绘 UI 库，
底层使用 Direct2D + DirectWrite + D3D11 + DirectComposition，失败时自动回退到
`UpdateLayeredWindow`。它专门解决三类问题：

1. **无边框、透明、GPU 合成的窗口**：`WS_EX_NOREDIRECTIONBITMAP` + DComp 直出。
2. **需要轻量交互动画**：hover / press / focus / glow 指数平滑，转场补间，帧率无关。
3. **自绘控件也要能输入中文**：IMM32 组合串、候选窗定位、密码框禁用输入法。

它提供**可选**的 rect-only 布局（`UiStack` 游标 / `UiFlex` basis+grow /
`UiGrid` 轨道网格 / 控件 `UiMeasureFn` 内在尺寸协议），但**不提供**保留式布局树、控件对象树、数据绑定、
MVVM 引擎或样式表解析。界面就是每帧按顺序“算矩形 → 画 + 注册”，交互状态从
稳定 ID 读取。

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
| 产物 | `cui_demo` / `cui_minimal` / `cui_advanced` / `cui_multi` / `cui_mvvm` / `cui_scroll` / `cui_tabs` / `cui_icons` / `cui_grid` / `cui_controls` / `cui_overlays` / `cui_textarea` / `cui_datepicker` |

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
- 布局是薄封装：`UiStack/UiFlex/UiGrid` 只算矩形；measure 协议用回调报告
  min/pref 尺寸，`Ui_FlexLayout`/`Ui_GridLayout` 据此求解；没有容器内换行或
  保留式布局树。

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
| 多行文本 | ✅ | `UiTextArea`：软换行/滚动/选择/IME（8.17）；无富文本/撤销栈 |
| 布局系统 | ✅ | `UiStack` 固定+Fill；`UiFlex` basis+grow；`UiGrid` fixed/auto/fr/minmax/span/隐式轨道；measure 协议 min/pref 两遍求解；无容器内换行 |
| 滚动容器 | ✅ | `UiScrollView`：视口裁剪/高度缓存/惯性/回弹（8.12） |
| 撤销/重做 | ❌ | 编辑框无撤销栈 |
| 窗内 Dialog / ConfirmDialog | ✅ | 开合动画、Tab 焦点循环、嵌套 Popup、输入 scope 隔离；body 仅布局（8.18） |
| Select / Menu | ✅ | 单选/多选/可搜索下拉、右键菜单；`UiPopupManager` 统一弹层（8.16） |
| 日期选择 | ✅ | DatePicker 四模式：日期/日期+时间/范围/范围+时间；可编辑触发器 + 滚动时间列（8.19） |
| 多级导航 | ✅ | `UiNavBar` 水平/垂直/rail、溢出收进 `>>`、无滚动续页（8.21） |
| 行内分页 | ✅ | `UiPagination` 页码窗口/自动紧凑退化/保持首条/键盘（8.22） |
| 原生文件对话框 | 由应用调用 | 注意动作延后，避免渲染重入 |
| 多主题/换肤 | ✅ | 内置 Light/Dark 全量令牌；`cui_theme_editor.exe` 可视化编辑、抽卡生成候选并导出预设（8.23） |
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

脚本分两层：

- `build.cmd`：在 `C:\Program Files\Microsoft Visual Studio\{18,2022,2019}\{Community,Professional,Enterprise}`
  下寻找 `vcvarsall.bat` 并加载 x64 环境；定位 Windows SDK 的 `fxc.exe`；
  生成头文件清单 `build\headers.mk`；调用 `nmake /f build.mk`；最后用
  `dumpbin /imports` 检查 0CRT 并列出产物。
- `build.mk`（NMAKE）：编译、着色器、链接的全部规则与依赖。

构建是**增量**的：

- 只重编比 `.obj` 新的 `.c/.cpp`；
- 任一 `src/**/*.h|*.hpp` 变化 → 全部对象重编（保守，但不会漏依赖）；
- `build.mk`（编译/链接参数）变化 → 全部对象重编；
- 无变化时只做 0CRT 校验与产物列表，基本不需要额外时间；
- 需要彻底重来时执行 `build.cmd clean`。

具体步骤：

1. 创建 `build\obj`、`bin`。
2. 用 `fxc.exe` 把 `src\gfx\grain.hlsl` 编译成 `build\grain_cso.h`
   （内嵌字节数组，运行时不依赖 d3dcompiler）。
3. 用 `/std:c11 /O1 /GS- /utf-8 /DNO_CRT_NO_DEFAULTLIB` 编译 C 文件。
4. 用 `/std:c++17 /EHs-c- /GR- /D_HAS_EXCEPTIONS=0` 编译 `gfx_d2d.cpp`。
5. 链接 `/SUBSYSTEM:WINDOWS /NODEFAULTLIB /ENTRY:startup /MANIFEST:NO`。
6. 用 `dumpbin /imports` 检查导入表，若出现 `msvcrt / ucrtbase / api-ms-win-crt-*`
   就判定 0CRT 构建失败。

产物：

```text
bin\cui_demo.exe
bin\cui_minimal.exe
bin\cui_advanced.exe
bin\cui_multi.exe
bin\cui_mvvm.exe
bin\cui_scroll.exe
bin\cui_tabs.exe
bin\cui_icons.exe
bin\cui_controls.exe
bin\cui_overlays.exe
bin\cui_textarea.exe
```

成功输出示例：

```text
[OK] no CRT dependency
=== BUILD OK ===
Output: ...\bin\cui_demo.exe (47104 bytes)
```

> 注意：`build.cmd` 与 `build.mk` 必须保持 **CRLF** 行尾；LF 会让 cmd 的多行
> 括号块/nmake 的续行解析出错。`build.mk` 还要保持 **纯 ASCII**：nmake 按本地
> 代码页解析，UTF-8 中文注释可能吞掉行尾反斜杠，导致后续行被当成命令。
>
> 注意：构建不再删除旧对象；需要全量重编请用 `build.cmd clean`，它只删除
> 构建目录中的中间文件与可执行文件，不碰源码。

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

需要调整配色时运行 `bin\cui_theme_editor.exe`：默认基础视图列 15 个常用
令牌（带中文短名），也可切高级视图看全量；点右侧预览里的组件可直接定位
对应令牌；没有思路时点"抽卡"生成 3 套候选，锁定喜欢的主色/中性色后重抽，
实时预览并导出可替换 `theme_apply_preset()` 的代码（8.23）。

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
| `src/ui/ui_dialog.*` | 窗内 Dialog / ConfirmDialog、动画、焦点循环 | body 仅布局；inputOwner/inputScope 隔离；每窗口一个 |
| `src/ui/ui_layout.*` | UiStack / UiFlex / measure 协议布局 | 只算矩形；padding/gap；固定+Fill / basis+grow / min-pref 两遍求解 |
| `src/ui/ui_grid.*` | Grid 布局：显式/隐式轨道、fixed/auto/fr、minmax、span、dense | 只算矩形；fr 以内容 min 为基分配富余；非固定轨道可压到 min，fixed 允许溢出 |
| `src/ui/ui_button_group.*` | 融合按钮组管理器 | NORMAL/SINGLE/MULTI、selected/mask/focus、键盘 |
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
- **任务栏最小化 / 还原带原生动画**：创建样式保留
  `WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU`，非客户区被
  `WM_NCCALCSIZE` 压没，外观仍全自绘；DWM 负责状态切换过渡，壳不做动画。
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
    WS_POPUP | WS_THICKFRAME | WS_CAPTION |
        WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
    0, 0, 0, 0,
    NULL, NULL, inst, NULL);
```

- `WS_EX_NOREDIRECTIONBITMAP` **必须在创建时指定**。运行时用
  `SetWindowLongPtr` 增删无效。它是 DComp 直出的前提。
- `WS_POPUP` 表示无系统边框；标题栏、关闭键由你自绘。
- **`WS_CAPTION` 决定 DWM 是否播放最小化 / 还原动画**：实测去掉它后即使
  走 DComp 直出，最小化 / 还原也是瞬间消失 / 出现；保留它并在
  `WM_NCCALCSIZE` 返回 0，系统 caption 不占像素也不绘制，动画仍由 DWM
  原生接管。`WS_MINIMIZEBOX | WS_SYSMENU` 决定任务栏点击是否下发
  `SC_MINIMIZE`（缺了只收到 `WM_ACTIVATE`，窗口不最小化）。
  详见 [CUI-窗口最小化还原-设计方案.md](CUI-窗口最小化还原-设计方案.md)。
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

- **先 `Log_Init()` 再 `Gfx_Init`**，退出时 `Log_Close()`。`Gfx_Init` 内部会调用
  `Log_Write`，而后者依赖 `Log_Init` 初始化的临界区；漏掉会在 `WM_CREATE` 里直接
  卡死（多窗口样例踩过）。
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

### 5.7 多窗口（低层 API）

`UiApp_Run` 是**单窗口阻塞壳**；多窗口直接用低层 API，每个 HWND 一套：

```text
HWND A -> Gfx A + UiTheme A + UiContext A + UiProgress A + UiToast A
HWND B -> Gfx B + UiTheme B + UiContext B + UiProgress B + UiToast B
          共享 UiFrameClock + 一个 GetMessage/PeekMessage 循环
```

规则：

- **不要跨 HWND 共享 `UiContext`**：注册表、hover/pressed、focus、模态 blocker
  都是窗口私有的。共享会导致一个窗口的鼠标状态污染另一个。
- `Gfx`、`UiProgress`、`UiToast` 每窗口独立；进度动画和 Toast 模态互不影响。
- `UiTheme` 每个窗口仍持有一个实例（字体绑定各自 Gfx），但**主题是全局的**：
  `UiTheme_SetTheme/SetColors` 广播到所有已注册实例，并按各自 `notifyHwnd`
  自动整窗失效；新窗口 `UiTheme_Init` 继承当前全局主题。
- 一个 `UiFrameClock` 就够；每帧对所有窗口 `Ui_AnimAdvance` / `UiProgress_Tick` /
  `UiToast_Tick`，再分别 `UpdateWindow`。
- 每个窗口的 `WM_PAINT` 里独立执行 `Gfx_SetDirtyRect` → `Gfx_Begin` → UI →
  `Gfx_End`，然后 `UiToast_EndFrame` / `UiProgress_EndFrame`。
- 关闭一个窗口只清它自己的 Gfx/Theme/Context；最后一个窗口销毁时 `PostQuitMessage`。
  主窗口关闭时若想连带退出，在 `WM_CLOSE` 里先 `DestroyWindow` 其它窗口。
- `Log_Init/Log_Close` 仍是进程级一次。

### 5.8 模态对话框（多窗口）

CUI 不提供阻塞式 `DialogBox`；模态窗口用普通 HWND + 共享消息循环实现：

1. 先创建主/二级窗口；对话框作为 `WS_POPUP` 窗口，`hWndParent` 指向 owner。
2. 创建后对所有其它 CUI 窗口 `EnableWindow(hwnd, FALSE)`，只保留对话框可用。
3. 对话框仍走同一个消息循环和 `UiFrameClock`，不需要嵌套 modal loop。
4. 对话框 `WM_CLOSE/WM_DESTROY` 时恢复 `EnableWindow(..., TRUE)`、把焦点还给
   owner，并销毁自己的 Gfx/Theme/Context。
5. 主题仍是全局的：在对话框里切主题，主窗/二级窗会一起重绘。

完整样例：[samples/multiwindow.c](../samples/multiwindow.c)。两个窗口共享全局主题，
各自有 4.5/6 秒进度 overlay 和独立 Toast；主窗按钮打开/聚焦二级窗口，也可以打开
一个禁用父窗口的模态对话框；关闭主窗会一并销毁二级窗和对话框。实测两窗口同时
动画时应用 CPU 约 **0.00%**。

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
| `Gfx_PushRotate` / `Gfx_PopTransform` | DIP 空间绕点旋转（最多 8 层），用于箭头/指示器补间 |
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

- `Gfx_Text` 的矩形同时是布局宽度与裁剪范围；默认不换行，
  `GFX_TEXT_WRAP` 可启用 DirectWrite 自动换行，不改变其他控件默认行为。
- 对齐标志：`GFX_TEXT_CENTER`、`GFX_TEXT_RIGHT`、`GFX_TEXT_VCENTER`、
  `GFX_TEXT_ELLIPSIS`；长文本 UTF-16 转换按需分配，不再受 1024 字符栈缓冲限制。
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

**瞬时状态**：下拉/菜单的列表项这类“指针离开就应立即还原”的临时行用
`Ui_StateInstant(&ui, id, rc, enabled)`。它同样登记命中，但不写动画通道，
`hover/press/focus` 直接返回 0/1，因此 hover 没有淡入淡出滞后，也不会创建
一次性动画条目。

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

### 7.9 鼠标光标

光标形状由控件在绘制时声明，应用在 `WM_SETCURSOR` 与帧末应用：

```c
/* 控件绘制内, 必须在 Ui_State/Ui_DrawSurface 注册之后 */
Ui_SetCursor(ui, id, UI_CURSOR_IBEAM);   /* ARROW/IBEAM/HAND/SIZE_NS/SIZE_WE */

/* 窗口过程 */
case WM_SETCURSOR:
    if (LOWORD(lp) == HTCLIENT) {
        POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
        SetCursor(Ui_CursorHandle(Ui_CursorAt(&ui,
            pt.x / scale, pt.y / scale)));
        return TRUE;
    }
    break;
```

- `Ui_SetCursor` 作用于本帧所有同 ID 注册（TextArea 的 surface 与 ScrollView
  viewport 同 ID，一起生效）；
- 命中按 z / 注册顺序取最高：滚动条 track/thumb 后注册，盖住文本区的 I 形光标；
- 弹层打开时只认 overlay 层；模态遮罩下返回箭头；
- `UiApp` 已内置上述接线，并在帧末再应用一次——弹层关闭、控件禁用、布局移动后
  不用等指针移动就能换形状；自建窗口过程（如 `samples/advanced.c`）按上面两处
  手动接线；
- `UiEdit`（`Edit_Draw`）与可编辑 `UiTextArea` 声明 I 形光标；禁用 TextArea
  保持箭头。

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
UiTheme_Init(&theme, gfx);       /* 创建 13 个字体 + 填颜色 */
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
| `fIcon` | 16 MDL2 | 图标按钮 / 导航箭头（Segoe MDL2 Assets） |
| `fIconS` | 12 MDL2 | 行内图标（分页箭头），与正文同视觉大小 |

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

**亮/暗预设（全局）**：

```c
UiTheme_SetTheme(&theme, UI_THEME_LIGHT);   /* 或 UI_THEME_DARK */
/* 所有窗口一起换肤并自动 InvalidateRect; 不需要再手动失效 */
```

**自定义 5 个语义色（全局）**：

```c
static const UiThemeColors brand = {
    0xFFE4572E,   /* primary */
    0xFFF2A93B,   /* warning */
    0xFF2FA56B,   /* success */
    0xFFD64545,   /* error */
    0xFFF3F5F9    /* neutral = default */
};
UiTheme_SetColors(&theme, UI_THEME_LIGHT, &brand);  /* 深色外壳传 DARK */
/* 同样广播到所有窗口并自动重绘 */
```

- **全局主题**：进程内所有 `UiTheme` 实例注册到内部链；
  `UiTheme_SetTheme/SetColors` 会应用到全部实例，按每个实例的 `notifyHwnd`
  整窗失效（`Ui_ContextInit` 自动绑定 HWND）。新建窗口的 `UiTheme_Init`
  继承当前全局主题。
- `UiTheme_SetColors` 先应用 light/dark 外壳（页面/卡片/文字），再覆盖 5 个语义色；
  hover/press/soft/对比文字/描边全部自动派生，不需要手算。
- `UiThemeColors` 字段传 `0` 表示保留该角色的预设；整个指针传 `NULL` 等价于
  `UiTheme_SetTheme(t, base)`。
- `neutral` 就是组件库里的 `default`（C 关键字不能作字段名）。
- 这 5 个色会同步写入旧令牌（`blue/blue2/btnPrimary*/progressFill*/green/orange/
  danger/dangerBg/btnBg/...`），所以老代码和所有内置组件一起换肤。
- `UiTheme_SetTheme/SetColors` 会让**每个**实例 `themeRev+1`；`UiProgress` 用
  `themeRev` 判断重建 overlay，`UiToast` 用它判断重画 layer。任意换肤都不需要手动销毁。
- 只想要某个窗口的完整自定义（页面/卡片/文字也改）时：直接改该 `UiTheme` 字段，
  再调用 `UiTheme_Touch(&theme)`；这是实例级操作，不会广播到其它窗口。
  如果改的是派生角色的源令牌（`blue/orange/green/danger/btnBg`），先调用
  `UiTheme_DeriveRoles(&theme)` 重算 `primary/warning/success/error/neutral`。
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
    ss.hoverBg = 0xFFE4E9F8;
    ss.pressBg = 0xFFD5DDF0;
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
- 组件自身不持有布局：矩形由应用或 `UiStack/UiFlex/UiGrid` 计算；需要
  内在尺寸时控件通过 `UiMeasureFn` 报告 min/pref（文本行数随 `maxW` 变化），
  组件内部仍没有自动流式布局。
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

### 8.8 布局：UiStack / UiFlex / UiGrid

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

**Measure 协议与 Ui_FlexLayout（内容自适应）**

```c
typedef struct {
    float maxW;                     /* 输入: <0 = 不限宽 */
    float minW, prefW, minH, prefH; /* 输出 */
    float baseline;                 /* 首行文字基线到顶边; 0 = 未知 */
} UiMeasure;
typedef void (*UiMeasureFn)(void *user, UiMeasure *m);

typedef struct { float w, h; } UiFixedSize;
void Ui_MeasureFixed(void *user, UiMeasure *m);
UiMeasureItem Ui_FixedItem(UiFixedSize *s, float grow); /* 头文件内联 */

void Ui_MeasureNormalize(UiMeasure *m);
void Ui_FlexMeasure(int dir, float gap,
                    const UiMeasureItem *items, int count, UiMeasure *out);
int  Ui_FlexLayout(UiRect inner, int dir, float gap,
                   const UiMeasureItem *items, int count, UiRect *out);
```

- 回调只报告尺寸，不绘制。`maxW<0` 取自然尺寸；`maxW>=0` 时 `prefH` 是
  换行后的高度。`minW` 是最长不可断内容的宽度，必须满足
  `0 <= minW <= prefW`、`0 <= minH <= prefH`（`Ui_MeasureNormalize` 兜底）。
- `UiMeasureItem` 在测量值上叠加主轴硬限制 `minSize/maxSize`、剩余空间权重
  `grow`，以及交叉轴 `minCross/maxCross/crossSize/stretch/align`。交叉轴限制
  同样先夹取再参与自然尺寸与摆放，`maxCross < minCross` 时以 `minCross` 为准。
- `align = UI_ALIGN_BASELINE` 仅用于行布局：按各项首行文字基线对齐，`baseline`
  为 0 的项退回起点对齐；列布局忽略基线。
- `Ui_FlexMeasure` 输出容器自然尺寸（行 = 各 min/pref 宽之和 + gap，交叉轴取最大
  高度；列相反），可交给父级继续测量。
- `Ui_FlexLayout` 两遍求解：先按自然 min/pref 分配主轴——空间不足按可压缩量
  等比压缩到硬下限（仍不足则保持 min 并溢出），富余按 `grow` 分配到硬上限；
  再以最终主轴尺寸重测交叉轴并摆放。
- 行项先按自然宽测量，最终宽确定后再次测量得到换行高度；列项始终在容器宽下
  测量。协议是“测量 → 摆放”两次回调，控件可缓存内部排版避免重复开销。
- ≤32 项使用栈内临时数组，更多项一次 `HeapAlloc` 并在函数返回前释放；没有
  逐项分配。布局结果仍是纯矩形，不注册控件也不持有状态。
- 固定/弹簧项可用 `UiFixedSize + Ui_FixedItem`：`w=固定宽, grow=0` 为固定项，
  `w=0, grow>0` 为弹簧项（需要硬下限时再设 `minSize`）。所有样例的横向按钮/
  卡片行已改用 `Ui_FlexLayout`；Markdown 表格列宽在总宽超出、列硬下限仍放得下
  时调用协议按 `nat-min` 容量压缩，极窄窗口保留等分兜底。

```c
UiMeasureItem items[2] = {
    { &title, text_measure, 1, 80, 0, 0, 0, 0, 0, UI_ALIGN_START },
    { &btn,   text_measure, 0, 90, 90, 0, 0, 0, 1, UI_ALIGN_CENTER }
};
UiRect out[2];
Ui_FlexLayout(rowRc, UI_ROW, 12, items, 2, out);
/* title 至少 80 宽并吸收剩余空间; btn 固定 90 宽并铺满行高 */
```

**规则总结**

| 项 | 规则 |
|---|---|
| padding | 容器四边，默认统一 |
| margin | 不支持；gap + `Ui_StackSkip` |
| 背景/边框 | 布局不画；外层 `Ui_Panel` 或自绘 |
| 固定尺寸 | `Ui_StackTake` / `basis` |
| 剩余空间 | `TakeFill` / `grow` 权重 |
| 百分比 | 不作为独立模式；需要就换算成 basis |
| 溢出 | `UiFlex` 不压缩；`Ui_FlexLayout` 先压到硬下限，仍不足才溢出 |
| 分配 | 栈上结构/调用方数组；`Ui_FlexLayout` ≤32 项零堆分配，更多项一次堆分配 |

demo 的卡片表单已改为 `UiStack` 纵向排布，按钮行用 `Ui_FlexLayout`
（`Ui_FixedItem` 固定项 + grow 弹簧，主题按钮靠右）；窗口可拖边缩放，
600×480 到 1000×700 之间布局自动重算，最小尺寸在 demo 初始化为 600×480
（再小按钮行会按“压到硬下限后溢出”的规则处理）。

**Grid 布局（Ui_GridMeasure / Ui_GridLayout）**

```c
enum { UI_GRID_FIXED, UI_GRID_AUTO, UI_GRID_FR };
typedef struct UiGridTrack {
    int kind;
    float value;      /* FIXED = DIP; FR = 权重 (<=0 视为 1); AUTO 忽略 */
    float minSize;    /* minmax 下限; 0 = 内容/0 */
    float maxSize;    /* minmax 上限; 0 = 内容/不限 */
} UiGridTrack;
typedef struct UiGridItem {
    void *user;
    UiMeasureFn measure;
    int row, col;     /* -1 = 自动 */
    int rowSpan, colSpan;
    int stretch;      /* 1 = 填满单元格 */
    int align;        /* 未拉伸时 UI_ALIGN_START/CENTER/END */
} UiGridItem;
typedef struct UiGrid {
    int rows, cols;   /* 显式轨道数; 0 = 隐式 */
    float rowGap, colGap;
    int dense;
} UiGrid;

void Ui_GridMeasure(const UiGrid *g, const UiGridTrack *colTracks,
                    const UiGridTrack *rowTracks,
                    const UiGridItem *items, int count, UiMeasure *out);
int  Ui_GridLayout(UiRect inner, const UiGrid *g, const UiGridTrack *colTracks,
                   const UiGridTrack *rowTracks, const UiGridItem *items,
                   int count, UiRect *out);
```

- 显式 `row/col` 同时给出的项先占位；只给行或列时在另一轴找空位；都未给时
  按行优先自动放置。`dense=1` 每项从 `(0,0)` 找最早空位，`dense=0` 用游标
  前进并保留空洞。`rowSpan/colSpan` 参与占位，超出显式轨道的项创建隐式 auto
  行/列（列数由 `g.cols` 或显式列项推断）。
- 列先测：按内容求每列 min/pref，`minmax` 夹取；有界布局下 `fr` 以内容 min
  （或 `minSize`）为基，再把剩余空间按权重分配，触到 `maxSize` 的冻结后余量
  最多再分 4 轮。行高在最终列宽下重测，因此跨列项能正确换行。
- 空间不足时先压缩非固定轨道（auto/fr）到各自 min；仍不足则保留 min 并溢出，
  固定轨道不压缩。`Ui_GridMeasure` 在 `maxW<0` 时报告自然尺寸，`maxW>=0` 时
  按约束求解并返回换行后的高度。
- 单元格默认 `stretch=1` 填满；否则按 measure 的 pref 尺寸放入并按 `align`
  在单元格内对齐。`Ui_GridLayout` 的 `out` 由调用方提供，纯矩形、不注册控件。
- ≤24 条轨道、≤24 项、≤512 单元格使用栈内暂存，超过后按需一次 `HeapAlloc`；
  行数上界 `显式行 + 项数 × 最大 rowSpan + 2`，超过 1,000,000 直接失败。

```c
UiGridTrack cols[3] = {
    { UI_GRID_FIXED, 112,   0,   0 },
    { UI_GRID_AUTO,    0, 150, 260 },
    { UI_GRID_FR,      1, 200,   0 }
};
UiGridItem items[3] = {
    { &header, measure, 0, 0, 1, 3, 1, UI_ALIGN_START },
    { &tall,   measure, 1, 2, 2, 1, 1, UI_ALIGN_START },
    { &wide,   measure, 2, 0, 1, 2, 1, UI_ALIGN_START }
};
UiGrid g = { 1, 3, 12, 12, 1 };   /* 1 条显式行 + 隐式 auto 行 */
UiRect rc[3];
Ui_GridLayout(inner, &g, cols, NULL, items, 3, rc);
```

完整样例：[samples/grid.c](../samples/grid.c)（`bin\cui_grid.exe`，20 卡片仪表盘：
dense/sparse、间距 12/6、明暗主题、跨 3 列标题、rowSpan=2 高卡、colSpan=2 宽卡、
卡片点击展开，整页放在 ScrollView 内滚动）；断言见
[tests/ui_grid_test.c](../tests/ui_grid_test.c)。

Grid 与 ScrollView 组合时按“先测高、再布格、后裁剪”三步走：用
`UiScrollView_ContentWidth` 作为 `Ui_GridMeasure` 的 `maxW`，得到换行后的
`prefH` 作为内容高传给 `UiScrollView_Begin`；`UiScrollView_ContentRect` 返回的
虚拟画布交给 `Ui_GridLayout`，每个项先经 `UiScrollView_Cull` 再绘制和注册命中。
这样行高、滚动范围与可见项数始终来自同一份布局结果，展开卡片等改变内容高的
交互也会同步更新滚动上限。

### 8.9 ButtonGroup 与键盘导航

`UiButtonGroup` 是一个**应用持有的状态管理器**，不是全局注册表。视觉上相邻按钮
共用外框/圆角，内部用 1px 分隔线融合。

```c
#include "ui/ui_button_group.h"

UiButtonGroup theme;
UiButtonGroup_Init(&theme, Ui_Id("vm.theme", 0, 0), UI_BG_SINGLE, 2);
UiButtonGroup_SetSelected(&theme, 0);
Ui_SetNavId(&ui, theme.id);

const char *const labels[2] = { "浅色", "深色" };
int clicked = UiButtonGroup_Draw(&ui, &theme, rc,
    labels, NULL /*等分*/, NULL /*全可用*/, UI_BTN_SECONDARY);
/* theme.selected / theme.changed 已更新; clicked 是本帧点击 index */
```

- 模式：
  - `UI_BG_NORMAL`：普通动作按钮分组，不保存选中；`clicked` 给应用执行命令。
  - `UI_BG_SINGLE`：单选，`selected` 由管理器维护。
  - `UI_BG_MULTI`：多选，`mask` 位图（最多 32 项）。
- `widths=NULL` 等分；否则按 DIP 宽度排列。`enabled` 可按项禁用。
- `variant` 决定选中段的强调角色：PRIMARY/SECONDARY -> primary，
  WARNING/SUCCESS/DANGER -> 对应角色；普通模式只影响键盘焦点色。
- 视觉：外层背景/边框只画一次；hover/press/selected **铺满整段**，首/中/尾
  分别使用 `{R,0,0,R}`、`{0,0,0,0}`、`{0,R,R,0}` 的每角圆角，因此强调色会
  一直填到组的外轮廓。外边框最后绘制并**内缩 0.5 DIP**，1px 描边完整落在
  组内，不会被选中填充盖住或产生半像素外溢。
- 边框/分隔线：
  - 相邻分段之间画 1px 分隔线；**与选中段相邻的分隔线不画**，避免灰线压在
    强调色填充上，选中块靠颜色本身与邻段区分。
  - 选中段不再额外画“选中边框”；边框只有组外轮廓和未选中段之间的分隔线。
  - 键盘焦点环是内缩 2 DIP 的独立描边，不压外框和分隔线；**鼠标点击不显示
    焦点环**（`focusVisible=0`），方向键/Home/End/Space/Enter 才置 1。
- 键盘：
  - `Ui_SetNavId(&ui, group.id)` 让组成为导航焦点；
  - `UiButtonGroup_Key(&ui, &group, vk)` 处理方向键/Home/End/Space/Enter；
  - 返回 1 时立刻读取 `group.clicked`（键盘事件在帧外发生，下一帧 Draw 会清空它）；
  - Tab 在组之间切换由应用负责（`Ui_SetNavId`）；进入新组时调用
    `UiButtonGroup_SetFocusVisible(&group, 1)` 显示焦点环，纯鼠标操作保持隐藏。
- 事件语义：`clicked` 是本帧点击/键盘激活 index；`changed` 表示 selected/mask
  真的变了。鼠标点击在 Draw 中写回，键盘激活在 Key 中写回。

### 8.10 MVVM-lite

CUI 不提供属性通知/绑定表达式；推荐 **VM struct + 纯 View + 显式命令**：

```c
typedef struct FormVM {
    UiButtonGroup themeGroup;   /* SINGLE */
    UiButtonGroup flagGroup;    /* MULTI */
    UiButtonGroup actionGroup;  /* NORMAL */
    void (*submit)(struct FormVM *);
    char status[192];
} FormVM;

static void form_view(UiContext *ui, FormVM *vm)
{
    UiButtonGroup_Draw(ui, &vm->themeGroup, themeRc,
        themeLabels, NULL, NULL, UI_BTN_SECONDARY);
    UiButtonGroup_Draw(ui, &vm->flagGroup, flagRc,
        flagLabels, NULL, NULL, UI_BTN_SECONDARY);
    UiButtonGroup_Draw(ui, &vm->actionGroup, actionRc,
        actionLabels, NULL, NULL, UI_BTN_SECONDARY);
}
```

- View 每帧读取 VM，并把控件的 `selected/mask/focus` 存在 VM 里；没有隐藏状态。
- 命中后在 `after_frame` 执行命令（提交/重置/退出），避免绘制中重入。
- 主题仍是全局的：VM 里切主题组 -> `UiTheme_SetTheme` -> 所有窗口重绘。
- 不做 property/binding 引擎；需要跨模块通知时用显式 command + Invalidate。
- 完整样例：[samples/mvvm.c](../samples/mvvm.c)（单选/多选/普通组 + 键盘导航）。

### 8.11 按钮样式与每角圆角

#### 8.11.1 为什么需要每角圆角

融合按钮组、分段控件、页签、输入框前后缀都要求**四个角不同**：组内首段是
左圆右直，中间全直，末段左直右圆。用“统一 radius + 内缩”模拟会在强调色和
外轮廓之间留下缝隙，也会破坏 1px 描边。CUI 的解法是直接把四个角作为几何：

```c
typedef struct GfxCorners { float tl, tr, br, bl; } GfxCorners; /* 顺时针 */

static __inline GfxCorners Gfx_Corners(float tl, float tr, float br, float bl);
static __inline GfxCorners Gfx_CornersAll(float r);

void Gfx_FillRoundRectCorners(Gfx *g, GfxRect r, GfxCorners corners, GfxColor c);
void Gfx_FillRoundRectCornersGradV(Gfx *g, GfxRect r, GfxCorners corners,
    GfxColor top, GfxColor bottom);
void Gfx_StrokeRoundRectCorners(Gfx *g, GfxRect r, GfxCorners corners,
    GfxColor c, float width);
void Gfx_StrokeRoundRectCornersDashed(Gfx *g, GfxRect r, GfxCorners corners,
    GfxColor c, float width);
```

示例：

```c
GfxRect r = { 24, 24, 120, 32 };

/* 胶囊左端 */
Gfx_FillRoundRectCorners(g, r, Gfx_Corners(16, 0, 0, 16), 0xFF3B82F6);
/* 普通独立按钮 */
Gfx_StrokeRoundRectCorners(g, r, Gfx_CornersAll(8), 0xFF94A3B8, 1.0f);
/* 虚线提醒按钮 */
Gfx_StrokeRoundRectCornersDashed(g, r, Gfx_CornersAll(8), 0xFFD64545, 1.0f);
```

实现约束（`gfx_d2d.cpp`）：

- 四段独立 `arc` 组成一条顺时针 `ID2D1PathGeometry`；半径先夹到
  `min(w,h)/2`，因此极端矩形不会自交。
- 几何以 **0.25 DIP 量化**后缓存：`(w,h,tl,tr,br,bl)` 为键，共 32 条，
  超出时淘汰最久未用的。按钮组每帧复用同一条几何，不产生堆分配/COM 对象抖动。
- 绘制时把缓存几何的局部原点平移到 `r.x/r.y`，并叠加当前 D2D 变换，
  所以 `Gfx_PushClip`、DPI 缩放、离屏效果都照常生效。
- 虚线使用 `Gfx_Init` 创建的 `ID2D1StrokeStyle`（flat cap / miter join），
  线宽仍由调用方给出；`Gfx_Shutdown` 统一释放几何缓存与虚线样式。
- 找不到路径几何（内存不足）时自动退回统一圆角的 `Fill/StrokeRoundRect`，
  调用方无需分支。

> 性能提示：每角几何只在**尺寸/角半径变化**时新建。做 hover 动画时请只改颜色，
> 不要把半径连续插值到小数后不停变化的值，否则会持续生成缓存条目。

#### 8.11.2 UiSurfaceStyle 接入

```c
typedef struct UiSurfaceStyle {
    /* ... bg/bg2/hoverBg/pressBg/border/... 见 8.3 ... */
    GfxCorners corners;   /* useCorners=1 时生效 */
    int        useCorners;
    int        dashed;    /* 边框用虚线 */
} UiSurfaceStyle;
```

- `useCorners=0`：沿用 `radius`（统一圆角）。
- `useCorners=1`：填充与边框都走每角路径；`radius` 只用于光晕。
- `dashed=1`：边框虚线；填充不变。实心按钮通常不需要虚线，常用在 plain/text。
- 光晕仍是统一圆角外扩：缓存几何 + 光晕叠加的复杂度不值得，视觉上也几乎不可见。

#### 8.11.3 UiButtonStyle 与 Ui_ButtonEx

按钮样式拆成四个正交维度，组合方式与 Element Plus 一致：

| 维度 | 取值 | 含义 |
|---|---|---|
| `type` | `UI_BTYPE_DEFAULT/PRIMARY/SUCCESS/WARNING/ERROR` | 语义角色，取主题的 `neutral/primary/success/warning/error` |
| `appearance` | `UI_BAPPEAR_SOLID/PLAIN/TEXT` | 实心 / 浅底彩字 / 无底无框 |
| `shape` | `UI_BSHAPE_RECT/ROUND/CIRCLE` | 主题圆角 / 胶囊 / 圆形 |
| `flags` | `UI_BFLAG_DASHED` | 虚线描边 |

```c
#include "ui/ui_widgets.h"

UiButtonStyle solid = UiButtonStyle_Make(
    UI_BTYPE_PRIMARY, UI_BAPPEAR_SOLID, UI_BSHAPE_RECT, 0);
UiButtonStyle plain = UiButtonStyle_Make(
    UI_BTYPE_SUCCESS, UI_BAPPEAR_PLAIN, UI_BSHAPE_RECT, 0);
UiButtonStyle text = UiButtonStyle_Make(
    UI_BTYPE_WARNING, UI_BAPPEAR_TEXT, UI_BSHAPE_RECT, 0);
UiButtonStyle dashed = UiButtonStyle_Make(
    UI_BTYPE_ERROR, UI_BAPPEAR_PLAIN, UI_BSHAPE_RECT, UI_BFLAG_DASHED);
UiButtonStyle round = UiButtonStyle_Make(
    UI_BTYPE_DEFAULT, UI_BAPPEAR_SOLID, UI_BSHAPE_ROUND, 0);

Ui_ButtonEx(&ui, Ui_Id("form.submit", 0, 0), submitRc, "提交", &solid, 1);
Ui_ButtonEx(&ui, Ui_Id("form.help", 0, 0), helpRc, "帮助", &text, 1);
```

派生规则：

- **SOLID + default**：`neutral.base/hover/press`，文字 `neutral.text`；
  悬停底色进一步混向 `neutral.press`，并加一层 `primary` 混合描边，避免白底
  按钮 hover 不明显；非 default 用 `role.base/hover/press`，文字按亮度自动
  选深/浅对比色（`btn_on`），并带角色色光晕。
- **PLAIN + default**：`neutral.soft` 底、`neutral.text` 字；悬停底色
  混向 `neutral.press` 并加 `primary` 混合描边。非 default 用 `role.soft`
  底、`role.base` 字与 150 alpha 描边。悬停时底色渐变为 `role.base`，
  文字同时混合到对比色，保证任意品牌色下都可读。
- **TEXT**：常态透明；default 用 `neutral.hover`、非 default 用 `role.soft`
  作为悬停/按下底，文字为 `text2` 或 `role.base`。亮色主题的
  `btnHoverBg/btnPressBg` 已加深到 `#E4E9F8/#D5DDF0`，否则贴近白色卡片的
  悬停几乎看不出来。
- **shape**：`RECT` 用 `theme.buttonRadius`；`ROUND` 取 `h/2`；
  `CIRCLE` 取 `min(w,h)/2`（配合等宽高即为正圆）。
- **disabled**：统一压成 `neutral.soft` 底、`neutral.border` 边、
  `btnDisabledText` 文字，并关闭光晕与 hover/press。
- 标签用主题 `fBtn`、UTF-8、水平垂直居中。返回值仍是“本帧是否点击”，
  动作请推迟到 `Ui_EndFrame` 之后执行。

旧接口 `Ui_Button(ui, id, rc, label, variant, enabled)` 保留为兼容包装：

| 旧 variant | 映射 |
|---|---|
| `UI_BTN_SECONDARY` / `UI_BTN_DEFAULT` | default + SOLID |
| `UI_BTN_PRIMARY` | primary + SOLID |
| `UI_BTN_SUCCESS` | success + PLAIN |
| `UI_BTN_WARNING` | warning + PLAIN |
| `UI_BTN_DANGER` / `UI_BTN_ERROR` | error + PLAIN |

#### 8.11.4 在 ButtonGroup 中使用

`UiButtonGroup_Draw` 内部已经利用每角几何实现真正的“融合”：

```text
首段: Gfx_Corners(R, 0, 0, R)
中间: Gfx_Corners(0, 0, 0, 0)
末段: Gfx_Corners(0, R, R, 0)
```

- 选中/hover/按下都铺满整个分段矩形，不再内缩；强调色直接贴到组的外轮廓。
- 相邻选中段的填充各向外重叠 0.5 DIP，消除两段独立抗锯齿在交界留下的
  背景色细缝；整段连续选中时看起来是一块完整的强调色。
- 选中段相邻的分隔线省略，未选中段之间保留 1px 分隔线；组外边框最后绘制，
  内缩 0.5 DIP，完整覆盖填充边缘。
- 键盘焦点环内缩 2 DIP，只在 `focusVisible=1` 时显示；鼠标点击会清除它，
  不会出现“选中边框”压在分隔线/外框上的双线。
- 只有 2 段时首段即末段，自动得到左圆右圆；1 段等价于普通圆角按钮。
- 组的外圆角取 `theme.buttonRadius`，与独立按钮视觉一致。

完整可运行样例见 [samples/mvvm.c](../samples/mvvm.c) 顶部样式条：
Solid / Plain / Text / Dashed / Round / Disabled 六个按钮，以及其下的三组
ButtonGroup。

#### 8.11.5 光晕纹理与 disabled

`Ui_DrawSurface` 的光晕优先走 `Gfx_DrawGlowRect()`：

- 后端按 `(宽, 高, 圆角, glowW, 颜色 RGB, DPI)` 生成一张预乘 alpha 的
  柔光纹理并缓存（24 条 LRU），动画只改 `DrawBitmap` 的 opacity；
- 一次 `DrawBitmap` 完成，不再每帧画 3 圈描边，也没有离屏 RT /
  `EndDraw` 目标切换；
- 纹理创建失败（尺寸过大/内存不足）才回退原来的 3 圈描边；
- 颜色/主题变化会按新 RGB 生成，旧纹理按 LRU 淘汰；resize/设备重建按
  `generation` 清空缓存；
- 光晕仍是统一圆角外扩，每角圆角按钮的填充/描边不受影响。

按钮 disabled 已支持：

```c
Ui_ButtonEx(ui, id, rc, "禁用", &style, 0);   /* 最后一个参数 enabled=0 */
```

- 视觉：`neutral.soft` 底、`neutral.border` 边、`btnDisabledText` 字，
  关闭光晕/hover/press；
- 交互：`Ui_State` 不注册命中，`Ui_ButtonEx` 返回 `enabled && clicked`，
  不会触发命令；
- `Ui_Button` 的 `enabled` 参数同样生效；MVVM 样式条已放一个 disabled
  按钮做回归样例。

### 8.12 ScrollView 滚动容器

设计细节与取舍见 [CUI-ScrollView-设计方案.md](CUI-ScrollView-设计方案.md)。
这里只给上手用法。

#### 8.12.1 定位

`UiScrollView` 不是列表控件，而是“固定视口 + 可溢出内容面”：

- 它只负责视口、`offset`、裁剪、物理和滚动条；
- 内容尺寸 `contentW/contentH` 由应用用 `UiScrollLayout` 或自己的布局算好后传入；
- 应用只遍历 `VisibleRange` 内的块绘制，完全在视口外的块不绘制、不注册命中；
- 状态由应用持有（建议放 ViewModel），不是全局注册表。

#### 8.12.2 最小用法

```c
#include "ui/ui_scroll.h"

UiScrollView sv;
UiScrollConfig cfg = UiScrollConfig_Desktop();

UiScrollView_Init(&sv, Ui_Id("list", 0, 0), &cfg, NULL);

/* 每帧 */
UiScrollView_Tick(&sv, &app->ui, (float)dtMs);          /* 动画帧才需要 */
UiScrollView_Begin(&sv, &app->ui, viewport, contentW, contentH);

UiRect content = UiScrollView_ContentRect(&sv);          /* 已含 -offset */
for (i = first; i <= last; i++) {
    UiRect rc = ...;                                     /* 局部 y + content.y */
    if (UiScrollView_Cull(&sv, rc)) continue;            /* 视口外直接跳过 */
    draw_block(ui, i, rc);
}

UiScrollView_End(&sv, &app->ui);                         /* 弹裁剪 + 画滚动条 */
```

- `UiScrollView_ContentRect()` 的 `y` 已经减去 offset，直接当屏幕坐标用。
- `UiScrollView_Cull()` 只做视口相交判断；绘制前调用可以省掉文本排版/UTF-8 转换。
- `Begin/End` 内部成对调用 `Gfx_PushClip` 与 `Ui_PushClipRect`，因此**视口外露出的
  部分也不会被点击**。
- `UiScrollView_Busy()` 在惯性、回弹、滚动条淡出/隐藏倒计时期间返回 1，
  在 `UiApp` 的 `animating` 回调里 OR 上它；静止后不再请求帧。

#### 8.12.3 固定行高

```c
UiScrollLayout fixed;

UiScrollLayout_Init(&fixed, NULL, 0);
UiScrollLayout_SetPadding(&fixed, 6, 8);
UiScrollLayout_SetFixed(&fixed, 10000, 34.0f);
UiScrollLayout_Commit(&fixed);

UiScrollView_Begin(&sv, &ui, viewport, viewport.w, fixed.contentH);
UiScrollLayout_VisibleRange(&fixed, sv.offset, viewport.h, &first, &last);
for (i = first; i <= last; i++) {
    UiRect row = UiScrollLayout_ItemRect(&fixed, i, UiScrollView_ContentRect(&sv));
    if (UiScrollView_Cull(&sv, row)) continue;
    /* 只画可见行 */
}
UiScrollView_End(&sv, &ui);
```

固定模式 `contentH` 是公式，`VisibleRange` 直接算下标，不需要每项缓存。

#### 8.12.4 变高内容与 measure 缓存

```c
UiScrollLayout lay;
UiScrollItem items[COUNT];          /* 调用方数组, 库不分配 */

UiScrollLayout_Init(&lay, items, COUNT);
UiScrollLayout_SetPadding(&lay, 8, 8);
UiScrollLayout_SetGap(&lay, 10);
UiScrollLayout_SetCount(&lay, COUNT);
for (i = 0; i < COUNT; i++)
    UiScrollLayout_SetItemFlags(&lay, i, UI_SCROLL_ITEM_WIDTH_DEPENDENT);

/* 每帧, 只重测失效项 */
UiScrollLayout_SetWidth(&lay, innerW);
for (i = 0; i < COUNT; i++) {
    if (UiScrollLayout_ItemNeedsMeasure(&lay, i, block[i].rev)) {
        float h = block_measure(&block[i], innerW);
        UiScrollLayout_SetItemHeight(&lay, i, block[i].rev, h);
    }
}
UiScrollLayout_Commit(&lay);         /* 重算前缀 y / contentH, revision++ */

UiScrollView_Begin(&sv, &ui, viewport, innerW, lay.contentH);
UiScrollLayout_VisibleRange(&lay, sv.offset, viewport.h, &first, &last);
for (i = first; i <= last; i++) {
    UiRect rc = UiScrollLayout_ItemRect(&lay, i, UiScrollView_ContentRect(&sv));
    if (UiScrollView_Cull(&sv, rc)) continue;
    draw_card(ui, &block[i], rc);
}
UiScrollView_End(&sv, &ui);
```

失效规则：

- 块内容变了：应用把该块 `rev++`；下一帧 `ItemNeedsMeasure` 返回 1，
  只重测这一项，`Commit` 更新它之后所有项的 y 与 contentH。
- 容器宽度变了：`SetWidth` 只失效带 `UI_SCROLL_ITEM_WIDTH_DEPENDENT` 的项；
  固定高度/图片块不受影响。
- 字体、DPI、主题字体变了：由应用递增相关 `rev` 或调用
  `UiScrollLayout_InvalidateAll`。
- 滚动帧不 measure、不 Commit；只做可见区间和绘制。

> `UiScrollLayout` 负责“缓存 + 前缀和 + 可见区间”，不负责知道子控件类型。
> 任意嵌套布局只要最终测出每块高度，就能接入。

#### 8.12.5 滚轮、惯性与回弹

`UiApp` 已接 `WM_MOUSEWHEEL`，滚动容器每帧登记视口；滚轮落在哪个视口，
`UiContext` 就把目标 id 交给该容器。应用只需：

```c
cb.tick = on_tick;
cb.animating = on_animating;

static void on_tick(UiApp *app, double dtMs, void *user)
{
    UiScrollView_Tick(&vm->sv, &app->ui, (float)dtMs);
}

static int on_animating(UiApp *app, void *user)
{
    return UiScrollView_Busy(&vm->sv);
}
```

配置：

```c
UiScrollConfig c = UiScrollConfig_Desktop();
c.wheelStep = 48.0f;       /* DIP / 格 */
c.wheelTauMs = 120.0f;     /* 0 = 立即 */
c.flingDecel = 2600.0f;    /* 惯性减速度 */
c.bounce = 0;              /* 桌面默认不回弹 */
c.maxOverscroll = 40.0f;

UiScrollConfig t = UiScrollConfig_Touch();  /* bounce=1 */
```

- 滚轮只做短平滑，不做长惯性；惯性主要给 `UiScrollView_Fling()` 或后续触摸手势。
- 回弹只允许 `maxOverscroll`，用阻尼弹簧回 0；`overscrollResist` 控制外向速度衰减。
- 内容/视口变矮导致 `maxOffset` 变小时，`Begin` 会**立即把 offset 夹到新的底部**
  并清速度，滚动条 thumb 位置同帧刷新；不会出现 thumb 停在越界位置、滚轮被
  回弹状态吞掉的情况。
- 惯性积分只在速度归零时把 target 对齐实际位置；滚轮可以随时中断惯性/回弹，
  不会被每帧的 target 覆盖。
- `UiScrollView_ScrollTo(sv, ui, offset, animate)` 可做“回到顶部/定位”。
- `UiScrollView_EnsureVisible(sv, ui, localY, h)` 可做键盘/表单聚焦自动滚入。

#### 8.12.6 el-scrollbar 风格滚动条

- overlay，不占内容宽度；默认宽 6 DIP、thumb 最小 24 DIP、右侧内缩 2 DIP。
- 悬停/滚动时淡入，停止后 `barHideDelayMs` 开始淡出，`barFadeMs` 完成。
- 内容不可滚动时 `Tick` 直接把 `barOpacity` 归零，不做淡出：滚动条本就不会
  绘制，若仍逐帧 `InvalidateRect` 会在 `WM_PAINT` 内排队下一次 `WM_PAINT`，
  饿死应用动画循环（Dialog/Select 首开曾被拖慢约 800ms）。
- thumb 可拖拽；点击轨道翻页；完全隐藏时不注册命中，不挡内容点击。
- 颜色从 `UiTheme` 解析，换肤自动生效。可覆盖：

```c
UiScrollbarStyle bs = UiScrollbarStyle_Default();
bs.width = 8.0f;
bs.minThumb = 32.0f;
bs.thumb = 0x663B82F6;       /* 0 = 主题默认 */
bs.thumbHover = 0xAA3B82F6;
bs.alwaysVisible = 1;        /* 覆盖 autoHideBar */
bs.reserveSpace = 1;         /* 内容右侧预留 thumb 槽, 不遮挡控件 */

UiScrollView_Init(&sv, id, &cfg, &bs);
```

#### 8.12.6.1 四向 padding 与不遮挡

```c
UiScrollPadding pad = { 6, 4, 8, 6 };   /* left, top, right, bottom */
UiScrollView_SetPadding(&sv, pad);

/* measure 前先取内容可用宽度, 与 Begin 内部计算一致 */
float cw = UiScrollView_ContentWidth(&sv, viewport);
contentHeight = measure_blocks(cw, ...);

UiScrollView_Begin(&sv, &ui, viewport, cw, contentHeight);
```

- `viewport` 是外层矩形：滚轮/hover/滚动条以它为准。
- `viewport` 去掉四向 padding，得到内容裁剪框 `inner`；上下 padding 固定，
  不随内容滚动，左右 padding 缩窄内容宽度。
- `reserveSpace=1` 时再扣除 `bar.width + bar.margin`，thumb 完全落在内容区
  右侧的槽里，不会压住按钮/文字；默认 0 是 el-scrollbar 的 overlay 行为。
- 如果只想留一点缝而不预留整条槽，可只设 `pad.right`（大于 thumb 宽即可）。
- `UiScrollView_Cull()`、输入命中和 `ContentRect` 都使用 `inner`，所以 padding
  区域不会误命中内容控件。

#### 8.12.7 命中裁剪与 wheel 目标

`Ui_PushClipRect/PopClipRect` 维护输入裁剪栈：控件注册时保存与裁剪的交集，
`Ui_HitTest` 同时检查控件矩形和裁剪。这样部分露出视口的行不会在视口外被点击。
`Ui_RegisterWheelTarget` 让 `Ui_OnMouseWheel` 选择最后登记（内层/后画优先）的
滚动视口；v1 不做滚动链。

#### 8.12.8 性能与边界

- 视口 400 DIP、行高 34 DIP：绘制约 12~13 行，10k 行与 100 行绘制成本相同。
- `UI_MAX_WIDGETS` 只有 256，长列表必须虚拟化；这不是可选优化，而是正确性要求。
- 滚动时整视口失效是必要的；只做滚动条淡出时只失效滚动条窄条。
- 文本布局缓存 128 条，可见行通常远小于该上限。
- 不在滚动内容里创建 D2D layer/effect；滚动条用普通绘制 + alpha。
- v1 限制：纵向滚动、不自动做滚动锚定、圆角视口按矩形裁剪、
  嵌套滚动不向上传递、双轴自由画布未开放。锚定可用
  `UiScrollLayout_ItemY()` 计算 delta 后由应用调整 `offset`。

完整样例：[samples/scrollview.c](../samples/scrollview.c)（10k 固定行 + 40 个
变高卡片 + 展开/收起 + 回弹 + 自动隐藏滚动条）。

### 8.13 TabBar 与 TabPager

设计说明：[CUI-Tab-设计方案.md](CUI-Tab-设计方案.md)。

#### 8.13.1 分层

- `UiTabBar`：只画 header，维护 `selected/focus/disabledMask/changed`。
- `UiTabPager`：只维护 `from/to/progress`，用回调绘制当前页/旧页/新页。
- 页面仍是即时模式绘制，不保存控件树；两个状态都由应用持有。

```c
#include "ui/ui_tab.h"

UiTabBar bar;
UiTabPager pager;
UiTabStyle style = UiTabStyle_Line();

UiTabBar_Init(&bar, Ui_Id("tabs.bar", 0, 0), 4);
UiTabPager_Init(&pager, Ui_Id("tabs.pager", 0, 0), NULL);

/* 每帧 */
int prev = bar.selected;
int clicked = UiTabBar_Draw(&bar, &ui, headerRc, labels, &style, 1);
if (clicked >= 0 && bar.selected != prev)
    UiTabPager_GoTo(&pager, &ui, bar.selected, 1);
UiTabPager_Draw(&pager, &ui, contentRc, draw_page, vm);
```

页面回调：

```c
static void draw_page(UiContext *ui, void *user, int index,
    UiRect rc, int interactive)
{
    /* rc 已包含滑动偏移; 直接在其中布局.
     * interactive=0 表示转场中, 但 UiContext 已经压了输入锁,
     * 控件通常可以照常绘制. */
}
```

#### 8.13.2 TabBar 样式

| 样式 | 视觉 | 默认 |
|---|---|---|
| `UI_TAB_LINE` | 下划线指示条 + header 底部 1px 分隔线；悬停为浅色底 + 文字提亮 | 默认 |
| `UI_TAB_PILL` | `neutral.soft` 胶囊底，选中项白底/边框，等宽 | 可选 |
| `UI_TAB_CARD` | 活动 tab 顶部圆角、底边留空，与内容面板连通 | 可选 |

```c
UiTabStyle line = UiTabStyle_Line();
line.stretch = 0;          /* 文字宽度+padX */
line.showDivider = 1;

UiTabStyle pill = UiTabStyle_Pill();
pill.stretch = 1;          /* 等宽 */

UiTabStyle card = UiTabStyle_Card();
card.mergeContent = 1;    /* 活动 tab 与内容面板连通 */
```

CARD 需要两阶段绘制：

```c
UiTabBar_Draw(&bar, &ui, headerRc, labels, &card, 1); /* header/命中 */
/* 应用画内容面板; 顶边在 UiTabBar_ActiveRect() 区间断开 */
UiTabPager_Draw(&pager, &ui, contentRc, draw_page, vm);
UiTabBar_DrawActive(&bar, &ui, labels, &card);        /* 活动 tab 覆盖接缝 */
```

- CARD 的 header/内容区之间不要留 gap；活动 tab 填充会向内容区延伸 1 DIP，
  盖住面板顶边。
- `UiTabBar_DrawActive` 只画不注册，不改变选中/命中；LINE/PILL 为空操作。
- 内容面板由应用绘制，因此 `contentBg` 应设为面板实际底色（默认 `theme.card`）。

- 指示条宽度 = 文字宽 + 12 DIP，夹在 tab 项宽度内；选中变化时平滑滑动。
- LINE 悬停不加底色；PILL 悬停有 soft 底，按下再加深。
- 禁用项：`disabledMask` 对应位为 1，鼠标不命中、键盘跳过、文字变灰。
- 键盘：Tab 进入组后，方向键/Home/End **移动 focus**，Space/Enter 才切换
  selected（与 ButtonGroup 一致）；`UiTabBar_SetFocusVisible` 控制焦点环。
- 数量多时 v1 只裁剪不换行；后续复用横向 `UiScrollView`。

#### 8.13.3 Pager 转场

```c
UiTabPagerConfig cfg;
ZeroMemory(&cfg, sizeof(cfg));
cfg.axis = UI_TAB_AXIS_HORIZONTAL;  /* 或 VERTICAL / NONE(仅淡入) */
cfg.fade = 0;                       /* 1 = 滑动同时淡入淡出 */
cfg.reverse = 0;                    /* 1 = 翻转滑动方向 */
cfg.durationMs = 260.0f;

UiTabPager_Init(&pager, Ui_Id("tabs.pager", 0, 0), &cfg);
```

- `GoTo(index, animate)`：转场中再次切换会先 snap 到当前目标，再开始新转场，
  不会三页叠加。
- `Tick(dtMs)` 推进 progress；`UiTabPager_Busy` 接入 `UiApp.animating`。
- `Draw` 负责：
  - 旧页/新页矩形按方向偏移（横/纵）；
  - 可选 `Gfx_PushAlpha` 淡入淡出；
  - `Gfx_PushClip` + `Ui_PushClipRect` 裁剪；
  - `Ui_PushInputBlock`：转场期间页面控件不注册命中/滚轮目标，header 不受影响。
- `reverse` 只翻转方向符号：从左向右/从右向左、从上向下/从下向上都能覆盖。
- 页面内控件 ID 必须包含页索引，旧页/新页同时绘制才不会撞 ID。

#### 8.13.4 边界

- v1 不做关闭按钮、badge、手势滑动、自动高度、页面 keep-alive。
  CARD 的几何、两阶段绘制和验收记录见
  [CUI-Tab-设计方案.md](CUI-Tab-设计方案.md) 第 7 节。
- 固定内容视口；不同页面高度不要自动改外层高度。
- 转场期间页面输入整体锁定（约 260ms）；header 仍可点击、键盘仍可切页。
- 滚轮不用于切页，避免和页面内 ScrollView 冲突；触摸手势后续做轴锁定。

完整样例：[samples/tabs.c](../samples/tabs.c)（LINE/PILL/CARD、横/纵/淡入、
reverse、键盘导航、转场输入锁）。

### 8.14 图标 (GLYPH 后端)

v1 先支持**图标字体**；`UiTheme` 在初始化时尝试创建
`Segoe MDL2 Assets` 16 DIP 的 `fIcon`，失败为 NULL（不静默用普通字体冒充）。

```c
#include "ui/ui_icon.h"

UiIcon gear = UiIcon_Glyph(app->theme.fIcon, 0xE713u);  /* Settings */
UiIcon_Draw(&ui, &gear, Ui_Rect(x, y, 24, 24), app->theme.primary.base, 1);
```

- 码点是 Unicode PUA（MDL2 通常 `E000..F8FF`），内部编码成 UTF-8 后走
  `Gfx_Text`；主题换色就是文字色，天然支持 light/dark、禁用、DPI。
- `tint=0` 用 `theme.text`；`enabled=0` 用 `btnDisabledText`。
- `Gfx_CreateFontFamily(g, L"Segoe MDL2 Assets", size, weight)` 可创建其他
  尺寸的图标字体；族不存在返回 NULL。
- 图标字体是单色、固定字号的；需要多色/任意 SVG 时，后续在同一 `UiIcon`
  上扩展 VECTOR/RUNTIME_SVG 后端（见设计讨论，不在 v1）。
- 图标绘制仍走文本布局缓存；同一码点/字号/矩形反复绘制不会重复排版。
- 组件接入：
  - `Ui_ButtonExIcon(ui, id, rc, &icon, label, style, enabled)`：图标 + 文本按钮；
    `label` 可为 NULL/空，纯图标按钮。
  - `UiTabBar_DrawEx(..., labels, icons, style, enabled)`：Tab 图标；
    CARD 需对应调用 `UiTabBar_DrawActiveEx`。
  - Toast 按 `kind` 自动带图标：Info `0xE946`、Success `0xE73E`、
    Error `0xE783`，无需额外 API。

示例：`samples/tabs.c` 概览页放了设置/网络/信息/刷新/禁用五个 MDL2 图标；
`samples/icons.c` 展示按钮 + 图标、Toast + 图标、TabBar + 图标和页面大图标。

### 8.15 表单控件 / Popup / Tooltip

#### 8.15.1 Checkbox / CheckboxGroup

```c
#include "ui/ui_controls.h"

UiCheckbox cb;
UiCheckbox_Init(&cb, Ui_Id("form.notify", 0, 0), 1);
if (Ui_Checkbox(&cb, &ui, rc, "启用通知", 1)) { /* cb.checked 已更新 */ }

UiCheckboxGroup feats;
UiCheckboxGroup_Init(&feats, Ui_Id("form.features", 0, 0), 3);
UiCheckboxGroup_SetMask(&feats, 0x1);
UiCheckboxGroup_Draw(&feats, &ui, rc, labels, 28.0f, 1);
```

- 单项支持 checked / indeterminate / disabled；点击 indeterminate 会先变 checked。
- checked/indeterminate 切换有淡入淡出：选中底色、边框、对勾/横线按
  `UI_AN_USER`（对勾）与 `UI_AN_USER+1`（横线）两个通道平滑过渡；
  indeterminate→checked 时横线淡出、对勾淡入，底色保持。
- **indeterminate 优先于 checked**：两者同时为 1 时只画横线（对勾目标值按
  `checked && !indeterminate`），避免初始“部分应用”状态出现对勾与横线重叠。
- Group 用 mask（最多 32 项），行高由 `rowH` 指定；键盘方向键/Home/End 移焦点，
  Space/Enter 切换。
- Group 的绘制和命中裁剪到 rc；超出 rc 的行不画、不命中。绘制裁剪左右各
  放宽 1 DIP：复选框描边中心正好在 `rc.x` 上，严格按 rc 裁剪会切掉左半像素
  抗锯齿，导致同列的单项 Checkbox 比 Group 内项多 1px；命中仍按 rc。
- 键盘焦点不画外框圆角：`Ui_SetNavId` 置 `navFocusVisible=1`，
  `Ui_OnMouseDown` 清 0；控件用 `primary.soft` 行底色 + 复选框/单选/轨道
  的 primary 描边表示焦点，鼠标点击不会留下焦点高亮。

#### 8.15.2 RadioGroup

```c
UiRadioGroup mode;
UiRadioGroup_Init(&mode, Ui_Id("form.mode", 0, 0), 3);
UiRadioGroup_Draw(&mode, &ui, rc, labels, 28.0f, 1);
```

- selected/focus/disabledMask 由应用持有的 manager 管理；点击或 Space 选择；
- 圆形选中点、hover 边框与 Checkbox 使用同一套主题色；换项时旧圆点淡出、
  新圆点淡入（每行独立 `UI_AN_USER` 通道）。

#### 8.15.3 Switch / Slider

```c
UiSwitch sw;
UiSwitch_Init(&sw, Ui_Id("form.wifi", 0, 0), 1);
Ui_Switch(&sw, &ui, rc, "Wi-Fi", 1);

UiSlider vol;
UiSlider_Init(&vol, Ui_Id("form.vol", 0, 0), 0.65f);
Ui_Slider(&vol, &ui, rc, "音量", 1);
/* vol.changed 拖动中；vol.committed 鼠标抬起后 */
```

- Switch 使用 `UI_AN_USER` 平滑 track/thumb 位置；
- Slider 支持点击定位、按住拖动、左右/上下方向键、Home/End；`changed`
  在拖动过程中即为真（可做实时数值显示），`committed` 在鼠标抬起时置真；
- Slider 的 hover/press 有明确视觉反馈：轨道加粗染色、拇指放大加深、百分比
  文字向主色偏移，均走 `UI_AN_HOVER/UI_AN_PRESS` 平滑；
- 拖动是 1:1 跟手的：按下时若抓住拇指，记录抓取偏移（`UiSlider.dragOfs`），
  拇指不会跳到指针正下方；按下轨道则直接定位。鼠标移出控件/窗口仍继续拖动，
  松手提交；
- 悬停/焦点按整行注册（百分比文字随 hover 变色，动画脏区覆盖整行）；
  但只有按在轨道上（含拇指外扩 12 DIP）才改值，点标签或百分比文字不会改值；
- 值到拇指中心的映射按 `SLIDER_MAP_R` 内缩，0%/100% 时拇指完整落在轨道内。

#### 8.15.4 Popup + Tooltip + IconButton

```c
UiRect tipRc = UiPopup_Place(&ui, anchor, w, h,
	UI_POPUP_BELOW, UI_POPUP_ALIGN_CENTER, 6.0f);

UiTooltip tip;
UiTooltip_Init(&tip, Ui_Id("tip", 0, 0));
UiTooltip_Update(&tip, &ui, targetId, anchor, "设置", UiFrame_Now());
UiTooltip_Draw(&tip, &ui);   /* 最后画 */

Ui_IconButton(ui, targetId, rc, &icon, &style, 1);
```

- `UiPopup_Place` 做 below/above/right/left 翻转 + 视口夹取；
  - 默认优先下方，下方空间不足改上方；
  - 宽高先夹到视口内（四周 8 DIP），水平/垂直最终夹取，绝不画出窗口；
  - 只要任一侧空间足够，候选矩形与 anchor 之间保留 `gap`，不会覆盖触发控件；
  - 两侧都不够时才选空间较大的一侧并夹取，此时覆盖 anchor 是不可避免的
    最后手段（例如窗口本身比 tooltip 还小）。
- Tooltip 由目标的 `ui->hover` 驱动，默认 400ms 延迟；不注册命中；
- `UiTooltip_Pending` 用于在 delay 到期前保持帧循环；
- IconButton 是 `<icon, label=NULL>` 的 Ui_ButtonExIcon 包装，tooltip 由应用
  绑定同一个目标 id。

#### 8.15.5 边界

- CheckboxGroup/RadioGroup 是固定行高，不做内部滚动；需要长列表时外层用
  ScrollView 或后续提供虚拟 provider。
- 弹层交互（点击外部/Esc 关闭、焦点回归、单弹层）已在 8.16 的
  `UiPopupManager` 中实现；Select/Menu 见下一节。菜单暂不支持子菜单。
- Tooltip 无淡入淡出；Checkbox/Radio 已有勾选/选中淡入淡出，但无缩放/描边宽度过渡。
- 控件状态都在应用/VM 里；键盘通过 `Ui_SetNavId` 接入。

完整样例：[samples/controls.c](../samples/controls.c)（图标按钮 + Tooltip、
Checkbox/Group、RadioGroup、Switch、Slider、键盘导航、禁用项与深色主题）。

### 8.16 弹层管理器 / Select / Menu

弹层（下拉、菜单、上下文菜单）需要跨帧保存“谁打开、点外部关闭、焦点回哪”，
不能塞进控件结构。`UiPopupManager` 由窗口/应用持有，同一时刻只允许一个弹层；
弹层内容仍由具体控件绘制。

#### 8.16.1 每帧顺序

```c
UiPopupManager pm;
UiPopupManager_Init(&pm);

/* frame 回调 */
UiPopupManager_BeginFrame(&pm, &ui);   /* 必须在最前 */
... 普通控件 ...
UiSelect_DrawPopup(&sel, &pm, &ui);    /* 帧末, overlay 层 */
UiMenu_Draw(&menu, &pm, &ui);

/* key 回调 */
if (UiPopupManager_Key(&pm, &ui, vk)) return 1;  /* Esc */
if (UiSelect_Key(&sel, &pm, &ui, vk)) return 1;
if (UiMenu_Key(&menu, &pm, &ui, vk)) return 1;
```

- `BeginFrame` 同步 `ui->popupOpen`；打开期间点击弹层外（含触发器）会关闭；
- 打开/关闭会自动整窗失效（弹层在触发器脏区之外，必须重画）；
- 弹层打开时 `Ui_HitTest` 只返回 overlay 层控件，底层控件不 hover/press；
- 滚轮目标新增 `layer`，弹层打开时只接受 overlay 层目标，底层 ScrollView 不跟着滚；
- 关闭后 `ui->navId` 回到 owner；键盘打开/Esc 关闭会显示焦点样式。

#### 8.16.2 Select

```c
static const char *const kItems[] = { "浅色", "深色", "跟随系统" };
UiSelect sel;
UiSelect_Init(&sel, Ui_Id("form.theme", 0, 0));
UiSelect_SetItems(&sel, kItems, 3);
UiSelect_SetPlaceholder(&sel, "选择主题");   /* 持久 placeholder */
UiSelect_SetSelected(&sel, 0);

/* 多选 + 可搜索 */
UiSelect tags;
UiSelect_Init(&tags, Ui_Id("form.tags", 0, 0));
UiSelect_SetItems(&tags, kTags, tagCount);
UiSelect_SetMultiple(&tags, 1);            /* 32 位 mask */
UiSelect_SetMask(&tags, (1u << 0) | (1u << 4));
UiSelect_SetFilterable(&tags, 1);
UiSelect_SetPlaceholder(&tags, "选择技术标签");
UiSelect_SetSearchHint(&tags, L"搜索标签…");  /* NULL = L"搜索…" */
UiSelect_AttachIme(&tags, &app->ime);      /* 搜索框接管 IME */

/* 每帧: 触发器必须在普通控件区域绘制 */
if (Ui_Select(&sel, &pm, &ui, Ui_Rect(24, 80, 220, 38), "选择主题", 1)) {
    /* 选中项已变化; 可在此提交 */
}
/* 帧末: 列表 */
UiSelect_DrawPopup(&sel, &pm, &ui);

/* 可搜索 Select 的三个应用回调 */
int  on_chr(UiApp *app, wchar_t ch, void *user) {
    return UiSelect_Char(&tags, &app->ui, ch);
}
void on_ime(UiApp *app, void *user) { UiSelect_OnIme(&tags, &app->ui); }
void on_timer(UiApp *app, void *user) { UiSelect_Blink(&tags, &app->ui); }
```

行为：

- 点击触发器打开/关闭；列表优先向下，空间不足自动翻转，宽度取
  `max(触发器宽度, 最长项)`，封顶视口；`listW/maxListH/rowH` 可调；
- placeholder：`Ui_Select` 的每帧参数非空时优先；否则用
  `UiSelect_SetPlaceholder` 的持久文本；两者都空则显示空白（`text4` 灰字）；
- 右侧箭头是矢量 chevron（两段圆头线），展开/收起做 180ms ease-out 旋转，
  颜色从 `text3` 渐到 `primary.base`；动画期间控件自行失效触发器矩形，
  不依赖应用持续 `animating` 回调；`animReduced` 时直接跳到终态；
- 列表滚动区**复用 `UiScrollView`**：滚轮 3 行/格（默认 100ms 平滑尾随，
  `UiSelect_SetScrollSmoothing(0)` 恢复即时）、thumb 拖动、轨道点击翻页、
  滚动条 hover 高亮、右侧预留槽全部走 ScrollView；track 透明，只显示 6 DIP thumb；
  列表项起点用 `Ui_PxSnap` 对齐设备像素，平滑滚动不出现亚像素抖动；
  弹层没有 UiApp tick，帧末 `End` 后按**真实帧间隔**补一次 `UiScrollView_Tick`
  （空闲首帧的 dt 尖峰按常规帧处理），`UiSelect_Busy()` 可让 animating 持续出帧；
- 打开时自动把选中项滚入可视区（`UiScrollView_ScrollTo(..., 0)`）；
- 列表项用 `Ui_StateInstant`：hover 立即出现/消失，不做淡入淡出，避免快速
  移动时的滞后/卡顿感；触发器本身仍用 `Ui_State` 的平滑状态；
- `disabledMask` 位标记禁用项，禁用项不注册命中、不可键盘选中；
- 多选：`mask` 最多 32 位；触发器显示前两个标签 + `+N`；鼠标点击/Enter 切换
  选中并**保持展开**，Esc/点击外部关闭；关闭时 `committed=1`；
- 可搜索：弹层顶部是内嵌 `UiEdit` 搜索框，打开接管 `focusId`/IME，关闭时
  取消组合并归还焦点；ASCII 大小写不敏感子串匹配，中文按字符精确匹配
  （不做拼音）；无结果显示“无匹配项”；
- 键盘（可搜索）：Down/Up/PageUp/PageDown 在结果中导航；Home/End、左右、
  Backspace/Delete、Ctrl+A/C/X/V 编辑搜索词；Enter 单选提交 / 多选切换；
  Space 在搜索框里输入空格（多选切换用 Enter 或鼠标）；Esc 关闭；
- 键盘（不可搜索）：Down/Up/Home/End/PageUp/PageDown 移动，Enter/Space
  单选提交或多选切换，Esc 关闭；
- `changed` 在每次选择/切换时为 1；`committed` 表示单选提交或多选关闭；
- 鼠标提交在 `Ui_Select`/`UiSelect_DrawPopup` 之后读取；键盘提交在
  `UiSelect_Key` 返回后立即读取（与 Slider 的 `changed/committed` 约定一致）。
- 与 `UiInput` 同窗口使用时：关闭可搜索 Select 会把 IME 焦点置空，应用若仍有
  聚焦表单字段，应在关闭后重新 `UiIme_SetFocus` 到该字段。

#### 8.16.3 Menu / 右键菜单

```c
static const UiMenuItem kMenu[] = {
    { "新建", "Ctrl+N", 0 },
    { "打开...", "Ctrl+O", 0 },
    { "", "", UI_MENU_DIVIDER },
    { "导出为 SVG", "", UI_MENU_DISABLED },
    { "删除项目", "Del", UI_MENU_DANGER }
};
UiMenu menu;
UiMenu_Init(&menu, Ui_Id("menu.file", 0, 0));
UiMenu_SetItems(&menu, kMenu, 5);

/* 按钮点击或键盘 Enter */
if (Ui_ButtonEx(ui, menuBtn, btnRc, "打开菜单", &style, 1))
    UiMenu_OpenAt(&menu, &pm, ui, menuBtn, btnRc, 0);

/* 帧末 + 按键 */
UiMenu_Draw(&menu, &pm, ui);
if (UiMenu_Key(&menu, &pm, ui, vk)) { /* changed/item 可提交 */ }
```

- 锚点可以是按钮矩形（下拉菜单）或鼠标点 `{x, y, 0, 0}`（右键菜单）；
- `UI_MENU_DISABLED` 不响应 hover/点击/键盘；`UI_MENU_DIVIDER` 画 1px 分隔线；
  `UI_MENU_DANGER` 使用 `error.base` 文字；`shortcut` 右对齐；
- 菜单项用 `Ui_StateInstant`，hover 瞬时切换（无 90ms 淡入淡出），键盘焦点行
  由 `keyFocus` 单独高亮；
- 方向键/Home/End 跳过禁用与分隔，Enter 激活，Esc 关闭；
- 激活后 `item` 为下标、`changed=1`，焦点回到 owner。

#### 8.16.4 右键入口

`UiAppCallbacks.mouse` 新增 `UI_APP_MOUSE_RDOWN/RUP`；右键只给应用，
不进入 `UiContext` 命中，因此不会误触底层按钮：

```c
static void on_mouse(UiApp *app, int msg, float x, float y, void *user)
{
    if (msg != UI_APP_MOUSE_RDOWN) return;
    UiMenu_OpenAt(&ctxMenu, &pm, &app->ui, UI_ID_NONE,
        Ui_Rect(x, y, 0, 0), 0);
}
```

#### 8.16.5 边界

- 弹层绘制在主窗口客户区内：不会超出窗口，也不能跨屏；需要跨屏时另做原生弹层窗口；
- 不做碰撞避让：只要一侧空间足够就不覆盖触发器，但会覆盖邻近控件（正常浮层语义）；
- 菜单不支持子菜单/图标列/勾选态，也不做内部滚动；超过视口高度会被裁剪，
  长菜单请分组或改用 Select；
- Select 已支持搜索与多选（≤32 项）；仍不支持用户自由输入值、拼音首字母匹配
  和虚拟 provider；超大列表（>1000）当前是固定行高逐项绘制。

完整样例：[samples/overlays.c](../samples/overlays.c)（单选/多选 Select、
可搜索多选标签、长列表滚动、禁用项、下拉菜单、右键上下文菜单、键盘与主题切换）。

### 8.17 TextArea 多行编辑器

```c
#include "ui/ui_textarea.h"

UiTextArea ta;
UiTextArea_Init(&ta, Ui_Id("form.note", 0, 0), 0);   /* 0 = 不限长 */
UiTextArea_SetPlaceholder(&ta, L"输入备注…");
UiTextArea_AttachIme(&ta, &app->ime);
UiTextArea_SetScrollSmoothing(&ta, 100.0f);  /* 0 = 即时; 默认 100ms */

/* frame */
if (Ui_TextArea(&ta, &ui, rc, 1)) { /* 文本已修改 */ }
/* animating 回调: 平滑滚动/滚动条淡入期间持续出帧 */
if (UiTextArea_Busy(&ta)) { /* 需要下一帧 */ }

/* 键盘 / 字符 / IME / 光标闪烁 */
UiTextArea_Key(&ta, &ui, vk);
UiTextArea_Char(&ta, &ui, ch);
UiTextArea_OnIme(&ta, &ui);
UiTextArea_Blink(&ta, &ui);       /* 530ms timer */

/* 窗口销毁 */
UiTextArea_Shutdown(&ta);
```

- **动态容量**：文本、行索引、UTF-8 缓冲按需倍增；`maxLen<=0` 不限长，
  正数仍限制表单长度。容量不会溢出，退出用 `Shutdown` 释放。
- **软换行 + 垂直滚动**：滚动区复用 `UiScrollView`（滚轮/thumb/轨道翻页/
  预留槽）；行高 = 输入字体高 + 4 DIP，并对齐设备像素。滚轮默认
  **100ms 平滑尾随**（`UiTextArea_SetScrollSmoothing(0)` 恢复即时）；
  `ensureCaret` 与拖选自动滚动始终即时；`UiTextArea_Busy` 供 animating。
- **鼠标光标**：文本视口声明 I 形光标，禁用状态保持箭头；滚动条 track/thumb
  是后注册的 overlay 控件，在滚动条上仍显示箭头。
- **增量布局**：末尾追加只从最后一行续排（O(新增字符)）；段落内普通编辑
  只重排编辑行到段落末尾，后续段落平移；插入/删除换行或宽度变化才全文重排。
- **宽度测量**：`Gfx_TextWidth` 使用 `widthIncludingTrailingWhitespace`，
  行尾空格计入行宽；鼠标列命中在“字符前/字符后”两个候选列中取最近者，
  不会跳到行末。
- **拖选自动滚动**：按住鼠标拖到文本视口上下边缘/外部时，每帧滚动
  6–40 DIP（越远越快），选区随滚动继续扩展；只在按住拖动时触发，
  行内移动不会滚动，也不会因为 `ensureCaret` 反馈而抖动。
- **编辑**：选区/拖选/Shift 扩展、Backspace/Delete（代理对整体）、
  Home/End、Ctrl+Home/End、方向键、Ctrl+左右按词；Ctrl+A/C/X/V
  （换行写 CRLF、粘贴归一化为 LF）；Tab 交给应用做焦点切换。
- **IME**：内部 `UiEdit` 代理接收上屏文本与组合串；组合串画在光标处并加
  下划线，候选窗跟随光标；代理文本在 Draw/Key/Char/OnIme 前排入正文。
- **readonly**：可聚焦、选择、复制，不可修改；**disabled**：不注册命中、
  不滚动、不显示光标。
- 焦点用 `Ui_SetFocusId`；点击字段内部自动聚焦，`clickedOutside` 清焦点。
- 单次超长段落中间编辑的成本仍与该段剩余长度线性相关；换行编辑会全文重排。

完整样例：[samples/textarea.c](../samples/textarea.c)（可编辑/只读/禁用、
7000+ 字符追加、中段编辑、滚动条、IME 接线）。


---

### 8.18 Dialog / ConfirmDialog

#### 8.18.1 设计与边界

Dialog 是应用持有的窗内模态状态，不建立控件树、不启动嵌套消息循环、
不创建 HWND。`UiDialog_Begin/End` 提供居中卡片、标题、矢量关闭键和内容矩形，
内容仍由应用调用已有组件绘制；`Ui_ConfirmDialog` 是同一管理器上的确认框配方。

输入采用独立 scope：`UiContext.inputOwner` 标记当前模态拥有者，注册的控件和
滚轮目标携带 `inputScope`。打开时立即过滤旧背景命中，后续背景控件不再注册；
Dialog 内部临时进入自己的 scope，Select/Menu 仍可在其中使用原 overlay 层。
这与 `Ui_SetModal` 的 Toast 全窗 blocker 分开，不改变后者的行为。

焦点顺序由应用通过 `UiDialog_SetFocusOrder` 提供借用的 ID 数组，数量不设
额外固定上限；只遍历当前已注册、未禁用的目标，关闭键自动加入循环。
Tab/Shift+Tab 在这些目标间循环，不离开 Dialog；保留鼠标设置的编辑焦点，
不每帧用 `navId` 覆盖 `focusId`。关闭后恢复原 nav/focus 与可见性。
可选 `UiDialog_AttachIme` 在打开时取消并暂停背景组合，关闭完成后恢复原编辑器。

打开 180ms、关闭 140ms，遮罩淡变、卡片轻微上移；Tick/Busy 显式驱动，
不依赖可能耗尽的控件动画池。仅转场期间失效整窗，静止不请求动画帧；
系统 reduced-motion 时直接到终态。淡出期间继续阻断输入，结果只在淡出完成后
发布一次，避免按钮松开或重复 Enter 穿透到背景。

本批只支持每窗口一个 Dialog，不做堆叠、拖动、缩放、跨窗口弹层或自动内容测高。
内容过长应在 body 中用 ScrollView/TextArea 的视口裁剪；确认正文在自身文本矩形内
换行并裁剪，不是由 Dialog body 裁剪，也不自动增高。
Dialog 属于主窗绘制，不能盖住独立 DComp visual，应用需暂停/隐藏该类 overlay。
打开前关闭外层 Select/Menu；Dialog 内的弹层需在 End 前绘制，并先处理其键盘。
键盘、字符、右键与业务快捷键仍由应用回调路由：模态可见期间只派发给 Dialog
及其子控件，不能继续调用背景回调。窗口系统关闭（Alt+F4）仍由 UiApp 处理。

#### 8.18.2 API 与接线

| API | 语义 |
| --- | --- |
| `UiDialog_Init(d,id)` | 初始化应用持有状态，无堆分配、无需 Shutdown |
| `UiDialog_AttachIme(d,ime)` | 可选：在关闭状态绑定窗口 IME，原编辑器须存活到 Dialog 关闭 |
| `UiDialog_SetFocusOrder(d,ids,count)` | 持久借用内容焦点顺序，绘制的禁用项不进入循环 |
| `UiDialog_Open(d,ui,owner)` | 打开并记住触发器；已有 Dialog、Toast blocker 或 Popup 时返回 0 |
| `UiDialog_Close(d,ui,result)` | 启动淡出；有子 Popup 时先关闭 Popup，再调用；重复调用不覆盖结果 |
| `UiDialog_IsOpen(d)` | 包含打开、停留、关闭动画，作为输入路由总开关 |
| `UiDialog_Busy(d)` / `Tick(d,ui,dtMs)` | 接到 UiApp 的 animating/tick，Tick 自动失效需要重画的帧 |
| `UiDialog_Begin/End` | 帧末绘制自定义 Dialog，body 仅布局、不推入绘制/输入裁剪；Begin 返回 1 必须配对 End |
| `UiDialog_Key(d,ui,vk)` | Esc 取消，Tab/Shift+Tab 焦点循环，关闭键可用 Enter/Space 激活 |
| `UiDialog_TakeResult(d)` | 淡出后一次性取 `UI_DIALOG_ACCEPT/CANCEL`，未完成或已消费为 `NONE` |
| `Ui_ConfirmDialog` | 默认 460×260 DIP，标题/换行正文/取消/确认；危险操作红色按钮 |

`closeOnBackdrop` 默认 0；设为 1 后单击遮罩等价取消，卡片空白不关闭。
确认框默认聚焦取消，不因 Enter 误执行危险操作；Tab 切到确认后 Enter/Space 才接受。
确认框的文字参数仅借用当前绘制调用，不复制到固定缓冲。

```c
UiDialog_Init(&vm.confirm, Ui_Id("project.confirm", 0, 0));
UiDialog_AttachIme(&vm.confirm, &app->ime);

/* frame: 先画背景触发器，最后画 Dialog。 */
if (Ui_Button(ui, deleteId, deleteRc, "删除…", UI_BTN_DANGER, 1))
    UiDialog_Open(&vm.confirm, ui, deleteId);
Ui_ConfirmDialog(&vm.confirm, ui, "删除项目？", "此操作无法撤销。",
    "确认删除", "取消", 1);

/* animating / tick */
return UiDialog_Busy(&vm.confirm);
UiDialog_Tick(&vm.confirm, ui, dtMs);

/* key: 必须位于背景快捷键之前。 */
if (UiDialog_IsOpen(&vm.confirm)) {
    UiDialog_Key(&vm.confirm, ui, vk);
    return 1;
}

/* frame 或 tick 消费；有副作用的业务动作仍放 after_frame。 */
if (UiDialog_TakeResult(&vm.confirm) == UI_DIALOG_ACCEPT)
    vm.pendingDelete = 1;
```

自定义内容使用 `UiDialog_Begin(&d,ui,title,width,height,&body)`；body 已扣除标题区，
留有 24 DIP 水平内边距，**仅供布局**。Begin 不推入 Gfx body clip 或 Ui body clip，
避免底部/右侧按钮描边、光晕和嵌套 Popup 列表行被 body 边界截断。各控件仍按自身
矩形命中，背景输入由 `inputOwner/inputScope` 阻断，不靠 body 裁剪实现模态。
正文控件与嵌套 Popup 均在 Begin/End 内绘制，先正文后弹层；Popup 自行提升到
overlay 层。仅真实视口需要显式配对 Gfx/Ui clip 或 ScrollView（TextArea 自带文本视口）；绘制 Popup 前应结束局部
视口裁剪，Dialog 外也不要遗留父级裁剪。
关闭动画期间不应继续向子控件派发键盘/字符/IME。文本输入控件的 `focused`、
UiInput 与 IME 焦点切换仍由应用根据当前焦点 ID 接线，不每帧强制重置编辑焦点。

样例 [samples/overlays.c](../samples/overlays.c) 同时演示确认框和自定义 Dialog
（Checkbox、Select、TextArea、禁用按钮、完成按钮）；新增 TextArea 用于编辑焦点
与长文本测试。背景主题按钮、Select、右键菜单在模态可见期间不可操作。
**验证状态**：200% DPI 原生窗口已验证按钮描边/光晕不被 body 裁切、嵌套 Popup
长列表可滚到末项并用鼠标选中、鼠标编辑焦点跨帧保持、Tab 跳过禁用项；真实鼠标
首次打开约 76ms（修复滚动条自失效导致的动画饿死后），静态 Dialog 为 0ms CPU。

### 8.19 DatePicker 日期选择

四种模式一个控件：`UI_DP_DATE`（纯日期）、`UI_DP_DATETIME`（日期+时间）、
`UI_DP_RANGE_DATE`（日期范围）、`UI_DP_RANGE_DATETIME`（日期+时间范围）。
形态镜像 UiSelect：触发器每帧画、弹层帧末画、状态由应用持有；
复用 `UiPopupManager`（单弹层/外部点击/Esc/焦点回归）、`UiPopup_Place`
（below 优先翻转+夹取）、`UiScrollView`（时间滚动列）、`UiEdit`（键入段）、
`UiAnim`（开合/月切换补间）、`UiIcon`（MDL2 日历/箭头）。
日期数学独立在 `ui_datetime.h/.c`：0CRT 的 civil↔day-number 换算
（Hinnant 算法，epoch 1970-01-01），唯一系统调用是 `GetLocalTime`；
headless 断言 1164 项覆盖闰年/月末/跨年/解析/格式化。

#### 8.19.1 每帧顺序与接线

```c
#include "ui/ui_datepicker.h"

/* 应用持有: UiDatePicker dp; UiPopupManager pm; UiIme ime; */
UiDatePicker_Init(&dp, Ui_Id("form.when", 0, 0), UI_DP_DATETIME);
UiDatePicker_SetShowSeconds(&dp, 1);          /* 默认到分 HH:mm */
UiDatePicker_SetBounds(&dp, &min, &max);      /* NULL = 该侧不限 */
UiDatePicker_AttachIme(&dp, &ime);

/* frame 内: */
UiPopupManager_BeginFrame(&pm, ui);           /* 最前 */
Ui_DatePicker(&dp, &pm, ui, rc, "选择日期时间", 1);
/* ...其余控件... */
UiDatePicker_DrawPopup(&dp, &pm, ui);         /* 帧末, overlay 层 */

/* 消息路由 (与 Select 相同, 先问弹层管理器再问控件): */
if (!UiPopupManager_Key(&pm, ui, vk)) UiDatePicker_Key(&dp, &pm, ui, vk);
UiDatePicker_Char(&dp, ui, ch);               /* WM_CHAR */
UiDatePicker_OnIme(&dp, ui);                  /* IME 消息后 */
UiDatePicker_Blink(&dp, ui);                  /* 530ms 定时器 */
```

`Ui_DatePicker` 返回 1 = 本帧值变化；`dp.committed` 单帧标记“提交”
（点日/Enter/确定/键入回车），`dp.changed` 含预览期变化。关闭弹层后焦点
回归触发器键入段（`focusOnClose` 语义同 Select）。

#### 8.19.2 触发器：可编辑键入

触发器 = 外层容器描边 + 内嵌 `UiEdit` 键入段（`flat=1`，见 11.3）+ 日历 glyph。
单值一段；范围两段（起/止）中间画 →。键入即解析：

- 格式 token：`YYYY MM DD HH mm ss`；默认 `YYYY-MM-DD` / `YYYY-MM-DD HH:mm[:ss]`，
  `UiDatePicker_SetFormat` 自定义后不再随 mode/秒自动改。解析时 `- / : 空格 .`
  分隔符互通，但必须完整匹配 pattern 且日期合法。
- Enter/失焦提交：空串 = 清除；解析失败或越界 = `invalid=1` 红色描边并回退原文；
  成功 = 写值、月历同步跳到该月。
- 中文输入法走 `UiIme` 组合串（与 UiEdit 同一套），密码语义不适用。
- hover 描边按**命中归属**平滑（父触发器或任一键入段/glyph 被命中才亮），
  被弹层盖住时不误亮；弹层 panel 登记了命中 blocker，禁用日格不注册命中。

#### 8.19.3 弹层：月历 / 范围 / 时间列 / 页脚

- 单月面板 276×340 DIP：头部 ‹ 年月 ›、星期行（`weekStart` 默认周一）、
  6×7 日格、页脚。日格用 `Ui_StateInstant`（列表项瞬时态），月切换整格淡入。
- 范围 = 双联动月历（左当月/右下一月，540 宽）：两阶段点选（phase 0 起 →
  phase 1 止），第二阶段悬停画连续预览带，点止后自动排序（起>止则交换）；
  端点实心、带内 soft 填充、today 细环。页脚预设：近7天/近30天/本月。
- 时间列：时/分（`showSeconds` 加秒）各一条 `UiScrollView` 滚动列，选中行
  高亮 + 自动滚到可视区；范围+时间**左右月历下各挂一套**（`timeSv[2][3]`，
  组标题 开始/结束，活动组标题高亮），点某组行只写该端点、另一端不变；
  单值模式单组居中、无标题。`activeEnd` 记录活动端点（键入段/端点日格/
  时间组点击/键盘焦点）。
- 页脚：今天/此刻（跳今天并写当前时间）、清除、确定。纯日期点日即提交并关闭；
  含时间模式点日只写日期、保持打开，确定才提交关闭。
- 定位：`UiPopup_Place` below 优先、放不下翻转 above；两侧都放不下时夹取进
  视口（**可能盖住触发器**，小窗实测内容完整可操作，Esc/外部点击照常关闭）。
- 键盘焦点环：方向键移动 `focusDay` 时画 2 DIP 粗环（统一 text 色；选中格上
  反色白环会融进浅色背景），仅键盘可见（`navFocusVisible`），与 today 细环区分；
  跨月自动翻月并淡入。

#### 8.19.4 键盘 map

| 键 | 行为 |
|---|---|
| Esc | 关闭弹层（焦点回归触发器）；编辑态先回退并退出编辑 |
| Enter/Space/Down/Up（关闭时） | 打开弹层 |
| ←→↑↓ | 焦点日 ±1/±7；跨月自动翻月 |
| PgUp/PgDn | 焦点日 ±1 月 |
| Home/End | 本周首日/末日（按 weekStart） |
| Enter | 日历焦点日点选；时间列焦点时退出时间焦点；编辑态提交键入 |
| Tab | 编辑态退出编辑；日历 → 活动端点时间组；时间组内 ←→ 循环两组全部列、↑↓ 改值并居中，Tab 下一组（末组后回日历），Enter 回日历 |

#### 8.19.5 边界与取舍

- 无农历/节假日/重复规则/时区换算；`disabled(dayNumber, user)` 回调做业务禁用
  （越界日格灰显且不注册命中），`min/max` 做硬边界。
- 范围预览带只在 phase 1 悬停时画；不持久化中间态，关闭即弃。
- 时间列精度到分或秒，不做毫秒/12 小时制；format 只支持上述 token。
- 设计与验证细节见 [docs/CUI-日期选择-设计方案.md](CUI-日期选择-设计方案.md)；
  样例 [samples/datepicker.c](../samples/datepicker.c)（`bin\cui_datepicker.exe`，
  四模式同窗 + ±30 天边界演示）。
**验证状态**：200% DPI 真实鼠标/键盘已验证四模式点选/提交/重开选中、范围两阶段
预览带与预设、范围双时间列端点隔离（点击/滚轮/键盘跨组）、键入非法红框回退/
合法提交、键盘焦点环（含选中格）与翻月、越界禁用、小窗夹取、暗色主题；
controls/textarea/scroll/overlays 回归干净。

---

### 8.20 Image 图片

> 设计细节：[CUI-图片-设计方案.md](CUI-图片-设计方案.md)；样例
> [samples/image.c](../samples/image.c)（`bin\cui_image.exe`）。

`UiImage` 是应用持有的图片组件，三种来源 + 自定义 provider：

```c
UiImage *img = UiImage_Create(app->gfx, net);   /* net 可 NULL */
UiImage_SetContext(img, &app->ui);              /* 自动失效自身矩形 */
UiImage_SetRect(img, Ui_Rect(x, y, 280, 220));
UiImage_SetFit(img, UI_IMG_FIT_COVER);          /* 留白/剪裁/拉伸/原始 */
UiImage_SetAnchor(img, UI_IMG_ANCHOR_MC);       /* 3x3 取景锚点 */
UiImage_SetBackground(img, 0xFFE9EFF7);         /* 留白底色 */

UiImage_LoadUrl(img, "https://host/a.png");     /* 走 UiNet (IOCP+SChannel) */
UiImage_LoadFile(img, L"C:\\pics\\a.jpg");      /* 同步读文件 + 解码 */
UiImage_SetBytes(img, pngBytes, pngLen);        /* 内存图片 */

/* 帧内 */
UiImage_Draw(img, &app->theme);

/* 自定义来源: 返回 1=同步字节, 0=异步后 SetBytes, -1=失败 */
UiImage_SetProvider(img, my_fetch, my_user);

UiImage_Destroy(img);                           /* 先于 Gfx_Shutdown */
```

语义：

- **解码**：WIC（`windowscodecs.lib`），输出预乘 BGRA 直接建 D2D 位图；支持
  PNG/JPEG/BMP/GIF 首帧等；单图编码上限 32MB、边长上限 8192。
- **加载动画**：`LOADING` 时画 GPU 矢量 spinner（圆环 + 旋转弧，`UiFrame_Now`
  驱动），播放器是组件自身登记的动画脏区；绑定 `UiContext` 后无需应用额外
  失效。`UiImage_Busy` 供应用在 `animating` 回调里继续出帧（可选）。
- **淡入**：解码完成 180ms 透明度过渡；设备丢失（`Gfx_Generation` 变化）时用
  保留的编码字节就地重建位图。
- **首帧前加载**：`on_init` 里调用 Load* 时 Gfx 尚未就绪，组件保持 `LOADING`，
  首帧绘制时补解码，不会误报失败。
- **fit**：CONTAIN 留白（等比完整）、COVER 剪裁（等比填满）、STRETCH 拉伸、
  NONE 原始像素（1 图素 = 1 物理像素，按 DPI 折算）；`SetAnchor` 9 个取景
  锚点决定留白余量/裁剪方向，`SetBackground` 给留白区铺底色。任何模式下
  超出组件矩形的绘制都会被裁剪，不会画出边界。
- **错误**：HTTP/TLS/超时复用 `UiNet_StrError`，本地/解码用
  `UiImage_StrError`；`ERROR` 画占位，可重试。
- **边界**：不支持 GIF 动画/WebP/EXIF 旋转/圆角裁剪；解码在 GUI 线程回调里
  完成（大图会占一帧，后续可加后台解码队列）。
- **样例**：URL 源为 Bing 每日图——先 `UiHttp_Send` 取 `HPImageArchive.aspx`
  JSON，解析出 `images[0].url` 后再 `UiImage_LoadUrl`；状态行显示版权。

### 8.21 NavBar 多级导航

> 设计细节：[CUI-导航菜单-设计方案.md](CUI-导航菜单-设计方案.md)；样例
> [samples/nav.c](../samples/nav.c)（`bin\cui_nav.exe`）；headless 断言
> `tests/ui_nav_test.c`（`bin\cui_nav_test.exe`）。

应用持有 `UiNavItem` 树，`UiNavBar` 只保存稳定 `UiId` 路径与打开层级：

```c
UiNavBar nav;
UiNavBar_Init(&nav, Ui_Id("nav.main", 0, 0));
UiNavBar_SetItems(&nav, roots, rootCount);
UiNavBar_SetLayout(&nav, UI_NAV_HORIZONTAL);   /* 或 UI_NAV_VERTICAL */
UiNavBar_SetRail(&nav, 1);                      /* 垂直只显示图标 */
UiId path[3] = { ID_PRODUCT, ID_DESKTOP, ID_COMPONENTS };
UiNavBar_SetSelected(&nav, path, 3);
```

```c
/* 帧内: 先导航栏, 再普通控件; 帧末画所有子菜单面板 */
UiPopupManager_BeginFrame(&pm, &ui);
UiNavBar_Draw(&nav, &pm, &ui, navRc, 1);
...其它控件...
UiNavBar_DrawPopups(&nav, &pm, &ui);
```

语义：

- **选中路径**：按 `UiId` 保存；数据暂时缺失时只高亮最长有效前缀，菜单
  恢复后自动还原完整路径；叶子/祖先持续高亮。
- **一个弹层会话**：多级子菜单全部属于 `nav.id` 的同一个 popup session，
  不是并列弹层；会话内点击外部/Esc 关闭整条路径。
- **水平溢出**：尾部放不下时收进 `>>`，点击打开隐藏顶级项；窗口变宽后
  自动取消。
- **垂直溢出**：高度不足时不出现滚动条，底部保留一行 `>>` 打开隐藏根部项；
  rail 模式同样适用，且有子项的项在行右缘显示 `›`，收起时也能分辨可展开项。
- **无滚动子菜单**：面板超过 `panelMaxH` 时首页末尾显示"更多 >>"，
  点击/回车在同会话下一层继续显示剩余行；Esc 从续页返回"更多"行。
  `UI_NAV_MAX_DEPTH = 8` 同时容纳逻辑层级与续页。
- **不越界**：每级面板都经 `UiPopup_Place`（8 DIP 边距、翻转/夹取），
  右/下空间不足时翻到左/上，极端窄窗口下允许覆盖父面板但不越出视口。
- **键盘**：方向键移动焦点，Enter 展开/提交，End 聚焦"更多"行，Esc 逐级
  返回。
- **悬停**：会话已打开时，悬停顶级项或父行立即切换/展开，无防抖延迟；
  关闭态不因鼠标路过而弹菜单，仍由点击打开。
- **动画**：只做颜色渐变（`Ui_State`），面板几何即时切换。

### 8.22 UiPagination 行内分页

> 设计细节：[CUI-分页-设计方案.md](CUI-分页-设计方案.md)；样例
> [samples/pagination.c](../samples/pagination.c)（`bin\cui_pagination.exe`）；
> headless 断言 `tests/ui_pagination_test.c`（`bin\cui_pagination_test.exe`，320 项）。

应用持有 `UiPagination`，组件只负责页数推导、页码窗口、绘制与键盘：

```c
UiPagination pg;
UiPagination_Init(&pg, Ui_Id("vm.pg", 0, 0), 1000, 20);   /* 50 页 */

UiPaginationStyle st;
ZeroMemory(&st, sizeof(st));
st.firstLast = 1;                    /* « ‹ 页码 › »，默认只有 ‹ › */
UiPagination_Draw(&pg, &ui, rc, &st);

int off = UiPagination_Offset(&pg);  /* page*pageSize，末页自行截断 */
if (UiPagination_TakeChanged(&pg)) { /* page 被点击/键盘/setter 改写 */ }
```

```c
/* 每页条数切换：保持原首条数据落在新页内 */
UiPagination_SetPageSize(&pg, &ui, 50);
/* 数据量变化：页数收缩时 clamp 到末页 */
UiPagination_SetTotal(&pg, &ui, newTotal);
/* 键盘：仅在 ui.navId == pg.id 时生效 */
if (UiPagination_Key(&pg, &ui, vk)) { /* ←/→ Home/End PgUp/PgDn */ }
```

语义：

- **数据模型**：`total + pageSize`，页数内部推导；页码 0 基、界面 +1。
- **宽度退化**：数字页码 → `‹ 6 / 20 ›` → `‹ ›` → 不绘制；所有阈值按
  实宽计算，任何宽度都不溢出给定矩形。
- **页码窗口**：首末页常驻，中间窗口 + 单省略号；`maxSlots` 自适应并
  clamp 到 `[5,9]`，`firstLast=1` 多出首/末两格。
- **保持首条**：`SetPageSize` 用 `offset = page*oldSize` 再除以新 size，
  切换 20→50→30 时原页首条仍在当前页。
- **单页**：`pageCount==1` 默认隐藏，`showSingle=1` 强制显示。
- **箭头**：默认用 `fIconS`（12 DIP MDL2 chevron），首/末页画双 chevron；
  可用 `prevText/nextText` 换成文本，无图标字体时自动回退 `‹ › « »`。
- **`changed`**：粘滞到 `TakeChanged`，点击/键盘/setter 都置位；
  同一帧里多个实例演示同一份数据时必须给不同 `p->id`，页码由应用同步。
- **键盘**：方向键 ±1、Home/End、PgUp/PgDn ±5；不占用滚轮，避免和
  `UiScrollView` 抢事件。
- **悬停**：页码/箭头与行内文字按钮共用 `neutral.hover/press` 实色，亮色主题
  下是 `#E4E9F8/#D5DDF0`，在白色卡片上可辨识（`primary.soft` 色差太小）。

### 8.23 主题编辑器与令牌工具

> 设计细节：[CUI-主题编辑器-设计方案.md](CUI-主题编辑器-设计方案.md)；样例
> [samples/theme_editor.c](../samples/theme_editor.c)（`bin\cui_theme_editor.exe`）；
> 令牌工具 `src/ui/ui_theme_edit.*`；headless 断言
> `tests/ui_theme_edit_test.c`（`bin\cui_theme_edit_test.exe`，7574 项）。

`ui_theme_edit` 把 `UiTheme` 的 68 个颜色令牌和 5 个圆角令牌抽成一张带
`offsetof` 的表，编辑器与测试共用；它不参与运行时渲染，只做"快照 ↔ 工程文件 ↔
C 代码"的转换。

```c
UiThemeSet light, dark;                    /* 68 色 + 5 圆角 */
UiThemeSet_Read(&light, &theme);           /* 从 UiTheme 实例读快照 */
UiThemeSet_Apply(&theme, UI_THEME_LIGHT, &light);   /* 写回并派生语义角色 */

char buf[16384];
UiThemeSet_Save(&light, &dark, buf, sizeof(buf));   /* cui-theme 1 工程文本 */
UiThemeSet_Load(&light, &dark, buf);                /* 容忍未知键/缺行 */
UiThemeSet_Codegen(&light, &dark, buf, sizeof(buf), "2026-11-15 12:00");
```

生成物是一段完整的预设函数，直接替换 `ui_theme.c` 里的同名函数：

```c
static void theme_apply_preset(UiTheme *t, UiThemeId id)
{
	t->themeId = id;

	if (id == UI_THEME_DARK) {
		t->page = 0xFF121722;
		/* … 68 色 … */
		t->windowRadius = 14.0f;
		/* … 5 圆角 … */
		return;
	}

	t->page = 0xFFF7F9FF;
	/* … 68 色 … */
	t->windowRadius = 14.0f;
}
```

- **颜色一律 8 位** `0x%08X`：6 位常量会把 alpha 补成 `00`；只有 100% 不透明
  的色块在界面里用 6 位 HEX 展示。
- **两模式各自输出圆角**：`UiThemeSet` 的圆角是独立的，生成器不能在分支外
  只写一份 Dark 值（曾经丢过 Light 的修改，已有断言兜住）。
- **工程文件是纯文本**：`cui-theme 1` + `[light]/[dark]`，键值来自令牌表；
  解析不认识的行直接跳过，缺行保留当前值，首行不匹配返回 0。
- **HEX**：接受 `RRGGBB` / `AARRGGBB`，可带 `0x` 与首尾空白；编辑器键入
  非法值显示"无效颜色"且不写入。
- **对比度**：`UiTheme_Contrast(fg, bg)` 先把半透明前景合成到背景，再按
  sRGB 亮度算比值；编辑器对卡片/窗口底色各显示一份，提示文字可读性。
- **0CRT**：`wvsprintfA` 不支持 `%f`，圆角/对比度用整数定点手写格式化；
  文件对话框用 `comdlg32!GetSaveFileNameW`，剪贴板用 `CF_UNICODETEXT`。

随机配色（抽卡）也由令牌工具提供，不依赖窗口，可 headless 复现：

```c
UiThemeGenOpts opts = {0};
opts.seed = 0xC0B63447u;                 /* 同 seed 必得同结果 */
opts.locks = UI_THEME_LOCK_NEUTRAL;      /* 锁住中性色组 */
opts.templateLight = &builtinLight;      /* 变换模板 */
opts.templateDark  = &builtinDark;
opts.keepLight = &currentLight;          /* 锁组与圆角的来源 */
opts.keepDark  = &currentDark;
UiThemeSet light, dark;
unsigned used = UiThemeGen_Palette(&light, &dark, &opts);
/* used != 0 表示成功；失败时输出保持不变 */
```

- 不是逐令牌随机：中性/主色/强调/语义/主按钮文字按族做受控变换，一个 seed
  同时生成 Light/Dark；圆角不随机，始终取 `keep*`。
- 配色方案在相近/互补/三角/分裂互补中随机，并避开语义色相；语义色只做
  微小色相扰动，保持红/橙/绿的识别。
- 生成结果自动按 12 组前景/背景配对校验对比度，不达标换 seed 重试（最多
  6 次），全部失败返回 0；被锁组完全覆盖的配对跳过。
- 锁组：`UI_THEME_LOCK_PRIMARY/NEUTRAL/SEMANTIC`，从 `keep*` 原样拷贝；
  `keep* = NULL` 时回退模板。

编辑器的交互约定：

- 左侧列表默认是**基础视图**：只列 15 个最常用令牌（窗口/卡片/边框/三级
  文字/主色/次级按钮/选中行/输入框/主按钮/主按钮文字 + 3 个圆角），每行
  显示中文短名 + key，组头按"外观/文字/交互/几何"归类；右上"基础/高级"
  分段按钮切到全量 73 项。搜索或"仅显示已改"生效时自动显示匹配的高级令牌。
- **预览即编辑器**：鼠标悬停预览组件时描出蓝色轮廓，并在"组件预览"标题旁
  显示"中文短名 · key"；点击后直接把对应令牌设为当前项、列表立即滚到该行。
  若该令牌不在基础视图（如 Toast 底），自动切到高级视图并给出状态提示。
  预览命中按"最小包围矩形优先"排序，所以按钮内层、输入框内层能准确落到
  `btnPrimary`/`inputBg` 等内层令牌，外层才能落到 border。
- 顶部 Light/Dark 切换当前编辑模式；左侧列表显示当前模式色块 + 另一模式
  对照色块 + "已修改"圆点，支持按 key/分组/中文描述搜索和"仅显示已改"。
- 中间是 HSV 方块、色相条、透明度条、HEX 输入与对比度；拾取器拖动实时
  写回令牌并触发整窗预览（编辑器本身也用当前配色绘制）。标题会显示
  "分组 · 中文短名"，与列表和预览悬停保持一致。
- 列表与预览各自是 `UiScrollView`，每帧 `UiScrollView_Tick` + `Busy`
  驱动滚轮/惯性/滚动条淡入；遗漏 Tick 时滚轮只改 target 不移动，属易踩坑。
  点选预览/切换视图的定位用 `ScrollTo(..., 0)` 立即落位，不陪滚动动画等待。
- 右侧预览覆盖文本层级、四种按钮、开关/复选/滑杆、输入框、进度、Toast、
  列表行、分页和 5 个语义角色色卡，用于检查派生角色在真实组件上的效果。
- **抽卡**：头部按钮打开模态面板，一次给 3 套候选（A/B/C），每张卡带
  Light/Dark 缩略预览与可复现 seed。顶部"主色/中性色/语义色"三枚 chip 可
  分别锁定，锁住的组不参与重抽；底部"重抽"换一批，"使用这套"应用并关闭，
  "撤销上次抽卡"回退到抽卡前的工程（单步）。快捷键：Esc 关闭、R 重抽、
  U 撤销、1/2/3 应用对应方案。
- 工程自动保存到 exe 同目录的 `theme_editor.palette`（启动时恢复，损坏则
  回退内置预设）；"导出代码"用系统保存对话框，"复制代码"进剪贴板。

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

## 10. 网络层与定时器（UiNet / UiHttp / UiWs / UiWsServer / UiTimer）

> 设计细节：[CUI-网络层-设计方案.md](CUI-网络层-设计方案.md)；样例
> [samples/net.c](../samples/net.c)、[samples/ws_server.c](../samples/ws_server.c)；
> headless/实网断言 [tests/net_test.c](../tests/net_test.c)。

### 10.1 线程模型

- 一个 App 持有 `UiNet`；内部只有 **1 个 IO 线程 + 1 个 IOCP**，连接、TLS 上下文、
  DNS 缓存、IO 定时器都只在该线程访问。
- GUI → IO：无锁 SLIST 命令队列 + `PostQueuedCompletionStatus` 唤醒（IOCP 等待
  不是 alertable，APC 唤不醒它）。
- IO → GUI：`QueueUserAPC`，事件只携带数据，业务状态只在 GUI 线程改。
- 因此 App **不需要任何锁**。代价是 GUI 线程必须处于 alertable 等待：
  `UiApp_Run` 已在内置循环与帧间隔里加了 `MWMO_ALERTABLE`，自定义消息循环
  必须自己加。

### 10.2 生命周期

```c
UiNetConfig cfg = {0};             /* 0 字段用默认值 */
cfg.trace = 1;
UiNet *net = UiNet_Create(&cfg);   /* GUI 线程 */
...
UiNet_Destroy(net);                /* 返回后保证不再回调 */
```

回调里不得调用 `UiNet_Destroy`（自等死锁），可以调用 `UiHttp_Cancel` /
`UiWs_Close`。Destroy 先停 IO 线程，再用哨兵 APC 排空已排队事件，避免回调
访问已释放对象。

### 10.3 HTTP

```c
typedef struct UiHttpRequest {
    const char *method;       /* NULL=GET */
    const char *url;          /* http:// 或 https:// */
    const char *contentType;  /* 有 body 时默认 octet-stream */
    const void *body; int bodyLen;
    const char *extraHeaders; /* "K: V\r\n..." */
    int timeoutMs;            /* 0=默认 15s */
} UiHttpRequest;

uint64_t UiHttp_Send(UiNet*, const UiHttpRequest*, UiHttpCallback, void *user);
int  UiHttp_Cancel(UiNet*, uint64_t id);
int  UiHttp_Header(const UiHttpResponse*, const char *name, char *out, int cap);
```

- 请求字符串/body 在 `Send` 返回后即可释放（内部深拷贝）；回调最多一次。
- 响应 header/body 是**借用切片**，回调返回即失效；需要留存请自行拷贝。
- `Cancel` 成功返回 1 后不再回调；超时/失败看 `UiHttpResponse.error`
  （`UiNet_StrError` 可读）。
- 内置 Content-Length / chunked / close-delimited、1xx 跳过、301/302/303/307/308
  重定向（303 改 GET、跨 host 去 Authorization/Cookie）、keep-alive 池
  （按 scheme+host+port 复用，空闲 60s 回收）、显式 HTTP 代理
  （https 走 CONNECT 隧道）。
- 传输层不解析 JSON，body 原样交付。

### 10.4 HTTPS 与 DNS

- TLS 用 SChannel（TLS 1.2/1.3），SNI 取 URL host。
- 默认做证书链 + 主机名校验（`CertGetCertificateChain` +
  `CERT_CHAIN_POLICY_SSL`）；失败报 `UI_NET_E_TLS`。
- `cfg.insecure = 1` 跳过校验（仅调试）；`cfg.pinSha256` 固定叶子证书指纹。
- DNS 是自研解析器：`GetAdaptersAddresses` 取系统 DNS 服务器，读 hosts 文件，
  UDP 组包查 A/AAAA（CNAME、压缩指针、TTL 缓存、超时轮换），响应被截断（TC）
  时自动转 TCP 重查。**全程不阻塞 IO 线程**，也不借用系统线程池；
  `localhost` 与 hosts 命中不发起网络查询。

### 10.5 WebSocket

```c
UiWsCallbacks cb = { on_open, on_message, on_close };
UiWsOptions opt = {0};
opt.autoReconnect = 1;
opt.reconnectMinMs = 500;      /* 退避下限 */
opt.reconnectMaxMs = 10000;    /* 退避上限 */
opt.reconnectBackoff = 2;      /* 退火倍率 1..10, 1=固定间隔 */
opt.reconnectJitterPct = 20;   /* ±抖动百分比 0..50 */
opt.reconnectStableMs = 30000; /* 稳定超过该时长后重置退避 */
opt.pingIntervalMs = 20000;
void *ws = UiWs_Open(net, "wss://host/path", &opt, &cb, user);
UiWs_Send(ws, data, len, 1);        /* 1=text 2=binary */
UiWs_Close(ws, 1000, "bye");
```

- 握手复用 HTTP 引擎（101 + `Sec-WebSocket-Accept` 校验），101 之后多余的字节
  直接进帧解析器。
- 帧编解码是每连接状态：客户端掩码就地写入发送缓冲；支持 16/64 位长度、分片、
  控制帧优先；单帧上限 16MB。
- 心跳 ping 超时未收到 pong 判死；`autoReconnect` 时指数退避 + ±20% 抖动，
  退避期间 `UiWs_Close` 立即停止。瞬断不投 `close`，只有最终关闭才回调一次。
- `wss` 与 https 共用同一 TLS 路径；v1 不支持 WS 代理。

### 10.6 WebSocket 服务端（UiWsServer）

```c
UiWsServerConfig sc = {0};
sc.bindHost = "0.0.0.0";
sc.port = 18082;              /* 0 = 临时端口, listening 回调给出实际值 */
sc.path = "/ws";              /* NULL = 接受任意路径 */
sc.subprotocols = "chat,superchat";
sc.certPath = "server.pfx";   /* 或 certSubject / certThumbprint: 启用 wss */
sc.certPassword = "secret";
UiWsServerCallbacks cb = { on_listening, on_open, on_message, on_close, on_error };
UiWsServer *srv = UiWsServer_Create(net, &sc, &cb, user);
UiWsServer_Broadcast(srv, data, len, 1);
UiWsServer_Close(conn, 1001, "bye");
UiWsServer_Destroy(srv);      /* 停监听 + 关闭全部连接, 返回后不再回调 */
```

- 服务端**复用同一个 `UiNet` IO 线程/IOCP**，不新增线程；回调同样在 GUI 线程，
  GUI 循环必须 alertable（`UiApp_Run` 已满足）。
- 证书来源：PFX 文件（可带密码）或 Windows 证书库（Subject / SHA-1 指纹，
  `CurrentUser\MY` 或 `LocalMachine\MY`，必须含私钥）；wss 与 ws 走同一套
  Upgrade/帧逻辑，TLS 解密后的明文直接进入握手解析。
- Upgrade：只接受 GET / HTTP/1.1 / Version 13；路径精确匹配，按配置挑选子协议；
  400/404/405/408/426/431/500/503 等错误带 `Connection: close`。
- 帧：强制客户端掩码；拒绝 RSV、非法控制帧；分片重组、UTF-8 校验、二进制含
  NUL；ping 自动回 pong；`maxMessageSize` / `maxSendBuffer` 防内存膨胀。
- 生命周期：`Create` 同步等到 listening/error（端口 0 可立即取实际端口）；
  `close` 对已 open 连接只来一次，服务端主动关闭也会投递；回调里不得
  `Destroy`（可 `Close`/`Send`）。`Destroy` 后连接与服务器句柄全部失效。
- 暂不支持 permessage-deflate、多监听地址、mTLS 客户端证书。

### 10.7 UiTimer（GUI 定时器）

```c
uint64_t id = UiTimer_After(app->timer, 500, cb, user);
UiTimer_Every(app->timer, 1000, tick_cb, user);
UiTimer_Cancel(app->timer, id);
```

`UiApp` 内嵌 `app->timer` 并接入消息循环：空闲时等待它的 waitable timer，
动画期按帧量化（≤8ms）。回调在 GUI 线程，可安全 `UiApp_Invalidate`，也可在
回调里取消自己。最小堆与 IO 定时器共用 `src/lib/heap.*`。

### 10.8 边界

- 无 HTTP/2、无 gzip（不发 Accept-Encoding）、无 WPAD/PAC（只支持显式代理）、
  无客户端证书；TLS renegotiation 不支持（遇到即断开）；服务端不支持
  permessage-deflate 与多监听地址。
- 连接/请求体/响应体全部动态增长，无固定上限；单帧 16MB、单响应头 64KB 是
  防护性上限。
- `UiNet` 必须与 `UiNet_Create` 同线程销毁；回调线程规则见 10.1。
- **验证状态**：`cui_net_test.exe` headless 945 断言 + `CUI_NET_LIVE=1`
  实网 52 断言（HTTP 200/chunked/redirect/POST、keep-alive 复用、400ms 超时、
  取消不回调、hosts、DNS 自解析 + HTTPS 证书校验、自签证书被拒、ws 回显、
  wss 回显）全部通过；服务端新增路径/版本拒绝、分片、ping-pong、广播、
  双向 close、连接计数回收与 WSS（PFX 自签证书）回环。`cui_net.exe` 200%
  DPI 真实鼠标/键盘验证 GET/POST/WS 连接/发送/断开与 https 键入请求；
  `cui_ws_server.exe` 提供 WS/WSS 启停、连接日志、回显与广播；22 个 exe 0CRT。

## 11. UiFrame 帧时钟

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

## 12. UiEdit 编辑框

### 11.1 数据与约束

```c
#define EDIT_MAX 512        /* 初始容量/组合串临时缓冲, 不是上限 */
#define EDIT_COMP_MAX 64
```

- 文本存 `wchar_t *text`，动态 `HeapAlloc/HeapReAlloc` 倍增；`cap` 是分配容量，
  `len` 是字符数（不是字节）。
- 单行；没有换行、自动折行、垂直滚动（长文本请用 TextArea）。
- 最大长度由 `Edit_Init(e, maxLen, ...)` 决定；**`maxLen <= 0` 表示不限长**，
  正数才限制字符数。
- UTF-8 转换与密码掩码显示用内部可增长缓冲，不再有 512 字符的隐藏截断。
- 组合串单独存 `comp[64]`，不进入正文，直到输入法上屏。
- `Edit_Init` 会分配初始缓冲，窗口销毁/字段移除时调用 `Edit_Shutdown`
  （`UiInput_Shutdown` 会遍历释放内部字段）。

### 11.2 初始化与读写

```c
UiEdit ed;
Edit_Init(&ed, 63, 0, NULL);            /* 最多 63 字，普通文本 */
Edit_Init(&edLong, 0, 0, NULL);         /* 0 = 不限长, 动态增长 */
Edit_Init(&edPass, 63, 1, NULL);        /* 密码模式 */
Edit_SetText(&ed, L"默认值");
Edit_SetTextUtf8(&ed, "默认值");
Edit_TextUtf8(&ed, buf, sizeof(buf));
Edit_TextW(&ed, wbuf, 64);
int empty = Edit_Empty(&ed);
Edit_Shutdown(&ed);                     /* 释放文本/转换缓冲 */
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
8. 声明 I 形鼠标光标（`Ui_SetCursor(id, UI_CURSOR_IBEAM)`）。

`ed.flat = 1` 时第 1 步的表面只保留命中注册，不画自身背景/边框/光晕
（文本/光标/组合串照画），用于嵌在外层容器里由容器统一画边框的场景
（DatePicker 触发器的键入段即如此，见 8.19）。

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
调用 `UiIme_Apply` 完成（见第 13 节）。同一时刻只有聚焦字段的 IME 状态生效。

密码框仍然支持鼠标定位、选区、剪贴板；只是显示为 `*`，并且输入法被禁用。

### 11.7 光标闪烁

demo 用 `SetTimer(hwnd, 2, 530, NULL)`，`WM_TIMER` 翻转 `blinkOn`，并在有焦点时
`InvalidateRect`。编辑框本身不拥有定时器，`caretOn` 由应用驱动。

输入/点击/提交/重置后应把 `blinkOn` 置 1，让光标立即出现。

---

## 13. UiIme 输入法

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

## 14. DPI 与多显示器

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

## 15. 0CRT 构建

### 14.1 入口

`build.mk`：

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

1. 在 `build.mk` 对应目录的对象清单里加 `$(OBJ)\名字.obj`（`src`、`src\ui`、
   `src\lib`、`src\net`、`src\gfx`、`samples`、`tests` 都有推断规则，
   源文件放对目录即可，不用另写编译命令）；
2. 需要参与链接的目标在对应 `link` 规则里补上该对象；
3. C 文件沿用 `CFLAGS`，C++ 文件沿用 `CXXFLAGS`；特殊标志（如 `mem_avx2.c`
   的 `/arch:AVX2`）用显式规则；
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

## 16. 日志与性能诊断

### 15.1 日志

```c
Log_Init();                          /* 初始化锁 */
Log_Write("[app] ready %d", 1);      /* 任意线程 */
Log_Close();
```

- **默认完全关闭**：`SC_LOG_ENABLE=0`（log.h 默认值），`Log_Init/Log_Write/
  Log_Close` 在调用点展开为 no-op，gfx/net/组件内的诊断日志一并消失，
  exe 中也不保留格式串；开发排查时加 `/DSC_LOG_ENABLE=1` 打开。
- 打开后输出到 `OutputDebugStringA`，用 DebugView / VS 输出窗口查看。
- 再叠加 `/DSC_LOG_FILE` 才会写 `exe 同目录 .log`。
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

## 17. 常用任务配方

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
7. 如果有输入法，按第 13 节接线，并在聚焦变化时 `UiIme_SetFocus`。

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

## 18. 已知限制与设计取舍

1. **布局是可选薄层**：`UiStack/UiFlex/UiGrid` 只算矩形；文本换行由控件
   measure 处理，滚动由 `UiScrollView/UiScrollLayout` 处理；没有保留式布局树。
   复杂页面仍可由应用自己算坐标。
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
29. **主题是进程级全局的**：`UiTheme_SetTheme/SetColors` 会改变所有窗口；
    只有 `UiTheme_Touch` 是单实例操作。多个 UiTheme 实例必须各自 Init/Shutdown，
    不要跨窗口共享同一个实例。
30. **ButtonGroup 状态在应用侧**：selected/mask/focus 由应用持有的
    `UiButtonGroup` 保存；MULTI 最多 32 项。键盘事件返回 1 后要立即读取
    `group.clicked`，下一帧 Draw 会清空本帧事件。
31. **没有 Radio/Checkbox 内置控件**：目前只提供 ButtonGroup；RadioGroup/
    Checkbox 可按同一“应用持有状态 + 本帧事件”模式扩展。
32. **ScrollView 只做纵向**：v1 不提供水平/双轴自由滚动；圆角视口按矩形裁剪，
    嵌套滚动不向上传递。内容尺寸必须由应用的 measure/layout 提供，库不会
    遍历控件树自动发现高度。
33. **ScrollView 的 measure 是全量缓存**：改宽/数据变更时 O(N) 重算，滚动帧
    仍是 O(可见)。上万异构项若不能接受首次全量 measure，需要后续 provider +
    估算总高 + scroll anchoring；这不是 v1 的能力。
34. **Tab 没有关闭按钮/badge**：CARD/LINE/PILL 已支持；关闭按钮、badge、
    横向溢出滚动、手势滑动仍未实现。header 数量超出时只裁剪，不换行。
35. **TabPager 转场期间页面输入整体锁定**（约 260ms），header 仍可操作；
    页面内控件在转场前/后正常。若需要“新页先可点”，要重新设计命中矩形
    随位移变化的注册方式。

---

## 19. 故障排查

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

## 20. 附录

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
Gfx_PushRotate / Gfx_PopTransform
Gfx_IsVisible
Gfx_CreateFont / Gfx_CreateFontFamily / Gfx_DestroyFont
Gfx_TextWidth / Gfx_TextHeight / Gfx_Text
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
Ui_State / Ui_StateInstant
Ui_RegisterWidget / Ui_HitTest / Ui_State / Ui_SetFocusId / Ui_SetNavId
/* Ui_SetNavId 同时置 navFocusVisible=1；Ui_OnMouseDown 清 0 */
Ui_SetModal
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

**UiButtonGroup**

```c
UiButtonGroup_Init / UiButtonGroup_SetCount / UiButtonGroup_SetSelected
UiButtonGroup_SetMask / UiButtonGroup_SetFocusVisible
UiButtonGroup_Draw / UiButtonGroup_Key
```

**UiPaint / UiTheme**

```c
Ui_DrawSurface
UiTheme_Init / UiTheme_Shutdown
UiTheme_SetTheme / UiTheme_SetColors / UiTheme_Touch / UiTheme_DeriveRoles
```

**Gfx 每角几何**

```c
Gfx_Corners / Gfx_CornersAll
Gfx_FillRoundRectCorners / Gfx_FillRoundRectCornersGradV
Gfx_StrokeRoundRectCorners / Gfx_StrokeRoundRectCornersDashed
Gfx_DrawGlowRect
```

**UiScrollView / UiScrollLayout**

```c
UiScrollView_Init / UiScrollView_Begin / UiScrollView_End
UiScrollView_SetPadding / UiScrollView_ContentWidth
UiScrollView_Tick / UiScrollView_Busy
UiScrollView_ContentRect / UiScrollView_MaxOffset / UiScrollView_Cull
UiScrollView_ScrollTo / UiScrollView_EnsureVisible / UiScrollView_Fling
UiScrollView_Wheel
UiScrollConfig_Desktop / UiScrollConfig_Touch / UiScrollbarStyle_Default
UiScrollLayout_Init / SetCount / SetPadding / SetGap / SetFixed / SetWidth
UiScrollLayout_ItemNeedsMeasure / SetItemFlags / SetItemHeight
UiScrollLayout_InvalidateItem / InvalidateAll / Commit
UiScrollLayout_ContentHeight / ItemY / VisibleRange / ItemRect
```

**UiTabBar / UiTabPager**

```c
UiTabStyle_Line / UiTabStyle_Pill / UiTabStyle_Card
UiTabBar_Init / SetCount / SetSelected / SetDisabledMask / SetFocusVisible
UiTabBar_Draw / DrawEx / DrawActive / DrawActiveEx / ActiveRect / Key
UiTabPager_Init / GoTo / Active / Busy / Tick / Draw
```

**UiIcon**

```c
UiIcon_Glyph / UiIcon_Draw
Gfx_CreateFontFamily / UiTheme.fIcon
Ui_IconButton
```

**UiControls / Popup / Tooltip**

```c
UiCheckbox_Init / Ui_Checkbox / UiCheckbox_Key
UiCheckboxGroup_Init / SetMask / SetDisabledMask / Draw / Key
UiRadioGroup_Init / SetSelected / SetDisabledMask / Draw / Key
UiSwitch_Init / Ui_Switch / UiSwitch_Key
UiSlider_Init / SetValue / Ui_Slider / UiSlider_Key
UiPopup_Place
UiTooltip_Init / SetDelay / Update / Pending / Draw
```

**UiPopupManager / UiSelect / UiMenu**

```c
UiPopupManager_Init / BeginFrame / IsOpen / AnyOpen / Open / Close / Key
UiSelect_Init / Shutdown / SetItems / SetSelected / SetDisabledMask
UiSelect_SetMultiple / SetMask / GetMask / SetFilterable / AttachIme
UiSelect_SetPlaceholder / SetSearchHint
UiSelect_SearchActive / Char / OnIme / Blink
Ui_Select / UiSelect_DrawPopup / UiSelect_Key
UiMenu_Init / SetItems / OpenAt / UiMenu_Draw / UiMenu_Key
```

**UiTextArea**

```c
UiTextArea_Init / Shutdown
UiTextArea_SetText / SetTextUtf8 / SetCaret / TextUtf8 / TextW
UiTextArea_SetPlaceholder / SetReadonly / AttachIme
Ui_TextArea / UiTextArea_Key / UiTextArea_Char
UiTextArea_OnIme / UiTextArea_Blink
```

**UiDialog / ConfirmDialog（8.18）**

```c
UiDialog_Init / UiDialog_AttachIme / UiDialog_SetFocusOrder
UiDialog_Open / UiDialog_Close / UiDialog_IsOpen
UiDialog_Begin / UiDialog_End / UiDialog_Key
UiDialog_Tick / UiDialog_Busy / UiDialog_TakeResult
Ui_ConfirmDialog
```

**UiDatePicker / UiDateTime（8.19）**

```c
UiDatePicker_Init / Shutdown / SetMode
UiDatePicker_SetValue / SetRange / SetBounds / SetDisabledFn
UiDatePicker_SetFormat / SetWeekStart / SetShowSeconds / SetPlaceholder
UiDatePicker_AttachIme
Ui_DatePicker / UiDatePicker_DrawPopup / UiDatePicker_Key
UiDatePicker_Char / UiDatePicker_OnIme / UiDatePicker_Blink
UiDate_Now / UiDate_Valid / UiDate_DayNumber / UiDate_FromDayNumber
UiDate_Weekday / UiDate_IsLeap / UiDate_DaysInMonth
UiDate_AddMonths / UiDate_AddDays / UiDate_Compare / UiDate_CompareDay
UiDate_Format / UiDate_Parse
```

**UiWidgets / UiToast / UiInput / UiProgress**

```c
Ui_Button / Ui_ButtonEx / Ui_ButtonExIcon / Ui_IconButton
UiButtonStyle_Make / Ui_Panel / Ui_ProgressBar
UiToast_Init / UiToast_Attach / UiToast_Show / UiToast_Hide
UiToast_IsVisible / UiToast_Tick / UiToast_Draw / UiToast_EndFrame
UiToast_UsingLayer / UiToast_Shutdown
UiToast_BackdropId / UiToast_CardId
UiInput_Init / UiInput_Shutdown / UiInput_Add / UiInput_BeginFrame / UiInput_SetRect
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
Edit_Init / Edit_Shutdown / Edit_SetText / Edit_SetTextUtf8
Edit_TextUtf8 / Edit_TextW / Edit_Empty
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
| `WM_MOUSELEAVE` | `Ui_OnMouseLeave` | 清 hover、mouseOver |
| `WM_MOUSEWHEEL` | `Ui_OnMouseWheel` + 可选 `wheel` 回调 | 屏幕坐标转客户区；`UiApp` 已内置 |
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
| Grid 布局 | [src/ui/ui_grid.c](../src/ui/ui_grid.c) / [samples/grid.c](../samples/grid.c) |
| 全局主题同步 | [src/ui/ui_theme.c](../src/ui/ui_theme.c) |
| 多窗口/模态样例 | [samples/multiwindow.c](../samples/multiwindow.c) |
| ScrollView 容器 | [src/ui/ui_scroll.c](../src/ui/ui_scroll.c) |
| ScrollView 设计 | [docs/CUI-ScrollView-设计方案.md](CUI-ScrollView-设计方案.md) |
| ScrollView 样例 | [samples/scrollview.c](../samples/scrollview.c) |
| Tab 容器 | [src/ui/ui_tab.c](../src/ui/ui_tab.c) |
| Tab 设计 | [docs/CUI-Tab-设计方案.md](CUI-Tab-设计方案.md) |
| Tab 样例 | [samples/tabs.c](../samples/tabs.c) |
| 图标 GLYPH | [src/ui/ui_icon.c](../src/ui/ui_icon.c) |
| 图标样例 | [samples/icons.c](../samples/icons.c) |
| 表单控件 | [src/ui/ui_controls.c](../src/ui/ui_controls.c) |
| Popup/Tooltip | [src/ui/ui_popup.c](../src/ui/ui_popup.c) / [src/ui/ui_tooltip.c](../src/ui/ui_tooltip.c) |
| 控件样例 | [samples/controls.c](../samples/controls.c) |
| 弹层管理器 | [src/ui/ui_popup.c](../src/ui/ui_popup.c) |
| Select 下拉 | [src/ui/ui_select.c](../src/ui/ui_select.c) |
| Menu/右键菜单 | [src/ui/ui_menu.c](../src/ui/ui_menu.c) |
| 弹层设计与样例（含 Dialog/ConfirmDialog、TextArea 测试） | [docs/CUI-弹层与选择控件-设计方案.md](CUI-弹层与选择控件-设计方案.md) / [samples/overlays.c](../samples/overlays.c) |
| Dialog / ConfirmDialog | [src/ui/ui_dialog.h](../src/ui/ui_dialog.h) / [src/ui/ui_dialog.c](../src/ui/ui_dialog.c)；设计/API 见 [8.18](#818-dialog--confirmdialog) |
| TextArea 编辑器 | [src/ui/ui_textarea.c](../src/ui/ui_textarea.c) |
| TextArea 设计/样例 | [docs/CUI-TextArea-设计方案.md](CUI-TextArea-设计方案.md) / [samples/textarea.c](../samples/textarea.c) |
| DatePicker 四模式 | [src/ui/ui_datepicker.c](../src/ui/ui_datepicker.c) / [src/ui/ui_datepicker.h](../src/ui/ui_datepicker.h) |
| 日期数学 (0CRT) | [src/ui/ui_datetime.c](../src/ui/ui_datetime.c) / [src/ui/ui_datetime.h](../src/ui/ui_datetime.h) |
| DatePicker 设计/样例 | [docs/CUI-日期选择-设计方案.md](CUI-日期选择-设计方案.md) / [samples/datepicker.c](../samples/datepicker.c) |
| 右键回调 | [src/ui/ui_app.c:259](../src/ui/ui_app.c#L259) |
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
  无 Toast 时应用进程 CPU 约 **0.00%**；十一份 exe 均为 0CRT。
- 0CRT 增强回归：独立自测程序覆盖 `%d/%u/%x/%s/%c/%lld/%zu/%08x/%.2f/%-5d`
  与边界截断、内存 0/7/15/16/31/32/63/64/65/511/512/513/4095/4096/4097/
  8192/10000 字节、字符串、malloc/calloc/realloc/qsort/bsearch，退出码 0；
  五份 exe 在新 crtstub 下均正常启动/输入/关闭；新增 `cui_scroll` 同样通过。
- **布局回归**：纯矩形自测覆盖 `UiStack` padding/gap/固定/Fill/Right/对齐、
  `UiFlex` basis+grow（50:80）、max 夹取重分、无 shrink 溢出；新增
  `Ui_FlexMeasure/Ui_FlexLayout` 的行/列黄金分配与宽度/高度扫描（`cui_layout_test`
  4239 断言：min 溢出、min~pref 压缩、grow 扩张、gap、交叉轴对齐与
  min/max、基线对齐、>32 项堆路径）；
  demo 表单迁移为 `UiStack`，按钮行及所有样例横向 flex 行改用 `Ui_FlexLayout`
  弹簧把主题按钮推到右侧；Markdown 表格列宽在总宽超出时按 `nat-min` 容量走协议
  压缩（`md_ui_test` 142 断言覆盖自然宽/压缩/极窄等分）；200% DPI、Toast/进度
  overlay 正常，≤32 项布局零堆分配。
- **Grid 回归**：`cui_grid_test` 74 断言覆盖黄金矩形、fixed/auto/fr、minmax、
  fr 权重与内容 min 基、span、隐式行/列、dense/sparse、压缩与 fixed 溢出、
  measure 换行、边界与 >24 项/单元格堆路径；`cui_grid.exe` 是 Grid + ScrollView
  的 20 卡片仪表盘，200% DPI 下 fixed 112 / auto(min 150/max 260) / 1fr(min 200)
  三列、跨 3 列标题、rowSpan=2 高卡、colSpan=2 宽卡、隐式行均正确，滚动/裁剪后
  dense 回填空洞、sparse 保留空洞；切换 dense、间距 12/6、明暗主题无残留或
  文字裁剪；点击卡片展开使 contentH 894 → 946、滚动上限同步变化。
- **ButtonGroup/MVVM 回归**：`cui_mvvm.exe` 的普通/单选/多选三组按钮融合显示；
  单选组切换全局主题，多选组更新 mask，普通组命令提交/重置/退出；Tab/方向键/
  Space 键盘导航正常；selected/mask/focus 全部在 `FormVM`，无隐藏状态。
- **按钮样式/每角圆角回归**：`cui_mvvm.exe` 样式条同时显示 SOLID(primary)、
  PLAIN(success)、TEXT(warning)、PLAIN+DASHED(error)、ROUND(default) 五种按钮，
  hover/press/disabled 状态与对比文字正常；ButtonGroup 首/中/尾段分别呈
  左圆-全直-右圆，选中色铺满到外轮廓且外边框不被覆盖；2 段/3 段/宽度不等
  三种布点均通过截图检查；每角路径几何在连续帧中复用缓存，未见新增长期缓存项。
- **ButtonGroup 选中边框/焦点环回归**：鼠标选中后不再出现蓝色“选中边框”，
  外框为完整 1px 统一色；选中段两侧分隔线省略；键盘 Tab/Home/方向键显示
  内缩 2 DIP 的焦点环，Space 切换后焦点环仍在，鼠标点击即隐藏；多选 A/B/C
  连续选中时填充无抗锯齿接缝，整组呈连续强调色；深浅两套主题均通过截图检查。
- **ScrollView 回归**：200% DPI 下 `cui_scroll.exe` 左侧 10,000 行列表只绘制
  16~17 行，滚轮/甩动后 offset 从 0 到 144/304，状态栏可见区间正确推进；
  拖动滚动条 thumb 可跳到 offset≈172482（5,073 行附近）；右侧 40 个变高卡片
  点击展开/收起后只重测该项，contentH/可见区间/thumb 长度同步更新；
  返回顶部按钮把 offset 平滑归零；“回弹”按钮使触摸配置的 var 视图出现
  offset=-17 的越界，随后弹簧回到 0；鼠标离开视口 1.6 秒后两个滚动条
  均淡出，静止 2 秒 CPU **0ms**；11 个 exe 全部 0CRT，原有 demo/minimal/mvvm
  启动/关闭回归通过。
- **ScrollView padding/滚动条槽回归**：样例设置 `{4,4,6,6}` 四向 padding 与
  `reserveSpace=1`；100%/200% DPI 下首行与左右边缘保持内边距，行尾
  “固定高度 34 DIP”不再被 thumb 遮挡，滚动条轨道落在预留槽内；
  `ContentWidth` 与 Begin 内部宽度一致，变高卡片 measure 无横向跳动。
- **ScrollView 缩高回滚回归**：在变高卡片视图滚动到底部 → 展开最后一张
  (#39) → 再滚到新底部 → 收起 #39（contentH 变小）后，offset 立即夹到
  新 `maxOffset`，thumb 回到合法底部，`Busy=0`，静止 CPU 0；随后滚轮
  （offset 4594→4294）和拖动 thumb 均可正常回滚；修复前会出现
  offset 略大于 max、速度/目标来回覆盖且 Busy 长期为 1 的 60Hz 空转。
- **按钮悬停可见性/disabled 回归**：`cui_tabs.exe` 顶部工具栏按钮由 PLAIN
  改为 SOLID 后，hover 呈明显灰/浅蓝底 + primary 混合描边；TabBar LINE 悬停
  增加 primary 调和的浅色底，不再只有文字变化；PILL/CARD 非活动项悬停为
  `primary.soft` 底 + primary alpha 描边，选中项仍保持白/面板底 + 蓝字；
  disabled 仍保持灰底灰字、无 hover/光晕。
- **光晕/disabled 回归**：Solid/Plain hover 光晕为预烘焙柔光纹理，一次
  `DrawBitmap`，无 3 圈描边带；同尺寸/颜色连续 hover 缓存命中，主题切换
  后按新角色色重建；`Ui_ButtonEx(..., enabled=0)` 不注册命中、不发光、
  返回 false；11 个 exe 0CRT。
- **TabBar/TabPager 回归**：`cui_tabs.exe` LINE/PILL/CARD 三种 header 正常；
  CARD 活动项上圆角、底边留空并与内容面板连通，`ActiveRect` 分段顶边 +
  `DrawActive` 覆盖后无 2px 双线；点击页签横向滑动 260ms，PILL 下选中胶囊
  切换；反向翻页方向翻转；纵向模式上下滑动；仅淡入模式旧页/新页交叉淡出；
  转场期间页面按钮不响应、header 仍可点击；Tab + 方向键 + Space 切换页并
  显示焦点环；转场结束 progress=100%、静止 CPU 0；`cui_tabs.exe` 为 0CRT。
- **图标组件回归**：`cui_icons.exe` 的按钮图标、Toast 图标、TabBar 图标
  与 40 DIP 页面大图标均正常；保存/刷新/删除按钮触发成功/信息/错误 Toast，
  Toast 按 kind 显示勾/信息/感叹号图标；TabBar 的 LINE/PILL/CARD 图标
  与文本一起居中；禁用按钮图标为 disabled 色；静止 CPU 0；`cui_icons`
  为 0CRT。
- **控件/Tooltip 回归**：`cui_controls.exe` 的 Checkbox（checked/
  indeterminate/disabled）、CheckboxGroup mask、RadioGroup、Switch、Slider
  点击/拖动/committed 正常；Group 内“开机启动”等项与上方单项 Checkbox 左边缘
  像素对齐（修复前少 1px）；勾选/取消约 140ms 淡入淡出，indeterminate→checked
  横线退、对勾进，RadioGroup 换项旧点淡出新点淡入；IconButton 悬停 450ms 显示 Tooltip，右侧翻转/
  夹取正确；Tab/方向键/空格导航、深色主题通过；静止 CPU 0；`cui_controls`
  为 0CRT。
- **弹层/Select/Menu 回归**：`cui_overlays.exe` 三个 Select 点击后在触发器
  下方展开，单选/多选选中项右侧显示勾，禁用城市灰显且不可选；多选点击
  南京后保持展开，触发器摘要变为“杭州, 南京 +1”，点击外部关闭；
  可搜索多选输入 `in` 后列表只剩 Rendering/Input，Backspace 回退为 `i`
  恢复 7 项，点击 Input 后摘要“UI, Input +1”，Enter 再切换当前高亮项，
  输入 `zz` 显示“无匹配项”；长列表滚轮 3 格偏移 288px（9 行），
  thumb 可拖动、轨道点击按 0.9 页翻页，选中项在打开时自动滚入可视区；
  下拉菜单方向键 Down×2 + Enter 提交“保存”；右键在鼠标点打开上下文菜单，
  分隔线/禁用项/危险项正确；Esc 与点击弹层外部均关闭并把焦点还给触发器；
  弹层打开时点击底层按钮不触发；初始主题 Select 显示 placeholder“选择主题”，
  展开时右侧矢量 chevron 在 ~60° 中间帧可见、180ms 后指向上方并转 primary 色，
  关闭后回落到向下/灰字；搜索框提示“搜索标签…”；下拉/菜单项 hover 在 30ms
  与 330ms 截图颜色一致（`Ui_StateInstant`，无淡变滞后）；深色主题下
  搜索框/列表/摘要正常；静止 CPU 0；`cui_overlays` 为 0CRT。
- **TextArea/动态容量回归**：`cui_textarea.exe` 的软换行、滚动条、拖选、
  只读/禁用外观正常；末尾追加 7000+ 字符（超过旧 4096 上限）无截断，
  只续排新字符；长段落中间插入 `QQ` 后 Ctrl+A/C 回读，长度 +2 且移除 `QQ`
  后与原文逐字一致；Backspace 回退后文本/行数一致；单独拖选含行尾空格的
  英文行可完整选中到 “dog.”，鼠标在行内移动时高亮不再跳行末；`UiEdit` 在
  maxLen=0 时接受 2550 字符不截断；静止 CPU 0；11 个 exe 0CRT。
- **多窗口/全局主题/模态回归**：`cui_multi.exe` 两个 HWND 各自持有
  Gfx/UiTheme/UiContext/UiProgress/UiToast；在主窗切主题，二级窗同步换肤并
  自动重绘（像素从浅色 245,248,254 变为深色 15,20,29）；二级窗独立 Toast
  不阻塞主窗；打开模态对话框后 `IsWindowEnabled` 对两个父窗均为 FALSE，
  对话框内切主题仍广播到父窗，关闭后父窗恢复可用；两窗口同时动画 CPU
  **0.00%**；关闭主窗后二级窗/对话框一并销毁。
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
- **DatePicker 四模式回归**：`cui_datepicker.exe` 200% DPI 真实鼠标/键盘验证：
  纯日期点选提交关闭/重开选中/today/清除/Esc/外部点击；日期+时间三滚动列
  （含秒）点日保持打开、确定提交；范围双联动月历两阶段 + 连续悬停预览带 +
  自动排序 + 近7天/近30天/本月预设；范围+时间只改活动端点（起点时间不变）。
  键入 `2026-13-45` 回车红框回退、`2026-11-20` 回车提交并跳月；键盘 Down 开、
  方向键焦点环（2 DIP 粗环、仅键盘可见）跨月翻月、Enter 点选；±30 天越界日格
  灰显且点击无效；小窗（客户区约 620 DIP）开 508 DIP 弹层走夹取盖住触发器但
  内容完整可操作；暗色主题四模式配色正常。触发器 hover 改命中归属后不再被
  弹层遮挡误亮。`ui_datetime` headless 1164 断言 0 失败；controls/textarea/
  scroll/overlays 冒烟回归干净；12 个 exe 0CRT。
- 无桌面环境（SSH/Session 0）不能目视验证，但编译、链接与 0CRT 判据可在 CI 跑。

---

## 21. WebView2 宿主（可选模块）

`src/ui/ui_webview.h` 提供纯 C 的 WebView2 宿主，能力对齐 DuiLib
`CWebview2UI`：脚本/对话框/WebMessage/错误页/拖放/右键菜单/加速键/状态栏/
缩放/DevTools 等设置，导航、脚本执行、宿主对象注入、虚拟主机、Cookie/
证书/权限/进程失败/WebResource 事件，以及原始 Environment/Controller/Core
COM 指针。页面消息双向：页面 `postMessage` 触发 `web_message`；Host 用
`UiWebView_PostWebMessageJson` / `...String`（对应
`PostWebMessageAsJson` / `PostWebMessageAsString`）。导航提供
`navigation_starting`（可取消）、`navigation_completed`（加载完成/失败）
与 `frame_navigation_completed`。宿主骨架事件：
`new_window`、`source_changed`、`history_changed`、`content_loading`、
`dom_content_loaded`、`document_title_changed`、`download_starting`、
`script_dialog`（有回调时自动关闭内置脚本对话框）。

用法：

```c
UiWebViewConfig cfg;
UiWebViewEvents ev;
UiWebViewConfig_Default(&cfg);
cfg.homepage = L"https://example.com";
ZeroMemory(&ev, sizeof(ev));
/* 填 created / navigation_completed / web_message ... */
wv = UiWebView_Create(app->gfx, Ui_Id("wv", 0, 0), &cfg, &ev);

/* 顶栏照常用 CUI 画；内容区每帧更新视觉矩形 */
Ui_WebView(wv, &app->ui, Ui_Rect(0, 54, app->w, app->h - 54));

/* 原始消息钩子: 鼠标/滚轮/最小化先给 WebView2 */
static int on_input(UiApp *app, UINT msg, WPARAM wp, LPARAM lp,
    LRESULT *result, void *user) {
    if (UiWebView_Input(wv, &app->ui, msg, wp, lp)) { *result = 0; return 1; }
    return 0;
}
```

要点与限制：

- DComp 可用时用 CompositionController + 最上层子视觉；ULW 回退为窗口模式。
- 需要 WebView2 Evergreen Runtime 与同目录 `WebView2Loader.dll`。
- 浏览器参数/GPU 开关在共享环境首次创建时生效；不提供 DuiLib 示例中
  关闭证书校验的 CORS 代理，证书策略由应用事件决定。
- 视觉托管下网页位于 CUI 根视觉之上，CUI Popup/Tooltip 不能覆盖网页。
- 详细设计与验证记录见 `docs/CUI-WebView2-设计方案.md`。

## 结语

CUI 的核心只有一句话：**每帧重新画、用稳定 ID 读交互、把动作推迟到帧结束**。
把这三件事做对，剩下的就是画矩形、写字和调动画曲线。

如果你准备把它接进真实产品，建议先按第 5 节的骨架跑通一个按钮，再逐项加编辑框、
输入法、动画和 DPI；每加一项，用 `src/demo.c` 对照行为。这样出问题时能快速定位是
“接线顺序”还是“绘制逻辑”。

祝接入顺利。
