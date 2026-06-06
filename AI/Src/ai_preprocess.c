/**
  ******************************************************************************
  * @file    ai_preprocess.c
  * @brief   图像预处理: 240x240 RGB565 → 32x32 灰度 float32
  *
  *          降采样策略:
  *            240 / 32 = 7.5, 取中央 224x224 区域 (7*32=224)
  *            7x7 块内取平均灰度, 同时完成归一化 [0.0, 1.0]
  *
  *          灰度转换: ITU-R BT.601
  *            Y = 0.299*R + 0.587*G + 0.114*B
  *            定点: (77*R + 150*G + 29*B) >> 8
  ******************************************************************************
  */

#include "ai_preprocess.h"

/* 源图/目标图尺寸 */
#define SRC_SIZE    240
#define DST_SIZE    32
#define BLOCK_SIZE  7                                           /* 7x7 块平均 */
#define OFFSET      ((SRC_SIZE - BLOCK_SIZE * DST_SIZE) / 2)   /* (240-224)/2 = 8 */

/**
 * @brief RGB565 单像素转灰度 (uint8)
 */
static inline uint8_t rgb565_to_gray(uint16_t pixel)
{
    uint8_t r5 = (pixel >> 11) & 0x1F;
    uint8_t g6 = (pixel >> 5)  & 0x3F;
    uint8_t b5 =  pixel        & 0x1F;

    /* 5/6-bit 扩展到 8-bit */
    uint8_t r = (r5 << 3) | (r5 >> 2);
    uint8_t g = (g6 << 2) | (g6 >> 4);
    uint8_t b = (b5 << 3) | (b5 >> 2);

    /* BT.601 定点灰度 */
    return (uint8_t)((77u * r + 150u * g + 29u * b) >> 8);
}

/**
 * @brief 240x240 RGB565 → 32x32 float32 灰度 [0,1]
 */
void AI_Preprocess_RGB565_to_Gray32x32(const uint16_t *rgb565_in,
                                        float gray_out[32 * 32])
{
    /* 1 / (49 * 255) 预计算, 块平均+归一化一步完成 */
    const float inv_block_max = 1.0f / (float)(BLOCK_SIZE * BLOCK_SIZE * 255);

    for (int dy = 0; dy < DST_SIZE; dy++)
    {
        int src_y0 = OFFSET + dy * BLOCK_SIZE;

        for (int dx = 0; dx < DST_SIZE; dx++)
        {
            int src_x0 = OFFSET + dx * BLOCK_SIZE;
            uint32_t sum = 0;

            /* 7x7 块累加灰度 */
            for (int ky = 0; ky < BLOCK_SIZE; ky++)
            {
                const uint16_t *row = &rgb565_in[(src_y0 + ky) * SRC_SIZE + src_x0];
                for (int kx = 0; kx < BLOCK_SIZE; kx++)
                {
                    sum += rgb565_to_gray(row[kx]);
                }
            }

            gray_out[dy * DST_SIZE + dx] = (float)sum * inv_block_max;
        }
    }
}

/**
 * @brief float [0,1] → uint8 [0,255] 转换
 */
void AI_Preprocess_Float_to_U8(const float float_in[32 * 32],
                                uint8_t u8_out[32 * 32])
{
    for (int i = 0; i < 32 * 32; i++)
    {
        float v = float_in[i] * 255.0f;
        if (v < 0.0f)   v = 0.0f;
        if (v > 255.0f) v = 255.0f;
        u8_out[i] = (uint8_t)(v + 0.5f);  /* 四舍五入 */
    }
}
