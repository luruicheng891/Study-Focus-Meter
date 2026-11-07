/**
  ******************************************************************************
  * @file    network.h
  * @author  AST Embedded Analytics Research Platform
  * @date    2026-06-03T16:20:19+0800
  * @brief   AI Tool Automatic Code Generator for Embedded NN computing
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  ******************************************************************************
  */
#ifndef AI_NETWORK_H
#define AI_NETWORK_H

#include "network_config.h"
#include "ai_platform.h"

/******************************************************************************/
#define AI_NETWORK_MODEL_NAME          "network"
#define AI_NETWORK_ORIGIN_MODEL_NAME   "model"

/******************************************************************************/
#define AI_NETWORK_ACTIVATIONS_ALIGNMENT   (4)
#define AI_NETWORK_INPUTS_IN_ACTIVATIONS   (4)
#define AI_NETWORK_OUTPUTS_IN_ACTIVATIONS  (4)

/******************************************************************************/
#define AI_NETWORK_IN_NUM        (1)

AI_DEPRECATED
#define AI_NETWORK_IN \
  ai_network_inputs_get(AI_HANDLE_NULL, NULL)

#define AI_NETWORK_IN_SIZE { \
  AI_NETWORK_IN_1_SIZE, \
}
#define AI_NETWORK_IN_SIZE_BYTES { \
  AI_NETWORK_IN_1_SIZE_BYTES, \
}
#define AI_NETWORK_IN_1_FORMAT      (AI_BUFFER_FORMAT_FLOAT)
#define AI_NETWORK_IN_1_HEIGHT      (32)
#define AI_NETWORK_IN_1_WIDTH       (32)
#define AI_NETWORK_IN_1_CHANNEL     (1)
#define AI_NETWORK_IN_1_SIZE        (1024)
#define AI_NETWORK_IN_1_SIZE_BYTES  (4096)

/******************************************************************************/
#define AI_NETWORK_OUT_NUM       (1)

AI_DEPRECATED
#define AI_NETWORK_OUT \
  ai_network_outputs_get(AI_HANDLE_NULL, NULL)

#define AI_NETWORK_OUT_SIZE { \
  AI_NETWORK_OUT_1_SIZE, \
}
#define AI_NETWORK_OUT_SIZE_BYTES { \
  AI_NETWORK_OUT_1_SIZE_BYTES, \
}
#define AI_NETWORK_OUT_1_FORMAT      (AI_BUFFER_FORMAT_FLOAT)
#define AI_NETWORK_OUT_1_CHANNEL     (3)
#define AI_NETWORK_OUT_1_SIZE        (3)
#define AI_NETWORK_OUT_1_SIZE_BYTES  (12)

/******************************************************************************/
#define AI_NETWORK_N_NODES (7)


AI_API_DECLARE_BEGIN

/******************************************************************************/
/*! Public API Functions Declarations */

AI_DEPRECATED
AI_API_ENTRY
ai_bool ai_network_get_info(
  ai_handle network, ai_network_report* report);

AI_API_ENTRY
ai_bool ai_network_get_report(
  ai_handle network, ai_network_report* report);

AI_API_ENTRY
ai_error ai_network_get_error(ai_handle network);

AI_API_ENTRY
ai_error ai_network_create(
  ai_handle* network, const ai_buffer* network_config);

AI_API_ENTRY
ai_handle ai_network_destroy(ai_handle network);

AI_API_ENTRY
ai_bool ai_network_init(
  ai_handle network, const ai_network_params* params);

AI_API_ENTRY
ai_error ai_network_create_and_init(
  ai_handle* network, const ai_handle activations[], const ai_handle weights[]);

AI_API_ENTRY
ai_buffer* ai_network_inputs_get(
  ai_handle network, ai_u16 *n_buffer);

AI_API_ENTRY
ai_buffer* ai_network_outputs_get(
  ai_handle network, ai_u16 *n_buffer);

AI_API_ENTRY
ai_i32 ai_network_run(
  ai_handle network, const ai_buffer* input, ai_buffer* output);

AI_API_ENTRY
ai_i32 ai_network_forward(
  ai_handle network, const ai_buffer* input);

AI_API_DECLARE_END

#endif /* AI_NETWORK_H */
