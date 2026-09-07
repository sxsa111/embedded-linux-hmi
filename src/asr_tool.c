#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "qisr.h"
#include "msp_cmn.h"
#include "msp_errors.h"

#define BUFFER_SIZE 4096

/* 从文件读取音频并识别 */
int recognize_audio(const char* audio_file, char* result, int result_len)
{
    int ret = MSP_SUCCESS;
    const char* sessionID = NULL;
    FILE* fp = NULL;
    char audio_buf[6400];  // 每次读200ms音频（16k, 16bit）
    int audio_len = 0;
    int ep_status = MSP_EP_LOOKING_FOR_SPEECH;
    int rec_status = MSP_REC_STATUS_SUCCESS;
    int aud_status = MSP_AUDIO_SAMPLE_CONTINUE;
    int first_block = 1;  // 标记是不是第一块音频
    
    // 识别参数
    const char* params = "sub = iat, domain = iat, language = zh_cn, accent = mandarin, sample_rate = 16000, result_type = plain, result_encoding = utf8";
    
    if (NULL == audio_file || NULL == result)
    {
        printf("params is error!\n");
        return -1;
    }
    
    fp = fopen(audio_file, "rb");
    if (NULL == fp)
    {
        printf("open %s error.\n", audio_file);
        return -1;
    }
    
    // 跳过wav文件头（44字节）
    fseek(fp, 44, SEEK_SET);
    
    /* 开始识别会话 */
    sessionID = QISRSessionBegin(NULL, params, &ret);
    if (MSP_SUCCESS != ret)
    {
        printf("QISRSessionBegin failed, error code: %d.\n", ret);
        fclose(fp);
        return ret;
    }
    
    printf("正在识别...\n");
    
    /* 循环写入音频数据 */
    while (1)
    {
        audio_len = fread(audio_buf, 1, sizeof(audio_buf), fp);
        if (audio_len <= 0)
            break;
        
        // 第一块音频标记为FIRST
        if (first_block)
        {
            aud_status = MSP_AUDIO_SAMPLE_FIRST;
            first_block = 0;
        }
        else
        {
            aud_status = MSP_AUDIO_SAMPLE_CONTINUE;
        }
        
        ret = QISRAudioWrite(sessionID, audio_buf, audio_len, aud_status, &ep_status, &rec_status);
        if (MSP_SUCCESS != ret)
        {
            printf("QISRAudioWrite failed, error code: %d.\n", ret);
            break;
        }
        
        // 获取识别结果
        if (MSP_REC_STATUS_SUCCESS == rec_status)
        {
            const char* r = QISRGetResult(sessionID, &rec_status, 0, &ret);
            if (MSP_SUCCESS != ret)
            {
                printf("QISRGetResult failed, error code: %d.\n", ret);
                break;
            }
            if (r != NULL && strlen(r) > 0)
            {
                strncat(result, r, result_len - strlen(result) - 1);
            }
        }
        
        if (MSP_EP_AFTER_SPEECH == ep_status)
            break;
        
        usleep(100 * 1000);
    }
    
    fclose(fp);
    
    /* 写入最后一段，告诉讯飞音频发完了 */
    QISRAudioWrite(sessionID, NULL, 0, MSP_AUDIO_SAMPLE_LAST, &ep_status, &rec_status);
    
    /* 获取剩余的识别结果 */
    while (MSP_REC_STATUS_COMPLETE != rec_status)
    {
        const char* r = QISRGetResult(sessionID, &rec_status, 0, &ret);
        if (MSP_SUCCESS != ret)
        {
            printf("QISRGetResult failed, error code: %d.\n", ret);
            break;
        }
        if (r != NULL && strlen(r) > 0)
        {
            strncat(result, r, result_len - strlen(result) - 1);
        }
        usleep(100 * 1000);
    }
    
    /* 结束会话 */
    QISRSessionEnd(sessionID, "Normal");
    
    return 0;
}

int main(int argc, char* argv[])
{
    int ret = MSP_SUCCESS;
    const char* login_params = "appid = 390e232b, work_dir = .";
    
    char result[1024] = {0};
    
    if (argc < 2)
    {
        printf("用法: %s 音频文件.wav\n", argv[0]);
        return 1;
    }
    
    const char* audio_file = argv[1];
    
    /* 登录 */
    ret = MSPLogin(NULL, NULL, login_params);
    if (MSP_SUCCESS != ret)
    {
        printf("MSPLogin failed, error code: %d.\n", ret);
        return ret;
    }
    
    /* 识别音频 */
    ret = recognize_audio(audio_file, result, sizeof(result));
    if (MSP_SUCCESS == ret)
    {
        printf("识别结果：%s\n", result);
    }
    else
    {
        printf("识别失败，错误码：%d\n", ret);
    }
    
    MSPLogout();
    return ret;
}