/**
  ******************************************************************************
  * @file    LearningScreen.c
  * @brief   学习专注界面 — 现代极简版 (智能台灯式陪伴)
  *
  *          设计原则:
  *            - 数据后台持续跑, 界面只呈现温柔陪伴, 不做实时审判
  *            - 产品气质: 书桌上的智能台灯, 不是监工
  *            - 任何异常 2~5 分钟才浮现提示, 数值变化 2~3 秒平滑过渡
  *            - 配色全部降饱和, 不用刺眼红/绿, 不强制打断
  *            - 现代极简排版, 大量留白, 大字时间, 弱化控件
  *
  *          布局 (320x240):
  *            y=0..14   顶部极简状态条 (状态/时间/摄像头缩略图)
  *            y=24..176 中央: 外环 (倒计时进度) + 48pt 大字倒计时 + 目标
  *            y=180..208 环境信息卡 (温度/湿度/光照) — 极淡
  *            y=210..222 噪音波形 — 安静时近静态
  *            y=224..240 极小控制按钮 (Pause/End) — 暗灰不抢眼
  *
  *          时间: 中央 48pt 倒计时 "MM:SS" (从 30min 倒数到 0)
  *                外环显示剩余比例 (从满到空); 用户偶尔瞥一眼, 故用最大字号
  ******************************************************************************
  */

#include "LearningScreen.h"
#include "StateTask.h"        /* LearningState_t, State_GetCurrent, State_PostEvent,
                                 State_GetSustainedDistract/Fatigue/Posture */
#include "WeatherTask.h"      /* WeatherInfo_t, Weather_GetLatest */
#include "FusionTask.h"       /* FusionResult_t, 调试面板用 (posture/score) */
#include "SlaveRxTask.h"      /* SlaveData_t, Slave_GetLatest — 调试面板原始分数/压力 */
#include "AITask.h"           /* AI_GetLatestProbs — 调试面板必须用实时, 不走 Fusion */
// #include "bg_img.h"           /* 背景图已移除 (省 ~150KB Flash) */
#include "FreeRTOS.h"
#include "task.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>           /* abs */
#include <math.h>

/* ======================== 字体定义 ========================
 * lv_conf.h 启用了 montserrat_12/14/16/18/20/28/48
 *   时间大字 48pt — 倒计时, 一眼看清
 *   14pt  — 调试面板分类结果
 *   小字  12pt  — 顶部/环境/按钮/调试细节 */
#define FONT_TIME       &lv_font_montserrat_48   /* 倒计时大字 — 一眼看清 */
#define FONT_MID        &lv_font_montserrat_14
#define FONT_SMALL      &lv_font_montserrat_12

/* ======================== 配色 (现代深色风, 全部降饱和) ======================== */
#define LR_BG_DEEP      lv_color_hex(0x0E1320)   /* 深蓝黑底 */
#define LR_BG_PANEL     lv_color_hex(0x161C2C)   /* 卡片底 (稍亮) */
#define LR_BG_GLOW      lv_color_hex(0x1F2738)   /* 控件底 */
#define LR_FG_SOFT      lv_color_hex(0xE8ECF1)   /* 柔白主文字 */
#define LR_FG_MID       lv_color_hex(0xA0AABE)   /* 中灰 */
#define LR_FG_DIM       lv_color_hex(0x6B7488)   /* 暗灰 */

/* 主色调: 柔绿 (陪伴) */
#define LR_GREEN_SOFT   lv_color_hex(0x86EFAC)   /* 柔绿 300 */
#define LR_GREEN_DEEP   lv_color_hex(0x4ADE80)   /* 柔绿 400 (深色用) */

/* 状态色 (全部用 100/200/300 浅色) */
#define LR_YELLOW_SOFT  lv_color_hex(0xFDE68A)   /* 柔黄 200 — 分心 */
#define LR_ORANGE_SOFT  lv_color_hex(0xFED7AA)   /* 柔橙 200 — 疲劳 */
#define LR_PINK_SOFT    lv_color_hex(0xFBCFE8)   /* 柔粉 200 — 坐姿异常 */

/* 进度色: 柔青 */
#define LR_PROG_CYAN    lv_color_hex(0x67E8F9)   /* 柔青 300 */
#define LR_PROG_TEAL    lv_color_hex(0x5EEAD4)   /* 柔青 200 */

/* 轨道色 */
#define LR_TRACK        lv_color_hex(0x222A3A)   /* 圆环轨道 (暗) */
#define LR_TRACK_SOFT   lv_color_hex(0x2A3346)   /* 圆环轨道 (稍亮) */

/* ======================== 背景状态色 ========================
 * 给整屏底色一个"此刻状态"氛围提示 (替代固定 LR_BG_DEEP), 作为
 * 采集(视觉+姿态) -> 计算 -> 反馈 闭环的最后一环。
 *
 * 关键设计原则: 背景色只反映用户当下能通过调整行为直接影响的信号——
 * 只用 vision_score(视觉专注度) + posture_score(坐姿), 按 FusionTask 中
 * 二者的原始权重比例 (40:30) 重新归一化为 focus_score (0..100)。
 *
 * 不使用 FusionResult_t.total_score, 因为它还混入了 env(环境) 和
 * duration(学习时长) 两项——这两项用户在"此刻"无法通过调整坐姿/专注度
 * 去改变或纠正, 混进实时反馈会让背景变色和用户行为的因果关系错配,
 * 闭环无法闭合 (例如: 坐姿端正+视觉专注, 但因为学太久 duration_score
 * 走低, 背景却变黄, 用户会觉得"我做对了却被判不专注")。
 * env/duration 更适合放进 SUMMARY 报告做整体建议, 不在这里体现。
 *
 *   ST_AWAY (从机压力消失 >=30s, StateTask 自动判定)     -> 暗红, 提示"无人"
 *   ST_LEARNING 且 focus_score >= LR_FOCUS_SCORE_TH      -> 暗蓝, 提示"专注"
 *   ST_LEARNING 且 focus_score <  LR_FOCUS_SCORE_TH      -> 暗黄, 提示"不专注"
 *   其余状态 (PAUSED 等)                                  -> 默认深色 LR_BG_DEEP
 * 全部用低亮度色调 (仅做底色氛围), 不影响白色文字对比度. */
#define LR_FOCUS_SCORE_TH   60      /* focus_score 门限: >= 为专注(蓝), < 为不专注(黄) */
#define LR_FOCUS_STREAK_TH  5       /* 连续 N 次同向才切换背景色 (避免 1Hz 抖动) */

#define LR_BG_FOCUS      lv_color_hex(0x102244)   /* 暗蓝 — 专注 */
#define LR_BG_DISTRACT   lv_color_hex(0xFF0000)   /* 暗红 — 不专注 */
#define LR_BG_AWAY       lv_color_hex(0xFF0000)   /* 暗红 — 无人 */

/* ======================== 动画时长 ======================== */
#define LR_PROGRESS_FILL_MS    3000   /* 进度环填充: 3s */
#define LR_BG_TRANSITION_MS    1500   /* 背景色渐变过渡: 1.5s, 避免突变刺眼 */

/* ======================== 几何参数 ======================== */
#define LR_PROGRESS_RADIUS     74     /* 进度环半径 (外) */
#define LR_RING_WIDTH_OUT      2      /* 外环线宽 (更细) */

/* 圆环中心 */
#define LR_CENTER_X            160
#define LR_CENTER_Y            105

/* ======================== 控件指针 ======================== */
static lv_obj_t *lr_screen         = NULL;  /* 根对象 */
static lv_obj_t *lr_label_time     = NULL;  /* 中央大字时间 */
static lv_obj_t *lr_label_target   = NULL;  /* 目标提示 */

/* 顶部状态 */
static lv_obj_t *lr_label_state    = NULL;  /* 状态文字 */
static lv_obj_t *lr_label_clock    = NULL;  /* 时钟 */
static lv_obj_t *lr_label_wifi     = NULL;  /* WiFi 状态 */
static lv_obj_t *lr_cam_thumb      = NULL;  /* 摄像头缩略图 */

/* 进度环 (倒计时剩余比例) */
static lv_obj_t *lr_arc_progress   = NULL;  /* 外环: 学习进度 */

/* 噪音波形 */
#define LR_WAVE_POINTS  48
static lv_obj_t   *lr_wave_line  = NULL;
static lv_point_t  lr_wave_pts[LR_WAVE_POINTS];
static uint8_t     lr_wave_buf[LR_WAVE_POINTS];
static uint8_t     lr_wave_idx   = 0;
static uint32_t    lr_wave_last_tick = 0;
#define LR_WAVE_SAMPLE_MS  250   /* 250ms 采一次 (波形更"温柔") */

/* 环境信息卡 */
static lv_obj_t *lr_env_panel     = NULL;
static lv_obj_t *lr_env_temp_lbl  = NULL;
static lv_obj_t *lr_env_humi_lbl  = NULL;
static lv_obj_t *lr_env_lux_lbl   = NULL;

/* 控制按钮 — 极小, 暗灰 */
static lv_obj_t *lr_btn_pause     = NULL;
static lv_obj_t *lr_btn_end       = NULL;
static lv_obj_t *lr_pause_lbl     = NULL;

/* 调试面板 (可切换) — 左侧滑出, 显示 AI 分类结果 + 当前评分 + 从机压力值 */
static lv_obj_t *lr_btn_dbg       = NULL;   /* 顶部 DBG 按钮 */
static lv_obj_t *lr_dbg_panel     = NULL;   /* 调试面板容器 */
static uint8_t   lr_dbg_visible   = 0;      /* 0=隐藏, 1=显示 */
static lv_obj_t *lr_dbg_lbl_ai    = NULL;   /* "AI" 标题 */
static lv_obj_t *lr_dbg_lbl_class = NULL;   /* ">> Focus" 主分类结果 */
static lv_obj_t *lr_dbg_lbl_score = NULL;   /* Sc: 88 (融合后当前评分) */
static lv_obj_t *lr_dbg_lbl_pr    = NULL;   /* Pr: 0.0 (从机压力值) */

/* 内部状态 */
static uint32_t lr_last_elapsed_sec = 0;

/* 背景状态色: 记录当前已应用的分类, 避免重复设置同一颜色 (0=默认深色,
 * 1=专注蓝, 2=不专注黄, 3=无人红), 首次进入时强制刷新一次 */
static int8_t   lr_bg_state = -1;
/* 去抖计数: 在 ST_LEARNING 下, 连续 LR_FOCUS_STREAK_TH 次同向
 * (>=TH 算专注, <TH 算不专注) 才允许切换分类。方向相反时计数清零。
 * -1 = 无累计方向 (状态切换/初始时清零) */
static int8_t   lr_focus_streak_dir = -1;
static uint8_t  lr_focus_streak_cnt = 0;
/* 最近一次"此刻专注分" (UpdateFusion 写入, 只含视觉+姿态, 见下方
 * lr_calc_focus_score), UpdateStatus 在状态切换瞬间 (此时可能还没有
 * 新的 Fusion 帧) 复用该值判断专注/不专注分类 */
static uint8_t  lr_last_focus_score = 0;

/* ======================== 内部小工具 ======================== */
static lv_obj_t *lr_make_label(lv_obj_t *parent, const char *txt,
                                const lv_font_t *font, lv_color_t color,
                                int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

/* ======================== 背景状态色渐变 ========================
 * 复用 lv_anim (与进度环相同套路), 用混色因子 0..255 在
 * lr_bg_anim_from_color -> lr_bg_anim_to_color 间平滑过渡,
 * 避免整屏底色突变刺眼 (符合"数值变化平滑"设计原则)。 */
/* lv_color_hex() 是内联函数非常量表达式, 不能用作全局初始值;
 * 统一在 LearningScreen_Init() 中赋值为 LR_BG_DEEP */
static lv_color_t lr_bg_current_color;
static lv_color_t lr_bg_anim_from_color;
static lv_color_t lr_bg_anim_to_color;

static void lr_bg_anim_exec_cb(void *obj, int32_t v)
{
    lv_color_t c = lv_color_mix(lr_bg_anim_to_color, lr_bg_anim_from_color, (lv_opa_t)v);
    lv_obj_set_style_bg_color((lv_obj_t *)obj, c, 0);
}

static void lr_bg_anim_ready_cb(lv_anim_t *a)
{
    LV_UNUSED(a);
    lr_bg_current_color = lr_bg_anim_to_color;
}

static void lr_bg_animate_to(lv_color_t target)
{
    if (lr_screen == NULL) return;
    lr_bg_anim_from_color = lr_bg_current_color;
    lr_bg_anim_to_color   = target;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, lr_screen);
    lv_anim_set_exec_cb(&a, lr_bg_anim_exec_cb);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, LR_BG_TRANSITION_MS);
    lv_anim_set_ready_cb(&a, lr_bg_anim_ready_cb);
    lv_anim_start(&a);
}

/* 从融合结果中提出"此刻专注分" — 只用 vision_score + posture_score,
 * 按 FusionTask 中二者的原始权重比例 (40:30) 归一化到 0..100:
 *   focus_score = (vision*40 + posture*30) / 70
 * 不含 env_score / duration_score (用户此刻无法通过调整行为改变这两项,
 * 混入会让背景反馈和用户行为的因果关系错配, 详见上方设计说明)。 */
static uint8_t lr_calc_focus_score(const FusionResult_t *f)
{
    if (!f) return 0;
    uint32_t weighted = (uint32_t)f->vision_score  * FUSION_W_VISION
                       + (uint32_t)f->posture_score * FUSION_W_POSTURE;
    uint32_t denom     = FUSION_W_VISION + FUSION_W_POSTURE;   /* = 70 */
    return (uint8_t)((weighted + denom / 2) / denom);
}

/* 依据当前学习状态机状态 + "此刻专注分" (仅视觉+姿态), 决定背景颜色
 * 分类并触发渐变:
 *   0 = 默认深色 — 非 LEARNING/AWAY (PICKING/PAUSED/SUMMARY 等)
 *   1 = 专注(蓝) — ST_LEARNING 且 focus_score >= LR_FOCUS_SCORE_TH
 *   2 = 不专注(黄) — ST_LEARNING 且 focus_score <  LR_FOCUS_SCORE_TH
 *   3 = 无人(红) — ST_AWAY (从机压力消失 >=30s, StateTask 已自动判定, 优先级最高)
 * 由 LearningScreen_UpdateFusion() 每次收到新融合结果 (1Hz) 时调用。
 * 加入"连续 N 次同向"去抖: 避免评分在阈值附近抖动时背景色频繁切换。
 * - ST_AWAY  / 其他状态: 立即切换 (状态本身有边沿, 不需要去抖)
 * - ST_LEARNING:        连续 LR_FOCUS_STREAK_TH 次同向才切换
 * 相同分类不重复触发动画。 */
static void lr_bg_update(uint8_t focus_score)
{
    if (lr_screen == NULL) return;

    LearningState_t s = State_GetCurrent();
    int8_t     new_state;
    lv_color_t target;

    if (s == ST_AWAY) {
        /* AWAY 是状态切换, 立即应用, 并清零 LEARNING 的去抖计数 */
        new_state = 3;
        target    = LR_BG_AWAY;
        lr_focus_streak_dir = -1;
        lr_focus_streak_cnt = 0;
    } else if (s == ST_LEARNING) {
        /* 本帧的"提议分类": 1=专注, 2=不专注 */
        int8_t proposed = (focus_score >= LR_FOCUS_SCORE_TH) ? 1 : 2;

        if (proposed == lr_focus_streak_dir) {
            if (lr_focus_streak_cnt < 255) lr_focus_streak_cnt++;
        } else {
            lr_focus_streak_dir = proposed;
            lr_focus_streak_cnt = 1;
        }

        /* 必须连续 LR_FOCUS_STREAK_TH 次同向, 且与当前显示分类不同, 才切换 */
        if (lr_focus_streak_cnt >= LR_FOCUS_STREAK_TH
            && proposed != lr_bg_state) {
            new_state = proposed;
            target    = (proposed == 1) ? LR_BG_FOCUS : LR_BG_DISTRACT;
        } else {
            /* 未达到阈值, 维持当前显示 (包括 lr_bg_state 还未设过的 -1) */
            return;
        }
    } else {
        /* PAUSED 等其他状态: 立即恢复默认深色, 清零去抖 */
        new_state = 0;
        target    = LR_BG_DEEP;
        lr_focus_streak_dir = -1;
        lr_focus_streak_cnt = 0;
    }

    if (new_state == lr_bg_state) return;  /* 分类未变, 不重复触发 */
    lr_bg_state = new_state;
    lr_bg_animate_to(target);
}

/* ======================== 进度环填充 (3s 慢) ======================== */
static void lr_progress_set_anim_cb(void *obj, int32_t v)
{
    lv_arc_set_value((lv_obj_t *)obj, (int16_t)v);
}

static void lr_progress_anim_to(int32_t new_value)
{
    if (lr_arc_progress == NULL) return;
    int16_t cur = lv_arc_get_value(lr_arc_progress);
    if (cur == (int16_t)new_value) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, lr_arc_progress);
    lv_anim_set_exec_cb(&a, lr_progress_set_anim_cb);
    lv_anim_set_values(&a, cur, new_value);
    lv_anim_set_time(&a, LR_PROGRESS_FILL_MS);
    lv_anim_start(&a);
}

/* ======================== 噪音波形 ======================== */
static void lr_wave_init(void)
{
    lr_wave_line = lv_line_create(lr_screen);
    int x0 = 20, x1 = 300, y0 = 213;
    int span = x1 - x0;
    for (int i = 0; i < LR_WAVE_POINTS; i++) {
        lr_wave_pts[i].x = (lv_coord_t)(x0 + (span * i) / (LR_WAVE_POINTS - 1));
        lr_wave_pts[i].y = (lv_coord_t)(y0 + 4);  /* 中心线 */
        lr_wave_buf[i]   = 0;
    }
    lv_line_set_points(lr_wave_line, lr_wave_pts, LR_WAVE_POINTS);
    lv_obj_set_style_line_color(lr_wave_line, LR_FG_DIM, 0);
    lv_obj_set_style_line_width(lr_wave_line, 1, 0);
    lv_obj_set_style_line_opa(lr_wave_line, LV_OPA_50, 0);
    lv_obj_set_style_line_rounded(lr_wave_line, true, 0);
}

/* 安静时近静态, 突响时短暂跳动 */
static void lr_wave_push(uint8_t sound_pct)
{
    if (lr_wave_line == NULL) return;

    lr_wave_buf[lr_wave_idx] = sound_pct;
    lr_wave_idx = (lr_wave_idx + 1) % LR_WAVE_POINTS;

    int yc = 217;
    int amp_max = 6;

    for (int i = 0; i < LR_WAVE_POINTS; i++) {
        uint8_t v = lr_wave_buf[i];
        int amp = (v * amp_max) / 100;
        if (amp < 1) amp = 1;
        int16_t offset = (int16_t)(amp *
            (int16_t)lv_trigo_sin(360 * i / LR_WAVE_POINTS) / 32767);
        lr_wave_pts[i].y = (lv_coord_t)(yc - offset);
    }
    lv_line_set_points(lr_wave_line, lr_wave_pts, LR_WAVE_POINTS);
}

/* ======================== 环境信息卡 ========================
 * 320x240 底部居中, 极淡卡片
 *   温度 °C | 湿度 % | 光照 lx
 *   样式: 圆角小卡片, 暗底, 灰文字
 */
static void lr_env_build(void)
{
    /* 卡片底 (圆角, 微微发亮) */
    lr_env_panel = lv_obj_create(lr_screen);
    lv_obj_set_size(lr_env_panel, 280, 22);
    lv_obj_set_pos(lr_env_panel, 20, 184);
    lv_obj_set_style_bg_color(lr_env_panel, LR_BG_PANEL, 0);
    lv_obj_set_style_bg_opa(lr_env_panel, LV_OPA_60, 0);
    lv_obj_set_style_radius(lr_env_panel, 11, 0);
    lv_obj_set_style_border_width(lr_env_panel, 0, 0);
    lv_obj_set_style_pad_all(lr_env_panel, 0, 0);
    lv_obj_clear_flag(lr_env_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* 温度 (左) */
    lr_env_temp_lbl = lv_label_create(lr_env_panel);
    lv_label_set_text(lr_env_temp_lbl, "--.- C");
    lv_obj_set_style_text_font(lr_env_temp_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_color(lr_env_temp_lbl, LR_FG_MID, 0);
    lv_obj_set_pos(lr_env_temp_lbl, 16, 5);

    /* 分隔点 */
    lv_obj_t *dot1 = lv_label_create(lr_env_panel);
    lv_label_set_text(dot1, "/");
    lv_obj_set_style_text_font(dot1, FONT_SMALL, 0);
    lv_obj_set_style_text_color(dot1, LR_FG_DIM, 0);
    lv_obj_set_pos(dot1, 100, 5);

    /* 湿度 (中) */
    lr_env_humi_lbl = lv_label_create(lr_env_panel);
    lv_label_set_text(lr_env_humi_lbl, "-- %");
    lv_obj_set_style_text_font(lr_env_humi_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_color(lr_env_humi_lbl, LR_FG_MID, 0);
    lv_obj_set_pos(lr_env_humi_lbl, 116, 5);

    /* 分隔点 */
    lv_obj_t *dot2 = lv_label_create(lr_env_panel);
    lv_label_set_text(dot2, "/");
    lv_obj_set_style_text_font(dot2, FONT_SMALL, 0);
    lv_obj_set_style_text_color(dot2, LR_FG_DIM, 0);
    lv_obj_set_pos(dot2, 188, 5);

    /* 光照 (右) */
    lr_env_lux_lbl = lv_label_create(lr_env_panel);
    lv_label_set_text(lr_env_lux_lbl, "-- lx");
    lv_obj_set_style_text_font(lr_env_lux_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_color(lr_env_lux_lbl, LR_FG_MID, 0);
    lv_obj_set_pos(lr_env_lux_lbl, 200, 5);
}

/* 更新环境显示 — 节流: 5s 一次 (环境数据不需要频繁更新) */
static uint32_t lr_env_last_tick = 0;
#define LR_ENV_INTERVAL_MS  5000
static void lr_env_update(const SensorData_t *d)
{
    if (!d || !lr_env_temp_lbl) return;
    TickType_t now = xTaskGetTickCount();
    if (lr_env_last_tick != 0 &&
        (now - lr_env_last_tick) < pdMS_TO_TICKS(LR_ENV_INTERVAL_MS)) return;
    lr_env_last_tick = now;

    char buf[16];
    /* SensorData_t 字段:
     *   temperature: centi-°C (2450 = 24.50°C)
     *   humidity:    centi-%   (6200 = 62.00%)
     *   lux_x100:    centi-lx  (32000 = 320 lx)
     *   soundIntensity: 0~100 百分比
     */
    int t_x100 = (int)(d->temperature);
    if (t_x100 < -5000) t_x100 = -5000;
    if (t_x100 >  8000) t_x100 =  8000;
    int t_int  = t_x100 / 100;
    int t_frac = abs(t_x100 % 100) / 10;  /* 保留一位小数 */
    if (t_frac < 0) t_frac = -t_frac;
    snprintf(buf, sizeof(buf), "%d.%d C", t_int, t_frac);
    lv_label_set_text(lr_env_temp_lbl, buf);

    int h = (int)(d->humidity / 100);
    if (h < 0)   h = 0;
    if (h > 100) h = 100;
    snprintf(buf, sizeof(buf), "%d %%", h);
    lv_label_set_text(lr_env_humi_lbl, buf);

    int lx = (int)(d->lux_x100 / 100);
    if (lx < 0)     lx = 0;
    if (lx > 99999) lx = 99999;
    snprintf(buf, sizeof(buf), "%d lx", lx);
    lv_label_set_text(lr_env_lux_lbl, buf);
}

/* ======================== 事件回调 ======================== */
static void lr_pause_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    LearningState_t s = State_GetCurrent();
    if (s == ST_LEARNING)      State_PostEvent(STATE_EVT_GUI_PAUSE,  0);
    else if (s == ST_PAUSED)   State_PostEvent(STATE_EVT_GUI_RESUME, 0);
}

static void lr_end_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    State_PostEvent(STATE_EVT_GUI_END, 0);
}

/* ======================== 调试面板 ========================
 *
 *  位置: 屏幕左侧, x=4, y=14, w=80, h=115
 *  默认隐藏; 点击顶部 "DBG" 按钮展开
 *
 *  内容 (14pt 分类结果 + 12pt 细节) — 仅显示 AI 识别结果 + 从机压力值:
 *
 *    AI Model
 *    >> Focus         ← 主分类结果 (14pt, 跟随概率最大类染色)
 *    ────────
 *    F: 85.2%         ← 专注概率 (绿)
 *    D: 10.1%         ← 分心概率 (黄)
 *    P:  4.7%         ← 玩手机概率 (橙)
 *    ────────
 *    Pr: 0.0          ← 从机刚收到的原始压力值
 *
 *  颜色用降饱和色, 仍与主界面气质一致.
 *  w=80 不与中央外环 (x=86) 重叠; 作为调试浮层, 仅在开发者点击 DBG 时出现.
 */
static lv_obj_t *lr_dbg_make_line(lv_obj_t *parent, const char *txt,
                                  int x, int y, lv_color_t color,
                                  const lv_font_t *font,
                                  lv_obj_t **out_lbl)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font ? font : FONT_SMALL, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    if (out_lbl) *out_lbl = l;
    return l;
}

static void lr_dbg_build(void)
{
    /* 面板底 (圆角暗卡) — 高度从 178 缩到 115, 只显示 AI 结果 + 压力 */
    lr_dbg_panel = lv_obj_create(lr_screen);
    lv_obj_set_size(lr_dbg_panel, 80, 115);
    lv_obj_set_pos(lr_dbg_panel, 4, 14);
    lv_obj_set_style_bg_color(lr_dbg_panel, LR_BG_PANEL, 0);
    lv_obj_set_style_bg_opa(lr_dbg_panel, LV_OPA_80, 0);
    lv_obj_set_style_radius(lr_dbg_panel, 6, 0);
    lv_obj_set_style_border_color(lr_dbg_panel, LR_GREEN_SOFT, 0);
    lv_obj_set_style_border_width(lr_dbg_panel, 1, 0);
    lv_obj_set_style_border_opa(lr_dbg_panel, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(lr_dbg_panel, 0, 0);
    lv_obj_clear_flag(lr_dbg_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* 行布局 (12pt 行高 ~13px, 14pt class 占 ~18px):
     *   y=2   标题 (12pt)
     *   y=15  class (14pt, 结束 y=33)
     *   y=35  divider 1
     *   y=45  Sc (当前融合评分, 0..100)
     *   y=58  divider 2
     *   y=68  Pr (从机原始压力值)
     *  末行结束 y=82, 面板高 115 留 33px 底部留白.
     *
     *  不再显示: F / D / P 概率 (主分类 >> 已能看出 AI 倾向,
     *           逐项百分比对调试价值低) + S (融合总分) +
     *           V (视觉子分) + Po (从机姿态分) + #seq (AI 帧号).
     *  新增: Sc (FusionResult_t.total_score, 0..100, 含视觉+姿态+环境+时长).
     */

    /* 标题 */
    lr_dbg_make_line(lr_dbg_panel, "AI Model", 6, 2,
                     LR_GREEN_SOFT, FONT_SMALL, &lr_dbg_lbl_ai);

    /* 主分类结果 (14pt, 跟随后续染色) */
    lr_dbg_make_line(lr_dbg_panel, ">> ---", 6, 15,
                     LR_FG_DIM, FONT_MID, &lr_dbg_lbl_class);

    /* 分隔线 1 */
    lr_dbg_make_line(lr_dbg_panel, "-------", 6, 35,
                     LR_FG_DIM, FONT_SMALL, NULL);

    /* Sc: 当前融合评分 (FusionResult_t.total_score, 0..100) */
    lr_dbg_make_line(lr_dbg_panel, "Sc: --", 6, 45,
                     LR_GREEN_SOFT, FONT_SMALL, &lr_dbg_lbl_score);

    /* 分隔线 2 */
    lr_dbg_make_line(lr_dbg_panel, "-------", 6, 58,
                     LR_FG_DIM, FONT_SMALL, NULL);

    /* 从机 (ESP32) 刚接收到的压力值 (仅此一项) */
    lr_dbg_make_line(lr_dbg_panel, "Pr: --", 6, 68,
                     LR_PROG_CYAN, FONT_SMALL, &lr_dbg_lbl_pr);

    /* 默认隐藏 */
    lv_obj_add_flag(lr_dbg_panel, LV_OBJ_FLAG_HIDDEN);
}

/* DBG 按钮: 顶部小开关, 高亮表示当前显示 */
static void lr_dbg_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lr_dbg_visible = !lr_dbg_visible;
    if (lr_dbg_visible) {
        lv_obj_clear_flag(lr_dbg_panel, LV_OBJ_FLAG_HIDDEN);
        /* 按钮变高亮 */
        lv_obj_set_style_bg_opa(lr_btn_dbg, LV_OPA_COVER, 0);
        lv_obj_set_style_border_opa(lr_btn_dbg, LV_OPA_70, 0);
    } else {
        lv_obj_add_flag(lr_dbg_panel, LV_OBJ_FLAG_HIDDEN);
        /* 按钮恢复暗色 */
        lv_obj_set_style_bg_opa(lr_btn_dbg, LV_OPA_40, 0);
        lv_obj_set_style_border_opa(lr_btn_dbg, LV_OPA_20, 0);
    }
}

/* ======================== 公开 API ======================== */

void LearningScreen_Init(void)
{
    if (lr_screen != NULL) return;

    /* ====== 屏幕根对象 — 现代深色 ====== */
    lr_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(lr_screen, LR_BG_DEEP, 0);
    lv_obj_set_style_bg_opa(lr_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(lr_screen, 0, 0);
    lv_obj_clear_flag(lr_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 背景状态色初始值: 默认深色, 分类标记复位为"未设置" */
    lr_bg_current_color   = LR_BG_DEEP;
    lr_bg_anim_from_color = LR_BG_DEEP;
    lr_bg_anim_to_color   = LR_BG_DEEP;
    lr_bg_state           = -1;
    /* 评分去抖计数: 每次进入学习屏重置, 避免上次会话的累计影响 */
    lr_focus_streak_dir   = -1;
    lr_focus_streak_cnt   = 0;

    /* 背景图已移除 (省 ~150KB Flash), 屏幕深色底 (LR_BG_DEEP) 作为兜底 */
    /* ==================== 顶部极小状态条 (y=0..14) ==================== */
    /* 左侧: 状态 (学习圆点 + 文字) */
    lv_obj_t *state_dot = lv_obj_create(lr_screen);
    lv_obj_set_size(state_dot, 6, 6);
    lv_obj_set_pos(state_dot, 12, 6);
    lv_obj_set_style_radius(state_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(state_dot, LR_GREEN_SOFT, 0);
    lv_obj_set_style_bg_opa(state_dot, LV_OPA_80, 0);
    lv_obj_set_style_border_width(state_dot, 0, 0);
    lv_obj_set_style_pad_all(state_dot, 0, 0);

    lr_label_state = lr_make_label(lr_screen, "Learning",
                                    FONT_SMALL, LR_FG_MID, 24, 3);

    /* 中间: 时钟 (偏右, 不抢眼) */
    lr_label_clock = lr_make_label(lr_screen, "--:--:--",
                                    FONT_SMALL, LR_FG_DIM, 168, 3);

    /* 右上: 摄像头缩略图 (极淡) */
    lr_cam_thumb = lv_obj_create(lr_screen);
    lv_obj_set_size(lr_cam_thumb, 36, 14);
    lv_obj_set_pos(lr_cam_thumb, 272, 0);
    lv_obj_set_style_radius(lr_cam_thumb, 3, 0);
    lv_obj_set_style_bg_color(lr_cam_thumb, LR_BG_PANEL, 0);
    lv_obj_set_style_bg_opa(lr_cam_thumb, LV_OPA_40, 0);
    lv_obj_set_style_border_color(lr_cam_thumb, LR_FG_DIM, 0);
    lv_obj_set_style_border_width(lr_cam_thumb, 1, 0);
    lv_obj_set_style_border_opa(lr_cam_thumb, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(lr_cam_thumb, 0, 0);
    lv_obj_clear_flag(lr_cam_thumb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *cam_lbl = lv_label_create(lr_cam_thumb);
    lv_label_set_text(cam_lbl, "CAM");
    lv_obj_set_style_text_font(cam_lbl, FONT_SMALL, 0);
    lv_obj_set_style_text_color(cam_lbl, LR_FG_DIM, 0);
    lv_obj_set_style_text_opa(cam_lbl, LV_OPA_50, 0);
    lv_obj_center(cam_lbl);

    /* WiFi 文字 (状态点右, 极小) — 简化: 复用 wifi label */
    lr_label_wifi = lr_make_label(lr_screen, "",
                                   FONT_SMALL, LR_FG_DIM, 232, 3);

    /* ==================== 外环: 学习进度 ====================
     * 中心 (160, 105), 半径 74, 跨 270° (起点 135°)
     * 线宽 2, 极淡, 像背景装饰, 不抢眼 */
    lr_arc_progress = lv_arc_create(lr_screen);
    lv_obj_set_size(lr_arc_progress,
                    (lv_coord_t)(LR_PROGRESS_RADIUS * 2),
                    (lv_coord_t)(LR_PROGRESS_RADIUS * 2));
    lv_obj_set_pos(lr_arc_progress,
                   (lv_coord_t)(LR_CENTER_X - LR_PROGRESS_RADIUS),
                   (lv_coord_t)(LR_CENTER_Y - LR_PROGRESS_RADIUS));
    lv_arc_set_range(lr_arc_progress, 0, 100);
    lv_arc_set_value(lr_arc_progress, 100);   /* 倒计时: 初始满, 随时间变空 */
    lv_arc_set_rotation(lr_arc_progress, 135);
    lv_arc_set_bg_angles(lr_arc_progress, 0, 270);
    /* 轨道 (极暗) */
    lv_obj_set_style_arc_color(lr_arc_progress, LR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_width(lr_arc_progress, LR_RING_WIDTH_OUT, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(lr_arc_progress, LV_OPA_50, LV_PART_MAIN);
    /* 指示器 — 柔青 */
    lv_obj_set_style_arc_color(lr_arc_progress, LR_PROG_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(lr_arc_progress, LR_RING_WIDTH_OUT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(lr_arc_progress, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(lr_arc_progress, LV_OPA_70, LV_PART_INDICATOR);
    lv_obj_clear_flag(lr_arc_progress, LV_OBJ_FLAG_CLICKABLE);

    /* ==================== 中央倒计时大字 (48pt, 一眼看清) ====================
     * 外环内径约 140px, 48pt "MM:SS" 约 130px 宽, 居中放下.
     * 用户偶尔瞥一眼, 故用最大可用字号突出剩余时间. */
    lr_label_time = lv_label_create(lr_screen);
    lv_label_set_text(lr_label_time, "30:00");
    lv_obj_set_style_text_font(lr_label_time, FONT_TIME, 0);
    lv_obj_set_style_text_color(lr_label_time, LR_FG_SOFT, 0);
    lv_obj_set_width(lr_label_time, 160);
    lv_obj_set_style_text_align(lr_label_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lr_label_time, LR_CENTER_X - 80, LR_CENTER_Y - 28);

    /* 目标提示 (12pt 暗灰, 倒计时下方) */
    lr_label_target = lv_label_create(lr_screen);
    lv_label_set_text(lr_label_target, "剩余 30 min");
    lv_obj_set_style_text_font(lr_label_target, FONT_SMALL, 0);
    lv_obj_set_style_text_color(lr_label_target, LR_FG_DIM, 0);
    lv_obj_set_width(lr_label_target, 160);
    lv_obj_set_style_text_align(lr_label_target, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lr_label_target, LR_CENTER_X - 80, LR_CENTER_Y + 34);

    /* ==================== 环境信息卡 (y=184..206) ==================== */
    lr_env_build();

    /* ==================== 调试面板 (默认隐藏) ==================== */
    lr_dbg_build();

    /* ==================== DBG 按钮 (顶部小开关) ====================
     * 位置: x=88, y=2, w=22, h=11 (在 "Learning" 文字和时钟之间)
     * 暗色小标签, 点击后变高亮表示调试面板已展开
     */
    lr_btn_dbg = lv_btn_create(lr_screen);
    lv_obj_set_size(lr_btn_dbg, 22, 11);
    lv_obj_set_pos(lr_btn_dbg, 88, 2);
    lv_obj_set_style_bg_color(lr_btn_dbg, LR_BG_GLOW, 0);
    lv_obj_set_style_bg_opa(lr_btn_dbg, LV_OPA_40, 0);
    lv_obj_set_style_radius(lr_btn_dbg, 3, 0);
    lv_obj_set_style_border_color(lr_btn_dbg, LR_FG_DIM, 0);
    lv_obj_set_style_border_width(lr_btn_dbg, 1, 0);
    lv_obj_set_style_border_opa(lr_btn_dbg, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(lr_btn_dbg, 0, 0);
    lv_obj_set_style_pad_all(lr_btn_dbg, 0, 0);
    {
        lv_obj_t *lbl = lv_label_create(lr_btn_dbg);
        lv_label_set_text(lbl, "DBG");
        lv_obj_set_style_text_color(lbl, LR_FG_MID, 0);
        lv_obj_set_style_text_font(lbl, FONT_SMALL, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(lr_btn_dbg, lr_dbg_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* ==================== 噪音波形 (y=210..222) ==================== */
    lr_wave_init();

    /* ==================== 极小控制按钮 (y=222..240) ====================
     * 右下角两个胶囊小按钮, 暗灰, 不抢眼
     *   Pause: x=178, w=66, h=18
     *   End:   x=248, w=58, h=18
     */
    lr_btn_pause = lv_btn_create(lr_screen);
    lv_obj_set_size(lr_btn_pause, 66, 18);
    lv_obj_set_pos(lr_btn_pause, 178, 220);
    lv_obj_set_style_bg_color(lr_btn_pause, LR_BG_GLOW, 0);
    lv_obj_set_style_bg_opa(lr_btn_pause, LV_OPA_60, 0);
    lv_obj_set_style_radius(lr_btn_pause, 9, 0);
    lv_obj_set_style_border_width(lr_btn_pause, 1, 0);
    lv_obj_set_style_border_color(lr_btn_pause, LR_FG_DIM, 0);
    lv_obj_set_style_border_opa(lr_btn_pause, LV_OPA_30, 0);
    lv_obj_set_style_shadow_width(lr_btn_pause, 0, 0);
    lv_obj_set_style_pad_all(lr_btn_pause, 0, 0);
    lr_pause_lbl = lv_label_create(lr_btn_pause);
    lv_label_set_text(lr_pause_lbl, "Pause");
    lv_obj_set_style_text_color(lr_pause_lbl, LR_FG_MID, 0);
    lv_obj_set_style_text_font(lr_pause_lbl, FONT_SMALL, 0);
    lv_obj_center(lr_pause_lbl);
    lv_obj_add_event_cb(lr_btn_pause, lr_pause_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lr_btn_end = lv_btn_create(lr_screen);
    lv_obj_set_size(lr_btn_end, 58, 18);
    lv_obj_set_pos(lr_btn_end, 248, 220);
    lv_obj_set_style_bg_color(lr_btn_end, LR_BG_GLOW, 0);
    lv_obj_set_style_bg_opa(lr_btn_end, LV_OPA_60, 0);
    lv_obj_set_style_radius(lr_btn_end, 9, 0);
    lv_obj_set_style_border_width(lr_btn_end, 1, 0);
    lv_obj_set_style_border_color(lr_btn_end, LR_FG_DIM, 0);
    lv_obj_set_style_border_opa(lr_btn_end, LV_OPA_30, 0);
    lv_obj_set_style_shadow_width(lr_btn_end, 0, 0);
    lv_obj_set_style_pad_all(lr_btn_end, 0, 0);
    lv_obj_t *end_lbl = lv_label_create(lr_btn_end);
    lv_label_set_text(end_lbl, "End");
    lv_obj_set_style_text_color(end_lbl, LR_FG_MID, 0);
    lv_obj_set_style_text_font(end_lbl, FONT_SMALL, 0);
    lv_obj_center(end_lbl);
    lv_obj_add_event_cb(lr_btn_end, lr_end_btn_event_cb, LV_EVENT_CLICKED, NULL);
}

lv_obj_t *LearningScreen_GetScreen(void)
{
    return lr_screen;
}

/* 状态文字 + 暂停/结束按钮 状态同步 */
void LearningScreen_UpdateStatus(void)
{
    if (!lr_label_state) return;
    LearningState_t s = State_GetCurrent();

    /* 顶部状态文字 */
    switch (s)
    {
    case ST_LEARNING: lv_label_set_text(lr_label_state, "Learning"); break;
    case ST_PAUSED:   lv_label_set_text(lr_label_state, "Paused");   break;
    case ST_AWAY:     lv_label_set_text(lr_label_state, "Away");     break;
    default:          lv_label_set_text(lr_label_state, "");         break;
    }

    /* 暂停/继续按钮文字 */
    if (lr_pause_lbl) {
        if (s == ST_PAUSED)      lv_label_set_text(lr_pause_lbl, "Resume");
        else if (s == ST_AWAY)   lv_label_set_text(lr_pause_lbl, "Away");
        else                     lv_label_set_text(lr_pause_lbl, "Pause");
    }
    /* AWAY 状态: 按钮置灰 */
    if (lr_btn_pause) {
        if (s == ST_AWAY) {
            lv_obj_set_style_bg_opa(lr_btn_pause, LV_OPA_20, 0);
            lv_obj_set_style_border_opa(lr_btn_pause, LV_OPA_10, 0);
        } else {
            lv_obj_set_style_bg_opa(lr_btn_pause, LV_OPA_60, 0);
            lv_obj_set_style_border_opa(lr_btn_pause, LV_OPA_30, 0);
        }
    }

    /* ===== 圆环颜色根据持续状态平滑过渡 ===== (已移除: 内环呼吸环删除后不再变色) */

    /* ===== 整屏底色: 依据状态 (+ 最近一次专注分) 切换 专注蓝/不专注黄/无人红 =====
     * 状态切换瞬间 (例如刚进入 ST_AWAY) 立即刷新一次, 不等下一帧 Fusion 结果 */
    lr_bg_update(lr_last_focus_score);
}

/* 室内数据更新 — 推波形 + 更新环境卡 */
void LearningScreen_UpdateSensor(const SensorData_t *d)
{
    if (!d) return;

    /* 节流: 250ms 推一次波形 */
    TickType_t now = xTaskGetTickCount();
    if ((now - lr_wave_last_tick) >= pdMS_TO_TICKS(LR_WAVE_SAMPLE_MS)) {
        lr_wave_last_tick = now;
        uint8_t sound = (d->soundIntensity > 100) ? 100 : (uint8_t)d->soundIntensity;
        lr_wave_push(sound);
    }

    /* 环境卡 — 5s 节流 */
    lr_env_update(d);
}

/* 中央倒计时大字更新
 *   显示剩余时间 "MM:SS" (从 30min 目标倒数到 0)
 *   同时驱动外环进度 (剩余比例, 从满到空)
 *   数据源: StateTask 累加器 (PAUSED/AWAY 时自动冻结) */
void LearningScreen_UpdateStopwatch(void)
{
    if (!lr_label_time) return;

    uint32_t sec = State_GetElapsedSeconds();
    if (sec == lr_last_elapsed_sec) return;
    lr_last_elapsed_sec = sec;

    /* 目标时长取自 StateTask (用户在 Picker 选择的值), 保证与选择同步。
     * 若无目标 (0), 回退到默认 30min 仅用于显示, 避免除 0。 */
    uint32_t tgt = State_GetTargetSeconds();
    if (tgt == 0) tgt = 30UL * 60UL;

    /* 倒计时: 剩余 = 目标 - 已用 (clamp >= 0) */
    uint32_t remain = (sec < tgt) ? (tgt - sec) : 0;
    uint32_t mm = remain / 60;
    uint32_t ss = remain % 60;

    char buf[24];
    snprintf(buf, sizeof(buf), "%lu:%02lu", (unsigned long)mm, (unsigned long)ss);
    lv_label_set_text(lr_label_time, buf);

    /* 目标提示: "剩余 X min" (剩余分钟, 基于用户选择的目标计算) */
    if (lr_label_target) {
        snprintf(buf, sizeof(buf), "剩余 %lu min", (unsigned long)mm);
        lv_label_set_text(lr_label_target, buf);
    }

    /* 进度环: 显示剩余比例 (从满到空, 倒计时直觉) */
    {
        uint32_t pct = (remain * 100UL) / tgt;
        if (pct > 100) pct = 100;
        lr_progress_anim_to((int32_t)pct);
    }
}

/* 顶部 WiFi + 时钟刷新 */
void LearningScreen_RefreshBar(void)
{
    if (lr_label_wifi) {
        if (g_last_remote_tick == 0) {
            lv_label_set_text(lr_label_wifi, "");
        } else if ((xTaskGetTickCount() - g_last_remote_tick) < pdMS_TO_TICKS(30000)) {
            lv_label_set_text(lr_label_wifi, "ON");
        } else {
            lv_label_set_text(lr_label_wifi, "OFF");
        }
    }
    if (lr_label_clock) {
        WeatherInfo_t wi;
        char buf[16];
        if (Weather_GetLatest(&wi) == 0 && wi.time[0] != '\0') {
            snprintf(buf, sizeof(buf), "%s", wi.time);
        } else {
            snprintf(buf, sizeof(buf), "--:--:--");
        }
        lv_label_set_text(lr_label_clock, buf);
    }
}

/* ======================== 调试面板更新 ========================
 * 把 AI 推理的实时结果 + 从机压力值 推送到调试面板.
 * 由 DisplayTask 在每次融合结果更新时调用.
 *
 * 关键: AI 概率 (用于主分类 >>) 必须从 AITask 实时取, 不可走 FusionTask
 *       (Fusion 1Hz 轮询, 滞后 0~1s, 与 Picker 屏幕显示不一致).
 *       调试面板仅显示: AI 主分类 + 当前融合评分 Sc + 从机压力 Pr.
 *       焦点分数 lr_last_focus_score 仍从 FusionResult_t 计算,
 *       用于驱动整屏底色 (非调试面板).
 */
void LearningScreen_UpdateFusion(const FusionResult_t *f)
{
    if (!lr_dbg_panel) return;

    char buf[24];

    /* ===== F / D / P / E 概率: 走 AITask 实时通道 =====
     * 4 类: 专心 / 走神 / 玩手机 / 无人 */
    float probs[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    uint32_t ai_ts = 0;
    int ai_valid = (AI_GetLatestProbs(probs, &ai_ts) == 0);

    int f_pct = 0, d_pct = 0, p_pct = 0, e_pct = 0;
    if (ai_valid) {
        f_pct = (int)(probs[0] * 1000.0f + 0.5f);  // 0.0~1.0 → 0~1000
        d_pct = (int)(probs[1] * 1000.0f + 0.5f);
        p_pct = (int)(probs[2] * 1000.0f + 0.5f);  // 玩手机
        e_pct = (int)(probs[3] * 1000.0f + 0.5f);  // 无人
        if (f_pct > 1000) f_pct = 1000; if (f_pct < 0) f_pct = 0;
        if (d_pct > 1000) d_pct = 1000; if (d_pct < 0) d_pct = 0;
        if (p_pct > 1000) p_pct = 1000; if (p_pct < 0) p_pct = 0;
        if (e_pct > 1000) e_pct = 1000; if (e_pct < 0) e_pct = 0;
    }

    /* ===== Sc: 当前融合评分 (FusionResult_t.total_score, 0..100) =====
     * 取自本函数入参 f, 实时反映本次融合结果 (不再单独从 AITask 取, 因为
     * 调试面板只展示"经过融合后给用户看的分数", 跟进度环、底色一致)。 */
    if (lr_dbg_lbl_score) {
        if (f) snprintf(buf, sizeof(buf), "Sc:%3u", (unsigned)f->total_score);
        else   snprintf(buf, sizeof(buf), "Sc: --");
        lv_label_set_text(lr_dbg_lbl_score, buf);
    }

    /* ===== 主分类结果 (取 4 个概率中的最大者) =====
     * 平局时按优先级: F > D > P > E
     * 用相应颜色染色, 一目了然
     * 类名取短以适配 14pt 字体: Focus / Distract / Phone / Empty */
    if (lr_dbg_lbl_class) {
        if (!ai_valid) {
            lv_label_set_text(lr_dbg_lbl_class, ">> ---");
            lv_obj_set_style_text_color(lr_dbg_lbl_class, LR_FG_DIM, 0);
        } else {
            int max_pct = f_pct;
            if (d_pct > max_pct) max_pct = d_pct;
            if (p_pct > max_pct) max_pct = p_pct;
            if (e_pct > max_pct) max_pct = e_pct;

            const char *class_name;
            lv_color_t  class_color;
            if (max_pct == f_pct) {
                class_name = "Focus";     class_color = LR_GREEN_SOFT;
            } else if (max_pct == d_pct) {
                class_name = "Distract";  class_color = LR_YELLOW_SOFT;
            } else if (max_pct == p_pct) {
                class_name = "Phone";     class_color = LR_ORANGE_SOFT;
            } else {
                class_name = "Empty";     class_color = LR_FG_DIM;
            }

            snprintf(buf, sizeof(buf), ">> %s", class_name);
            lv_label_set_text(lr_dbg_lbl_class, buf);
            lv_obj_set_style_text_color(lr_dbg_lbl_class, class_color, 0);
        }
    }

    /* ===== Pr: 从机 (ESP32) 刚接收到的原始压力值 =====
     * 直接走 Slave_GetLatest 取最新一帧, 不经融合, 反映"刚收到"的值。
     * 无数据时显示 "--" 占位。
     * 调试面板仅保留此一项从机数据, 其它姿态分等已隐藏。 */
    {
        SlaveData_t sd;
        if (Slave_GetLatest(&sd) == 0) {
            snprintf(buf, sizeof(buf), "Pr:%.1f", (double)sd.pressure);
            if (lr_dbg_lbl_pr) lv_label_set_text(lr_dbg_lbl_pr, buf);
        } else {
            if (lr_dbg_lbl_pr) lv_label_set_text(lr_dbg_lbl_pr, "Pr: --");
        }
    }

    /* ===== 计算"此刻专注分", 驱动整屏底色 (非调试面板) =====
     * 注意: lr_last_focus_score 仍需更新, 否则 LearningScreen_UpdateStatus
     * 复用旧值导致整屏底色卡在初始色。
     * lr_calc_focus_score 走 FusionResult_t (含视觉+姿态, 不被扣分影响)。 */
    if (f) {
        lr_last_focus_score = lr_calc_focus_score(f);
        lr_bg_update(lr_last_focus_score);
    }
}
