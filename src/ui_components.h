// ui_components.h
#ifndef _UI_COMPONENTS_H_
#define _UI_COMPONENTS_H_

/* ====== 全局颜色定义（浅白蓝新拟态主题） ====== */
#define COLOR_BG       0xFFF7F8FA  /* 浅灰白背景 */
#define COLOR_CARD     0xFFFFFFFF  /* 纯白卡片 */
#define COLOR_CARD_BD  0xFFE0E0E5  /* 卡片边框灰 */
#define COLOR_TEXT_T   0xFF1D1D1F  /* 标题黑色 */
#define COLOR_TEXT_G   0xFF6E6E73  /* 提示灰色 */
#define COLOR_ACCENT   0xFF5B8FF9  /* 主题蓝色 */
#define COLOR_ACCENT_L 0xFFB0D0FF  /* 浅蓝色 */
#define COLOR_BTN_BACK 0xFFE8ECF0  /* 返回按钮底色 */
#define COLOR_BTN_TEXT 0xFF1D1D1F  /* 按钮文字色 */
#define COLOR_WHITE    0xFFFFFFFF  /* 纯白 */
#define COLOR_GREEN    0xFF00CC66  /* AI回复绿 */
#define COLOR_RED      0xFFFF0000  /* 错误红 */

// 声明显存变量（外部引用，实际定义在 main 中）
extern unsigned char *fbp;
extern int line_len;

// 声明外部绘图的依赖函数
extern void my_draw_pixel(int x, int y, unsigned int color);
extern void my_draw_text(int x, int y, const char *str, unsigned int color);
extern int get_text_width(const char *str);

// 声明本文件里的 UI 组件函数
void draw_rounded_rect(int x, int y, int w, int h, int r, unsigned int color);
void draw_rounded_rect_fill(int x, int y, int w, int h, int r, unsigned int color);
void draw_nav_button(int x, int y, int w, int h, const char* text, int is_selected);

// 新增：基础几何绘制
void draw_solid_circle(int cx, int cy, int r, unsigned int color);
void draw_circle_outline(int cx, int cy, int r, unsigned int color, int thickness);
void draw_arc(int cx, int cy, int r, int start_angle, int end_angle, unsigned int color, int thickness);
void draw_line(int x1, int y1, int x2, int y2, unsigned int color, int thickness);
void draw_right_arrow(int x, int y, int size, unsigned int color);
void draw_vertical_bar(int x, int y, int h, int thickness, unsigned int color);

#endif