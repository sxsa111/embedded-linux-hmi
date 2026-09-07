#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/fb.h>
#include <linux/input.h>

#include "draw_utils.h"
#include "ui_components.h"
#include "touch.h"
#include "voice_ui.h"
#include "draw_ui.h"
#include "album_ui.h"
#include "settings_ui.h"

#define SCREEN_W 1024
#define SCREEN_H 600

/* ====== 主界面 GB2312 中文字符串常量（源文件 UTF-8，编译后 hex 数组是 GB2312） ====== */
static const unsigned char GB_WELCOME[]  = {0xBB,0xB6,0xD3,0xAD,0xB9,0xE2,0xC1,0xD9,0};        /* "欢迎光临" */
static const unsigned char GB_WIFI_ON[]  = {0x57,0x49,0x46,0x49,0xD2,0xD1,0xC1,0xAC,0xBD,0xD3,0}; /* "WIFI已连接" */
static const unsigned char GB_WIFI_OFF[] = {0x57,0x49,0x46,0x49,0xCE,0xB4,0xC1,0xAC,0xBD,0xD3,0}; /* "WIFI未连接" */
static const unsigned char GB_VOICE[]    = {0xD3,0xEF,0xD2,0xF4,0xD6,0xFA,0xCA,0xD6,0};        /* "语音助手" */
static const unsigned char GB_HINT[]     = {0xC7,0xEB,0xCB,0xB5,0xB3,0xF6,0xC4,0xFA,0xB5,0xC4,0xD0,0xE8,0xC7,0xF3,0}; /* "请说出您的需求" */
static const unsigned char GB_DRAW[]     = {0xD6,0xC7,0xBB,0xDB,0xBB,0xAD,0xB0,0xE5,0};        /* "智慧画板" */
static const unsigned char GB_ALBUM[]    = {0xCE,0xD2,0xB5,0xC4,0xCF,0xE0,0xB2,0xE1,0};        /* "我的相册" */
static const unsigned char GB_SETTINGS[] = {0xCF,0xB5,0xCD,0xB3,0xC9,0xE8,0xD6,0xC3,0};        /* "系统设置" */

/* GB2312 中文字符串常量以上已定义，颜色宏定义在 ui_components.h 中 */

AppPage current_page = PAGE_HOME;

// 其他未开发模块的声明
void file_browser_init(void);

// ==============================================================
// 1. 显存初始化
// ==============================================================
int fb_init(void)
{
    int fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd == -1) { perror("open /dev/fb0 failed"); return -1; }
    struct fb_fix_screeninfo finfo;
    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) == -1) { perror("ioctl failed"); close(fbfd); return -1; }
    line_len = finfo.line_length;
    unsigned int screen_size = line_len * SCREEN_H;
    fbp = mmap(NULL, screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (fbp == MAP_FAILED) { perror("mmap failed"); close(fbfd); return -1; }
    system("echo 1 > /sys/class/graphics/fb0/blank");
    system("echo 0 > /sys/class/graphics/fb0/blank");
    return fbfd;
}

// ==============================================================
// 2. 网络状态检测
// ==============================================================
int check_wifi_ip(void) {
    /*
     * WiFi 自动恢复机制：
     *   Broadcom AP6212 驱动（dhd）在某些情况下会固件崩溃（FW TRAP），
     *   导致 wlan0 变为 DOWN、IP 丢失。"ip link set up" 可触发芯片
     *   重新上电 + 固件重载 + 重连 AP，DHCP 续租恢复 IP。
     *   每 30 个周期（~60秒）允许一次恢复尝试，避免死循环。
     */
    static int recovery_cooldown = 0;
    
    /* 1. 检查 operstate */
    FILE *fp = fopen("/sys/class/net/wlan0/operstate", "r");
    if (!fp) {
        /* wlan0 设备不存在 */
        return 0;
    }
    char state[16] = {0};
    int n = fread(state, 1, sizeof(state) - 1, fp);
    fclose(fp);
    if (n <= 0) return 0;

    int link_up = (strncmp(state, "up", 2) == 0);

    /* 2. 检查 IP */
    fp = popen("ip -4 addr show wlan0 2>/dev/null | grep -q 'inet '", "r");
    int ip_ok = (pclose(fp) == 0);

    /* 3. 正常：在线+有IP */
    if (link_up && ip_ok) {
        recovery_cooldown = 0;
        return 1;
    }

    /* 4. 异常：尝试自动恢复（有冷却间隔） */
    if (recovery_cooldown > 0) {
        recovery_cooldown--;
        return 0;
    }

    /* 驱动崩溃后 operstate 变 "down"，尝试拉起接口 */
    if (!link_up) {
        printf("[WiFi] 检测到 wlan0 DOWN，尝试重启接口...\n");
        int ret = system("ip link set wlan0 up 2>/dev/null");
        if (ret != 0) {
            printf("[WiFi] 接口启动失败\n");
            recovery_cooldown = 30;
            return 0;
        }
        /* 等 2 秒让固件加载完成 */
        sleep(2);
        recovery_cooldown = 60;
        return 0; /* 下一轮再检查 IP */
    }

    /* 接口 UP 但无 IP，续租 DHCP */
    if (!ip_ok) {
        printf("[WiFi] wlan0 无 IP，尝试 DHCP 续租...\n");
        system("udhcpc -i wlan0 -t 5 -n 2>/dev/null &");
        recovery_cooldown = 60;
        return 0;
    }

    recovery_cooldown = 60;
    return 0;
}

// ==============================================================
// 3. 主界面 UI 渲染（新拟态风格）
// ==============================================================

static void draw_card_shadow(int x, int y, int w, int h, int r)
{
    // 模拟新拟态阴影：底部和右侧稍深，顶部和左侧稍亮
    unsigned int dark  = 0xFFD0D0D5;  /* 右下角阴影 */
    unsigned int light = 0xFFFFFFFF;  /* 左上角高光 */

    // 底部微阴影（1px）
    for (int dx = r; dx < w - r; dx++)
        my_draw_pixel(x + dx, y + h, dark);
    // 右侧微阴影（1px）
    for (int dy = r; dy < h - r; dy++)
        my_draw_pixel(x + w, y + dy, dark);
}

void draw_home_page(void)
{
    /* 1. 背景 */
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            my_draw_pixel(x, y, COLOR_BG);

    /* 2. 顶部状态栏 */
    // 左侧竖线装饰 + 欢迎光临
    draw_vertical_bar(20, 24, 28, 4, COLOR_ACCENT);
    my_draw_text(36, 24, (const char*)GB_WELCOME, COLOR_TEXT_T);

    // 右侧 WiFi 状态
    int wifi_connected = check_wifi_ip();
    const char *wifi_str = wifi_connected ? (const char*)GB_WIFI_ON : (const char*)GB_WIFI_OFF;
    int wifi_w = get_text_width(wifi_str);
    int wifi_x = SCREEN_W - 20 - wifi_w;
    my_draw_text(wifi_x, 24, wifi_str, wifi_connected ? COLOR_TEXT_T : COLOR_TEXT_G);
    // 简单 WiFi 信号图标（三个小圆点 + 弧线）
    int sig_x = wifi_x - 35;
    int sig_y = 38;
    if (wifi_connected) {
        draw_solid_circle(sig_x + 4,  sig_y, 2, COLOR_ACCENT);
        draw_solid_circle(sig_x + 12, sig_y, 2, COLOR_ACCENT);
        draw_solid_circle(sig_x + 20, sig_y, 2, COLOR_ACCENT);
        draw_arc(sig_x + 12, sig_y, 14, 210, 330, COLOR_ACCENT, 2);
    } else {
        draw_solid_circle(sig_x + 4,  sig_y, 2, COLOR_TEXT_G);
        draw_solid_circle(sig_x + 12, sig_y, 2, COLOR_TEXT_G);
        draw_solid_circle(sig_x + 20, sig_y, 2, COLOR_TEXT_G);
    }

    /* 3. 左侧大卡片 — 语音助手 */
    int v_x = 20, v_y = 70, v_w = 480, v_h = 510, v_r = 24;
    draw_rounded_rect_fill(v_x, v_y, v_w, v_h, v_r, COLOR_CARD);
    draw_rounded_rect(v_x, v_y, v_w, v_h, v_r, COLOR_CARD_BD);

    // 麦克风图标（蓝色实心圆 + 白色竖线）
    int mic_cx = v_x + v_w / 2;      // 260
    int mic_cy = v_y + 130;          // 200
    int mic_r = 40;
    draw_solid_circle(mic_cx, mic_cy, mic_r, COLOR_ACCENT);
    // 麦克风头（竖线）
    draw_line(mic_cx, mic_cy - mic_r - 10, mic_cx, mic_cy - 5, 0xFFFFFFFF, 4);
    // 麦克风底座（横线）
    draw_line(mic_cx - 20, mic_cy + mic_r + 5, mic_cx + 20, mic_cy + mic_r + 5, 0xFFFFFFFF, 3);
    // 支架竖线
    draw_line(mic_cx, mic_cy + 5, mic_cx, mic_cy + mic_r + 5, 0xFFFFFFFF, 3);

    // 声波弧线（左右各3条）
    for (int i = 0; i < 3; i++) {
        int rr = 55 + i * 16;
        // 左侧弧线
        draw_arc(mic_cx, mic_cy, rr, 210, 330, COLOR_ACCENT_L, 2);
        // 右侧弧线
        draw_arc(mic_cx, mic_cy, rr, 30, 150, COLOR_ACCENT_L, 2);
    }

    // 文字
    int title_w = get_text_width((const char*)GB_VOICE);
    my_draw_text(v_x + (v_w - title_w) / 2, v_y + 250, (const char*)GB_VOICE, COLOR_TEXT_T);
    int hint_w = get_text_width((const char*)GB_HINT);
    my_draw_text(v_x + (v_w - hint_w) / 2, v_y + 300, (const char*)GB_HINT, COLOR_TEXT_G);

    /* 4. 右上横条卡片 — 智慧画板 */
    int d_x = 520, d_y = 70, d_w = 484, d_h = 130, d_r = 20;
    draw_rounded_rect_fill(d_x, d_y, d_w, d_h, d_r, COLOR_CARD);
    draw_rounded_rect(d_x, d_y, d_w, d_h, d_r, COLOR_CARD_BD);

    // 画板图标（蓝色小方块 + 圆点）
    int icon_x = d_x + 40;
    int icon_y = d_y + 45;
    // 画板外框
    draw_rounded_rect_fill(icon_x, icon_y, 40, 40, 6, COLOR_ACCENT);
    // 画板上的小圆点（模拟画笔/图片）
    draw_solid_circle(icon_x + 20, icon_y + 20, 8, 0xFFFFFFFF);
    // 文字
    my_draw_text(icon_x + 60, icon_y + 5, (const char*)GB_DRAW, COLOR_TEXT_T);
    // 右箭头
    draw_right_arrow(d_x + d_w - 40, d_y + d_h / 2, 12, COLOR_TEXT_G);

    /* 5. 右下左卡片 — 我的相册 */
    int a_x = 520, a_y = 220, a_w = 232, a_h = 360, a_r = 20;
    draw_rounded_rect_fill(a_x, a_y, a_w, a_h, a_r, COLOR_CARD);
    draw_rounded_rect(a_x, a_y, a_w, a_h, a_r, COLOR_CARD_BD);

    // 相册图标（两个重叠的矩形）
    int a_icon_x = a_x + a_w / 2 - 20;
    int a_icon_y = a_y + 80;
    draw_rounded_rect_fill(a_icon_x - 5, a_icon_y - 5, 50, 40, 4, COLOR_ACCENT);
    draw_rounded_rect_fill(a_icon_x + 5, a_icon_y + 5, 50, 40, 4, COLOR_ACCENT_L);
    // 小图标记（小圆点）
    draw_solid_circle(a_icon_x + 15, a_icon_y + 15, 6, 0xFFFFFFFF);
    // 文字
    int album_w = get_text_width((const char*)GB_ALBUM);
    my_draw_text(a_x + (a_w - album_w) / 2, a_y + 160, (const char*)GB_ALBUM, COLOR_TEXT_T);

    /* 6. 右下右卡片 — 系统设置 */
    int s_x = 772, s_y = 220, s_w = 232, s_h = 360, s_r = 20;
    draw_rounded_rect_fill(s_x, s_y, s_w, s_h, s_r, COLOR_CARD);
    draw_rounded_rect(s_x, s_y, s_w, s_h, s_r, COLOR_CARD_BD);

    // 设置图标（齿轮模拟：一个圆 + 几条放射线）
    int s_icon_x = s_x + s_w / 2;
    int s_icon_y = s_y + 100;
    draw_solid_circle(s_icon_x, s_icon_y, 25, COLOR_ACCENT);
    // 齿轮齿（6条短线）
    for (int i = 0; i < 6; i++) {
        double angle = i * 3.14159265 / 3.0;
        int x1 = s_icon_x + (int)(18 * cos(angle));
        int y1 = s_icon_y + (int)(18 * sin(angle));
        int x2 = s_icon_x + (int)(30 * cos(angle));
        int y2 = s_icon_y + (int)(30 * sin(angle));
        draw_line(x1, y1, x2, y2, COLOR_ACCENT, 3);
    }
    // 中心白圆
    draw_solid_circle(s_icon_x, s_icon_y, 8, 0xFFFFFFFF);
    // 文字
    int set_w = get_text_width((const char*)GB_SETTINGS);
    my_draw_text(s_x + (s_w - set_w) / 2, s_y + 160, (const char*)GB_SETTINGS, COLOR_TEXT_T);
}

void refresh_ui(void)
{
    switch (current_page) {
        case PAGE_HOME:   draw_home_page(); break;
        case PAGE_VOICE:  voice_init(); break;
        case PAGE_DRAW:   draw_init(); break;
        case PAGE_ALBUM:  album_init(); break;
        case PAGE_FILE:   file_browser_init(); break;
        case PAGE_SETTING: settings_init(); break;
    }
}

void handle_touch_event(int action, int x, int y)
{
    if (current_page != PAGE_HOME) {
        if (current_page == PAGE_VOICE) {
            voice_handle_touch(action, x, y);
            return; 
        } else if (current_page == PAGE_DRAW) {
            draw_handle_touch(action, x, y);
            return; 
        } else if (current_page == PAGE_ALBUM) {
            album_handle_touch(action, x, y);
            return;
        } else if (current_page == PAGE_SETTING) {
            settings_handle_touch(action, x, y);
            return;
        }
        if (action == TOUCH_CLICK) { 
            current_page = PAGE_HOME;
            refresh_ui();
        }
        return;
    }

    if (action == TOUCH_CLICK) {
        // 左侧大卡片 — 语音助手
        if (x >= 20 && x <= 500 && y >= 70 && y <= 580) {
            current_page = PAGE_VOICE;
            refresh_ui();
            return;
        }
        // 右上横条 — 智慧画板
        if (x >= 520 && x <= 1004 && y >= 70 && y <= 200) {
            current_page = PAGE_DRAW;
            refresh_ui();
            return;
        }
        // 右下左 — 我的相册
        if (x >= 520 && x <= 752 && y >= 220 && y <= 580) {
            current_page = PAGE_ALBUM;
            refresh_ui();
            return;
        }
        // 右下右 — 系统设置
        if (x >= 772 && x <= 1004 && y >= 220 && y <= 580) {
            current_page = PAGE_SETTING;
            refresh_ui();
            return;
        }
    }
}

// ==============================================================
// 4. 主函数
// ==============================================================
int main(int argc, char *argv[])
{
    if (fb_init() < 0) return -1;
    if (hzk_init("./HZK16") < 0) printf("未找到 HZK16\n");
    if (touch_init() < 0) return -1;
    
    // 一开始不强制连AI语音，让它在主循环里检测到有网了再自动连
    refresh_ui();

    int touch_fd = touch_get_fd();
    fd_set readfds;

    // 用于网络状态轮询的计数器
    static int net_check_cnt = 0;
    // 重连退避：防止 WiFi 假在线（驱动崩溃但 IP 未释放）时反复重连刷屏
    static int reconnect_attempts = 0;
    static int reconnect_cooldown = 0;

    while (1) {
        // ============= 动态网络状态监控 =============
        net_check_cnt++;
        if (net_check_cnt % 200 == 0) { // 大约 2-3 秒检查一次网络状态
            int current_net_status = check_wifi_ip();
            // 如果网络好了，但语音模块还没开启，则开启语音连网
            if (current_net_status && !voice_is_network_enabled()) {
                printf("[System] 检测到 WiFi 已连上并分配了 IP，启动 AI 语音服务...\n");
                voice_allow_connection(1);
                // 刷新主界面上的 WiFi 状态（以防退出设置页时不刷新）
                if (current_page == PAGE_HOME) draw_home_page();
            } 
            // 如果网络断了，但语音模块还开着，强制关闭语音模块
            else if (!current_net_status && voice_is_network_enabled()) {
                printf("[System] 检测到 WiFi 掉线，断开 AI 语音服务...\n");
                voice_allow_connection(0);
                reconnect_attempts = 0;
                reconnect_cooldown = 0;
                if (current_page == PAGE_HOME) draw_home_page();
            }
            // 如果网络在线，但 socket 断了（比如服务器重启），触发重连
            else if (current_net_status && voice_is_network_enabled()) {
                int ts = voice_get_text_sock();
                int as = voice_get_audio_sock();
                if (ts <= 0 || as <= 0) {
                    if (reconnect_cooldown > 0) {
                        reconnect_cooldown--;
                    } else {
                        printf("[System] 检测到语音 socket 断线 (text=%d, audio=%d)，正在重连...\n", ts, as);
                        voice_init_network();
                        reconnect_attempts++;
                        /* 指数退避：每次失败后冷却时间翻倍，上限 60 次（约 2 分钟） */
                        reconnect_cooldown = (reconnect_attempts < 10) ? reconnect_attempts * 3
                                           : (reconnect_attempts < 30) ? reconnect_attempts
                                           : 60;
                        /* 如果连续 10 次重连都失败，WiFi 很可能已经假在线，强制断开 */
                        if (reconnect_attempts >= 10) {
                            int verify = check_wifi_ip();
                            if (!verify) {
                                printf("[System] WiFi 二次确认失败，强制断开语音服务...\n");
                                voice_allow_connection(0);
                                reconnect_attempts = 0;
                                reconnect_cooldown = 0;
                                if (current_page == PAGE_HOME) draw_home_page();
                            }
                        }
                    }
                } else {
                    /* socket 正常，重置计数器 */
                    reconnect_attempts = 0;
                    reconnect_cooldown = 0;
                }
            }
        }
        // ================================================

        // 每次循环动态更新 max_fd（socket 断线重连后 fd 值会变化）
        int text_sock  = voice_get_text_sock();
        int audio_sock = voice_get_audio_sock();
        int max_fd = touch_fd;
        if (text_sock  > max_fd) max_fd = text_sock;
        if (audio_sock > max_fd) max_fd = audio_sock;

        FD_ZERO(&readfds);
        FD_SET(touch_fd, &readfds);
        if (text_sock > 0) FD_SET(text_sock, &readfds);
        if (audio_sock > 0) FD_SET(audio_sock, &readfds);
        
        struct timeval tv = { 0, 10000 }; // 10ms 超时，避免 net_check_cnt 轮询被 select 永久阻塞
        select(max_fd + 1, &readfds, NULL, NULL, &tv);

        // 处理触摸
        if (FD_ISSET(touch_fd, &readfds)) {
            int action = touch_read_action();
            if (action != TOUCH_NONE) handle_touch_event(action, touch_x(), touch_y());
        }

        // 处理服务器文本
        if (text_sock > 0 && FD_ISSET(text_sock, &readfds)) {
            voice_check_network(); 
        }

        // 处理服务器音频
        if (audio_sock > 0 && FD_ISSET(audio_sock, &readfds)) {
            voice_recv_and_play_audio(); 
        }
    }
    return 0;
}