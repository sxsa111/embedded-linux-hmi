#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include "draw_utils.h"
#include "ui_components.h"
#include "voice_ui.h"
#include "touch.h"

extern AppPage current_page;
extern void refresh_ui(void);

// 👇 请确保这里填的是你电脑端的真实 IPv4 地址
#define SERVER_IP "192.168.1.100"  /* TODO: 改成运行 ai_server/as.py 的电脑 IPv4 */
#define PORT 8080
#define AUDIO_PORT 8081
#define RECORD_PORT 8082

static int network_ready = 0; 
static int text_sock = -1;
static int audio_sock = -1;

#define MAX_VOICE_LINE 120
#define MAX_LINE_PIXELS 1004   /* 1024 - 20 margin */
typedef struct { char text[256]; unsigned int color; } VoiceLine;
VoiceLine voice_history[MAX_VOICE_LINE];
int voice_count = 0;
int is_recording = 0;
static int history_offset = 0;

/* ====== GB2312 字符串常量（源文件 UTF-8，运行时须发 GB2312） ====== */
/* 这些 hex 数组在编译后就是纯粹的 GB2312 字节序列，
 * 与 my_draw_text(HZK16 GB2312字库) 和服务端发来的数据完全匹配。 */
static const unsigned char GB_BACK[]         = {0xB7, 0xB5, 0xBB, 0xD8, 0};                          /* "返回" */
static const unsigned char GB_TITLE[]        = {0xA1, 0xBE, 0x20, 0x41, 0x49, 0x20, 0xD3, 0xEF, 0xD2, 0xF4, 0xD6, 0xFA, 0xCA, 0xD6, 0x20, 0xA1, 0xBF, 0}; /* "【 AI 语音助手 】" */
static const unsigned char GB_HINT[]         = {0xB5, 0xE3, 0xBB, 0xF7, 0xB5, 0xD7, 0xB2, 0xBF, 0xC0, 0xB6, 0xC9, 0xAB, 0xB0, 0xB4, 0xC5, 0xA5, 0xBF, 0xAA, 0xCA, 0xBC, 0xCB, 0xB5, 0xBB, 0xB0, 0xA3, 0xAC, 0xC9, 0xCF, 0xCF, 0xC2, 0xBB, 0xAC, 0xB6, 0xAF, 0xBF, 0xB4, 0xC0, 0xFA, 0xCA, 0xB7, 0}; /* "点击底部蓝色按钮开始说话，上下滑动看历史" */
static const unsigned char GB_TAP_SPEAK[]    = {0xB5, 0xE3, 0xBB, 0xF7, 0xCB, 0xB5, 0xBB, 0xB0, 0};  /* "点击说话" */
static const unsigned char GB_RECORDING[]    = {0xC2, 0xBC, 0xD2, 0xF4, 0xD6, 0xD0, 0x2E, 0x2E, 0x2E, 0}; /* "录音中..." */
static const unsigned char GB_RECOGNIZING[]  = {0xC4, 0xE3, 0xA3, 0xBA, 0xD5, 0xFD, 0xD4, 0xDA, 0xCA, 0xB6, 0xB1, 0xF0, 0x2E, 0x2E, 0x2E, 0}; /* "你：正在识别..." */
static const unsigned char GB_UPLOAD_FAIL[]  = {0xC9, 0xCF, 0xB4, 0xAB, 0xCA, 0xA7, 0xB0, 0xDC, 0xA3, 0xAC, 0xC7, 0xEB, 0xBC, 0xEC, 0xB2, 0xE9, 0xCD, 0xF8, 0xC2, 0xE7, 0xBB, 0xF2, 0xB7, 0xC0, 0xBB, 0xF0, 0xC7, 0xBD, 0}; /* "上传失败，请检查网络或防火墙" */
/* 前缀比较用（不带结尾0，strncmp 精确长度比较） */
static const unsigned char GB_NI[]           = {0xC4, 0xE3, 0xA3, 0xBA};       /* "你：" 4字节 */
static const unsigned char GB_AI[]           = {0x41, 0x49, 0xA3, 0xBA};       /* "AI：" 4字节 */

int voice_record_audio(const char *filename, int duration);
int voice_upload_record(const char *filename);
void voice_add_chat_line(const char *text, unsigned int color);

void voice_draw_ui(void)
{
    /* 浅白蓝主题背景 */
    for (int y = 0; y < 600; y++)
        for (int x = 0; x < 1024; x++) my_draw_pixel(x, y, COLOR_BG);

    /* 左上角返回按钮（浅灰卡片 + 深色文字） */
    draw_rounded_rect_fill(10, 10, 80, 40, 8, COLOR_BTN_BACK);
    my_draw_text(18, 22, (const char*)GB_BACK, COLOR_BTN_TEXT);

    /* 标题 */
    my_draw_text(120, 20, (const char*)GB_TITLE, COLOR_ACCENT);
    my_draw_text(120, 60, (const char*)GB_HINT, COLOR_TEXT_G);

    int y_line = 100;
    int line_height = 40;
    int max_display_lines = 10;
    int start = history_offset;
    if (start < 0) start = 0;
    if (start > voice_count) start = voice_count;
    if (start + max_display_lines > voice_count) {
        start = voice_count - max_display_lines;
        if (start < 0) start = 0;
    }

    for (int i = start; i < voice_count && i < start + max_display_lines; i++) {
        my_draw_text(20, y_line, voice_history[i].text, voice_history[i].color);
        y_line += line_height;
    }

    /* 底部录音按钮 */
    int btn_x = (1024 - 200) / 2;
    int btn_y = 500;
    unsigned int bg_color = is_recording ? COLOR_RED : COLOR_ACCENT;
    const char *txt = is_recording ? (const char*)GB_RECORDING : (const char*)GB_TAP_SPEAK;
    draw_rounded_rect_fill(btn_x, btn_y, 200, 80, 20, bg_color);
    my_draw_text(btn_x + 50, btn_y + 30, txt, COLOR_WHITE);
}

void voice_init(void) {
    voice_draw_ui();
}

void voice_handle_touch(int action, int x, int y)
{
    if (action == TOUCH_CLICK && x > 10 && x < 90 && y > 10 && y < 50) {
        current_page = PAGE_HOME;
        refresh_ui(); 
        return;
    }

    if (action == TOUCH_SLIDE_UP) {
        if (history_offset > 0) {
            history_offset -= 3; 
            if (history_offset < 0) history_offset = 0;
            voice_draw_ui();
        }
        return;
    }
    if (action == TOUCH_SLIDE_DOWN) {
        if (history_offset + 10 < voice_count) {
            history_offset += 3; 
            voice_draw_ui();
        }
        return;
    }

    if (action == TOUCH_CLICK) {
        int btn_x = (1024 - 200) / 2;
        int btn_y = 500;
        if (x >= btn_x && x <= btn_x + 200 && y >= btn_y && y <= btn_y + 80) {
            if (!is_recording) {
                is_recording = 1;
                voice_draw_ui();
                if (voice_record_audio("/tmp/record.wav", 5) == 0) {
                    voice_add_chat_line((const char*)GB_RECOGNIZING, COLOR_TEXT_G);
                    if (voice_upload_record("/tmp/record.wav") < 0) {
                        voice_add_chat_line((const char*)GB_UPLOAD_FAIL, COLOR_RED);
                    }
                }
                is_recording = 0;
                voice_draw_ui();
            }
        }
    }
}

int voice_record_audio(const char *filename, int duration)
{
    char cmd[512];
    /* 修复录音截断的三个问题：
     * 1. hw:1,0 → plughw:1,0 — hw 要求硬件原生支持 S16_LE/16kHz，
     *    RK1808 音频编解码器原生格式往往是 48kHz 立体声，不加 plug
     *    插件会直接失败或立即 xrun（缓冲区溢出），录音瞬间截断。
     * 2. --buffer-time=500000 — 500ms 大缓冲区，防止系统繁忙时 xrun。
     * 3. 去掉 2>/dev/null 改为重定向到日志文件，方便排查。 */
    snprintf(cmd, sizeof(cmd),
        "arecord -D plughw:1,0 -d %d -c 1 -r 16000 -t wav -f S16_LE "
        "--buffer-time=500000 %s 2>>/tmp/arecord_err.log",
        duration, filename);
    printf("[Record] 开始录音 %d 秒...\n", duration);
    int ret = system(cmd);
    if (ret != 0) {
        printf("[Record] arecord 返回错误码 %d，请查看 /tmp/arecord_err.log\n", ret);
    }
    /* 验证录音文件确实有内容（空文件 = 录音失败） */
    FILE *check = fopen(filename, "rb");
    if (check) {
        fseek(check, 0, SEEK_END);
        long sz = ftell(check);
        fclose(check);
        if (sz <= 44) { /* WAV 头 44 字节，只有头 = 没录到 */
            printf("[Record] 录音文件仅 %ld 字节，录音失败！\n", sz);
            return -1;
        }
        printf("[Record] 录音完成，文件大小 %ld 字节\n", sz);
    } else {
        printf("[Record] 录音文件不存在！\n");
        return -1;
    }
    return ret;
}

int voice_upload_record(const char *filename)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(RECORD_PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) {
            close(sock); return -1; 
        }
        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(sock, &fdset);
        struct timeval tv;
        tv.tv_sec = 3;
        tv.tv_usec = 0;

        if (select(sock + 1, NULL, &fdset, NULL, &tv) == 1) {
            int so_error;
            socklen_t len = sizeof(so_error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
            if (so_error != 0) {
                fcntl(sock, F_SETFL, flags); /* 恢复阻塞模式再关闭 */
                close(sock); return -1;
            }
        } else {
            fcntl(sock, F_SETFL, flags); /* 超时也要恢复再关闭 */
            close(sock);
            printf("[Upload] 上传超时(连不上电脑)，请检查防火墙！\n");
            return -1; 
        }
    }
    fcntl(sock, F_SETFL, flags);

    FILE *fp = fopen(filename, "rb");
    if (!fp) { close(sock); return -1; }
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned int net_size = htonl(file_size);
    send(sock, &net_size, 4, 0);
    
    char buf[4096];
    while (1) {
        int n = fread(buf, 1, sizeof(buf), fp);
        if (n <= 0) break;
        /* 安全的 send：处理 EINTR */
        int sent = 0;
        while (sent < n) {
            int ret = send(sock, buf + sent, n - sent, 0);
            if (ret < 0) {
                if (errno == EINTR) continue;
                printf("[Upload] send 错误, errno=%d\n", errno);
                fclose(fp); close(sock);
                return -1;
            }
            sent += ret;
        }
    }
    fclose(fp); close(sock);
    printf("[Upload] 录音上传完毕\n");
    return 0;
}

void voice_add_chat_line(const char *text, unsigned int color)
{
    if (voice_count >= MAX_VOICE_LINE) return;

    /* 如果新文本是"你："开头，且最后一行也是"你："开头，替换最后一行
     * 这样"你：正在识别..." 会被 "你：xxx" 替换，避免冗余显示。
     * 用户语音通常很短，不做折行处理（直接替换最后一行即可）。 */
    if (voice_count > 0 && memcmp(text, GB_NI, 4) == 0) {
        if (memcmp(voice_history[voice_count - 1].text, GB_NI, 4) == 0) {
            strncpy(voice_history[voice_count - 1].text, text, 255);
            voice_history[voice_count - 1].text[255] = '\0';
            voice_history[voice_count - 1].color = color;
            if (current_page == PAGE_VOICE) voice_draw_ui();
            return;
        }
    }

    /* 自动折行：将长文本按屏幕像素宽度拆成多行存储。
     * ASCII 字符 = 16px, 中文字符 = 32px, 最大行宽 = 1004px。
     * 折行后每条 VoiceLine 都保证能完整显示在 LCD 上。 */
    int len = strlen(text);
    int pos = 0;
    int added = 0;

    while (pos < len && voice_count < MAX_VOICE_LINE) {
        int chunk_start = pos;
        int pixels = 0;

        while (pos < len) {
            int cw;
            if ((unsigned char)text[pos] < 0x80) {
                cw = 16;
            } else {
                if (text[pos + 1] == '\0') break; /* 残缺 GB 字节，跳过 */
                cw = 32;
            }
            if (pixels + cw > MAX_LINE_PIXELS) break;
            pixels += cw;
            pos += ((unsigned char)text[pos] < 0x80) ? 1 : 2;
        }

        if (pos > chunk_start) {
            int clen = pos - chunk_start;
            if (clen > 255) clen = 255;
            strncpy(voice_history[voice_count].text, text + chunk_start, clen);
            voice_history[voice_count].text[clen] = '\0';
            voice_history[voice_count].color = color;
            voice_count++;
            added++;
        } else {
            break; /* 连一个字符都放不下，放弃 */
        }
    }

    /* 滚动到最新内容 */
    if (voice_count > 10) {
        history_offset = voice_count - 10;
    } else {
        history_offset = 0;
    }
    if (current_page == PAGE_VOICE && added > 0)
        voice_draw_ui();
}

/* 安全的 recv 封装：处理 EINTR 和短读，返回实际读取字节数，<=0 表示错误/关闭 */
static int safe_recv(int sock, void *buf, int len)
{
    int total = 0;
    while (total < len) {
        int n = recv(sock, (char*)buf + total, len - total, 0);
        if (n == 0) {
            /* 对端正常关闭 */
            return (total > 0) ? total : 0;
        }
        if (n < 0) {
            if (errno == EINTR) continue;       /* 被信号中断，重试 */
            if (total > 0) return total;        /* 已收到部分数据，返回 */
            return -1;                           /* 真正的错误 */
        }
        total += n;
    }
    return total;
}

void voice_recv_and_play_audio(void)
{
    if (audio_sock <= 0) return;

    /* 先读取 4 字节长度头（使用安全 recv，处理 EINTR 和短读） */
    unsigned int audio_len = 0;
    int ret = safe_recv(audio_sock, &audio_len, 4);
    if (ret <= 0) {
        if (ret == 0) {
            printf("[Voice] 音频连接正常关闭\n");
        } else {
            printf("[Voice] 音频 recv 错误, errno=%d\n", errno);
        }
        close(audio_sock);
        audio_sock = -1;
        return;
    }
    audio_len = ntohl(audio_len);
    if (audio_len == 0 || audio_len > 1024 * 1024) {
        printf("[Voice] 音频长度异常：%u，跳过\n", audio_len);
        return;
    }
    printf("[Voice] 准备接收 TTS 音频，长度：%u 字节\n", audio_len);

    char *buf = malloc(audio_len);
    if (!buf) return;

    ret = safe_recv(audio_sock, buf, audio_len);
    if (ret < audio_len) {
        printf("[Voice] 音频数据接收不完整，期望 %u，实际 %d\n", audio_len, ret);
        free(buf);
        /* 不关闭连接，可能只是数据还没到 */
        return;
    }

    FILE *fp = fopen("/tmp/ai_tts.wav", "wb");
    if (fp) {
        fwrite(buf, 1, audio_len, fp);
        fclose(fp);
        /* 后台播放：不阻塞主循环，避免影响文本接收和触摸响应。
         * 先杀掉上一个还在播的 aplay（如果有），防止两个音频重叠。 */
        system("killall aplay 2>/dev/null; aplay -D plughw:1,0 /tmp/ai_tts.wav &");
    }
    free(buf);
}

/* 文本接收累积缓冲区 —— 解决 TCP 流式传输下一条消息被
 * 拆分到多次 recv 导致前缀匹配失败的问题。 */
#define TEXT_RECV_BUF_SIZE 4096
static char  text_recv_buf[TEXT_RECV_BUF_SIZE];
static int   text_recv_len = 0;

void voice_check_network(void)
{
    if (text_sock <= 0) return;

    /* 接收新数据，追加到累积缓冲区 */
    int space = TEXT_RECV_BUF_SIZE - text_recv_len - 1;
    if (space <= 0) {
        printf("[Voice] 文本缓冲区满，丢弃\n");
        text_recv_len = 0;
        return;
    }
    int len = recv(text_sock, text_recv_buf + text_recv_len, space, 0);
    if (len < 0) {
        if (errno == EINTR) {
            /* 被信号中断（如 aplay 子进程退出），非错误，下次再读 */
            return;
        }
        printf("[Voice] 文本 recv 错误, errno=%d\n", errno);
        close(text_sock);
        text_sock = -1;
        network_ready = 1;
        text_recv_len = 0;
        return;
    }
    if (len == 0) {
        printf("[Voice] 文本连接断开，将在下次检测时重连\n");
        close(text_sock);
        text_sock = -1;
        network_ready = 1;
        text_recv_len = 0;
        return;
    }
    text_recv_len += len;
    text_recv_buf[text_recv_len] = '\0';

    printf("[Voice] 收到文本 (%d 字节): \"%s\"\n", len, text_recv_buf + text_recv_len - len);

    /* 逐行消费：已完整的行（以 \n 结尾）处理掉，
     * 不完整的尾巴留在缓冲区等下一次 recv 补齐。 */
    char *scan = text_recv_buf;
    while (1) {
        char *nl = strchr(scan, '\n');
        if (!nl) break;  /* 没有完整的行，留着下次再试 */

        *nl = '\0';       /* 临时截断为独立的一行 */
        char *line = scan;

        /* 去掉行尾可能的 \r（兼容 Windows 服务端） */
        int line_len = strlen(line);
        if (line_len > 0 && line[line_len - 1] == '\r')
            line[line_len - 1] = '\0';

        printf("[Voice] 处理文本行: \"%s\"\n", line);

        /* GB2312: "你："=4字节  "AI："=4字节 */
        if (memcmp(line, GB_NI, 4) == 0) {
            voice_add_chat_line(line, COLOR_TEXT_T);
        } else if (memcmp(line, GB_AI, 4) == 0) {
            voice_add_chat_line(line, COLOR_ACCENT);
        } else if (strlen(line) > 0) {
            /* 非预期格式的行也显示出来（灰色），方便排查协议 */
            printf("[Voice] 未知格式行，灰色显示: \"%s\"\n", line);
            voice_add_chat_line(line, COLOR_TEXT_G);
        }

        scan = nl + 1;
    }

    /* 把未消费的尾巴移到缓冲区开头 */
    int remain = text_recv_len - (scan - text_recv_buf);
    if (remain > 0 && scan != text_recv_buf) {
        memmove(text_recv_buf, scan, remain);
    }
    text_recv_len = remain;
    text_recv_buf[text_recv_len] = '\0';
}

// ==================== 新增：解决链接错误的接口 ====================
void voice_request_connect(void) {
    // 外部（比如设置页面连上WiFi后）调用此函数，触发语音连网
    if (!network_ready) {
        network_ready = 1;
        if (text_sock > 0) close(text_sock);
        if (audio_sock > 0) close(audio_sock);
        text_sock = -1; audio_sock = -1;
        voice_init_network();
    }
}

int voice_has_pending_connect(void) {
    // 检查语音模块是否处于“已开启状态”或“正在尝试重连”
    return network_ready;
}
// ===============================================================

/* 带超时的非阻塞 connect 辅助函数，成功返回0，失败返回-1 */
static int connect_nonblock(int sock, struct sockaddr *addr, socklen_t addrlen, int timeout_sec)
{
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    int ret = connect(sock, addr, addrlen);
    if (ret == 0) {
        /* 极少情况下立即连上 */
        fcntl(sock, F_SETFL, flags);
        return 0;
    }
    if (errno != EINPROGRESS) {
        fcntl(sock, F_SETFL, flags);
        return -1;
    }

    fd_set wset;
    FD_ZERO(&wset);
    FD_SET(sock, &wset);
    struct timeval tv = { timeout_sec, 0 };
    ret = select(sock + 1, NULL, &wset, NULL, &tv);
    if (ret != 1) {
        /* 超时或出错 */
        fcntl(sock, F_SETFL, flags);
        return -1;
    }
    int so_error = 0;
    socklen_t len = sizeof(so_error);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
    fcntl(sock, F_SETFL, flags);  /* 无论如何先恢复阻塞模式 */
    return (so_error == 0) ? 0 : -1;
}

void voice_init_network(void)
{
    if (!network_ready) return;
    if (text_sock > 0) close(text_sock);
    if (audio_sock > 0) close(audio_sock);
    text_sock = -1;
    audio_sock = -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    /* 连接文本端口（3秒超时，不阻塞主循环） */
    text_sock = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_port = htons(PORT);
    if (connect_nonblock(text_sock, (struct sockaddr *)&addr, sizeof(addr), 3) < 0) {
        //printf("[Voice] 连接文本端口 %d 失败\n", PORT);
        close(text_sock);
        text_sock = -1;
    } else {
        printf("[Voice] 文本服务连接成功\n");
    }

    /* 连接音频端口（3秒超时） */
    audio_sock = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_port = htons(AUDIO_PORT);
    if (connect_nonblock(audio_sock, (struct sockaddr *)&addr, sizeof(addr), 3) < 0) {
        //printf("[Voice] 连接音频端口 %d 失败\n", AUDIO_PORT);
        close(audio_sock);
        audio_sock = -1;
    } else {
        printf("[Voice] 音频服务连接成功\n");
    }
}

void voice_allow_connection(int allow) {
    if (allow && !network_ready) {
        voice_request_connect();
    } else if (!allow && network_ready) {
        network_ready = 0;
        if (text_sock > 0) close(text_sock);
        if (audio_sock > 0) close(audio_sock);
        text_sock = -1; audio_sock = -1;
    }
}
int voice_is_network_enabled(void) { return network_ready; }
int voice_get_text_sock(void) { return text_sock; }
int voice_get_audio_sock(void) { return audio_sock; }