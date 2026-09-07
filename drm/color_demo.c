#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define __LOG_EN

#define RK1808_LCD_DEV      "/dev/fb0"

/* 
需求：让开发板的第一行像素点全部显示红色
    a.需要操作的硬件对象是屏幕，需要借助屏幕驱动文件来处理
    b.打开LCD驱动文件
    c.将需要的颜色数据写入到驱动文件当中
    d.关闭打开的文件
*/

int main(int argc, char *argv[])
{
    int lcd_fd = open(RK1808_LCD_DEV, O_RDWR);
    if (lcd_fd == -1)
    {
        #ifdef __LOG_EN
            printf("open %s seccesfully!\n",RK1808_LCD_DEV);
        #endif
        return -1;
    }
#ifdef __LOG_EN
    printf("open %s seccesfully!\n",argv[1]);
#endif

    char lcd_data[1024*600*4];
    for (int j = 0; j < 600; j++)
    for (int i = 0; i < 1024; i++)
    {
        lcd_data[0 + 4*i + 4*j*1024] = 0;//B
        lcd_data[1 + 4*i + 4*j*1024] = 0;//G
        lcd_data[2 + 4*i + 4*j*1024] = 255;//R
        lcd_data[3 + 4*i + 4*j*1024] = 0;//A
    }
    write(lcd_fd, lcd_data, sizeof(lcd_data));

    close(lcd_fd);
    return 0;
}

