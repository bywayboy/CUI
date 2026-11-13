/*
 * ui_image.h - 图片组件: URL / 本地文件 / 内存字节 / 自定义 provider
 *
 * 所有 API 与回调都在 GUI 线程。解码用 WIC, 绘制用 Gfx 位图 (预乘 BGRA)。
 * 加载中由组件画 GPU 矢量 loading 动画; 应用在 animating 回调里返回
 * UiImage_Busy() 即可持续出帧。设备丢失 (Gfx_Generation 变化) 自动重建。
 *
 * 设计见 docs/CUI-图片-设计方案.md
 */
#ifndef CUI_UI_IMAGE_H
#define CUI_UI_IMAGE_H

#include <windows.h>
#include "gfx/gfx.h"
#include "net/ui_net.h"
#include "ui/ui_context.h"
#include "ui/ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UiImage UiImage;

enum {
	UI_IMG_FIT_CONTAIN = 0,   /* 留白缩放: 等比完整显示 (默认) */
	UI_IMG_FIT_COVER,         /* 剪裁缩放: 等比填满, 超出部分裁掉 */
	UI_IMG_FIT_STRETCH,       /* 拉伸到矩形 (不保持比例) */
	UI_IMG_FIT_NONE           /* 原始尺寸, 不缩放 */
};

/* 对齐锚点: 留白/裁剪的取景位置 (3x3) */
enum {
	UI_IMG_ANCHOR_TL = 0, UI_IMG_ANCHOR_TC, UI_IMG_ANCHOR_TR,
	UI_IMG_ANCHOR_ML,     UI_IMG_ANCHOR_MC, UI_IMG_ANCHOR_MR,
	UI_IMG_ANCHOR_BL,     UI_IMG_ANCHOR_BC, UI_IMG_ANCHOR_BR
};

enum {
	UI_IMG_IDLE = 0,
	UI_IMG_LOADING,
	UI_IMG_READY,
	UI_IMG_ERROR
};

/* 本地/解码错误 (网络错误复用 UiNetErr, 为正值) */
enum {
	UI_IMG_E_DECODE = -1,
	UI_IMG_E_FILE = -2,
	UI_IMG_E_OOM = -3,
	UI_IMG_E_PARAM = -4
};

/* 返回 1: 已同步给出字节 (组件 free() 释放); 0: 异步, 稍后 UiImage_SetBytes;
 * -1: 失败 */
typedef int (*UiImageFetchFn)(void *user, const char *source,
	void **outBytes, int *outLen);

UiImage *UiImage_Create(Gfx *gfx, UiNet *net /* 可 NULL */);
void UiImage_Destroy(UiImage *img);

/* 可选: 绑定 UiContext 后, 加载完成/失败与 loading 动画会自动失效自身矩形;
 * 不绑定则需应用自行逐帧重绘。 */
void UiImage_SetContext(UiImage *img, UiContext *ui);

void UiImage_SetRect(UiImage *img, GfxRect rc);
void UiImage_SetFit(UiImage *img, int fit);
/* 留白/裁剪的取景锚点, 默认 UI_IMG_ANCHOR_MC (居中) */
void UiImage_SetAnchor(UiImage *img, int anchor);
/* 留白底色 (0xAARRGGBB, alpha=0 表示不铺底), 默认透明 */
void UiImage_SetBackground(UiImage *img, GfxColor color);
void UiImage_SetProvider(UiImage *img, UiImageFetchFn fn, void *user);

int  UiImage_LoadUrl(UiImage *img, const char *url);
int  UiImage_LoadFile(UiImage *img, const wchar_t *path);
int  UiImage_SetBytes(UiImage *img, const void *bytes, int len);
void UiImage_Clear(UiImage *img);

int UiImage_State(const UiImage *img);
int UiImage_LastError(const UiImage *img);
int UiImage_Busy(const UiImage *img);
const char *UiImage_StrError(int err);

/* 必须在 Ui_BeginFrame/EndFrame 之间调用 */
void UiImage_Draw(UiImage *img, const UiTheme *theme);

#ifdef __cplusplus
}
#endif

#endif /* CUI_UI_IMAGE_H */
