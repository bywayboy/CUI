# CUI 图片组件 UiImage 设计方案

> 状态：已实现（2026-11-13）
> 关联：`src/ui/ui_image.*`（新增）、`src/gfx/gfx.*`（新增 `Gfx_Ready`）、
> `src/net/ui_net.h`（HTTP 取图）、`samples/image.c`（新增）、
> `tests/net_server_http.ps1`（/img 测试图）、白皮书 §8.20

## 1. 目标

一个**应用持有**的图片组件，三种图形来源 + 可扩展提供者：

| 来源 | API | 行为 |
| --- | --- | --- |
| HTTP(S) URL | `UiImage_LoadUrl` | 有 `UiNet` 时异步请求；`UiNet=NULL` 报错 |
| 本地文件 | `UiImage_LoadFile` | 同步读文件 + 解码（小/中图） |
| 内存字节 | `UiImage_SetBytes` | 直接解码（PNG/JPEG/BMP/GIF 首帧…） |
| 自定义提供者 | `UiImage_SetProvider` | 返回已同步字节，或异步完成后调 `UiImage_SetBytes` |

显示能力：

- fit 模式：`CONTAIN`（完整）、`COVER`（填满裁剪）、`STRETCH`、`NONE`（居中原始尺寸）；
- 加载中画 **GPU 矢量 loading 动画**（圆环 + 旋转弧，D2D 每帧重绘，无 CPU 位图上传）；
- 完成后 180ms 透明度淡入；
- 失败画占位与错误态，可重试；
- Gfx 设备丢失（`Gfx_Generation` 变化）自动从保留的编码字节重建位图。

非目标（v1）：

- GIF/WebP 动画（只取首帧；WebP 需额外解码器）；
- 圆角裁剪（D2D 位图无圆角 clip；需要时用 Layer 预合成，后续）；
- EXIF 方向自动旋转、渐进式 JPEG、HDR/广色域；
- 在 IO 线程解码（v1 在 GUI 线程回调里解码，单图上限 32MB；大图后续可加后台解码队列）。

## 2. 状态机

```
IDLE --LoadUrl/LoadFile/SetBytes--> LOADING --解码成功--> READY --(180ms 淡入)--> READY
                                        \--失败---------> ERROR
READY/ERROR --Clear--> IDLE
LOADING --Clear/Cancel--> IDLE
```

`UiImage_Busy`：`LOADING` 或淡入未结束 → 1，应用在 `animating` 回调里返回它即可
持续出帧（spinner / 淡入）。

## 3. API

```c
typedef struct UiImage UiImage;          /* 不透明, App 持有指针 */

enum { UI_IMG_FIT_CONTAIN = 0, UI_IMG_FIT_COVER, UI_IMG_FIT_STRETCH, UI_IMG_FIT_NONE };
enum { UI_IMG_IDLE = 0, UI_IMG_LOADING, UI_IMG_READY, UI_IMG_ERROR };

/* 返回 1: *outBytes/*outLen 已同步给出 (组件负责 free)
 * 返回 0: 异步, 稍后调用 UiImage_SetBytes
 * 返回 -1: 失败 */
typedef int (*UiImageFetchFn)(void *user, const char *source,
                              void **outBytes, int *outLen);

UiImage *UiImage_Create(Gfx *gfx, UiNet *net /*可 NULL*/);
void UiImage_Destroy(UiImage *img);
void UiImage_SetContext(UiImage *img, UiContext *ui);  /* 可选: 自动失效 */
void UiImage_SetRect(UiImage *img, GfxRect rc);
void UiImage_SetFit(UiImage *img, int fit);       /* CONTAIN/COVER/STRETCH/NONE */
void UiImage_SetAnchor(UiImage *img, int anchor);  /* 3x3 取景锚点, 默认居中 */
void UiImage_SetBackground(UiImage *img, GfxColor color); /* 留白底色, 0=透明 */
void UiImage_SetProvider(UiImage *img, UiImageFetchFn fn, void *user);

int  UiImage_LoadUrl(UiImage *img, const char *url);
int  UiImage_LoadFile(UiImage *img, const wchar_t *path);
int  UiImage_SetBytes(UiImage *img, const void *bytes, int len);
void UiImage_Clear(UiImage *img);

int  UiImage_State(const UiImage *img);
int  UiImage_LastError(const UiImage *img);
int  UiImage_Busy(const UiImage *img);
const char *UiImage_StrError(int err);

void UiImage_Draw(UiImage *img, const UiTheme *theme);
```

错误码：复用 `UiNetErr`（网络/HTTP/TLS/超时/取消），本地/解码用负值：
`UI_IMG_E_DECODE = -1`、`UI_IMG_E_FILE = -2`、`UI_IMG_E_OOM = -3`、
`UI_IMG_E_PARAM = -4`。

## 4. 解码（WIC）

- `CoCreateInstance(CLSID_WICImagingFactory)`（`UiApp_Run` 已 `CoInitializeEx`），
  工厂随 `UiImage_Init` 创建、`Shutdown` 释放。
- 内存：`IWICStream::InitializeFromMemory` → `CreateDecoderFromStream` →
  `GetFrame(0)` → `IWICFormatConverter` 转到
  `GUID_WICPixelFormat32bppPBGRA`（与 `Gfx_CreateBitmap` 的预乘 BGRA 完全一致）→
  `CopyPixels` 到临时缓冲 → `Gfx_CreateBitmap` → 释放临时缓冲。
- 文件：先 `CreateFileW/ReadFile` 读入编码字节（上限 32MB），再走内存解码；
  编码字节保留用于设备丢失重建。
- 解码失败/超限 → `ERROR`，不崩溃。

## 5. 绘制

- READY：按 fit 计算 dst；**留白**（CONTAIN/NONE）用 `SetBackground` 铺底，
  **剪裁**（COVER）用 `Gfx_PushClip(rc)`；`SetAnchor` 选 3×3 取景位置
  （留白时决定余量分布，剪裁时决定裁掉哪一边）；淡入用
  `Gfx_PushAlpha(alpha)` + `Gfx_DrawBitmap`。
- LOADING：先铺一层面板底色（`theme->frame` / `card` 半透明），中心画
  **GPU loading 动画**：静态圆环（`Gfx_StrokeEllipse`，`theme->border`）+ 旋转
  100° 圆头弧（`Gfx_StrokeArc`，`theme->blue`），角度 = `UiFrame_Now()*0.18`。
  全部是 D2D 矢量，GPU 每帧重绘，无纹理上传。
- ERROR/IDLE：圆角占位 + 虚线边框 + 居中提示文字（`theme->fSmall`）。
- 设备丢失：`Gfx_Generation(gfx)` 与位图生成号不一致时，用保留的编码字节
  就地重建（`Gfx_CreateBitmap` 允许在帧内创建，光晕缓存已是此模式）。

## 6. 线程与生命周期

- 所有 API/回调都在 GUI 线程（`UiHttp` 回调本身归位到 GUI）；
- `UiImage_Shutdown` 会 `UiHttp_Cancel` 未完成请求，之后不再回调；
- 位图归 `UiImage` 所有，`Shutdown` 释放；`Gfx` 必须先于 `UiImage` 存活；
- 设备重建不需要应用干预。

## 7. 验证判据

| 项 | 判据 |
| --- | --- |
| URL | 本机 `/img`（延迟 900ms）显示 spinner，随后 480×300 PNG 淡入 |
| 文件 | 生成的 BMP 文件同步加载成功 |
| 内存 | 生成的 BMP 字节 `SetBytes` 成功 |
| fit | CONTAIN/COVER/STRETCH/NONE 四模式截图 |
| 错误 | 404 / 非图片字节 → ERROR 占位，可重试 |
| 设备 | 改变窗口尺寸触发重建后图片仍在（路径不变） |
| 构建 | `cui_image.exe` 0CRT，含 `windowscodecs.lib` |

## 8. 实现修订与验证记录（2026-11-13）

实现中暴露并修正的四个问题（都影响"异步完成后的可见性"）：

1. **首帧前解码**：`on_init` 里加载图片时 Gfx 绘制表面尚未创建，`Gfx_CreateBitmap`
   必然失败。新增 `Gfx_Ready()`；组件在表面未就绪时保持 `LOADING` 并置
   `needDecode`，首帧绘制时补解码（`UiImage_Draw` 内完成）。
2. **异步完成没有重绘**：`UiImage` 由网络回调改状态后，应用没有失效对应矩形，
   DComp 脏区重绘下画面停在旧帧。新增 `UiContextInvalidate(ctx, rc)`（状态变化时
   立即失效）与 `Ui_AnimDirtyRect(ctx, rc)`（逐帧动画登记脏区）。
3. **`WM_PAINT` 内直接 `InvalidateRect` 会卡死消息循环**：spinner 每帧失效会让
   `PeekMessage` 循环永远排不空，GUI 线程不再进入 alertable 等待，网络 APC 被饿死
   （表现为 HTTP 图片永远 loading）。逐帧动画改用动画脏区，由 `Ui_AnimTakeDirty`
   在帧循环里统一失效。
4. **动画脏区被 Advance 清掉**：`Ui_AnimAdvance` 原来每帧清 `animDirty`，而组件是在
   上一帧绘制时登记，导致逐帧自绘丢帧。改为只由 `Ui_AnimTakeDirty` 消费。
5. **帧超时仍要排空 APC**：`UiFrame_Delay` 在 ms ≤ 0.5 时直接返回，满帧动画下
   GUI 线程可能长时间不进入 alertable 等待；改为 `SleepEx(0, TRUE)` 零等待排空。

验证结果：

| 项 | 结果 |
| --- | --- |
| URL | Bing 每日图：`HPImageArchive.aspx` JSON → 解析 `url` → HTTPS 加载 1920×1080 JPEG，spinner → 180ms 淡入（需联网）；离线可用 tests 的 `/img` |
| 文件/内存 | `on_init` 首帧前加载走延迟解码，首帧后显示 320×200 BMP |
| 重载 | 三源同时重载，URL 重新走 spinner → 淡入，不再停在旧帧 |
| fit | CONTAIN 留白居中、COVER 剪裁填满、STRETCH 拉伸；NONE 原始尺寸（仅缩略窗口可见）；9 个锚点逐一切换，留白余量/裁剪方向随锚点移动 |
| 底色 | `SetBackground(0xFFE9EFF7)` 留白区可见；alpha=0 保持透明 |
| 清除 | 回到占位态（"图片"） |
| 构建 | `cui_image.exe` 0CRT（`windowscodecs.lib`），14→15 个 exe |
