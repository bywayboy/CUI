# CUI WebView2 组件设计方案

目标：在 CUI 中提供与 DuiLib `CWebview2UI` 同级的 WebView2 宿主能力，
保持 `src/ui` 纯 C、0CRT、无第三方运行时依赖（只动态加载官方
`WebView2Loader.dll`）。

## 1. 约束与边界

- **纯 C / 0CRT**：`src/ui/ui_webview.c` 只用 Win32 + 手写 COM vtable；
  不使用 C++/WRL/wil/STL/new/delete/异常，不链接 import lib 或 CRT。
- **可选模块**：`ui_webview.obj` 只链接进 `cui_webview.exe`；核心库与
  其它样例的依赖关系不变。
- **外部依赖**：WebView2 Evergreen Runtime（系统级）与
  `WebView2Loader.dll`（随 exe 放在同目录）。SDK 头文件已 vendor 到
  `third_party/webview2/`，不参与运行期分发。
- **安全**：不复制 DuiLib 示例里"禁用证书校验的 CORS 代理"。证书错误通过
  事件交给应用；不回调时使用 WebView2 默认行为（阻止并显示错误页）。

## 2. 生命周期

```
UiWebView_Create
  └─ LoadLibrary("WebView2Loader.dll")
     └─ 共享环境（进程内一次）
        └─ 等 gfx 首帧决定后端（DComp / ULW）
           ├─ DComp : CreateCoreWebView2CompositionController
           │          + CreateVisual + put_RootVisualTarget
           └─ ULW   : CreateCoreWebView2Controller（窗口模式）
```

- 环境在进程内共享；首个控件发起创建，其余控件进入等待队列。环境使用者
  归零时释放。浏览器参数通过进程级
  `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS` 传给共享环境（因此只对首次创建
  生效），并设置 `COREWEBVIEW2_FORCED_HOSTING_MODE=WINDOW_TO_VISUAL`。
- 默认缓存目录 `%LOCALAPPDATA%\CUI\WebView2`。
- **不能**在环境就绪时立刻建 controller：此时 gfx 可能还没决定 DComp/ULW。
  `Ui_WebView()` 每帧查询 `Gfx_IntRenderMode()`，后端确定后再创建，避免
  DComp 窗口误入窗口模式。

## 3. 视觉托管与坐标

- CUI 的 DComp 根视觉是 `Gfx` 的 swapchain 内容；WebView2 视觉作为其
  **最上层子视觉**插入。
- `Gfx_IntCreateChildVisual/UpdateChildVisual` 接收 DIP 矩形，内部乘
  `gfx->scale`：DComp 的 visual offset/clip 是物理像素。
- CompositionController：
  - `put_Bounds` 使用 **物理像素**（`rc.w*scale, rc.h*scale`）；
  - `put_RasterizationScale(scale)`；
  - 输出视觉尺寸 = Bounds / RasterizationScale，与 clip 一致。
- 窗口模式回退：Bounds 为宿主客户区物理像素的绝对矩形；WebView2 自建子
  HWND，CUI 不再转发鼠标。
- 最小化/隐藏：`WM_SIZE(SIZE_MINIMIZED)` 与 `WM_SHOWWINDOW` 通过
  `put_IsVisible(FALSE)` + 视觉 clip 归零冻结渲染；还原时恢复。
  应用也可用 `UiWebView_SetVisible`。

## 4. 输入模型

- `UiAppCallbacks.input` 是壳的原始消息钩子，在 CUI 默认派发前调用。
  样例把消息转给 `UiWebView_Input`。
- 视觉托管下鼠标/滚轮由 `ICoreWebView2CompositionController::SendMouseInput`
  转发；坐标是 **WebView 左上角原点的物理像素**（与 DuiLib 一致）。
  `WM_MOUSEMOVE` 不消费，CUI 仍可更新 hover；按键/滚轮消费。
- 按下时 `SetCapture` 宿主窗口并 `MoveFocus(PROGRAMMATIC)`；抬起释放。
- 键盘：移动焦点后由 WebView2 通过宿主 HWND 的消息钩子处理，CUI 不需要
  转发 `WM_KEYDOWN/WM_CHAR`（实测页面输入框可正常键入）。
- 光标：`WM_SETCURSOR` 在 WebView2 区域内使用
  `ICoreWebView2CompositionController::get_Cursor`，区域外交回 CUI。
- 最小化消息在 `UiWebView_Input` 内部处理并返回 0（不消费）。

## 5. 公共 API / 事件（与 DuiLib 对齐）

`src/ui/ui_webview.h`：

| DuiLib | CUI |
| --- | --- |
| 属性 `url` / `SetHomepage` | `UiWebViewConfig.homepage` / `UiWebView_SetHomepage` |
| 脚本 / 对话框 / WebMessage / 错误页 / 拖放 / 右键菜单 / 加速键 / 状态栏 / 缩放 / DevTools | `UI_WVF_*` 与 `UiWebView_SetSetting` |
| `SetVirtualHostNameToFolderMapping` | `UiWebView_SetVirtualHost` |
| `AddScriptToExecuteOnDocumentCreated` | `UiWebView_AddScriptOnDocumentCreated` |
| `ExecuteScript` | `UiWebView_ExecuteScript`（完成回调） |
| `InjectObjectToScript` | `UiWebView_AddHostObject`（`IDispatch*`，NULL 移除） |
| `Navigate` / `Reload` / `GotoHomePage` | `UiWebView_Navigate` / `Reload` / `GoHome` |
| WebMessage | `UiWebView_PostWebMessageJson` / `...String` + `web_message` 事件 |
| `GetGetWebView2Env` / Controller / Core | `UiWebView_Environment` / `Controller` / `Core` / `CompositionController` |

事件：`created`、`create_failed`、`navigation_starting(cancel)`、
`navigation_completed`、`frame_navigation_completed`、`web_message`、
`process_failed`、`permission_requested(allow)`、`certificate_error(allow)`、
`web_resource_requested`（原始 COM，由应用完全控制）、`focus_changed`、
`accelerator_key(handled)`。

P0 宿主事件（对齐常见 WebView2 宿主骨架）：

- `new_window`：`target=_blank`/`window.open`，`handled=1` 后宿主决定当前视图
  或新窗口（原始 args 可设置 NewWindow）；
- `source_changed` / `history_changed`：地址栏、前进/后退状态；配套
  `UiWebView_CanGoBack/CanGoForward/GoBack/GoForward`；
- `content_loading` / `dom_content_loaded`：加载进度与 DOM 就绪；
- `document_title_changed`：窗口/Tab 标题；
- `download_starting`：可取消或接管保存路径（原始 args）；
- `script_dialog`：alert/confirm/prompt/beforeunload。**设置该回调时组件自动
  关闭 WebView2 内置对话框**，由宿主绘制 UI；`handled=1` 时按 `accept` 调用
  `Accept`，prompt 的 `result_text` 作为返回值。

## 6. 与 DuiLib 的差异

- DuiLib 的 WebResourceRequested CORS 代理会关闭证书校验；CUI 只暴露原始
  COM 参数，代理策略由应用实现并自行校验证书。
- 浏览器参数是进程级共享环境参数；同一进程内多控件不能各用不同参数。
- 视觉托管下 WebView2 位于 CUI 根视觉最上层，CUI 的 Popup/Tooltip 不能
  覆盖到网页之上（需要网页内 UI 或另建顶层窗口）。
- ULW 回退使用窗口模式，网页内容由 WebView2 子 HWND 呈现；不依赖 DComp。

## 7. 样例与验证

`samples/webview.c` + `samples/webview_test.html`：

- 顶栏 CUI 立即模式按钮（主页/刷新/退出），内容区 WebView2；
- 虚拟主机 `https://cui.local/webview_test.html` 映射到 exe 目录；
- 页面提供按钮、输入框与 `chrome.webview.postMessage`。

验证记录（Windows 10 22H2 / 200% DPI）：

- DComp 视觉托管：页面铺满内容区；窗口 resize、最小化/还原正常；
- 鼠标点击按钮、滚轮滚动、输入框键入与 Enter 均生效；
- ULW 回退：窗口模式页面正常显示、点击/键入/退出正常，无挂起；
- P0 事件：标题变化、新窗口请求、下载拦截、alert/confirm/prompt 自定义处理
  均验证通过；底栏计数确认 SourceChanged / HistoryChanged / ContentLoading /
  DOMContentLoaded 均触发；
- `dumpbin` 检查 `cui_webview.exe` 无 CRT 导入。
