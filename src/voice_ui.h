// voice_ui.h
#ifndef _VOICE_UI_H_
#define _VOICE_UI_H_

typedef enum {
    PAGE_HOME,
    PAGE_VOICE,
    PAGE_DRAW,
    PAGE_ALBUM,
    PAGE_FILE,
    PAGE_SETTING
} AppPage;

extern AppPage current_page;

// 声明在 voice_ui.c 中实现的函数
void voice_init(void);
void voice_handle_touch(int action, int x, int y);
void voice_init_network(void);
int voice_get_text_sock(void);
int voice_get_audio_sock(void);
void voice_recv_and_play_audio(void);
void voice_check_network(void);
void voice_allow_connection(int allow);
int voice_is_network_enabled(void);
// 异步连接：设置页 WiFi 连接成功后调用，不阻塞，主循环执行真正的 connect
void voice_request_connect(void);
int voice_has_pending_connect(void);
#endif