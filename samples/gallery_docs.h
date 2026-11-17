/*
 * gallery_docs.h - 组件画廊的 Markdown 文档文本
 *
 * gallery.c 按页码取回 "说明文档 / 帮助 API / 事件连接 / 最佳实践" 四段 Markdown,
 * 用内置的 UiMarkdown 组件渲染。全部为 UTF-8 常量字符串。
 */
#ifndef CUI_GALLERY_DOCS_H
#define CUI_GALLERY_DOCS_H

/* 页面顺序: gallery.c 的页面表与本文档表共用同一顺序。 */
enum {
	PG_WELCOME = 0,
	PG_BUTTON,
	PG_BGROUP,
	PG_ICON,
	PG_INPUT,
	PG_TEXTAREA,
	PG_CHECKBOX,
	PG_RADIO,
	PG_SWITCH,
	PG_SLIDER,
	PG_SELECT,
	PG_DATEPICKER,
	PG_TABS,
	PG_NAV,
	PG_PAGINATION,
	PG_SCROLL,
	PG_LAYOUT,
	PG_DIALOG,
	PG_MENU,
	PG_TOAST,
	PG_TOOLTIP,
	PG_PROGRESS,
	PG_IMAGE,
	PG_MARKDOWN,
	PG_THEME,
	PG_NET,
	PG_WS,
	PG_WSSRV,
	PG_APP,
	PG_COUNT
};

typedef struct GalleryDocText {
	const char *intro;   /* 欢迎/导览页正文 (仅 PG_WELCOME 使用) */
	const char *doc;     /* 说明文档 */
	const char *api;     /* 帮助 API */
	const char *best;    /* 最佳实践 */
	const char *events;  /* 事件连接方法: 事件模型 + 接线代码示例 */
} GalleryDocText;

/* 页码越界返回 NULL; 未编写文档的页字段为 NULL, 调用方回退到占位文本。 */
const GalleryDocText *GalleryDoc_Get(int page);

#endif /* CUI_GALLERY_DOCS_H */
