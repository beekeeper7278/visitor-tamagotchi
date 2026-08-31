#pragma once
/* Host stub: the evolution survey never draws. */
typedef struct _lv_obj_t lv_obj_t;
typedef struct { int dummy; } lv_color_t;
typedef int lv_coord_t;
typedef int lv_align_t;
typedef int lv_anim_t;
typedef int lv_timer_t;
static inline lv_color_t lv_color_hex(unsigned int c){ lv_color_t r; r.dummy=(int)c; return r; }
