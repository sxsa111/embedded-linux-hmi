#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "draw_utils.h"
#include "ui_components.h"
#include "draw_ui.h"
#include "touch.h"
#include "voice_ui.h"

#define SCREEN_W 1024
#define SCREEN_H 600

#define PEN_SIZE    6
#define PEN_COLOR   0xFFFF0000
#define CLEAR_BTN_X (SCREEN_W - 80)
#define CLEAR_BTN_Y 10
#define CLEAR_BTN_W 70
#define CLEAR_BTN_H 40
#define CLEAR_BTN_COLOR 0xFFE8ECF0
#define CLEAR_BTN_TEXT  0xFF1D1D1F

extern AppPage current_page;
extern void refresh_ui(void);

// 严格复刻 fb_touch.c 的画笔画法
void draw_pen(int x, int y, unsigned int color)
{
    for (int dy = -PEN_SIZE/2; dy < PEN_SIZE/2; dy++) {
        for (int dx = -PEN_SIZE/2; dx < PEN_SIZE/2; dx++) {
            my_draw_pixel(x + dx, y + dy, color);
        }
    }
}

/*
 * Bresenham 直线插值 —— 用粗画笔从 (x0,y0) 画到 (x1,y1)
 * 解决触摸采样率不足导致的断点问题：手指快速移动时，
 * 连续两个 TOUCH_DRAG 采样点间距可达 30-50 像素，
 * 单个 6×6 方块根本连不上。此函数在两点之间逐像素填充，
 * 保证画出的线条连续密集。
 */
static void draw_pen_line(int x0, int y0, int x1, int y1, unsigned int color)
{
    int dx  = abs(x1 - x0);
    int dy  = -abs(y1 - y0);
    int sx  = (x0 < x1) ? 1 : -1;
    int sy  = (y0 < y1) ? 1 : -1;
    int err = dx + dy;  /* error = dx + dy (dy is negative) */

    while (1) {
        draw_pen(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {  /* e_xy + e_x > 0 → step in X */
            if (x0 == x1) break;
            err += dy;
            x0  += sx;
        }
        if (e2 <= dx) {  /* e_xy + e_y < 0 → step in Y */
            if (y0 == y1) break;
            err += dx;
            y0  += sy;
        }
    }
}

// 清屏按钮（浅白蓝主题适配）
void draw_clear_button(void)
{
    draw_rounded_rect_fill(CLEAR_BTN_X, CLEAR_BTN_Y, CLEAR_BTN_W, CLEAR_BTN_H, 8, CLEAR_BTN_COLOR);
    my_draw_text(CLEAR_BTN_X + (CLEAR_BTN_W - get_text_width("清屏")) / 2, CLEAR_BTN_Y + 12, "清屏", CLEAR_BTN_TEXT);
}

// 严格复刻 fb_touch.c 的清屏函数
void draw_clear_screen(void)
{
    /* 浅灰白主题背景 */
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            my_draw_pixel(x, y, COLOR_BG);
        }
    }
    /* 左上角返回按钮 */
    draw_rounded_rect_fill(10, 10, 80, 40, 8, COLOR_BTN_BACK);
    my_draw_text(18, 22, "返回", COLOR_BTN_TEXT);
    /* 右上角清屏按钮 */
    draw_clear_button();
}

void draw_init(void)
{
    draw_clear_screen();
}

void draw_handle_touch(int action, int x, int y)
{
    /* 跟踪上一帧的触摸状态，用于线段插值 */
    static int prev_x = -1, prev_y = -1;
    static int was_dragging = 0;  /* 上一帧是否在拖拽画线 */

    // ── 左上角返回按钮 ──
    if (action == TOUCH_CLICK && x >= 10 && x <= 90 && y >= 10 && y <= 50) {
        was_dragging = 0;
        prev_x = -1; prev_y = -1;
        current_page = PAGE_HOME;
        refresh_ui();
        return;
    }

    // ── 右上角清屏按钮 ──
    if (action == TOUCH_CLICK) {
        if (x >= CLEAR_BTN_X && x <= CLEAR_BTN_X + CLEAR_BTN_W &&
            y >= CLEAR_BTN_Y && y <= CLEAR_BTN_Y + CLEAR_BTN_H) {
            was_dragging = 0;
            prev_x = -1; prev_y = -1;
            draw_clear_screen();
            return;
        }
    }

    // ── 拖拽画线（手指按住滑动）──
    if (action == TOUCH_DRAG) {
        if (was_dragging && prev_x >= 0) {
            /* 连续拖拽：从上一个采样点画到当前位置 */
            draw_pen_line(prev_x, prev_y, x, y, PEN_COLOR);
        } else {
            /* 第一笔落下：只画一个方块 */
            draw_pen(x, y, PEN_COLOR);
        }
        prev_x = x;
        prev_y = y;
        was_dragging = 1;
        return;
    }

    // ── 手指抬起（TOUCH_CLICK 在画布区域）──
    if (action == TOUCH_CLICK) {
        if (was_dragging && prev_x >= 0) {
            /* 拖拽结束：补画最后一段微短线 */
            draw_pen_line(prev_x, prev_y, x, y, PEN_COLOR);
        } else {
            /* 独立点击：画一个点 */
            draw_pen(x, y, PEN_COLOR);
        }
        /* 无论哪种情况，笔画结束，重置状态 */
        was_dragging = 0;
        prev_x = -1;
        prev_y = -1;
        return;
    }

    /* 其他触摸事件（滑动等）→ 重置笔画状态 */
    was_dragging = 0;
    prev_x = -1;
    prev_y = -1;
}