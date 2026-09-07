#ifndef _DRAW_UTILS_H_
#define _DRAW_UTILS_H_

// 声明显存和行宽
extern unsigned char *fbp;
extern int line_len;

// 声明你要用到的绘图函数
void my_draw_pixel(int x, int y, unsigned int color);
void my_draw_text(int x, int y, const char *str, unsigned int color);
int get_text_width(const char *str);
// 声明加载字库的函数
int hzk_init(const char *path);
#endif