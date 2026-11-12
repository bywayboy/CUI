# CUI 窗口最小化 / 还原：保留 DWM 原生动画

> 目标：任务栏按钮点击可以最小化 / 还原窗口，且过渡动画与系统原生窗口
> 完全一致；不出现系统标题栏，圆角 / 阴影 / 标题栏仍全自绘。
>
> 实现状态：已落地，仅窗口样式修正（`src/ui/ui_app.c` 的
> `CreateWindowExW`），无自定义动画代码。
> 决策：采用"保留 DWM 所需样式 + `WM_NCCALCSIZE` 压掉非客户区"路线；
> 合成器自绘动画（DComp visual 缩放 + 淡变）与 WAM 路线均不采用。

## 1. 实测：三个样式位各自决定什么

Win10 19045 / 200% DPI 真机，`cui_overlays.exe`，真实鼠标点击任务栏 +
窗口消息日志（临时 `SC_LOG_FILE` 插桩）+ 屏幕逐帧采样（A/B 对照）：

| 样式 | 任务栏点击 | 最小化 / 还原过渡 |
| --- | --- | --- |
| `WS_POPUP \| WS_THICKFRAME`（原状） | 只收到 `WM_ACTIVATE(0)`，**无 `SC_MINIMIZE`**，窗口不最小化且丢前台 | 无 |
| 补 `WS_MINIMIZEBOX \| WS_MAXIMIZEBOX \| WS_SYSMENU` | 34ms 内 `IsIconic=TRUE`，命令路由恢复 | **无**：整窗瞬间消失 / 瞬间出现（还原方向 1000ms 内仅 2 个屏幕状态） |
| 再补 `WS_CAPTION` | 正常 | **DWM 原生动画恢复**：最小化朝任务栏缩小淡出、还原放大淡入，连续多帧 |

结论：

1. **任务栏是否下发 `SC_MINIMIZE` 取决于 `WS_MINIMIZEBOX` / `WS_SYSMENU`。**
   与自绘标题栏无关；缺样式时 Explorer 只做激活切换。
2. **DWM 是否播放最小化 / 还原过渡取决于 `WS_CAPTION`。**
   此前"DComp + `WS_EX_NOREDIRECTIONBITMAP` 没有 DWM 动画"的判断是错的：
   那次 A/B 只补了 boxes 没补 caption。补上 caption 后，同一条 DComp
   直出路径（无重定向位图）依然拿到原生过渡——DWM 对带 caption 的窗口
   自行快照做缩放，不依赖应用的重定向位图。
3. 因此不需要任何应用侧动画代码：保留样式位，把非客户区压没即可。

## 2. 落地方式

```c
hwnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP,
    L"CuiAppWnd", title,
    WS_POPUP | WS_THICKFRAME | WS_CAPTION |
        WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
    0, 0, 0, 0, NULL, NULL, inst, app);
```

- `WM_NCCALCSIZE(wp=TRUE)` 仍返回 0：客户区铺满整个窗口，系统 caption /
  边框不占一个像素，也不绘制（实测窗口顶部截图只有自绘内容）。
  圆角、阴影、标题栏、关闭键继续由应用自绘。
- `WS_EX_NOREDIRECTIONBITMAP` 保留：DComp 零拷贝直出路径不变，性能基线不变。
- 附带恢复的标准行为：标题栏（`HTCAPTION`）双击最大化 / 还原、
  Alt+Space 系统菜单、任务栏右键菜单的最小化 / 最大化项。
- 最小化 / 还原的消息路径回到系统默认：`WM_SYSCOMMAND` 不做拦截，
  `WM_SIZE` 只维护 `app->minimized` 与复位帧时钟；动画期间壳不请求帧，
  DWM 在合成器侧完成过渡，应用 CPU 为 0。

## 3. 边界与不做的事

- 低层手写窗口（`samples/advanced.c` / `multiwindow.c` 这类不走 UiApp 的
  演示）要同样的行为需自行带上述样式位；壳不代管。
- 不做合成器自绘动画：评审否决——无论曲线怎么调都不是 DWM 原生观感
  （任务栏缩略图联动、多显示器 DPI 过渡、与系统设置"动画效果"开关联动
  等都拿不到）。曾实现的 `Gfx_SetWindowVisual` + 壳状态机已整体回退。
- 不引入 Windows Animation Manager：原生动画已由 DWM 提供，WAM 只在
  完全自定义转场时才需要，本期无此需求。
- 最大化 / 还原最大化的过渡同样由 DWM 接管（带 caption 后一并恢复），
  壳不做额外处理。

## 4. 验证

- 真机真实点击任务栏：最小化 / 还原各采到连续过渡帧（缩小淡出 /
  放大淡入），与系统窗口观感一致；`IsIconic` 边沿正常。
- 加 `WS_CAPTION` 前后窗口顶部 / 内容截图对比：无系统非客户区残留。
- 回归：overlays 全交互脚本、141 条 headless 断言、首开延迟复测
  66–82ms（与基线一致）、动画结束后静态 0ms CPU/1.5s。