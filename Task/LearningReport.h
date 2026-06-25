/**
  ******************************************************************************
  * @file    LearningReport.h
  * @brief   "Learning Report" screen - Wabi-sabi warm gray style (Flyme-like)
  *          LVGL v8.3, 320x240 embedded display
  *
  *  Style:  warm gray / whitespace / restraint / poetic
  *  Colors: warm cream -> deep warm gray gradient bg, pure white card, gold accent
  *
  *  Data displayed (from LRSessionData_t, mirrors SessionSummary_t fields):
  *    Hero row (3 cards):
  *      - Duration (actual mm:ss + "/ target" if target set)
  *      - Avg Score
  *      - Focus %
  *    Mode distribution bar (3 segments):
  *      - Focus / Distract / Fatigue
  *    End mode banner:
  *      - Manual / Auto (5min away) / Time Complete
  *    Stats row (3 cells):
  *      - Posture abnormal count
  *      - Away count
  *      - Pause count
  *
  *  Public API:
  *    LearningReport_Init()           - Build the screen once
  *    LearningReport_GetScreen()      - Get the lv_obj screen (for lv_scr_load)
  *    LearningReport_RefreshAnim()    - Re-trigger card fade-in animation
  *    LearningReport_Update(...)      - Push real session data into the screen
  *    LearningReport_SetBackCallback  - Register the top-right "Back" handler
  ******************************************************************************
  */

#ifndef __LEARNING_REPORT_H
#define __LEARNING_REPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdint.h>

/**
 * @brief Data packet for LearningReport_Update()
 *        Mirrors SessionSummary_t fields.  Decouples the screen from StateTask.
 */
typedef struct {
    uint32_t total_seconds;            /* actual study duration (s) */
    uint16_t target_seconds;           /* target duration (s, 0 = no target) */
    uint8_t  avg_total_score;          /* average score (0..100) */
    uint8_t  focus_pct;                /* focus share (0..100) */
    uint8_t  distract_pct;             /* distract share (0..100) */
    uint8_t  fatigue_pct;              /* fatigue share (0..100) */
    uint8_t  posture_abnormal_count;   /* posture abnormal times */
    uint32_t away_count;               /* away times */
    uint32_t pause_count;              /* pause times */
    uint8_t  auto_ended;               /* 0=manual, 1=5min away, 2=time complete */
} LRSessionData_t;

/**
 * @brief Build the learning report screen
 * @note  Idempotent: creates the screen, styles, and widgets on first call.
 *        Subsequent calls are no-ops.
 */
void LearningReport_Init(void);

/**
 * @brief Get the learning report screen object
 * @return lv_obj_t* screen pointer, used by DisplayTask for lv_scr_load()
 */
lv_obj_t *LearningReport_GetScreen(void);

/**
 * @brief Re-trigger the card fade-in animation
 * @note  The mode distribution bar is animated by LearningReport_Update()
 *        (using the real focus_pct), so this only handles the card fade-in.
 */
void LearningReport_RefreshAnim(void);

/**
 * @brief Push real session data into the screen
 * @param data  session data pointer (see LRSessionData_t)
 * @note  Called by DisplayTask on ST_SUMMARY, replacing placeholder values.
 *        Animates the mode bar to the new focus/distract/fatigue ratio.
 */
void LearningReport_Update(const LRSessionData_t *data);

/**
 * @brief Register the "Back" button callback
 * @param cb callback function (DisplayTask implements it, posts STATE_EVT_GUI_BACK)
 * @note  Must be called after LearningReport_Init() to take effect.
 */
typedef void (*LearningReportBackCb_t)(void);
void LearningReport_SetBackCallback(LearningReportBackCb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* __LEARNING_REPORT_H */
