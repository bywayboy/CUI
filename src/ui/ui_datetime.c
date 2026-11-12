#include "ui/ui_datetime.h"
#include <windows.h>

int UiDate_IsLeap(int y)
{
	return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

int UiDate_DaysInMonth(int y, int m)
{
	static const int tab[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (m < 1 || m > 12) return 0;
	if (m == 2 && UiDate_IsLeap(y)) return 29;
	return tab[m - 1];
}

/* Hinnant civil 算法: 以 0000-03-01 为 era 起点, 719468 = 1970-01-01 的偏移。 */
int UiDate_DayNumber(int y, int m, int d)
{
	int era, yoe, doy, doe;
	y -= (m <= 2);
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = y - era * 400;
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

void UiDate_FromDayNumber(int n, int *y, int *m, int *d)
{
	int era, doe, yoe, doy, mp, dd, mm, yy;
	n += 719468;
	era = (n >= 0 ? n : n - 146096) / 146097;
	doe = n - era * 146097;
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	yy = yoe + era * 400;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	dd = doy - (153 * mp + 2) / 5 + 1;
	mm = mp + (mp < 10 ? 3 : -9);
	*y = yy + (mm <= 2);
	*m = mm;
	*d = dd;
}

int UiDate_Weekday(int dayNumber)
{
	/* 1970-01-01 (n=0) 是星期四; 星期日=0 → 偏移 +4。 */
	int w = (dayNumber + 4) % 7;
	return w < 0 ? w + 7 : w;
}

int UiDate_Valid(const UiDateTime *v)
{
	if (!v) return 0;
	if (v->y < 1 || v->y > 9999) return 0;
	if (v->m < 1 || v->m > 12) return 0;
	if (v->d < 1 || v->d > UiDate_DaysInMonth(v->y, v->m)) return 0;
	if (v->hh < 0 || v->hh > 23) return 0;
	if (v->mm < 0 || v->mm > 59) return 0;
	if (v->ss < 0 || v->ss > 59) return 0;
	return 1;
}

static int cmp3(int a, int b)
{
	return a < b ? -1 : (a > b ? 1 : 0);
}

int UiDate_CompareDay(const UiDateTime *a, const UiDateTime *b)
{
	int c = cmp3(a->y, b->y);
	if (c) return c;
	c = cmp3(a->m, b->m);
	if (c) return c;
	return cmp3(a->d, b->d);
}

int UiDate_Compare(const UiDateTime *a, const UiDateTime *b)
{
	int c = UiDate_CompareDay(a, b);
	if (c) return c;
	c = cmp3(a->hh, b->hh);
	if (c) return c;
	c = cmp3(a->mm, b->mm);
	if (c) return c;
	return cmp3(a->ss, b->ss);
}

void UiDate_AddMonths(UiDateTime *v, int months)
{
	int total, ny, nm, dim;
	if (!v) return;
	total = v->y * 12 + (v->m - 1) + months;
	ny = total / 12;
	nm = total % 12;
	if (nm < 0) { nm += 12; ny -= 1; }
	v->y = ny;
	v->m = nm + 1;
	dim = UiDate_DaysInMonth(v->y, v->m);
	if (v->d > dim) v->d = dim;
}

void UiDate_AddDays(UiDateTime *v, int days)
{
	int n;
	if (!v) return;
	n = UiDate_DayNumber(v->y, v->m, v->d) + days;
	UiDate_FromDayNumber(n, &v->y, &v->m, &v->d);
}

void UiDate_Now(UiDateTime *out)
{
	SYSTEMTIME st;
	if (!out) return;
	GetLocalTime(&st);
	out->y = st.wYear;
	out->m = st.wMonth;
	out->d = st.wDay;
	out->hh = st.wHour;
	out->mm = st.wMinute;
	out->ss = st.wSecond;
}

/* ── format ─────────────────────────────────────────────────────────── */

static void fmt_int(char *out, int cap, int *pos, int val, int width)
{
	char buf[16];
	int k = 0;
	if (val < 0) val = 0;
	do {
		buf[k++] = (char)('0' + val % 10);
		val /= 10;
	} while (val > 0 && k < 16);
	while (k < width && k < 16) buf[k++] = '0';
	while (k > 0 && *pos < cap - 1) out[(*pos)++] = buf[--k];
}

void UiDate_Format(const UiDateTime *v, const char *pattern, char *out, int cap)
{
	int pos = 0, i = 0;
	if (cap <= 0) return;
	out[0] = 0;
	if (!v || !pattern) return;
	while (pattern[i] && pos < cap - 1) {
		char c = pattern[i];
		if (c == 'Y' && pattern[i+1] == 'Y' && pattern[i+2] == 'Y' && pattern[i+3] == 'Y') {
			fmt_int(out, cap, &pos, v->y, 4); i += 4;
		} else if (c == 'M' && pattern[i+1] == 'M') {
			fmt_int(out, cap, &pos, v->m, 2); i += 2;
		} else if (c == 'D' && pattern[i+1] == 'D') {
			fmt_int(out, cap, &pos, v->d, 2); i += 2;
		} else if (c == 'H' && pattern[i+1] == 'H') {
			fmt_int(out, cap, &pos, v->hh, 2); i += 2;
		} else if (c == 'm' && pattern[i+1] == 'm') {
			fmt_int(out, cap, &pos, v->mm, 2); i += 2;
		} else if (c == 's' && pattern[i+1] == 's') {
			fmt_int(out, cap, &pos, v->ss, 2); i += 2;
		} else {
			out[pos++] = c; i++;
		}
	}
	out[pos] = 0;
}

/* ── parse ──────────────────────────────────────────────────────────── */

static int read_int(const char *s, int *j, int width, int *val)
{
	int k = 0, v = 0;
	while (k < width && s[*j] >= '0' && s[*j] <= '9') {
		v = v * 10 + (s[*j] - '0');
		(*j)++;
		k++;
	}
	if (k == 0) return 0;
	*val = v;
	return 1;
}

static int is_sep(char c)
{
	return c == '-' || c == '/' || c == ':' || c == ' ' || c == '.';
}

int UiDate_Parse(const char *text, const char *pattern, UiDateTime *out)
{
	UiDateTime v;
	int i = 0, j = 0, val;
	int gotY = 0, gotM = 0, gotD = 0;
	if (!text || !pattern || !out) return 0;
	v.y = 1; v.m = 1; v.d = 1; v.hh = 0; v.mm = 0; v.ss = 0;
	while (text[j] == ' ') j++;
	while (pattern[i]) {
		char c = pattern[i];
		if (c == 'Y' && pattern[i+1] == 'Y' && pattern[i+2] == 'Y' && pattern[i+3] == 'Y') {
			if (!read_int(text, &j, 4, &val)) return 0;
			v.y = val; gotY = 1; i += 4;
		} else if (c == 'M' && pattern[i+1] == 'M') {
			if (!read_int(text, &j, 2, &val)) return 0;
			v.m = val; gotM = 1; i += 2;
		} else if (c == 'D' && pattern[i+1] == 'D') {
			if (!read_int(text, &j, 2, &val)) return 0;
			v.d = val; gotD = 1; i += 2;
		} else if (c == 'H' && pattern[i+1] == 'H') {
			if (!read_int(text, &j, 2, &val)) return 0;
			v.hh = val; i += 2;
		} else if (c == 'm' && pattern[i+1] == 'm') {
			if (!read_int(text, &j, 2, &val)) return 0;
			v.mm = val; i += 2;
		} else if (c == 's' && pattern[i+1] == 's') {
			if (!read_int(text, &j, 2, &val)) return 0;
			v.ss = val; i += 2;
		} else {
			if (!text[j]) return 0;
			if (is_sep(c)) {
				if (text[j] >= '0' && text[j] <= '9') return 0;
				j++;
			} else {
				if (text[j] != c) return 0;
				j++;
			}
			i++;
		}
	}
	while (text[j] == ' ') j++;
	if (text[j]) return 0;
	if (!gotY || !gotM || !gotD) return 0;
	if (!UiDate_Valid(&v)) return 0;
	*out = v;
	return 1;
}