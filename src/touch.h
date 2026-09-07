#ifndef _TOUCH_H_
#define _TOUCH_H_

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <stdlib.h>

#define TOUCH_DEV_PATH  "/dev/input/event2"
#define SLIDE_THRESHOLD 50

#define TOUCH_NONE      0
#define TOUCH_CLICK     1
#define TOUCH_SLIDE_UP  2
#define TOUCH_SLIDE_DOWN 3
#define TOUCH_SLIDE_LEFT 4
#define TOUCH_SLIDE_RIGHT 5
#define TOUCH_DRAG      6  // 👈 新增：用于手指按住滑动

static int _touch_fd = -1;
static int _start_x = 0, _start_y = 0;
static int _cur_x = 0, _cur_y = 0;
static int _is_pressed = 0;

static inline int touch_init(void)
{
    _touch_fd = open(TOUCH_DEV_PATH, O_RDONLY);
    if (_touch_fd < 0) {
        perror("open touch device failed");
        return -1;
    }
    return 0;
}

static inline int touch_get_fd(void)
{
    return _touch_fd;
}

static inline int touch_read_action(void)
{
    struct input_event ev;
    if (_touch_fd < 0) return TOUCH_NONE;

    int ret = read(_touch_fd, &ev, sizeof(ev));
    if (ret < sizeof(ev)) return TOUCH_NONE;

    switch (ev.type) {
        case EV_ABS:
            if (ev.code == ABS_X) _cur_x = ev.value;
            else if (ev.code == ABS_Y) _cur_y = ev.value;
            break;

        case EV_KEY:
            if (ev.code == BTN_TOUCH || ev.code == BTN_MOUSE) {
                if (ev.value == 1) {
                    _is_pressed = 1;
                    _start_x = _cur_x;
                    _start_y = _cur_y;
                } else {
                    _is_pressed = 0;
                    int dx = _cur_x - _start_x;
                    int dy = _cur_y - _start_y;

                    if (abs(dx) < SLIDE_THRESHOLD && abs(dy) < SLIDE_THRESHOLD) {
                        return TOUCH_CLICK;
                    } else if (abs(dx) > abs(dy)) {
                        return dx > 0 ? TOUCH_SLIDE_RIGHT : TOUCH_SLIDE_LEFT;
                    } else {
                        return dy > 0 ? TOUCH_SLIDE_DOWN : TOUCH_SLIDE_UP;
                    }
                }
            }
            break;

        case EV_SYN:
            // 👇 严格匹配 fb_touch.c 的逻辑，手指按住移动时触发画图
            if (_is_pressed) {
                return TOUCH_DRAG;
            }
            break;
    }
    return TOUCH_NONE;
}

static inline int touch_x(void) { return _cur_x; }
static inline int touch_y(void) { return _cur_y; }

#endif // _TOUCH_H_