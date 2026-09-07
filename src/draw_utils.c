#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "draw_utils.h"
#include "font_8x16.h" // 你的8x16英文字模头文件

// ==============================================================
// 1. 全局变量定义（确保项目中只有这里定义了 fbp 和 line_len）
// ==============================================================
unsigned char *fbp = NULL;
int line_len = 0;

// 汉字字库文件描述符（局部静态变量，防止外部乱用）
static int hzk_fd = -1;

// ==============================================================
// 2. 像素绘制（加了空指针保护，防止 fbp 未初始化导致崩溃）
// ==============================================================
void my_draw_pixel(int x, int y, unsigned int color)
{
    // 核心安全保护：如果 fbp 还没初始化，立刻返回，绝不导致段错误
    if (fbp == NULL) return; 

    // 边界保护：防止画出屏幕外导致内存踩踏
    if (x < 0 || x >= 1024 || y < 0 || y >= 600) return;

    long loc = x * 4 + y * line_len;
    *(unsigned int *)(fbp + loc) = color;
}

// ==============================================================
// 3. 英文字符绘制 (根据 font_8x16.h 渲染)
// ==============================================================
void my_draw_char(int x, int y, char ch, unsigned int color)
{
    if (ch < ' ' || ch > '~') ch = ' ';
    int index = ch - ' ';
    const unsigned char *font = font_8x16[index];
    
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 8; col++) {
            if (font[row] & (0x80 >> col)) {
                // 将字模放大2倍绘制（8x16 放大为 16x32）
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        my_draw_pixel(x + col * 2 + dx, y + row * 2 + dy, color);
                    }
                }
            }
        }
    }
}

// ==============================================================
// 4. 汉字绘制 (根据 HZK16 点阵字库渲染)
// ==============================================================
void my_draw_chinese(int x, int y, const unsigned char *ch, unsigned int color)
{
    if (hzk_fd < 0) return; // 字库没加载直接退出

    // 计算汉字在 GB2312 中的区位码
    int qu = ch[0] - 0xA1;
    int wei = ch[1] - 0xA1;
    if (qu < 0 || qu > 93 || wei < 0 || wei > 93) return;

    // 计算在文件中的偏移量 (每个汉字32字节)
    int offset = (qu * 94 + wei) * 32;
    unsigned char dot_buf[32];
    
    lseek(hzk_fd, offset, SEEK_SET);
    if (read(hzk_fd, dot_buf, 32) != 32) return;

    // 将16x16点阵放大2倍绘制
    for (int row = 0; row < 16; row++) {
        unsigned char high = dot_buf[row * 2];
        unsigned char low = dot_buf[row * 2 + 1];
        unsigned short dots = (high << 8) | low;

        for (int col = 0; col < 16; col++) {
            if (dots & (0x8000 >> col)) {
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        my_draw_pixel(x + col * 2 + dx, y + row * 2 + dy, color);
                    }
                }
            }
        }
    }
}

// ==============================================================
// 5. 中英混合文本绘制
// ==============================================================
void my_draw_text(int x, int y, const char *str, unsigned int color)
{
    int i = 0;
    int cur_x = x;
    while (str[i] != '\0') {
        if ((unsigned char)str[i] < 0x80) {
            // 遇到 ASCII 字符
            my_draw_char(cur_x, y, str[i], color);
            cur_x += 8 * 2; // 每次前进 8*2=16 像素
            i++;
        } else {
            // 遇到中文字符（GB2312编码占2字节）
            if (str[i+1] == '\0') break;
            my_draw_chinese(cur_x, y, (const unsigned char *)&str[i], color);
            cur_x += 16 * 2; // 每次前进 16*2=32 像素
            i += 2;
        }
    }
}

// ==============================================================
// 6. 计算文本占据的宽度（用于 UI 居中排版）
// ==============================================================
int get_text_width(const char *str)
{
    int w = 0;
    int i = 0;
    while (str[i] != '\0') {
        if ((unsigned char)str[i] < 0x80) {
            w += 8 * 2;
            i++;
        } else {
            w += 16 * 2;
            i += 2;
        }
    }
    return w;
}

// ==============================================================
// 7. 加载汉字字库文件
// ==============================================================
int hzk_init(const char *path)
{
    hzk_fd = open(path, O_RDONLY);
    if (hzk_fd < 0) {
        perror("open HZK16 failed");
        return -1;
    }
    printf("[OK] 汉字字库加载成功: %s\n", path);
    return 0;
}