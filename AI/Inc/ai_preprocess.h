/**
  ******************************************************************************
  * @file    ai_preprocess.h
  * @brief   图像预处理接口: 240x240 RGB565 → 32x32 灰度 float32
  ******************************************************************************
  */
#ifndef AI_PREPROCESS_H
#define AI_PREPROCESS_H

#include <stdint.h>

/**
 * @brief 将 240x240 RGB565 帧转换为 32x32 float32 灰度图
 * @param rgb565_in  输入: 240*240 RGB565 像素 (uint16_t[57600])
 * @param gray_out   输出: 32*32 float32 灰度值 [0.0, 1.0]
 * @note  使用中央 224x224 区域做 7x7 块平均降采样
 */
void AI_Preprocess_RGB565_to_Gray32x32(const uint16_t *rgb565_in,
                                        float gray_out[32 * 32]);

/**
 * @brief 将 float32 [0,1] 灰度图转为 uint8 [0,255]
 * @param float_in   输入: 32*32 float [0.0, 1.0]
 * @param u8_out     输出: 32*32 uint8 [0, 255]
 */
void AI_Preprocess_Float_to_U8(const float float_in[32 * 32],
                                uint8_t u8_out[32 * 32]);

#endif /* AI_PREPROCESS_H */
