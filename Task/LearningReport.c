/**
  ******************************************************************************
  * @file    LearningReport.c
  * @brief   "Learning Report" screen - Wabi-sabi warm gray style
  *          LVGL v8.3, 320x240
  *
  *  Data displayed (mirrors SessionSummary_t):
  *    y=  0..38   Title "Learning Report" + date subtitle + Back button
  *    y= 46..96   Hero cards (3): Duration, Avg Score, Focus %
  *    y=108..126  Mode distribution bar: Focus / Distract / Fatigue
  *    y=130..146  Combined mode percentage label
  *    y=150..170  End mode banner
  *    y=170..240  Empty whitespace (wabi-sabi)
  *
  *  Animations:
  *    - 4 elements fade in staggered (opacity 0->255, 300ms, ease_out)
  *    - Mode bar segments grow from 0 to target widths (700ms, ease_out)
  *    - Cards have no click state, stay static
  *
  *  Performance:
  *    - All static lv_style_t, objects created once
  *    - No images / no blur / no expensive filters
  *    - Animations use lv_anim_t (HW timer driven)
  *
  *  Encoding note:
  *    armcc does not accept multi-byte (UTF-8) string literals, so this
  *    file keeps all strings in plain ASCII.  SourceHanSerifSC fonts also
  *    include ASCII glyphs, so the same fonts can render English labels.
  ******************************************************************************
  */

#include "LearningReport.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* Fonts: project GUI/LVGL_myGui/src/generated/guider_fonts/ has these.
 *   - lv_font_SourceHanSerifSC_Regular_12/17/30  (serif, also has ASCII)
 *   - lv_font_montserrat_14/20                   (sans, for numbers)
 * Enable the corresponding macros in lv_conf.h if not already on. */
extern const lv_font_t lv_font_SourceHanSerifSC_Regular_12;
extern const lv_font_t lv_font_SourceHanSerifSC_Regular_17;
extern const lv_font_t lv_font_montserrat_14;
extern const lv_font_t lv_font_montserrat_20;

/* Font shortcuts */
#define FONT_TITLE    (&lv_font_SourceHanSerifSC_Regular_17)  /* title */
#define FONT_BODY     (&lv_font_SourceHanSerifSC_Regular_12)  /* small text */
#define FONT_NUM      (&lv_font_montserrat_20)                /* large numbers */
#define FONT_NUM_S    (&lv_font_montserrat_14)                /* small numbers */

/* ======================== Colors (high contrast, dark text for readability) ======================== */
#define LR_BG_TOP          lv_color_hex(0xE8E1D5)   /* warm cream */
#define LR_BG_BOTTOM       lv_color_hex(0xCFC8B8)   /* deep warm gray */
#define LR_CARD_BG         lv_color_hex(0xFFFFFF)   /* pure white card */
#define LR_SHADOW          lv_color_hex(0x6E6759)   /* warm gray shadow (darker) */
#define LR_TEXT_PRIMARY    lv_color_hex(0x1A1815)   /* near-black (max contrast) */
#define LR_TEXT_SECONDARY  lv_color_hex(0x3A3631)   /* dark warm gray (was too light) */
#define LR_TEXT_MUTED      lv_color_hex(0x5C5852)   /* medium dark warm gray (was too light) */
#define LR_ACCENT_GOLD     lv_color_hex(0x7A5F3A)   /* darker warm gold (was too light) */
#define LR_DATA_HILITE     lv_color_hex(0x6B4A2A)   /* deep sandalwood (darker, high contrast) */
#define LR_MODE_FOCUS      lv_color_hex(0x7A5F3A)   /* mode bar: focus (darker gold) */
#define LR_MODE_DISTRACT   lv_color_hex(0x5C5852)   /* mode bar: distract (darker mid gray) */
#define LR_MODE_FATIGUE    lv_color_hex(0x1A1815)   /* mode bar: fatigue (near-black) */
#define LR_TRACK           lv_color_hex(0xB5AE9F)   /* mode bar track (visible) */
#define LR_BANNER_BG       lv_color_hex(0xE5DDCB)   /* end-mode banner background */

/* ======================== Geometry (320x240) ======================== */
#define LR_SCR_W           320
#define LR_SCR_H           240
#define LR_SIDE_PAD        16
#define LR_CARD_W          90    /* 16 + 90 + 9 + 90 + 9 + 90 + 16 = 320 */
#define LR_CARD_H          50
#define LR_CARD_GAP        9

/* Vertical layout y-coordinates */
#define LR_TITLE_Y         8
#define LR_SUBTITLE_Y      30
#define LR_HERO_Y          46
#define LR_BAR_Y           112   /* mode bar top y */
#define LR_BAR_H           14
#define LR_BAR_LBL_Y       130   /* mode bar labels under */
#define LR_END_BANNER_Y    150
#define LR_END_BANNER_H    18

/* ======================== Animation durations ======================== */
#define LR_ANIM_FADE_MS    300
#define LR_ANIM_BAR_MS     700

/* ======================== Default demo values (used only at Init) ======================== */
#define LR_DEFAULT_FOCUS     65
#define LR_DEFAULT_DISTRACT  20
#define LR_DEFAULT_FATIGUE   15

/* ======================== Static styles (initialized once) ======================== */
static lv_style_t st_screen;        /* screen: vertical gradient bg */
static lv_style_t st_title;         /* title text */
static lv_style_t st_subtitle;      /* subtitle text */
static lv_style_t st_back_btn;      /* back button (transparent + gold text) */
static lv_style_t st_card;          /* card: white + 20 radius + soft shadow */
static lv_style_t st_card_lbl;      /* card label (top, dark warm gray small) */
static lv_style_t st_card_val;      /* card value (bottom, dark sandalwood numeric) */
static lv_style_t st_card_sub;      /* card sub-label (top-right, muted, small) */
static lv_style_t st_section_lbl;   /* section label (small, dark warm gray) */
static lv_style_t st_bar_seg;       /* mode bar segment (no border, no radius) */
static lv_style_t st_bar_track;     /* mode bar track (under segments) */
static lv_style_t st_end_banner;    /* end-mode banner (warm cream bg) */
static lv_style_t st_end_text;      /* end-mode text (dark) */

/* ======================== Widget pointers ======================== */
static lv_obj_t *lr_screen     = NULL;
static lv_obj_t *lr_btn_back   = NULL;

/* Hero cards */
static lv_obj_t *lr_card_dur   = NULL;
static lv_obj_t *lr_card_score = NULL;
static lv_obj_t *lr_card_focus = NULL;
static lv_obj_t *lr_lbl_dur_sub= NULL;   /* "of 30:00" small top-right of duration */
static lv_obj_t *lr_lbl_dur_val= NULL;
static lv_obj_t *lr_lbl_score  = NULL;
static lv_obj_t *lr_lbl_focus  = NULL;

/* Mode distribution bar */
static lv_obj_t *lr_bar_track  = NULL;   /* full-width track behind */
static lv_obj_t *lr_bar_focus  = NULL;   /* focus segment */
static lv_obj_t *lr_bar_dist   = NULL;   /* distract segment */
static lv_obj_t *lr_bar_fat    = NULL;   /* fatigue segment */
static lv_obj_t *lr_lbl_modes  = NULL;   /* "Focus 75%  Distract 15%  Fatigue 10%" */

/* End mode banner */
static lv_obj_t *lr_end_banner  = NULL;
static lv_obj_t *lr_lbl_end     = NULL;

/* Back button callback (registered by DisplayTask) */
static LearningReportBackCb_t s_back_cb = NULL;

/* ======================== Animation callbacks ======================== */

/**
 * @brief Generic opacity animation callback
 * @param obj target object
 * @param v   current opacity value (0..255)
 */
static void anim_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

/**
 * @brief Width animation callback for mode bar segments
 *        Animates the segment width to "v" pixels
 */
static void anim_w_cb(void *obj, int32_t v)
{
    lv_obj_set_width((lv_obj_t *)obj, (lv_coord_t)v);
}

/**
 * @brief "Back" button event: invokes the externally-registered callback
 *        (DisplayTask posts STATE_EVT_GUI_BACK from there)
 */
static void lr_back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (s_back_cb) {
        s_back_cb();
    }
}

/* ======================== Animation starters ======================== */

/**
 * @brief Start a fade-in animation (opacity 0 -> 255) on an object
 * @param obj   target object
 * @param delay start delay (ms), used to stagger multiple objects
 */
static void lr_fade_in(lv_obj_t *obj, uint32_t delay)
{
    if (obj == NULL) return;

    /* initial state: fully transparent */
    lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, LR_ANIM_FADE_MS);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/**
 * @brief Start a width animation on an object
 * @param obj   target object
 * @param from  start width (px)
 * @param to    end width (px)
 * @param delay start delay (ms)
 */
static void lr_anim_width(lv_obj_t *obj, lv_coord_t from, lv_coord_t to, uint32_t delay)
{
    if (obj == NULL) return;

    /* Remove any existing animation on this object */
    lv_anim_del(obj, NULL);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, anim_w_cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, LR_ANIM_BAR_MS);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/* ======================== Style init (one-shot) ======================== */
static void lr_style_init(void)
{
    /* Screen: vertical gradient warm gray */
    lv_style_init(&st_screen);
    lv_style_set_bg_color(&st_screen, LR_BG_TOP);
    lv_style_set_bg_grad_color(&st_screen, LR_BG_BOTTOM);
    lv_style_set_bg_grad_dir(&st_screen, LV_GRAD_DIR_VER);
    lv_style_set_bg_grad_stop(&st_screen, 255);
    lv_style_set_pad_all(&st_screen, 0);
    lv_style_set_border_width(&st_screen, 0);

    /* Title: dark warm gray + 17pt serif + letter-spacing */
    lv_style_init(&st_title);
    lv_style_set_text_color(&st_title, LR_TEXT_PRIMARY);
    lv_style_set_text_font(&st_title, FONT_TITLE);
    lv_style_set_text_letter_space(&st_title, 2);

    /* Subtitle: dark warm gray + 12pt serif */
    lv_style_init(&st_subtitle);
    lv_style_set_text_color(&st_subtitle, LR_TEXT_SECONDARY);
    lv_style_set_text_font(&st_subtitle, FONT_BODY);
    lv_style_set_text_letter_space(&st_subtitle, 1);

    /* Back button: transparent bg + dark gold text + no shadow, wabi-sabi minimal */
    lv_style_init(&st_back_btn);
    lv_style_set_bg_opa(&st_back_btn, LV_OPA_TRANSP);
    lv_style_set_border_width(&st_back_btn, 0);
    lv_style_set_shadow_width(&st_back_btn, 0);
    lv_style_set_radius(&st_back_btn, 0);
    lv_style_set_pad_all(&st_back_btn, 0);
    lv_style_set_text_color(&st_back_btn, LR_ACCENT_GOLD);
    lv_style_set_text_font(&st_back_btn, FONT_BODY);
    lv_style_set_text_letter_space(&st_back_btn, 1);

    /* Card: pure white + 20 radius + warm gray soft shadow (lifted off bg) */
    lv_style_init(&st_card);
    lv_style_set_bg_color(&st_card, LR_CARD_BG);
    lv_style_set_radius(&st_card, 20);
    lv_style_set_shadow_color(&st_card, LR_SHADOW);
    lv_style_set_shadow_width(&st_card, 14);
    lv_style_set_shadow_opa(&st_card, LV_OPA_40);
    lv_style_set_shadow_ofs_y(&st_card, 3);
    lv_style_set_shadow_spread(&st_card, 0);
    lv_style_set_border_width(&st_card, 0);
    lv_style_set_pad_all(&st_card, 0);

    /* Card label: top-left, dark warm gray small text */
    lv_style_init(&st_card_lbl);
    lv_style_set_text_color(&st_card_lbl, LR_TEXT_SECONDARY);
    lv_style_set_text_font(&st_card_lbl, FONT_BODY);

    /* Card value: bottom-left, dark sandalwood numeric, large */
    lv_style_init(&st_card_val);
    lv_style_set_text_color(&st_card_val, LR_DATA_HILITE);
    lv_style_set_text_font(&st_card_val, FONT_NUM);
    lv_style_set_text_letter_space(&st_card_val, 1);

    /* Card sub-label: top-right, medium dark warm gray, small (used for "of mm:ss") */
    lv_style_init(&st_card_sub);
    lv_style_set_text_color(&st_card_sub, LR_TEXT_MUTED);
    lv_style_set_text_font(&st_card_sub, FONT_NUM_S);
    lv_style_set_text_letter_space(&st_card_sub, 0);

    /* Section label: small dark warm gray (used for the mode bar label) */
    lv_style_init(&st_section_lbl);
    lv_style_set_text_color(&st_section_lbl, LR_TEXT_SECONDARY);
    lv_style_set_text_font(&st_section_lbl, FONT_BODY);

    /* Mode bar segment: no border, no radius, no padding, no shadow */
    lv_style_init(&st_bar_seg);
    lv_style_set_bg_opa(&st_bar_seg, LV_OPA_COVER);
    lv_style_set_border_width(&st_bar_seg, 0);
    lv_style_set_radius(&st_bar_seg, 0);
    lv_style_set_pad_all(&st_bar_seg, 0);
    lv_style_set_shadow_width(&st_bar_seg, 0);

    /* Mode bar track: full width, visible warm gray, underneath the segments */
    lv_style_init(&st_bar_track);
    lv_style_set_bg_color(&st_bar_track, LR_TRACK);
    lv_style_set_border_width(&st_bar_track, 0);
    lv_style_set_radius(&st_bar_track, 0);
    lv_style_set_pad_all(&st_bar_track, 0);

    /* End-mode banner: warm cream background, dark text */
    lv_style_init(&st_end_banner);
    lv_style_set_bg_color(&st_end_banner, LR_BANNER_BG);
    lv_style_set_border_width(&st_end_banner, 0);
    lv_style_set_radius(&st_end_banner, 8);
    lv_style_set_pad_all(&st_end_banner, 0);
    lv_style_set_shadow_width(&st_end_banner, 0);

    /* End-mode banner text: dark warm gray, 12pt */
    lv_style_init(&st_end_text);
    lv_style_set_text_color(&st_end_text, LR_TEXT_PRIMARY);
    lv_style_set_text_font(&st_end_text, FONT_BODY);
    lv_style_set_text_letter_space(&st_end_text, 1);
}

/* ======================== Sub-builders ======================== */

/**
 * @brief Create the top title row
 *        Layout: "Learning Report" (y=8) + date subtitle (y=30), left aligned
 *                Top-right: "< Back" text button
 */
static void lr_create_title(void)
{
    lv_obj_t *t = lv_label_create(lr_screen);
    lv_label_set_text(t, "Learning Report");
    lv_obj_add_style(t, &st_title, 0);
    lv_obj_set_pos(t, LR_SIDE_PAD, LR_TITLE_Y);

    lv_obj_t *s = lv_label_create(lr_screen);
    lv_label_set_text(s, "06.25  Wed  -  Sunny");
    lv_obj_add_style(s, &st_subtitle, 0);
    lv_obj_set_pos(s, LR_SIDE_PAD, LR_SUBTITLE_Y);

    /* Back button (top-right, wabi-sabi minimal text button) */
    lr_btn_back = lv_btn_create(lr_screen);
    lv_obj_set_size(lr_btn_back, 50, 24);
    lv_obj_set_pos(lr_btn_back, LR_SCR_W - LR_SIDE_PAD - 50, LR_TITLE_Y);
    lv_obj_add_style(lr_btn_back, &st_back_btn, 0);
    lv_obj_t *back_lbl = lv_label_create(lr_btn_back);
    lv_label_set_text(back_lbl, "< Back");
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(lr_btn_back, lr_back_btn_event_cb, LV_EVENT_CLICKED, NULL);
}

/**
 * @brief Create one stat card with a top-right sub-label
 * @param x,y     top-left of the card
 * @param lbl     card label (top-left, dark warm gray)
 * @param val     card value (bottom-left, dark sandalwood)
 * @param sub     card sub-label (top-right, medium dark warm gray, small) or NULL
 * @param out_val [out] pointer to the value label, used by Update()
 * @param out_sub [out] pointer to the sub label, used by Update() (can be NULL)
 * @return card object pointer
 */
static lv_obj_t *lr_create_card_with_sub(int16_t x, int16_t y,
                                         const char *lbl, const char *val,
                                         const char *sub,
                                         lv_obj_t **out_val,
                                         lv_obj_t **out_sub)
{
    lv_obj_t *c = lv_obj_create(lr_screen);
    lv_obj_set_size(c, LR_CARD_W, LR_CARD_H);
    lv_obj_set_pos(c, x, y);
    lv_obj_add_style(c, &st_card, 0);

    /* Label: top-left */
    lv_obj_t *l = lv_label_create(c);
    lv_label_set_text(l, lbl);
    lv_obj_add_style(l, &st_card_lbl, 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 8, 6);

    /* Sub-label: top-right (only if provided) */
    if (sub) {
        lv_obj_t *sl = lv_label_create(c);
        lv_label_set_text(sl, sub);
        lv_obj_add_style(sl, &st_card_sub, 0);
        lv_obj_align(sl, LV_ALIGN_TOP_RIGHT, -6, 8);
        if (out_sub) *out_sub = sl;
    } else if (out_sub) {
        *out_sub = NULL;
    }

    /* Value: bottom-left */
    lv_obj_t *v = lv_label_create(c);
    lv_label_set_text(v, val);
    lv_obj_add_style(v, &st_card_val, 0);
    lv_obj_align(v, LV_ALIGN_BOTTOM_LEFT, 8, -6);
    if (out_val) *out_val = v;

    return c;
}

/**
 * @brief Create one stat card (no sub-label)
 */
static lv_obj_t *lr_create_card(int16_t x, int16_t y,
                                const char *lbl, const char *val,
                                lv_obj_t **out_val)
{
    return lr_create_card_with_sub(x, y, lbl, val, NULL, out_val, NULL);
}

/**
 * @brief Create the hero row: 3 cards (Duration, Avg Score, Focus)
 *        Duration has a top-right "of mm:ss" sub-label that shows target
 *        Avg Score / Focus show plain values
 */
static void lr_create_hero(void)
{
    int16_t x = LR_SIDE_PAD;

    /* Card 1: Duration (with target sub-label) */
    lr_card_dur = lr_create_card_with_sub(x, LR_HERO_Y,
                                          "Duration", "00:00", NULL,
                                          &lr_lbl_dur_val, &lr_lbl_dur_sub);
    x += LR_CARD_W + LR_CARD_GAP;

    /* Card 2: Avg Score */
    lr_card_score = lr_create_card(x, LR_HERO_Y,
                                   "Avg Score", "0", &lr_lbl_score);
    x += LR_CARD_W + LR_CARD_GAP;

    /* Card 3: Focus */
    lr_card_focus = lr_create_card(x, LR_HERO_Y,
                                   "Focus", "0%", &lr_lbl_focus);

    /* Staggered fade-in: 0 / 80 / 160 ms */
    lr_fade_in(lr_card_dur,   0);
    lr_fade_in(lr_card_score, 80);
    lr_fade_in(lr_card_focus, 160);
}

/**
 * @brief Create the mode distribution bar (Focus / Distract / Fatigue)
 *        Layout:
 *          y=112  full-width track (14px), segments grow on top of it
 *          y=130  combined percentage label "Focus 75%  Distract 15%  Fatigue 10%"
 *        Bar widths are set to 0 in the builder; LearningReport_Update()
 *        animates them to the real values.
 */
static void lr_create_mode_bar(void)
{
    const lv_coord_t bar_w = LR_SCR_W - LR_SIDE_PAD * 2;  /* 288 */

    /* Combined mode label (initially "0% 0% 0%"; updated by LearningReport_Update) */
    lr_lbl_modes = lv_label_create(lr_screen);
    lv_label_set_text(lr_lbl_modes, "Focus 0%  Distract 0%  Fatigue 0%");
    lv_obj_add_style(lr_lbl_modes, &st_section_lbl, 0);
    lv_obj_set_pos(lr_lbl_modes, LR_SIDE_PAD, LR_BAR_LBL_Y);

    /* Track (under segments) */
    lr_bar_track = lv_obj_create(lr_screen);
    lv_obj_set_size(lr_bar_track, bar_w, LR_BAR_H);
    lv_obj_set_pos(lr_bar_track, LR_SIDE_PAD, LR_BAR_Y);
    lv_obj_add_style(lr_bar_track, &st_bar_track, 0);

    /* Focus segment (dark gold) - starts at track left */
    lr_bar_focus = lv_obj_create(lr_screen);
    lv_obj_set_size(lr_bar_focus, 0, LR_BAR_H);
    lv_obj_set_pos(lr_bar_focus, LR_SIDE_PAD, LR_BAR_Y);
    lv_obj_add_style(lr_bar_focus, &st_bar_seg, 0);
    lv_obj_set_style_bg_color(lr_bar_focus, LR_MODE_FOCUS, 0);

    /* Distract segment - to the right of focus */
    lr_bar_dist = lv_obj_create(lr_screen);
    lv_obj_set_size(lr_bar_dist, 0, LR_BAR_H);
    lv_obj_set_pos(lr_bar_dist, LR_SIDE_PAD, LR_BAR_Y);
    lv_obj_add_style(lr_bar_dist, &st_bar_seg, 0);
    lv_obj_set_style_bg_color(lr_bar_dist, LR_MODE_DISTRACT, 0);

    /* Fatigue segment - to the right of distract */
    lr_bar_fat = lv_obj_create(lr_screen);
    lv_obj_set_size(lr_bar_fat, 0, LR_BAR_H);
    lv_obj_set_pos(lr_bar_fat, LR_SIDE_PAD, LR_BAR_Y);
    lv_obj_add_style(lr_bar_fat, &st_bar_seg, 0);
    lv_obj_set_style_bg_color(lr_bar_fat, LR_MODE_FATIGUE, 0);

    /* Demo: animate default bar at init time (overridden by Update on ST_SUMMARY) */
    const lv_coord_t def_focus_w = bar_w * LR_DEFAULT_FOCUS / 100;
    const lv_coord_t def_dist_w  = bar_w * LR_DEFAULT_DISTRACT / 100;
    const lv_coord_t def_fat_w   = bar_w * LR_DEFAULT_FATIGUE / 100;

    lr_anim_width(lr_bar_focus, 0, def_focus_w, 250);
    lr_anim_width(lr_bar_dist,  0, def_dist_w,  250);
    lr_anim_width(lr_bar_fat,   0, def_fat_w,   250);
}

/**
 * @brief Create the end-mode banner (full-width subtle bar with centered text)
 *        Text is set by LearningReport_Update().
 */
static void lr_create_end_banner(void)
{
    lr_end_banner = lv_obj_create(lr_screen);
    lv_obj_set_size(lr_end_banner, LR_SCR_W - LR_SIDE_PAD * 2, LR_END_BANNER_H);
    lv_obj_set_pos(lr_end_banner, LR_SIDE_PAD, LR_END_BANNER_Y);
    lv_obj_add_style(lr_end_banner, &st_end_banner, 0);

    lr_lbl_end = lv_label_create(lr_end_banner);
    lv_label_set_text(lr_lbl_end, "End: --");
    lv_obj_add_style(lr_lbl_end, &st_end_text, 0);
    lv_obj_center(lr_lbl_end);

    lr_fade_in(lr_end_banner, 280);
}

/* ======================== Public API ======================== */

/**
 * @brief Build the learning-report screen
 * @note  Creates a detached lv_obj screen, NOT the active screen.
 *        DisplayTask can lv_scr_load() to it later.
 */
void LearningReport_Init(void)
{
    if (lr_screen != NULL) {
        return;   /* idempotent */
    }

    /* 1. Initialize all static styles (runs once) */
    lr_style_init();

    /* 2. Create the screen object (container, not active) */
    lr_screen = lv_obj_create(NULL);
    lv_obj_set_size(lr_screen, LR_SCR_W, LR_SCR_H);
    lv_obj_add_style(lr_screen, &st_screen, 0);
    lv_obj_clear_flag(lr_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 3. Build top-down: title -> hero -> mode bar -> end banner */
    lr_create_title();
    lr_create_hero();
    lr_create_mode_bar();
    lr_create_end_banner();
}

/**
 * @brief Get the screen object
 */
lv_obj_t *LearningReport_GetScreen(void)
{
    return lr_screen;
}

/**
 * @brief Re-trigger the card fade-in animation
 * @note  The mode bar segment widths are animated by LearningReport_Update()
 *        using real percentages, so this function only handles card fade-in
 *        to avoid overriding the Update's animation target.
 */
void LearningReport_RefreshAnim(void)
{
    if (lr_screen == NULL) return;

    /* Hero cards */
    lr_fade_in(lr_card_dur,   0);
    lr_fade_in(lr_card_score, 80);
    lr_fade_in(lr_card_focus, 160);

    /* End-mode banner */
    lr_fade_in(lr_end_banner, 280);
}

/* ======================== Helpers ======================== */

/**
 * @brief Format seconds -> "mm:ss" (or "h:mm:ss" if >= 1 hour)
 * @param sec  input seconds
 * @param buf  output buffer
 * @param sz   buffer size
 */
static void lr_fmt_duration(uint32_t sec, char *buf, size_t sz)
{
    uint32_t mm = sec / 60;
    uint32_t ss = sec % 60;
    if (mm >= 60) {
        snprintf(buf, sz, "%lu:%02lu:%02lu",
                 (unsigned long)(mm / 60),
                 (unsigned long)(mm % 60),
                 (unsigned long)ss);
    } else {
        snprintf(buf, sz, "%lu:%02lu",
                 (unsigned long)mm, (unsigned long)ss);
    }
}

/* ======================== Data Update ======================== */

/**
 * @brief Push real session data into the screen
 * @param data session data pointer (see LRSessionData_t)
 * @note  Updates all displayed data fields and animates the mode bar to the
 *        new focus/distract/fatigue ratio.  Safe to call on a NULL pointer
 *        (it will display zeros without crashing).
 */
void LearningReport_Update(const LRSessionData_t *data)
{
    if (lr_screen == NULL) return;

    /* Default to all zeros if data is NULL */
    uint32_t total_s   = data ? data->total_seconds   : 0;
    uint16_t target_s  = data ? data->target_seconds  : 0;
    uint8_t  avg_score = data ? data->avg_total_score : 0;
    uint8_t  focus     = data ? data->focus_pct       : 0;
    uint8_t  distract  = data ? data->distract_pct    : 0;
    uint8_t  fatigue   = data ? data->fatigue_pct     : 0;
    uint8_t  auto_end  = data ? data->auto_ended      : 0;

    char buf[24];

    /* Hero card 1: Duration (actual mm:ss + "/ target" if target set) */
    if (lr_lbl_dur_val) {
        lr_fmt_duration(total_s, buf, sizeof(buf));
        lv_label_set_text(lr_lbl_dur_val, buf);
    }
    if (lr_lbl_dur_sub) {
        if (target_s > 0) {
            char tmp[16];
            lr_fmt_duration(target_s, tmp, sizeof(tmp));
            snprintf(buf, sizeof(buf), "of %s", tmp);
            lv_label_set_text(lr_lbl_dur_sub, buf);
        } else {
            lv_label_set_text(lr_lbl_dur_sub, "");
        }
    }

    /* Hero card 2: Avg Score */
    if (lr_lbl_score) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)avg_score);
        lv_label_set_text(lr_lbl_score, buf);
    }

    /* Hero card 3: Focus % */
    if (lr_lbl_focus) {
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)focus);
        lv_label_set_text(lr_lbl_focus, buf);
    }

    /* Mode distribution bar segments (widths proportional to percentages) */
    const lv_coord_t bar_w   = LR_SCR_W - LR_SIDE_PAD * 2;
    const lv_coord_t seg_f_w = bar_w * focus    / 100;
    const lv_coord_t seg_d_w = bar_w * distract / 100;
    const lv_coord_t seg_a_w = bar_w * fatigue  / 100;

    /* Position distract right after focus, fatigue right after distract */
    if (lr_bar_dist) lv_obj_set_x(lr_bar_dist, LR_SIDE_PAD + seg_f_w);
    if (lr_bar_fat)  lv_obj_set_x(lr_bar_fat,  LR_SIDE_PAD + seg_f_w + seg_d_w);

    /* Animate widths from 0 -> target */
    lr_anim_width(lr_bar_focus, 0, seg_f_w, 200);
    lr_anim_width(lr_bar_dist,  0, seg_d_w, 200);
    lr_anim_width(lr_bar_fat,   0, seg_a_w, 200);

    /* Combined mode label (under the bar): "Focus 75%  Distract 15%  Fatigue 10%" */
    if (lr_lbl_modes) {
        lv_label_set_text_fmt(lr_lbl_modes, "Focus %u%%  Distract %u%%  Fatigue %u%%",
                              (unsigned)focus, (unsigned)distract, (unsigned)fatigue);
    }

    /* End mode banner text */
    if (lr_lbl_end) {
        const char *end_str = "End: Manual";
        switch (auto_end) {
        case 1:  end_str = "End: Auto (5min away)"; break;
        case 2:  end_str = "End: Time Complete!";   break;
        default: end_str = "End: Manual";            break;
        }
        lv_label_set_text(lr_lbl_end, end_str);
    }
}

/**
 * @brief Register the "Back" button callback
 * @note  DisplayTask calls this with a function that posts STATE_EVT_GUI_BACK.
 */
void LearningReport_SetBackCallback(LearningReportBackCb_t cb)
{
    s_back_cb = cb;
}
