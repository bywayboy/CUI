# CUI 日期选择 DatePicker 设计方案

> 状态：设计中（待实施）
> 关联：`src/ui/ui_datetime.*`（新增）、`src/ui/ui_datepicker.*`（新增）、
> `samples/datepicker.c`（新增）、复用 `ui_popup.*` / `ui_scroll.*` / `edit.*` /
> `ui_ime.*` / `ui_anim.*` / `ui_icon.*`、白皮书 DatePicker 章节

## 1. 目标

在 `UiSelect` 之后补一个**应用持有的日期选择控件**，一套 manager 收敛四种模式：

| 模式 | 含义 | 面板 |
| --- | --- | --- |
| `UI_DP_DATE` | 纯日期 | 1 月历 |
| `UI_DP_DATETIME` | 日期 + 时间 | 1 月历 + 时间列 |
| `UI_DP_RANGE_DATE` | 日期范围 | 2 联动月历 |
| `UI_DP_RANGE_DATETIME` | 日期 + 时间范围 | 2 月历 + 当前端点时间列 |

内部由 mode 派生两个布尔 `isRange = mode >= UI_DP_RANGE_DATE`、
`hasTime = (mode == UI_DP_DATETIME || mode == UI_DP_RANGE_DATETIME)`，
避免四套绘制/输入代码。

**已确认的四项产品决定**（讨论后定稿）：

1. 范围用**双联动月历**（左右两月，两阶段选择 + 悬停预览）；
2. 时间用**滚动列**（时/分，秒可选），复用 `UiScrollView`；
3. v1 触发器**可编辑键入**（内嵌 `UiEdit`，parse/format/IME/校验回退）；
4. 时间精度**默认到分 `HH:mm`**，`showSeconds` 开关可选加秒列。

非目标（v1 不做）：

- 农历 / 周选 / 年选 / 月选等其它 picker 变体（只做日网格）；
- 时区、UTC 偏移（只用本地时间 `GetLocalTime`）；
- 绑定引擎 / 全局单例状态（同 Select，App 持有 manager）；
- 完整 ICU locale：v1 用 format pattern + 内置中文名称表，名称表后续可由 App 提供；
- 原生独立弹层窗口：弹层仍画在主窗客户区内（同 Select/Menu）。

## 2. 值类型与日期数学（0CRT，新增 ui_datetime.*）

不碰任何 CRT 时间函数（`time`/`localtime`/`mktime`/`sscanf`/`strtol` 全不用）。
日期用纯整数 civil 算法（Howard Hinnant `days_from_civil` / `civil_from_days`），
以 **day-number**（距 1970-01-01 的天数，可负）做比较/加减/星期，O(1) 无浮点。

```c
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
int  UiDate_DayNumber(int y, int m, int d);          /* civil -> days */
void UiDate_FromDayNumber(int n, int *y, int *m, int *d);
int  UiDate_Weekday(int dayNumber);                  /* 0=日 .. 6=六 */
int  UiDate_Compare(const UiDateTime *a, const UiDateTime *b); /* 全字段 */
int  UiDate_CompareDay(const UiDateTime *a, const UiDateTime *b); /* 只比日期 */
void UiDate_AddMonths(UiDateTime *v, int months);    /* 夹取到月长 */
void UiDate_Now(UiDateTime *out);                    /* GetLocalTime, 唯一系统调用 */
int  UiDate_Valid(const UiDateTime *v);              /* 字段范围 + 月长校验 */
```

format / parse 也放这里（与 UI 无关，可 headless 测试）：

```c
/* token: YYYY MM DD HH mm ss; 其余字符原样。定宽 token, v1 不支持单位数 M/D。 */
void UiDate_Format(const UiDateTime *v, const char *pattern, char *out, int cap);
/* 成功返回 1 并填 out; 失败返回 0 (不改 out)。手动逐字符整数解析, 无 CRT。 */
int  UiDate_Parse(const char *text, const char *pattern, UiDateTime *out);
```

默认 pattern：日期 `"YYYY-MM-DD"`，日期+时间 `"YYYY-MM-DD HH:mm"`，
含秒时 `"YYYY-MM-DD HH:mm:ss"`。`UiDate_Parse` 对分隔符宽松（按 pattern 位置取数字），
但字段必须落在合法范围且通过 `UiDate_Valid`，否则判失败。

## 3. 架构与生命周期

### 3.1 manager 状态（App 持有，每窗一个或多个）

```c
typedef enum {
    UI_DP_DATE = 0, UI_DP_DATETIME, UI_DP_RANGE_DATE, UI_DP_RANGE_DATETIME
} UiDatePickerMode;

typedef struct UiDatePicker {
    UiId id;
    int  mode;
    UiDateTime value;      /* 单值 / 范围起点 */
    UiDateTime end;        /* 范围终点 (isRange 时有效) */
    int  hasValue;         /* 0 = 未选 (显示 placeholder) */
    int  hasEnd;

    int  viewY, viewM;     /* 左面板显示月; 右面板 = 下一月 */
    int  phase;            /* 范围: 0=选起点 1=选终点 */
    int  activeEnd;        /* 范围+时间: 当前编辑端点 0=起 1=止 */
    int  hoverDay;         /* 范围预览: 悬停 day-number, -1=无 */
    int  focusDay;         /* 键盘焦点 day-number */

    UiDateTime min, max;   /* 边界 (hasMin/hasMax 控制) */
    int  hasMin, hasMax;
    int  (*disabled)(int dayNumber, void *user);  /* 可选禁用日谓词 */
    void *user;

    int  weekStart;        /* 0=日 1=一 .. 6=六; 默认 1 */
    int  showSeconds;      /* 时间列是否含秒 */
    char format[40];       /* 触发器/键入 pattern */

    UiRect anchor;         /* 最近触发器矩形 */
    UiRect panelRc;        /* 本帧弹层矩形 */
    UiScrollView timeSv[2][3]; /* [端点][时/分/秒]; 范围每端点一套 */
    int  timeFocus;        /* 键盘焦点列: -1=日历, 否则 端点*ncol+列 */
    int  timeCenter;       /* 位掩码: 下一帧居中对应端点时间列选中行 */

    UiEdit edit[2];        /* 触发器键入段: [0]=起/单值 [1]=止 (isRange) */
    UiIme *ime;
    int  editActive;       /* 当前聚焦段 -1/0/1 */
    int  blinkOn;
    int  invalid;          /* 键入解析失败: 触发器错误描边, 关闭/失焦回退 */

    int  wasOpen;
    int  changed;          /* 本帧值变化 */
    int  committed;        /* 本帧提交 (确定/单选关闭/Enter) */
    char placeholder[96];
} UiDatePicker;
```

不拥有弹层注册表；开合/外部点击/Esc/焦点回归全部交给 `UiPopupManager`。

### 3.2 每帧顺序（同 Select）

```c
UiPopupManager_BeginFrame(&pm, &ui);     /* 最前 */
... 普通控件 ...
Ui_DatePicker(&dp, &pm, &ui, rc, ph, enabled);   /* 触发器 (内嵌键入段) */
... 帧末 ...
UiDatePicker_DrawPopup(&dp, &pm, &ui);   /* overlay 层画月历/时间/页脚 */
```

### 3.3 打开 / 关闭

| 动作 | 结果 |
| --- | --- |
| 点触发器文本段 | 聚焦该 `UiEdit` + 打开弹层（范围按段设 `activeEnd`/`phase`） |
| 点日历 glyph 按钮 | 打开/关闭弹层（不改键入焦点） |
| 键盘 Enter/Space/Down/Up（触发器聚焦） | 打开弹层，`focusOnClose=1` |
| 点弹层外 / Esc | `UiPopupManager` 关闭；键入未提交则校验回退 |
| 单选选中日 / 点确定 | 提交并 `Close` |
| 范围点终点 / 点确定 | 两端就绪后提交并 `Close` |

关闭时 `ui->navId = dp.id`、`navFocusVisible = pm.focusOnClose`（同 Select）。
鼠标打开 `focusOnClose=0`，键盘打开/Esc 关闭为 1。

### 3.4 输入路由

弹层打开时命中只看 `UI_LAYER_OVERLAY`（`Ui_HitTest` 已支持 `popupOpen` 过滤），
滚轮只接受 overlay 层目标（时间列登记在 overlay）。底层控件照常绘制但不响应。
日格/时间行/页脚按钮都在 `UI_Z_OVERLAY` 注册命中。

## 4. 触发器与可编辑键入

### 4.1 布局

- 单值：`[ UiEdit 文本段 ............ ][日历 glyph]`
- 范围：`[ UiEdit 起 ][→][ UiEdit 止 ][日历 glyph]`

外框圆角 6 DIP。键入段用 `UiEdit.flat=1`（零底色/描边/光晕，仍注册命中），
外框描边统一由触发器画：open/focus 用 `primary.base`，键入非法用 `error.base`，
hover 用 `neutral.border→primary.base` 插值（`Ui_AnimSmooth` 平滑）。
**hover 按命中归属判定**：子键入段/glyph 会抢走 `ui->hover`，父触发器把
`hover ∈ {自身, edit0, edit1, glyph}` 视为悬停；纯几何判定会在触发器被弹层
盖住时误亮（弹层 panel 已登记命中 blocker，归属判定天然排除遮挡）。
glyph 用 `UiIcon_Glyph(t->fIcon, 0xE787 /*Calendar*/)`，`fIcon==NULL` 时退回自绘日历矢量。
glyph 按钮单独注册命中（`Ui_IconButton` 语义），点击切换弹层。

### 4.2 键入 / parse / format / 校验回退

- 每段是一个 `UiEdit`（`Edit_Init(maxLen=0 不限, password=0, allow=NULL)`），
  文本 = 当前值按 `format` 格式化；未选时为空，显示 placeholder。
- 聚焦段：`Ui_SetFocusId(editId)` + `UiIme_SetFocus(ime, &edit[i])`；
  `Edit_Draw` 前设 `focused=1`、`caretOn=blinkOn`；点击 `Edit_PlaceCaret`。
- 字符/IME：`UiDatePicker_Char` → `Edit_Char`；`UiDatePicker_OnIme` 重画；
  `UiDatePicker_Blink`（530ms 定时器）翻转 `blinkOn` 并失效触发器。
- **提交时机**：Enter / 失焦 / 点确定 / 弹层关闭。提交时 `UiDate_Parse`：
  - 成功且通过 min/max/disabled → 写入 `value`/`end`，`changed=committed=1`，
    同步 `viewY/viewM` 到该值，重格式化文本（规范化）；
  - 失败 → `invalid=1`，触发器错误描边（`error.base`），**回退到上一个合法值**
    并重格式化（不保留非法文本）；范围若起>止则自动交换。
- 面板选日/选时间会即时回写对应段的文本（保持键入与面板一致）。
- 组合期间（`Edit_HasComposition`）Enter/Esc/Tab/方向交给输入法收尾（同 Select）。

### 4.3 API（镜像 Select）

```c
void UiDatePicker_Init(UiDatePicker *dp, UiId id, int mode);
void UiDatePicker_Shutdown(UiDatePicker *dp);
void UiDatePicker_SetMode(UiDatePicker *dp, int mode);
void UiDatePicker_SetValue(UiDatePicker *dp, const UiDateTime *v);   /* 单值/起点 */
void UiDatePicker_SetRange(UiDatePicker *dp, const UiDateTime *a, const UiDateTime *b);
void UiDatePicker_SetBounds(UiDatePicker *dp, const UiDateTime *min, const UiDateTime *max); /* NULL=不限 */
void UiDatePicker_SetDisabledFn(UiDatePicker *dp, int (*fn)(int dayNumber, void*), void *user);
void UiDatePicker_SetFormat(UiDatePicker *dp, const char *pattern);
void UiDatePicker_SetWeekStart(UiDatePicker *dp, int ws);   /* 0=日 1=一 */
void UiDatePicker_SetShowSeconds(UiDatePicker *dp, int on);
void UiDatePicker_SetPlaceholder(UiDatePicker *dp, const char *text);
void UiDatePicker_AttachIme(UiDatePicker *dp, UiIme *ime);

int  Ui_DatePicker(UiDatePicker *dp, UiPopupManager *pm, UiContext *ui,
                   UiRect rc, const char *placeholder, int enabled); /* 返回 changed */
void UiDatePicker_DrawPopup(UiDatePicker *dp, UiPopupManager *pm, UiContext *ui);
int  UiDatePicker_Key(UiDatePicker *dp, UiPopupManager *pm, UiContext *ui, UINT vk);
int  UiDatePicker_Char(UiDatePicker *dp, UiContext *ui, wchar_t ch);
void UiDatePicker_OnIme(UiDatePicker *dp, UiContext *ui);
void UiDatePicker_Blink(UiDatePicker *dp, UiContext *ui);
```

## 5. 月历面板

### 5.1 几何

- 单月面板 ≈ 280×320 DIP：头部 40 + 星期行 28 + 6×7 网格（格 ≈ 36）216 + 页脚 44。
- 范围双面板 ≈ 560×320：左 = `viewY/viewM`，右 = 下一月；中缝 12。
- `UiPopup_Place(ui, anchor, w, h, UI_POPUP_BELOW, UI_POPUP_ALIGN_START, 6)`
  自动翻转/夹取，四周留 8 DIP。窄窗放不下双面板时夹取到视口宽（v1 不降级单面板）。
- 面板底板 `toastBg` + `toastBorder` 圆角 8（同 Select），`Ui_PushLayer(UI_Z_OVERLAY)`。

### 5.2 头部与星期

- 头部：`‹`（上月）`2026年 1月`（标签）`›`（下月）。范围双面板各一套导航，
  联动：左面板导航时右面板恒为左+1 月。导航按钮 `Ui_IconButton`（MDL2 Chevron）。
- 星期行：按 `weekStart` 排 `一二三四五六日`（ws=1）或 `日一二三四五六`（ws=0），
  非交互文本，`t->text3`。

### 5.3 日格状态（6×7=42 格）

每格 `Ui_StateInstant`（即时 hover，无淡出滞后，同 Select/Menu 行）。
day-number 由 `UiDate_DayNumber(viewY, viewM, 1)` 起算，前置补上月末、后置补下月初。

| 状态 | 绘制 |
| --- | --- |
| 非本月日 | `text4` 淡化（仍占位，保持 42 格对齐） |
| 普通日 | `toastText` |
| 今天（未选中） | `primary.base` 1 DIP 描边环（cell inset 2） |
| 键盘焦点日 | 2 DIP 描边环（cell inset 1、圆角 7、统一 `text` 色；选中格反色白环会融进浅色背景），仅 `navFocusVisible && timeFocus<0` 时画 |
| 选中 / 范围端点 | `primary.base` 实心 + `primary.text` 文字 |
| 范围内（端点之间） | `primary.soft` 淡底 + `text` |
| 悬停（即时） | `Ui_ColorAlpha(primary.base, hover*22)` 叠加（同 Select 行） |
| 范围预览（phase=1 悬停） | 起点→悬停日临时 `primary.soft`，悬停日实心 |
| 禁用（min/max 外或谓词） | `btnDisabledText`，不响应 hover/click |

点击日格：
- 单值：写 `value`（保留原时分秒），`changed=1`；`UI_DP_DATE` 直接提交关闭，
  `UI_DP_DATETIME` 留在面板继续选时间（点确定才关）。
- 范围：phase=0 写起点→phase=1；phase=1 写终点→自动排序（起>止交换）→
  两端就绪，`UI_DP_RANGE_DATE` 提交关闭，`RANGE_DATETIME` 留面板选时间。

### 5.4 页脚

- 左：`今天`（单值跳今天）/`此刻`（hasTime，取 `UiDate_Now`）；范围左：预设
  `今天 / 近7天 / 近30天 / 本月`（小按钮，点击即设两端并提交）。
- 右：`清除`（置空 hasValue/hasEnd）+ `确定`（提交关闭）。
- 按钮走 `Ui_ButtonEx`（`UI_BAPPEAR_TEXT`/`PLAIN`），`primary` 强调确定。

## 6. 时间滚动列（hasTime）

- 时（0..23）/分（0..59）/秒（0..59，`showSeconds`）各一列，
  `UiScrollLayout_SetFixed(count, rowH=28)` + `UiScrollView`（overlay 登记滚轮）。
- 只画 `UiScrollView_Cull` 通过的可见行；行 `Ui_StateInstant`，选中行 `primary.soft`
  底 + `primary.base` 文字，居中高亮带。
- `DrawPopup` 内 `UiScrollView_Tick(sv, ui, 16)`（同 Select：弹层无 App tick 回调，
  自推进 + 自失效；静止时不失效，避免饿死消息泵）。
- 打开/值变化时 `UiScrollView_ScrollTo` 把当前值滚到居中（animate=0）。
- 范围+时间：左右月历下**各挂一套**时间列（`timeSv[2][3]`），组标题 开始/结束
  （活动组标题 `primary.base`），面板高度加一行标题 20 DIP；点某组行只写该
  端点并置 `activeEnd`；单值模式单组居中、无标题。
- 点行/滚轮停稳即写回 `value`/`end` 的时分秒，`changed=1`，并回写触发器文本。

## 7. 键盘

`UiDatePicker_Key` 以 `ui->navId == dp.id` 为守卫（同 Select），`navFocusVisible=1`。

- 触发器聚焦未打开：Enter/Space/Down/Up 打开；键入段聚焦时 Enter 提交、
  Down/Space 打开，其余编辑键交 `Edit_Key`。
- 打开且焦点在日历（`timeFocus<0`）：
  - ←/→ ±1 天，↑/↓ ±7 天，PgUp/PgDn ±1 月（同步 viewY/M），Home/End 周首/周末；
  - Enter：单值选中/提交；范围 phase 推进；
  - 焦点日跨月时自动翻 view 面板。
- 焦点在时间列（Tab 切入，`timeFocus=端点*ncol+列`）：↑/↓ 调值并居中该组，
  ←/→ 循环两组全部列，Enter 回日历。
- 键入段聚焦：Backspace/Delete/←/→/Home/End/Ctrl+A/C/X/V 交 `Edit_Key`；
  字符走 `Char`；Enter 提交 parse；组合期间确认/方向交输入法。
- Esc：`UiPopupManager_Key` 关闭并显示触发器焦点；键入未提交则回退。
- Tab：键入段聚焦 → 退出键入回日历；日历 → 活动端点时间组列 0（hasTime）否则
  聚焦键入段 0；时间组内 Tab → 下一组列 0，末组后回日历；Enter 回日历。
  页脚按钮不参与 Tab 循环（鼠标点击）。

## 8. 动画

- **开合 / 月切换**：用 `Ui_Anim` 内核（`UI_AN_ALPHA` + `UI_AN_OFFSET`）按 panel/child id
  补间 ~160ms ease-out；`Ui_AnimBusy` 自动驱动帧循环（已核实 `ui_app.c:464`），
  `Ui_AnimSetRect(panelId, panel)` 提供脏区。月切换网格淡入 + 轻微横移。
- **日格 hover**：`Ui_StateInstant` 即时，无淡出滞后（同 Select/Menu）。
- **选中 / 范围填充**：即时绘制（状态明确）；端点切换随月切换补间一起过渡。
- **触发器描边**：命中归属 hover + `Ui_AnimSmooth` 平滑（见 §4.1）；
  open/focus/invalid 即时换色，不做补间。
- **时间列**：`UiScrollView` 滚轮平滑 / 惯性 / 回弹（桌面 cfg，bounce=0）。
- `animReduced`（系统关闭显示动画）时所有补间立即到位。

## 9. 控件预算与裁剪

`UI_MAX_WIDGETS = 256`（`ui_context.h:17`）。范围弹层打开时峰值：
84 日格 + 导航/页脚 ~12 + 时间可见行（裁剪后 ~7×3×2 组=42）≈ 138 交互控件，
加触发器与 App 其它控件仍 < 256。关闭时只注册触发器（1~3 段 + glyph）。
日格仅在本弹层打开时注册；时间行 `Cull` 跳过视口外；星期/标签为非交互文本不注册。

## 10. 复用映射

| 需求 | 复用 |
| --- | --- |
| 开合/外部点击/Esc/焦点回归/翻转夹取 | `UiPopupManager` + `UiPopup_Place` |
| 触发器 hover 平滑、开合/月切换补间 | 命中归属 hover + `Ui_AnimSmooth` / `Ui_Anim`（`Ui_AnimBusy` 自动驱动） |
| 日格/时间行即时 hover | `Ui_StateInstant` |
| 时间列滚动/滚轮/裁剪/惯性 | `UiScrollView` + `UiScrollLayout_SetFixed` + `Cull` |
| 触发器键入 | `UiEdit` + `UiIme`（同 Select 搜索框接线） |
| 日历/导航/清除图标 | `UiIcon_Glyph`（MDL2），`fIcon==NULL` 退回矢量 |
| 页脚/导航按钮 | `Ui_IconButton` / `Ui_ButtonEx` |
| 选中/范围/今天色 | `UiTheme.primary`（base/soft/text/border）、`error.base` |
| 日期数学/format/parse | 新增 `ui_datetime.*`（0CRT，headless 可测） |

## 11. 测试计划

- **headless 0CRT 断言**（`ui_datetime`）：day-number 往返（含 1970 前后/闰年/世纪闰）、
  星期、月长、`AddMonths` 夹取（1/31 + 1 月 → 2/28）、`Compare`、`Valid` 边界、
  format/parse 往返与非法输入拒绝。脱离窗口构建运行。
- **原生 200% DPI 交互/视觉**（真实 mouse_event/keybd_event + 截图）：
  四模式打开/选日/范围两阶段预览/自动排序/时间滚动/键入 parse 与无效回退/
  键盘导航/Esc/外部点击关闭/弹层翻转夹取；核对 hover/选中/范围 soft/今天环/禁用色。
- **回归**：overlays（Select/Menu/Dialog）、scrollview、textarea 不破坏；
  全部 exe 0CRT 无 msvcrt/ucrtbase 依赖；弹层静止 CPU ~0%。

## 12. 落地文件清单

- 新增 `src/ui/ui_datetime.h` / `ui_datetime.c`
- 新增 `src/ui/ui_datepicker.h` / `ui_datepicker.c`
- 新增 `samples/datepicker.c`（四模式演示 + UiApp 回调接线）
- `build.cmd`：源列表加 `ui_datetime.c`/`ui_datepicker.c`；加 datepicker 编译/链接/
  CRT 校验/输出/del 行（**保持 CRLF**）
- headless 测试（临时，不入仓库或入 `tests/`）
- 白皮书 DatePicker 章节 + changelog；README 控件列表 + 回归条目

## 13. 实现修订与验证（2026-11-11）

落地后相对本设计的修订（已回写上文）：

1. **触发器 hover 改命中归属判定**（§4.1）：子键入段/glyph 抢走 `ui->hover`、
   纯几何判定在弹层遮挡时误亮，两个问题一次解决；键入段 `flat=1` 零表面样式。
2. **日格补键盘焦点环**（§5.3）：2 DIP 外圈环、统一 `text` 色（选中格反色白环
   会融进浅色背景不可见，实测后修正），仅键盘可见焦点且焦点在日历时画；
   与 today 1 DIP 环可叠加区分。
3. **打开键含 Up**、Tab 语义按 §7 落地（页脚不入 Tab 循环）。
4. **范围+时间改双套时间列**（§6）：左右月历下各一套、开始/结束标题；
   `timeSv[2][3]`、`timeFocus=端点*ncol+列`、`timeCenter` 位掩码；
   点击/滚轮/键盘按端点各自独立。

验证：headless 0CRT 断言 1164/0；原生 200% DPI 真实鼠标/键盘 + 截图核对：
四模式开合与选日、范围两阶段悬停预览/自动排序、范围双时间列端点隔离
（点止组只改止段、双组滚轮独立、Tab/←→ 跨组、↑↓ 改值居中）、键入非法
红框回退/合法提交、键盘焦点环（含选中格）与跨月自动翻页、min/max ±30 天
禁用拒点、弹层遮挡触发器不误亮、暗色主题 DATETIME、小窗夹取完整可操作；
controls/textarea/scroll/overlays 回归干净。12 个 exe 均 0CRT 依赖校验通过。