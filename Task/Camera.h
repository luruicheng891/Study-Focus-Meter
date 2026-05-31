#ifndef __CAMERA_H
#define __CAMERA_H

#include "lvgl.h"

/* 摄像头 LVGL canvas 对象, 由 Weather_Task 控制显示/隐藏 */
extern lv_obj_t *g_cam_canvas;

void Camera_task(void *argument);

#endif
