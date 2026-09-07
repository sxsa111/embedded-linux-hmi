#ifndef __DRMWRAP_H__
#define __DRMWRAP_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

struct drmHandle
{
    uint32_t width; 
    uint32_t height;
    uint32_t pitch;  // 行距，一行的字节数
    uint32_t handle;
    uint32_t size;
    uint8_t *vaddr;  // 显存入口地址
    uint32_t fb_id;
};

// 初始化 DRM 设备，并获得连接器信息
void DRMinit(int fd);

// 申请一块映射到用户空间的显存，可以申请多次
int  DRMcreateFB(int fd, struct drmHandle *drm);

// 更新画面
int DRMshowUp(int fd, struct drmHandle *drm);

// 释放相关资源
void DRMfreeResources(int fd, struct drmHandle *drm);

#endif