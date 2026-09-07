#include "ui_components.h"
#include "draw_utils.h"
// ui_components.c
#include <stdlib.h>
#include <math.h>
#include "font_8x16.h" // 引用你的通用头文件

// 外部引用的画像素函数和显存变量（后续编译时链接）
extern unsigned char *fbp;
extern int line_len;
void my_draw_pixel(int x, int y, unsigned int color);

// 1. 画圆角矩形边框（只描边，不填充）
void draw_rounded_rect(int x, int y, int w, int h, int r, unsigned int color) {
    // 如果宽高太小，直接画普通矩形
    if (w < 2*r || h < 2*r) {
        for (int dy = 0; dy < h; dy++) {
            for (int dx = 0; dx < w; dx++) {
                my_draw_pixel(x + dx, y + dy, color);
            }
        }
        return;
    }

    // 用逐点扫描的方式绘制圆角区域
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            // 核心逻辑：判断当前点是在圆角区域内部还是外部
            if (dx < r && dy < r) {
                if ((dx - r)*(dx - r) + (dy - r)*(dy - r) > r*r) continue; // 左上角外
            } else if (dx >= w-r && dy < r) {
                if ((dx - (w-r))*(dx - (w-r)) + (dy - r)*(dy - r) > r*r) continue; // 右上角外
            } else if (dx < r && dy >= h-r) {
                if ((dx - r)*(dx - r) + (dy - (h-r))*(dy - (h-r)) > r*r) continue; // 左下角外
            } else if (dx >= w-r && dy >= h-r) {
                if ((dx - (w-r))*(dx - (w-r)) + (dy - (h-r))*(dy - (h-r)) > r*r) continue; // 右下角外
            }
            my_draw_pixel(x + dx, y + dy, color);
        }
    }
}

// 1.5 画填充圆角矩形（填充内部，圆角边缘）
void draw_rounded_rect_fill(int x, int y, int w, int h, int r, unsigned int color) {
    if (w < 2*r || h < 2*r) {
        for (int dy = 0; dy < h; dy++)
            for (int dx = 0; dx < w; dx++)
                my_draw_pixel(x + dx, y + dy, color);
        return;
    }
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            if (dx < r && dy < r) {
                if ((dx - r)*(dx - r) + (dy - r)*(dy - r) > r*r) continue;
            } else if (dx >= w-r && dy < r) {
                if ((dx - (w-r))*(dx - (w-r)) + (dy - r)*(dy - r) > r*r) continue;
            } else if (dx < r && dy >= h-r) {
                if ((dx - r)*(dx - r) + (dy - (h-r))*(dy - (h-r)) > r*r) continue;
            } else if (dx >= w-r && dy >= h-r) {
                if ((dx - (w-r))*(dx - (w-r)) + (dy - (h-r))*(dy - (h-r)) > r*r) continue;
            }
            my_draw_pixel(x + dx, y + dy, color);
        }
    }
}

// 2. 画实心圆
void draw_solid_circle(int cx, int cy, int r, unsigned int color) {
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx*dx + dy*dy <= r*r) {
                my_draw_pixel(cx + dx, cy + dy, color);
            }
        }
    }
}

// 3. 画空心圆（圆环）
void draw_circle_outline(int cx, int cy, int r, unsigned int color, int thickness) {
    for (int t = 0; t < thickness; t++) {
        int rr = r - t;
        if (rr <= 0) break;
        for (int dy = -rr; dy <= rr; dy++) {
            for (int dx = -rr; dx <= rr; dx++) {
                int d2 = dx*dx + dy*dy;
                if (d2 >= (rr-1)*(rr-1) && d2 <= rr*rr) {
                    my_draw_pixel(cx + dx, cy + dy, color);
                }
            }
        }
    }
}

// 4. 画圆弧（角度范围：0~360，顺时针）
void draw_arc(int cx, int cy, int r, int start_angle, int end_angle, unsigned int color, int thickness) {
    for (int t = 0; t < thickness; t++) {
        int rr = r - t;
        if (rr <= 0) break;
        for (int dy = -rr; dy <= rr; dy++) {
            for (int dx = -rr; dx <= rr; dx++) {
                int d2 = dx*dx + dy*dy;
                if (d2 < (rr-1)*(rr-1) || d2 > rr*rr) continue;
                // 计算角度（atan2 返回弧度，范围 -PI ~ PI）
                double angle = atan2(dy, dx) * 180.0 / 3.141592653589793;
                if (angle < 0) angle += 360.0;
                if (start_angle <= end_angle) {
                    if (angle >= start_angle && angle <= end_angle)
                        my_draw_pixel(cx + dx, cy + dy, color);
                } else {
                    if (angle >= start_angle || angle <= end_angle)
                        my_draw_pixel(cx + dx, cy + dy, color);
                }
            }
        }
    }
}

// 5. 画直线（Bresenham，支持 thickness）
void draw_line(int x1, int y1, int x2, int y2, unsigned int color, int thickness) {
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    while (1) {
        for (int ty = -thickness/2; ty <= thickness/2; ty++) {
            for (int tx = -thickness/2; tx <= thickness/2; tx++) {
                my_draw_pixel(x1 + tx, y1 + ty, color);
            }
        }
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx) { err += dx; y1 += sy; }
    }
}

// 6. 画右箭头（">" 形状）
void draw_right_arrow(int x, int y, int size, unsigned int color) {
    // 画一个右箭头，顶点朝右
    for (int i = 0; i < size; i++) {
        draw_line(x, y - i, x + i, y, color, 1);
        draw_line(x, y + i, x + i, y, color, 1);
    }
}

// 7. 画竖线装饰条
void draw_vertical_bar(int x, int y, int h, int thickness, unsigned int color) {
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < thickness; dx++) {
            my_draw_pixel(x + dx, y + dy, color);
        }
    }
}

// 8. 画带有颜色条和发光效果的底部导航按钮
void draw_nav_button(int x, int y, int w, int h, const char* text, int is_selected) {
    unsigned int bg_color = is_selected ? 0xFF1E3A5F : 0xFF0A0D16; // 选中深蓝，未选中黑色透明
    unsigned int text_color = is_selected ? 0xFF00FFFF : 0xFF555555;
    
    // 1. 先画背景圆角矩形
    draw_rounded_rect(x, y, w, h, 8, bg_color);
    
    // 2. 画一个上边发光的线条模拟科技感
    for (int dx = x+10; dx < x+w-10; dx++) {
        my_draw_pixel(dx, y, 0xFF00FFFF);
    }
    
    // 3. 画居中文字
    my_draw_text(x + (w - get_text_width(text)) / 2, y + (h - 16*2) / 2, text, text_color);
}