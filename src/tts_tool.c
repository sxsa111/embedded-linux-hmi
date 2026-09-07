#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "qtts.h"
#include "msp_cmn.h"
#include "msp_errors.h"

/* wav音频头部格式 */
typedef struct _wave_pcm_hdr
{
    char            riff[4];
    int             size_8;
    char            wave[4];
    char            fmt[4];
    int             fmt_size;
    short int       format_tag;
    short int       channels;
    int             samples_per_sec;
    int             avg_bytes_per_sec;
    short int       block_align;
    short int       bits_per_sample;
    char            data[4];
    int             data_size;
} wave_pcm_hdr;

wave_pcm_hdr default_wav_hdr = 
{
    { 'R', 'I', 'F', 'F' },
    0,
    {'W', 'A', 'V', 'E'},
    {'f', 'm', 't', ' '},
    16,
    1,
    1,
    16000,
    32000,
    2,
    16,
    {'d', 'a', 't', 'a'},
    0  
};

/* 文本合成 */
int text_to_speech(const char* src_text, const char* des_path, const char* params)
{
    int          ret          = -1;
    FILE*        fp           = NULL;
    const char*  sessionID    = NULL;
    unsigned int audio_len    = 0;
    wave_pcm_hdr wav_hdr      = default_wav_hdr;
    int          synth_status = MSP_TTS_FLAG_STILL_HAVE_DATA;

    if (NULL == src_text || NULL == des_path)
    {
        printf("params is error!\n");
        return ret;
    }
    fp = fopen(des_path, "wb");
    if (NULL == fp)
    {
        printf("open %s error.\n", des_path);
        return ret;
    }

    /* 开始合成 */
    sessionID = QTTSSessionBegin(params, &ret);
    if (MSP_SUCCESS != ret)
    {
        printf("QTTSSessionBegin failed, error code: %d.\n", ret);
        fclose(fp);
        return ret;
    }

    ret = QTTSTextPut(sessionID, src_text, (unsigned int)strlen(src_text), NULL);
    if (MSP_SUCCESS != ret)
    {
        printf("QTTSTextPut failed, error code: %d.\n",ret);
        QTTSSessionEnd(sessionID, "TextPutError");
        fclose(fp);
        return ret;
    }

    fwrite(&wav_hdr, sizeof(wav_hdr) ,1, fp);
    while (1) 
    {
        const void* data = QTTSAudioGet(sessionID, &audio_len, &synth_status, &ret);
        if (MSP_SUCCESS != ret)
            break;
        if (NULL != data)
        {
            fwrite(data, audio_len, 1, fp);
            wav_hdr.data_size += audio_len;
        }
        if (MSP_TTS_FLAG_DATA_END == synth_status)
            break;
        usleep(150*1000);
    }

    if (MSP_SUCCESS != ret)
    {
        printf("QTTSAudioGet failed, error code: %d.\n",ret);
        QTTSSessionEnd(sessionID, "AudioGetError");
        fclose(fp);
        return ret;
    }

    /* 修正wav文件头 */
    wav_hdr.size_8 += wav_hdr.data_size + (sizeof(wav_hdr) - 8);
    fseek(fp, 4, 0);
    fwrite(&wav_hdr.size_8,sizeof(wav_hdr.size_8), 1, fp);
    fseek(fp, 40, 0);
    fwrite(&wav_hdr.data_size,sizeof(wav_hdr.data_size), 1, fp);
    fclose(fp);
    fp = NULL;

    ret = QTTSSessionEnd(sessionID, "Normal");
    if (MSP_SUCCESS != ret)
    {
        printf("QTTSSessionEnd failed, error code: %d.\n",ret);
    }

    return ret;
}

int main(int argc, char* argv[])
{
    int         ret                  = MSP_SUCCESS;
    const char* login_params         = "appid = 390e232b, work_dir = .";
    const char* session_begin_params = "voice_name = xiaoyan, text_encoding = utf8, sample_rate = 16000, speed = 50, volume = 50, pitch = 50, rdn = 2";

    if (argc < 3)
    {
        printf("用法: %s \"要合成的文字\" 输出文件名.wav\n", argv[0]);
        return 1;
    }

    const char* text = argv[1];
    const char* filename = argv[2];

    /* 用户登录 */
    ret = MSPLogin(NULL, NULL, login_params);
    if (MSP_SUCCESS != ret)
    {
        printf("MSPLogin failed, error code: %d.\n", ret);
        return ret;
    }

    /* 文本合成 */
    ret = text_to_speech(text, filename, session_begin_params);
    if (MSP_SUCCESS != ret)
    {
        printf("text_to_speech failed, error code: %d.\n", ret);
    }

    MSPLogout();
    return ret;
}