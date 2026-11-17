/*
 * gallery_icons.h - Segoe MDL2 Assets 官方图标档案 (画廊 Icon 页数据)
 *
 * 数据逐条提取自 Microsoft Learn 官方文档:
 *   "Segoe MDL2 Assets icons" - Segoe MDL2 Assets icons
 *   分区地址: https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-ui-symbol-font
 * 共 1386 个 glyph (码点唯一, 升序), 覆盖官方全部六个 PUA 分区:
 *   E700-E9xx / EA00-ECxx / ED00-EFxx / F000-F2xx / F300-F5xx / F600-F8xx
 * 名称保留官方档案原文 (PascalCase 英文描述名)。
 *
 * 注意: Windows 11 推荐改用 Segoe Fluent Icons 字体 (MDL2 仍内置可用)。
 */
#ifndef CUI_GALLERY_ICONS_H
#define CUI_GALLERY_ICONS_H

typedef struct GalleryIcon {
	unsigned int cp;      /* Unicode 码点 (PUA 私有区) */
	const char *name;     /* 官方描述名 (英文) */
} GalleryIcon;

/* vis[] 过滤索引数组的容量上限 (>= kMdl2IconCount; 1386 时留余量) */
#define GALLERY_ICON_MAX 1400

extern const GalleryIcon kMdl2Icons[];
extern const int kMdl2IconCount;

#endif /* CUI_GALLERY_ICONS_H */
