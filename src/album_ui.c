// album_ui.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include "draw_utils.h"
#include "ui_components.h"
#include "album_ui.h"
#include "touch.h"
#include "voice_ui.h"

#define SCREEN_W 1024
#define SCREEN_H 600
#define BMP_DIR "/kkk/bmp/"

extern AppPage current_page;
extern void refresh_ui(void);

static int bmp_count = 0;
static char *bmp_files[256];
static int fullscreen_idx = 0;
static int is_fullscreen = 0;

void scan_bmp_files(void) {
    // 重置计数器，防止重复累加
    bmp_count = 0;
    for (int i = 0; i < 256; i++) bmp_files[i] = NULL; 

    struct dirent *entry;
    DIR *dir = opendir(BMP_DIR);
    if (dir == NULL) {
        printf("[Album] [Warn] 无法打开目录 %s\n", BMP_DIR);
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        char *ext = strrchr(entry->d_name, '.');
        if (ext && (strcmp(ext, ".bmp") == 0 || strcmp(ext, ".BMP") == 0)) {
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s%s", BMP_DIR, entry->d_name);
            bmp_files[bmp_count] = strdup(full_path);
            bmp_count++;
        }
    }
    closedir(dir);
    printf("[Album] [OK] 扫描到 %d 张图片\n", bmp_count);
}

unsigned char* load_bmp_file(const char *filename, int *file_size) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    *file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char *data = (unsigned char *)malloc(*file_size);
    if (!data) { fclose(fp); return NULL; }
    fread(data, 1, *file_size, fp);
    fclose(fp);
    return data;
}

void draw_bmp_scaled(unsigned char *bmp_data, int file_size, int container_w, int container_h, int container_x, int container_y, const char* filename) {
    if (!bmp_data) return;

    // 1. 极重要：使用 memcpy 代替强转指针，解决 RK1808 内存对齐崩溃问题！
    unsigned short magic;
    int pixel_offset, width, height;
    unsigned short bit_count;
    
    memcpy(&magic, bmp_data, 2);
    memcpy(&pixel_offset, bmp_data + 10, 4);
    memcpy(&width, bmp_data + 18, 4);
    memcpy(&height, bmp_data + 22, 4);
    memcpy(&bit_count, bmp_data + 28, 2);

    if (magic != 0x4D42) {
        printf("[Album] [Error] 文件头不是 BMP 格式\n");
        return;
    }

    if (bit_count != 24) {
        printf("[Album] [Error] 需要 24位BMP，当前是 %d 位\n", bit_count);
        return;
    }

    // 2. 处理上下翻转
    int flip = 1;
    if (height < 0) {
        height = -height;
        flip = 0;
    }

    // 3. 计算完美的等比例缩放
    float scale_x = (float)width / container_w;
    float scale_y = (float)height / container_h;
    float scale = (scale_x > scale_y) ? scale_x : scale_y;

    int draw_w = (int)(width / scale);
    int draw_h = (int)(height / scale);
    if (draw_w <= 0) draw_w = 1;
    if (draw_h <= 0) draw_h = 1;

    int x_off = container_x + (container_w - draw_w) / 2;
    int y_off = container_y + (container_h - draw_h) / 2;

    // 4. 像素绘制
    unsigned char *pixel_start = bmp_data + pixel_offset;
    // 4字节对齐的行宽公式
    int row_size = (width * 3 + 3) & ~3;

    for (int y = 0; y < draw_h; y++) {
        int src_y = (int)(y * height / draw_h);
        int draw_y;
        if (flip) {
            draw_y = y_off + draw_h - 1 - y;
        } else {
            draw_y = y_off + y;
        }

        for (int x = 0; x < draw_w; x++) {
            int src_x = (int)(x * width / draw_w);
            unsigned char *p = pixel_start + src_y * row_size + src_x * 3;
            
            unsigned char b = p[0];
            unsigned char g = p[1];
            unsigned char r = p[2];
            unsigned int color = 0xFF000000 | (r << 16) | (g << 8) | b;
            my_draw_pixel(x_off + x, draw_y, color);
        }
    }
}

void draw_album_grid(void) {
    /* 浅灰白主题背景 */
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++) my_draw_pixel(x, y, COLOR_BG);

    draw_rounded_rect_fill(10, 10, 80, 40, 8, COLOR_BTN_BACK);
    my_draw_text(18, 22, "返回", COLOR_BTN_TEXT);
    
    const char *right_text = "点击任意图放大";
    int txt_w = get_text_width(right_text);
    my_draw_text(SCREEN_W - 20 - txt_w, 20, right_text, COLOR_TEXT_G);

    if (bmp_count == 0) {
        my_draw_text(SCREEN_W/2 - 150, SCREEN_H/2 - 20, "没有找到 BMP 图片！", COLOR_RED);
        return;
    }

    // 布局：3x3 排列
    int grid_x[3] = { 31, 362, 693 };
    int grid_y[3] = { 80, 260, 440 };
    int thumb_w = 300, thumb_h = 120;

    for (int i = 0; i < 9 && i < bmp_count; i++) {
        int col = i % 3;
        int row = i / 3;
        int x = grid_x[col];
        int y = grid_y[row];

        // 白色卡片底框 + 浅阴影效果
        draw_rounded_rect_fill(x - 2, y - 2, thumb_w + 4, thumb_h + 4, 10, COLOR_CARD);

        int file_size;
        unsigned char *data = load_bmp_file(bmp_files[i], &file_size);
        if (data) {
            draw_bmp_scaled(data, file_size, thumb_w, thumb_h, x, y, bmp_files[i]);
            free(data);
        }
    }
}

void draw_album_fullscreen(void) {
    /* 浅灰白主题背景 */
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++) my_draw_pixel(x, y, COLOR_BG);

    draw_rounded_rect_fill(10, 10, 80, 40, 8, COLOR_BTN_BACK);
    my_draw_text(18, 22, "返回", COLOR_BTN_TEXT);
    
    char info[50];
    snprintf(info, sizeof(info), "第 %d / %d 张", fullscreen_idx + 1, bmp_count);
    my_draw_text(SCREEN_W - 250, 20, info, COLOR_TEXT_G);

    if (fullscreen_idx >= 0 && fullscreen_idx < bmp_count) {
        int file_size;
        unsigned char *data = load_bmp_file(bmp_files[fullscreen_idx], &file_size);
        if (data) {
            draw_bmp_scaled(data, file_size, SCREEN_W, SCREEN_H, 0, 0, bmp_files[fullscreen_idx]);
            free(data);
        }
    }
}

void album_init(void) {
    scan_bmp_files();
    is_fullscreen = 0;
    draw_album_grid();
}

void album_handle_touch(int action, int x, int y) {
    if (action == TOUCH_CLICK && x >= 10 && x <= 90 && y >= 10 && y <= 50) {
        current_page = PAGE_HOME;
        refresh_ui();
        return;
    }

    if (!is_fullscreen) {
        if (action == TOUCH_CLICK) {
            int grid_x[3] = { 31, 362, 693 };
            int grid_y[3] = { 80, 260, 440 };
            int thumb_w = 300, thumb_h = 120;

            for (int i = 0; i < 9 && i < bmp_count; i++) {
                int col = i % 3;
                int row = i / 3;
                int start_x = grid_x[col];
                int start_y = grid_y[row];
                if (x >= start_x && x <= start_x + thumb_w && y >= start_y && y <= start_y + thumb_h) {
                    fullscreen_idx = i;
                    is_fullscreen = 1;
                    draw_album_fullscreen();
                    return;
                }
            }
        }
    } else {
        if (action == TOUCH_SLIDE_LEFT) { // 左滑下一张
            if (fullscreen_idx + 1 < bmp_count) { fullscreen_idx++; draw_album_fullscreen(); }
            return;
        }
        if (action == TOUCH_SLIDE_RIGHT) { // 右滑上一张
            if (fullscreen_idx > 0) { fullscreen_idx--; draw_album_fullscreen(); }
            return;
        }
        if (action == TOUCH_CLICK) { // 轻点屏幕退出全屏
            is_fullscreen = 0;
            draw_album_grid();
            return;
        }
    }
}