# ============================================================
#  嵌入式 Linux HMI (RK1808) 构建文件
#  默认交叉编译；在板端也可用 gcc 直接编（CROSS 留空）。
# ============================================================

CROSS      ?=
CC         = $(CROSS)gcc

# 讯飞 MSC SDK 路径（需自行从开放平台下载，未纳入仓库）
IFLYTEK_SDK ?= third_party/iflytek-sdk
IFLYTEK_INC  = src/include/iflytek
IFLYTEK_LIB  = $(IFLYTEK_SDK)/libs

CFLAGS  = -Wall -O2 -I src -I src/include
LDFLAGS = -lpthread -ldl -lm

# ---- 主 HMI 应用（运行在板端）----
APP_SRCS = src/main_ui.c \
           src/draw_utils.c \
           src/ui_components.c \
           src/voice_ui.c \
           src/draw_ui.c \
           src/album_ui.c \
           src/settings_ui.c \
           src/app_stubs.c
APP_BIN  = hmi_demo

# ---- 讯飞 TTS / ASR 辅助工具 ----
TTS_BIN  = tts_tool
ASR_BIN  = asr_tool

.PHONY: all app tools clean

all: app tools

app: $(APP_BIN)

tools: $(TTS_BIN) $(ASR_BIN)

$(APP_BIN): $(APP_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TTS_BIN): src/tts_tool.c
	$(CC) -I $(IFLYTEK_INC) $^ -o $@ -L $(IFLYTEK_LIB) -lmsc $(LDFLAGS)

$(ASR_BIN): src/asr_tool.c
	$(CC) -I $(IFLYTEK_INC) $^ -o $@ -L $(IFLYTEK_LIB) -lmsc $(LDFLAGS)

clean:
	rm -f $(APP_BIN) $(TTS_BIN) $(ASR_BIN)

# 使用示例：
#   交叉编译: make CROSS=arm-linux-gnueabihf-
#   本地编译: make
#   指定SDK: make tools IFLYTEK_LIB=/opt/iflytek/libs
