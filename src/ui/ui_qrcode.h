/*
 * ui_qrcode.h - 二维码组件 (Byte 模式, 版本 1-40 自动, ECC L/M/Q/H)
 *
 * 编码: UTF-8 原始字节直接入码, 8 种掩码按标准惩罚规则自动选择.
 * 绘制: 位图纹理按设备像素生成, 与目标 1:1 映射避免线性插值糊边;
 *       设备丢失或 DPI 变化时用缓存的矩阵重建, 不重新编码.
 *
 * 用法:
 *   UiQrCode_Init(&q, UI_QR_ECC_M);
 *   UiQrCode_SetText(&q, url);          // 内容未变不重编码
 *   UiQrCode_SetLogoSrc(&q, "logo.png"); // 可选: 中央 Logo (支持透明 PNG)
 *   UiQrCode_SetLogoRatio(&q, 0.2f);     // Logo 占码区边长比例
 *   UiQrCode_Draw(&q, &app->ui, rc);    // 每帧
 *   UiQrCode_Shutdown(&q);
 */
#ifndef CUI_UI_QRCODE_H
#define CUI_UI_QRCODE_H

#include "ui/ui_context.h"

#define UI_QR_MAX_VERSION   40
#define UI_QR_MODULES_MAX   177
#define UI_QR_TEXT_MAX      2953
#define UI_QR_MATRIX_BYTES  ((UI_QR_MODULES_MAX * UI_QR_MODULES_MAX + 7) / 8)
#define UI_QR_PAD_DEFAULT   1.0f   /* 码区到组件四边的留白 (模块), 背景色填充 */
#define UI_QR_PAD_MAX       8.0f
#define UI_QR_BITMAP_MAX    1024   /* 缓存纹理最长边 (设备像素) */
#define UI_QR_LOGO_MAX      260    /* Logo 路径 (UTF-8) */
#define UI_QR_LOGO_BITMAP_MAX 512  /* Logo 解码最长边 (像素) */
#define UI_QR_LOGO_RATIO_DEFAULT 0.2f
#define UI_QR_LOGO_RATIO_MAX     0.35f

enum {
	UI_QR_ECC_L = 0,
	UI_QR_ECC_M,
	UI_QR_ECC_Q,
	UI_QR_ECC_H
};

enum {
	UI_QR_OK = 0,
	UI_QR_E_EMPTY,   /* 空内容 */
	UI_QR_E_LONG,    /* 超出当前纠错等级容量 */
	UI_QR_E_PARAM,   /* 非法参数/缓冲区不足 */
	UI_QR_E_OOM
};

typedef struct UiQrCode {
	char *text;              /* 内容副本 (堆) */
	int textLen;
	int ecc;
	uint32_t fg, bg;         /* 0xAARRGGBB, 支持 alpha */
	unsigned char *matrix;   /* 位打包矩阵, 行优先 MSB */
	int size;                /* 每边模块数 (不含静区) */
	int version;
	int error;               /* UI_QR_* */
	int rev;                 /* 内容/样式版本: 纹理缓存键 */
	int dirty;
	GfxBitmap *bmp;
	Gfx *gfx;                /* 最近一次绘制设备 (Shutdown 释放纹理) */
	int bmpPx;               /* 纹理边长 (设备像素) */
	int bmpN;                /* 含静区总模块数 */
	int bmpRev;
	int bmpGen;              /* Gfx_DeviceGeneration */
	char logoSrc[UI_QR_LOGO_MAX]; /* 中央 Logo 图片 (本地文件, UTF-8) */
	float logoRatio;         /* Logo 边长 / 码区边长 (不含静区) */
	float padding;           /* 绘制区域到组件四边留白 (模块) */
	GfxBitmap *logoBmp;
	int logoW, logoH;
	int logoGen;             /* Logo 纹理的设备代际 */
	int logoState;           /* 0=未加载 1=就绪 -1=失败 */
	int logoSrcDirty;        /* 路径变更后待重载 */
	Gfx *logoGfx;
} UiQrCode;

/* ── 编码 ───────────────────────────────────────────────────────────── */

/* 编码到调用方缓冲区; matrix 位打包 (比特序号 y*size+x, MSB 优先).
 * matrixCap 单位字节; 成功写入 *outSize 并返回 UI_QR_OK. */
int UiQrCode_Encode(const char *utf8, int ecc, unsigned char *matrix,
	int matrixCap, int *outSize);

static __inline int UiQrCode_Bit(const unsigned char *matrix, int size, int x,
	int y)
{
	int i = y * size + x;
	return (matrix[i >> 3] >> (7 - (i & 7))) & 1;
}

/* ── 组件 ───────────────────────────────────────────────────────────── */

void UiQrCode_Init(UiQrCode *q, int ecc);
void UiQrCode_Shutdown(UiQrCode *q);
void UiQrCode_SetEcc(UiQrCode *q, int ecc);
void UiQrCode_SetColors(UiQrCode *q, uint32_t fg, uint32_t bg);
/* 返回 1 = 可绘制; 0 = 空/超长/内存不足 */
int  UiQrCode_SetText(UiQrCode *q, const char *utf8);
/* 中央 Logo: 本地图片路径 (UTF-8). NULL/"" 关闭. 首次绘制时解码. */
void UiQrCode_SetLogoSrc(UiQrCode *q, const char *utf8Path);
/* Logo 边长 / 码区边长 (不含静区), 钳到 [0, UI_QR_LOGO_RATIO_MAX]. */
void UiQrCode_SetLogoRatio(UiQrCode *q, float ratio);
/* 绘制区域到组件四边的留白, 单位=模块, 钳到 [0, UI_QR_PAD_MAX]. */
void UiQrCode_SetPadding(UiQrCode *q, float modules);
int  UiQrCode_Error(const UiQrCode *q);
int  UiQrCode_Version(const UiQrCode *q);
void UiQrCode_Draw(UiQrCode *q, UiContext *ui, UiRect rc);

#endif /* CUI_UI_QRCODE_H */