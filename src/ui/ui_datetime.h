/*
 * ui_datetime.h - 民用日期/时间值 + 0CRT 日期数学 (无 CRT 时间函数)
 *
 * 以 day-number (距 1970-01-01 的天数, 可负) 做比较/加减/星期, 纯整数 O(1)。
 * civil 算法来自 Howard Hinnant (days_from_civil / civil_from_days)。
 * format/parse 与 UI 无关, 可脱离窗口 headless 测试。
 */
#ifndef CUI_UI_DATETIME_H
#define CUI_UI_DATETIME_H

typedef struct UiDateTime {
	int y;    /* 1..9999 */
	int m;    /* 1..12 */
	int d;    /* 1..31 */
	int hh;   /* 0..23 */
	int mm;   /* 0..59 */
	int ss;   /* 0..59 */
} UiDateTime;

int  UiDate_IsLeap(int y);
int  UiDate_DaysInMonth(int y, int m);
int  UiDate_DayNumber(int y, int m, int d);
void UiDate_FromDayNumber(int n, int *y, int *m, int *d);
int  UiDate_Weekday(int dayNumber);                  /* 0=日 .. 6=六 */
int  UiDate_Valid(const UiDateTime *v);
int  UiDate_Compare(const UiDateTime *a, const UiDateTime *b);     /* 全字段 */
int  UiDate_CompareDay(const UiDateTime *a, const UiDateTime *b);  /* 只比日期 */
void UiDate_AddMonths(UiDateTime *v, int months);    /* 日夹取到目标月长 */
void UiDate_AddDays(UiDateTime *v, int days);        /* 保留时分秒 */
void UiDate_Now(UiDateTime *out);                    /* GetLocalTime */

/* token: YYYY MM DD HH mm ss (定宽, 区分大小写: M=月 m=分); 其余字符原样。 */
void UiDate_Format(const UiDateTime *v, const char *pattern, char *out, int cap);
/* 按 pattern 解析; 数字段接受 1..width 位, 分隔符宽松。成功返回 1 并填 out
 * (需至少含年月日且通过 UiDate_Valid); 失败返回 0, 不改 out。 */
int  UiDate_Parse(const char *text, const char *pattern, UiDateTime *out);

#endif /* CUI_UI_DATETIME_H */