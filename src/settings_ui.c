// settings_ui.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "draw_utils.h"
#include "ui_components.h"
#include "settings_ui.h"
#include "touch.h"
#include "voice_ui.h"

#define SCREEN_W 1024
#define SCREEN_H 600

extern AppPage current_page;
extern void refresh_ui(void);

// 设置页面的子状态
typedef enum {
    SETTING_MENU,       // 主菜单
    SETTING_WIFI_SCAN,  // WiFi扫描列表
    SETTING_ABOUT,      // 关于信息
    SETTING_FILE_BROWSER // 文件管理器
} SettingPageState;

static SettingPageState setting_state = SETTING_MENU;
static char wifi_list[20][50];
static int wifi_count = 0;

// ================== 新增：WiFi 列表滑动偏移量 ==================
static int wifi_scroll_offset = 0;
static const char *wifi_pwd = "YOUR_WIFI_PASSWORD";  /* 公开仓库示例：请改成你自己的 WiFi 密码 */ // 你的 WiFi 密码

// ================== 文件管理器静态数据 ==================
static char file_items[50][256];
static int file_is_dir[50];
static int file_count = 0;
static char current_dir_path[256] = "/";

// ==================== 1. WiFi 功能（核心修复：解决扫描不到的问题） ====================
void scan_wifi_networks(void) {
    wifi_count = 0;
    
    // 1. 强制拉起网卡
    system("ifconfig wlan0 up 2>/dev/null");
    system("iwconfig wlan0 power off 2>/dev/null");

    printf("[Setting] 正在扫描 WiFi...\n");
    
    // 2. 👇 核心修复：先主动触发一次扫描，并强制等待 2 秒钟，让驱动完成工作！
    system("iwlist wlan0 scan 2>/dev/null");
    sleep(2); 

    // 3. 再去读取扫描结果
    FILE *fp = popen("iwlist wlan0 scan 2>/dev/null | grep ESSID", "r");
    if (!fp) {
        printf("[Setting] 无法执行 WiFi 扫描命令\n");
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL && wifi_count < 20) {
        char *start = strstr(line, "ESSID:\"");
        if (start) {
            start += 7;
            char *end = strchr(start, '\"');
            if (end) {
                *end = '\0';
                strncpy(wifi_list[wifi_count], start, 49);
                wifi_count++;
                printf("[Setting] 扫描到 WiFi: %s\n", wifi_list[wifi_count-1]);
            }
        }
    }
    pclose(fp);
    printf("[Setting] 最终扫描到 %d 个 WiFi 网络\n", wifi_count);
}

// ==================== 连接 WiFi ====================
void connect_to_wifi(const char *ssid) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), 
        "echo -e 'ctrl_interface=/var/run/wpa_supplicant\\nnetwork={\\nssid=\"%s\"\\npsk=\"%s\"\\nkey_mgmt=WPA-PSK\\npriority=1\\n}' > /etc/wpa_supplicant.conf && killall wpa_supplicant 2>/dev/null; wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf -D nl80211; sleep 2; killall udhcpc 2>/dev/null; udhcpc -i wlan0 -b", 
        ssid, wifi_pwd);
    
    printf("[Setting] 执行命令: %s\n", cmd);
    int ret = system(cmd);
    
    if (ret != 0) {
        my_draw_text(SCREEN_W/2 - 150, SCREEN_H/2 + 40, "连接失败！请检查 WiFi 状态", COLOR_RED);
    } else {
        my_draw_text(SCREEN_W/2 - 100, SCREEN_H/2 + 40, "正在获取 IP 地址...", COLOR_ACCENT);
        // 异步触发 AI 语音连接（不阻塞触摸处理）
        voice_request_connect();
    }
}

// ==================== 2. 文件管理扫描功能 ====================
void scan_directory(const char *dirpath) {
    strncpy(current_dir_path, dirpath, 255);
    struct dirent *entry;
    struct stat st;
    DIR *dir = opendir(dirpath);
    file_count = 0;
    if (!dir) return;

    while ((entry = readdir(dir)) != NULL && file_count < 50) {
        if (entry->d_name[0] == '.') continue;
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", dirpath, entry->d_name);
        if (stat(full_path, &st) == 0) {
            strncpy(file_items[file_count], entry->d_name, 255);
            file_is_dir[file_count] = S_ISDIR(st.st_mode);
            file_count++;
        }
    }
    closedir(dir);
}

// ==================== 3. UI 绘制函数 ====================

void draw_settings_menu(void) {
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++) my_draw_pixel(x, y, COLOR_BG);
    draw_rounded_rect_fill(10, 10, 80, 40, 8, COLOR_BTN_BACK);
    my_draw_text(18, 22, "返回", COLOR_BTN_TEXT);
    my_draw_text(100, 20, "【 系统设置 】", COLOR_ACCENT);

    int card_w = 280, card_h = 160;
    int start_x = (SCREEN_W - 3 * card_w - 2 * 40) / 2;
    int card_top = 150;

    const char *titles[3] = {"Wi-Fi 网络", "设备信息", "文件管理"};
    
    for (int i = 0; i < 3; i++) {
        int x = start_x + i * (card_w + 40);
        draw_rounded_rect_fill(x, card_top, card_w, card_h, 16, COLOR_CARD);
        my_draw_text(x + (card_w - get_text_width(titles[i]))/2, card_top + 70, titles[i], COLOR_TEXT_T);
    }
}

// ==================== 修复 WiFi 列表滚动显示 ====================
void draw_wifi_list(void) {
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++) my_draw_pixel(x, y, COLOR_BG);
    draw_rounded_rect_fill(10, 10, 80, 40, 8, COLOR_BTN_BACK);
    my_draw_text(18, 22, "返回", COLOR_BTN_TEXT);
    my_draw_text(100, 20, "【 可用 WiFi 网络 】", COLOR_ACCENT);

    if (wifi_count == 0) {
        scan_wifi_networks();
    }

    int y_start = 80;
    for (int i = wifi_scroll_offset; i < wifi_count && i < wifi_scroll_offset + 8; i++) {
        draw_rounded_rect_fill(30, y_start, SCREEN_W - 60, 50, 10, COLOR_CARD);
        my_draw_text(50, y_start + 15, wifi_list[i], COLOR_TEXT_T);
        y_start += 70;
    }
    if (wifi_count == 0) {
        my_draw_text(SCREEN_W/2 - 100, SCREEN_H/2, "正在扫描 WiFi 信号，请稍候...", COLOR_TEXT_G);
    }
}

void draw_about_page(void) {
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++) my_draw_pixel(x, y, COLOR_BG);
    draw_rounded_rect_fill(10, 10, 80, 40, 8, COLOR_BTN_BACK);
    my_draw_text(18, 22, "返回", COLOR_BTN_TEXT);
    my_draw_text(100, 20, "【 设备信息 】", COLOR_ACCENT);
    my_draw_text(100, 100, "设备型号: RK1808 ", COLOR_TEXT_T);
    my_draw_text(100, 160, "系统版本: v1.0.0", COLOR_TEXT_T);
    my_draw_text(100, 220, "屏幕分辨率: 1024x600", COLOR_TEXT_T);
}

void draw_file_browser(void) {
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++) my_draw_pixel(x, y, COLOR_BG);
    draw_rounded_rect_fill(10, 10, 80, 40, 8, COLOR_BTN_BACK);
    my_draw_text(18, 22, "返回", COLOR_BTN_TEXT);
    my_draw_text(100, 20, "【 文件管理器 】", COLOR_ACCENT);
    my_draw_text(100, 60, current_dir_path, COLOR_TEXT_G);

    int y_start = 100;
    for (int i = 0; i < file_count && i < 10; i++) {
        draw_rounded_rect_fill(30, y_start, SCREEN_W - 60, 40, 8, COLOR_CARD);
        if (file_is_dir[i]) {
            char name[260];
            snprintf(name, sizeof(name), "%s/", file_items[i]);
            my_draw_text(50, y_start + 10, name, COLOR_ACCENT); 
        } else {
            my_draw_text(50, y_start + 10, file_items[i], COLOR_TEXT_T);
        }
        y_start += 50;
    }
}

// ==================== 4. 模块入口和触摸交互 ====================
void settings_init(void) {
    setting_state = SETTING_MENU;
    draw_settings_menu();
}

void settings_handle_touch(int action, int x, int y) {
    if (action == TOUCH_CLICK && x >= 10 && x <= 90 && y >= 10 && y <= 50) {
        if (setting_state == SETTING_FILE_BROWSER) {
            if (strcmp(current_dir_path, "/") == 0) {
                setting_state = SETTING_MENU;
                draw_settings_menu();
            } else {
                char *last_slash = strrchr(current_dir_path, '/');
                if (last_slash != NULL) {
                    *last_slash = '\0';
                    if (strlen(current_dir_path) == 0) {
                        strcpy(current_dir_path, "/");
                    }
                    scan_directory(current_dir_path);
                    draw_file_browser();
                } else {
                    setting_state = SETTING_MENU;
                    draw_settings_menu();
                }
            }
            return;
        } 
        else if (setting_state != SETTING_MENU) {
            setting_state = SETTING_MENU;
            draw_settings_menu();
        } else {
            current_page = PAGE_HOME;
            refresh_ui();
        }
        return;
    }

    if (setting_state == SETTING_MENU) {
        if (action == TOUCH_CLICK) {
            int card_w = 280, card_h = 160;
            int start_x = (SCREEN_W - 3 * card_w - 2 * 40) / 2;
            int card_top = 150; /* 重命名避免遮蔽参数 y */

            for (int i = 0; i < 3; i++) {
                int card_x = start_x + i * (card_w + 40);
                if (x >= card_x && x <= card_x + card_w && y >= card_top && y <= card_top + card_h) {
                    if (i == 0) { // Wi-Fi
                        setting_state = SETTING_WIFI_SCAN;
                        wifi_scroll_offset = 0; 
                        wifi_count = 0;         // 重置计数，触发重新扫描
                        draw_wifi_list();
                    } else if (i == 1) { // 关于
                        setting_state = SETTING_ABOUT;
                        draw_about_page();
                    } else if (i == 2) { // 文件管理
                        setting_state = SETTING_FILE_BROWSER;
                        scan_directory("/"); 
                        draw_file_browser();
                    }
                    return;
                }
            }
        }
    } 
    else if (setting_state == SETTING_WIFI_SCAN) {
        // -------- WiFi 列表 ----------
        if (action == TOUCH_SLIDE_UP) {
            if (wifi_scroll_offset + 8 < wifi_count) {
                wifi_scroll_offset += 4;
                if (wifi_scroll_offset + 8 > wifi_count) wifi_scroll_offset = wifi_count - 8;
                if (wifi_scroll_offset < 0) wifi_scroll_offset = 0;
                draw_wifi_list();
            }
            return;
        }
        if (action == TOUCH_SLIDE_DOWN) {
            if (wifi_scroll_offset > 0) {
                wifi_scroll_offset -= 4;
                if (wifi_scroll_offset < 0) wifi_scroll_offset = 0;
                draw_wifi_list();
            }
            return;
        }

        if (action == TOUCH_CLICK) {
            int y_start = 80;
            for (int i = wifi_scroll_offset; i < wifi_count && i < wifi_scroll_offset + 8; i++) {
                int btn_y = y_start + (i - wifi_scroll_offset) * 70;
                if (x >= 30 && x <= SCREEN_W - 30 && y >= btn_y && y <= btn_y + 50) {
                    printf("[Setting] 尝试连接 WiFi: %s\n", wifi_list[i]);
                    connect_to_wifi(wifi_list[i]);
                    return;
                }
            }
        }
    }
    else if (setting_state == SETTING_FILE_BROWSER) {
        if (action == TOUCH_CLICK) {
            int y_start = 100;
            for (int i = 0; i < file_count && i < 10; i++) {
                int btn_y = y_start + i * 50;
                if (x >= 30 && x <= SCREEN_W - 30 && y >= btn_y && y <= btn_y + 40) {
                    if (file_is_dir[i]) {
                        char new_path[512];
                        snprintf(new_path, sizeof(new_path), "%s/%s", current_dir_path, file_items[i]);
                        scan_directory(new_path);
                        draw_file_browser();
                    } else {
                        printf("[File] 点击了文件: %s/%s\n", current_dir_path, file_items[i]);
                        my_draw_text(SCREEN_W/2 - 100, SCREEN_H/2, "暂不支持打开此文件", COLOR_RED);
                    }
                    return;
                }
            }
        }
    }
}