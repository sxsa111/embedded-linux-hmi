# -*- coding: utf-8 -*-
from zhipuai import ZhipuAI
import socket
import os
import subprocess
import threading
import time

# ========== 配置区 ==========
API_KEY = os.environ.get("ZHIPUAI_API_KEY", "")  # 安全：从环境变量读取，勿硬编码
PORT = 8080          # 文本聊天端口（服务端→客户端）
AUDIO_PORT = 8081    # TTS语音端口（服务端→客户端）
RECORD_PORT = 8082   # 录音上传端口（客户端→服务端）
TTS_TOOL = "./tts_tool"
ASR_TOOL = "./asr_tool"
# ============================

client = ZhipuAI(api_key=API_KEY)

# 全局连接
text_conn = None
audio_conn = None

# ★ 线程安全锁：防止录音线程和主线程同时往同一个 socket 写数据导致字节交错
#    text_conn 和 audio_conn 是不同的 TCP 连接，本该独立，但两个线程都可能写它们，
#    所以用同一把锁串行化所有 socket 写入
send_lock = threading.Lock()

# 服务端 listener socket（用于客户端断线重连时重新 accept）
text_server = None
audio_server = None


# ========== 非阻塞重连 ==========
def try_accept_reconnects():
    """非阻塞地检查是否有新客户端连接，3 秒超时匹配客户端 connect 超时"""
    global text_conn, audio_conn, text_server, audio_server
    reconnected = False

    if text_conn is None and text_server is not None:
        try:
            # ★ 超时 3 秒，与客户端 connect_nonblock 的 3 秒超时匹配
            #    之前 0.2 秒太短导致第一次重连几乎必然失败
            text_server.settimeout(3.0)
            conn, addr = text_server.accept()
            text_conn = conn
            text_server.settimeout(None)
            print(f"[Reconnect] 文本重连成功：{addr}")
            reconnected = True
        except socket.timeout:
            text_server.settimeout(None)
            print("[Reconnect] 文本端口 accept 超时（3秒内无客户端连接）")
        except Exception as e:
            text_server.settimeout(None)
            print(f"[Reconnect] 文本 accept 异常：{e}")

    if audio_conn is None and audio_server is not None:
        try:
            audio_server.settimeout(3.0)
            conn, addr = audio_server.accept()
            audio_conn = conn
            audio_server.settimeout(None)
            print(f"[Reconnect] 音频重连成功：{addr}")
            reconnected = True
        except socket.timeout:
            audio_server.settimeout(None)
            print("[Reconnect] 音频端口 accept 超时（3秒内无客户端连接）")
        except Exception as e:
            audio_server.settimeout(None)
            print(f"[Reconnect] 音频 accept 异常：{e}")

    return reconnected


# ========== TTS 语音生成 ==========
def generate_tts(text, output_file="tts_temp.wav"):
    try:
        print(f"[TTS] 正在生成语音：{text}")
        result = subprocess.run(
            [TTS_TOOL, text, output_file],
            capture_output=True,
            text=True
        )
        if result.returncode == 0 and os.path.exists(output_file):
            file_size = os.path.getsize(output_file)
            print(f"[TTS] 生成成功，大小：{file_size} 字节")
            return True
        else:
            print(f"[TTS] 生成失败：{result.stderr}")
            return False
    except Exception as e:
        print(f"[TTS] 出错：{e}")
        return False


# ========== 发送音频（TTS → 客户端播放） ==========
def send_audio(audio_file):
    global audio_conn
    with send_lock:
        if audio_conn is None:
            print("[Audio] 音频连接未建立，跳过")
            return False
        if not os.path.exists(audio_file):
            print("[Audio] 音频文件不存在")
            return False
        try:
            file_size = os.path.getsize(audio_file)
            print(f"[Audio] 正在发送TTS语音，大小：{file_size} 字节")
            audio_conn.sendall(file_size.to_bytes(4, byteorder='big'))
            with open(audio_file, 'rb') as f:
                audio_conn.sendall(f.read())
            print("[Audio] TTS语音发送完成")
            return True
        except Exception as e:
            print(f"[Audio] 发送失败：{e}")
            try:
                audio_conn.close()
            except:
                pass
            audio_conn = None
            return False


# ========== 发送文字 ==========
def send_text(text):
    global text_conn
    with send_lock:
        if text_conn is None:
            print("[Text] 文本连接未建立，跳过")
            return False
        try:
            send_msg = text + "\n"
            text_conn.sendall(send_msg.encode("gb2312", errors="ignore"))
            print(f"[Text] 已发送: {text}")
            return True
        except Exception as e:
            print(f"[Text] 发送失败：{e}")
            try:
                text_conn.close()
            except:
                pass
            text_conn = None
            return False


# ========== 语音识别 ==========
def recognize_audio(audio_file):
    try:
        print(f"[ASR] 正在识别音频...")
        result = subprocess.run(
            [ASR_TOOL, audio_file],
            capture_output=True,
            text=True
        )
        output = result.stdout
        print(f"[ASR] 输出：{output}")
        if "识别结果：" in output:
            text = output.split("识别结果：")[1].strip()
            print(f"[ASR] 识别成功：{text}")
            return text
        else:
            print(f"[ASR] 识别失败")
            return None
    except Exception as e:
        print(f"[ASR] 出错：{e}")
        return None


# ========== 处理一次录音对话（录音线程中调用） ==========
def process_voice_chat(audio_file):
    # 1. 语音识别
    user_text = recognize_audio(audio_file)
    if not user_text:
        error_msg = "抱歉，没听清你说什么，请再说一遍"
        send_text(f"AI：{error_msg}")
        tts_file = "tts_temp.wav"
        if generate_tts(error_msg, tts_file):
            send_audio(tts_file)
            if os.path.exists(tts_file):
                os.remove(tts_file)
        return

    # 2. 显示用户说的话
    send_text(f"你：{user_text}")

    # 3. 调用AI
    try:
        print(f"[AI] 正在获取回复...")
        response = client.chat.completions.create(
            model="glm-4-flash",
            messages=[
                {"role": "user", "content": user_text + " 请用中文回复，简短一点，控制在两句话以内，不要用任何emoji表情和特殊符号。"}
            ],
        )
        answer = response.choices[0].message.content.strip()
        print(f"[AI] 回复：{answer}")

        # 4. 发AI回复文字
        send_text(f"AI：{answer}")

        # 5. 生成语音并发送
        tts_file = "tts_temp.wav"
        if generate_tts(answer, tts_file):
            send_audio(tts_file)
            if os.path.exists(tts_file):
                os.remove(tts_file)

    except Exception as e:
        print(f"[AI] 调用出错：{e}")
        error_msg = "抱歉，我暂时无法回答这个问题"
        send_text(f"AI：{error_msg}")
        tts_file = "tts_temp.wav"
        if generate_tts(error_msg, tts_file):
            send_audio(tts_file)
            if os.path.exists(tts_file):
                os.remove(tts_file)


# ========== 录音上传线程 ==========
def record_server_thread():
    record_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    record_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    record_server.bind(("0.0.0.0", RECORD_PORT))
    record_server.listen(5)

    print(f"[OK] 录音上传服务已启动，端口{RECORD_PORT}")

    while True:
        try:
            conn, addr = record_server.accept()
            print(f"[Record] 收到录音上传：{addr}")

            # 接收4字节长度
            len_data = conn.recv(4)
            if len(len_data) < 4:
                conn.close()
                continue

            audio_len = int.from_bytes(len_data, byteorder='big')
            print(f"[Record] 录音大小：{audio_len} 字节")

            if audio_len > 1024 * 1024:
                conn.close()
                continue

            # 接收音频数据
            audio_data = b""
            while len(audio_data) < audio_len:
                data = conn.recv(audio_len - len(audio_data))
                if not data:
                    break
                audio_data += data

            conn.close()

            if len(audio_data) < audio_len:
                print("[Record] 接收不完整")
                continue

            # 保存为临时文件
            record_file = "record_temp.wav"
            with open(record_file, 'wb') as f:
                f.write(audio_data)

            print("[Record] 录音保存成功，开始处理...")

            # 处理语音对话
            process_voice_chat(record_file)

            # 删除临时文件
            if os.path.exists(record_file):
                os.remove(record_file)

        except Exception as e:
            print(f"[Record] 出错：{e}")


# ========== 主程序 ==========
if __name__ == "__main__":
    # 启动录音服务线程
    t = threading.Thread(target=record_server_thread, daemon=True)
    t.start()

    # 文本服务
    text_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    text_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    text_server.bind(("0.0.0.0", PORT))
    text_server.listen(1)

    # 音频服务
    audio_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    audio_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    audio_server.bind(("0.0.0.0", AUDIO_PORT))
    audio_server.listen(1)

    print(f"[OK] AI聊天服务端已启动，端口{PORT}")
    print(f"[OK] TTS音频服务已启动，端口{AUDIO_PORT}")
    print("等待开发板连接...")

    # 等待文本连接
    text_conn, addr = text_server.accept()
    print(f"[OK] 开发板已连接（文本）：{addr}")

    # 等待音频连接
    print("[Audio] 等待音频连接...")
    audio_conn, audio_addr = audio_server.accept()
    print(f"[OK] 音频连接已建立：{audio_addr}")

    print("\n提示：")
    print("  - 在开发板上点击屏幕底部开始说话")
    print("  - 也可以在这里输入文字，回车发送")
    print("  - 输入exit退出\n")

    # 主线程处理键盘输入（文字聊天）
    while True:
        try:
            # ★ 每次 input 前非阻塞检查是否有客户端重连
            if text_conn is None or audio_conn is None:
                print("[Main] 检测到连接断开，尝试接受重连...")
                try_accept_reconnects()

            question = input("你说：")
            if question == "exit":
                break
            if not question:
                continue

            # 发用户消息（带锁保护）
            ok = send_text(f"你：{question}")
            if not ok:
                print("[Main] 文本发送失败，跳过本轮对话")
                continue

            # 调用AI
            response = client.chat.completions.create(
                model="glm-4-flash",
                messages=[
                    {"role": "user", "content": question + " 请用中文回复，简短一点，控制在两句话以内，不要用任何emoji表情和特殊符号。"}
                ],
            )
            answer = response.choices[0].message.content.strip()
            print(f"AI说：{answer}")

            # 发AI回复
            ok = send_text(f"AI：{answer}")
            if not ok:
                print("[Main] AI文本发送失败，跳过TTS")
                continue

            # 生成并发送语音
            tts_file = "tts_temp.wav"
            if generate_tts(answer, tts_file):
                send_audio(tts_file)
                if os.path.exists(tts_file):
                    os.remove(tts_file)

        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"出错：{e}")

    # 清理
    for c in [text_conn, audio_conn]:
        try:
            if c:
                c.close()
        except:
            pass
    for s in [text_server, audio_server]:
        try:
            if s:
                s.close()
        except:
            pass
    print("聊天结束")
