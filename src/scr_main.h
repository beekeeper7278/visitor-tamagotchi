#pragma once
/* ===========================================================================
 * scr_main - the resident pet screen  [SPEC section 9]
 *
 * Created once at boot and never deleted: the main screen is entered and
 * left constantly, so rebuilding it per visit would churn the LVGL heap.
 * Menu pages and games are the heavy, rare screens and get built on demand
 * from Phase 3 onward.
 *
 * Object tree (section 9). Phase 2 builds the marked rows; hud_top and
 * btn_menu are named here but deliberately NOT created - they belong to the
 * Phase 3 menu and creating them now would mean guessing their contents.
 *
 *   scr_main
 *   |- bg              gradient                        [PHASE 2]
 *   |- room_layer      lights-off dim + mess pool      [PHASE 4/5 - empty]
 *   |- pet_layer       the pet primitives              [PHASE 2]
 *   |- pet_overlay     PNG accessories + effects       [PHASE 2 - empty seam]
 *   |- bubble          container + label               [PHASE 2]
 *   |- hud_top         mood icon, battery, clock       [PHASE 3]
 *   \- btn_menu        bottom-centre handle            [PHASE 3]
 * ======================================================================== */

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void      scr_main_create(void);
void      scr_main_show(void);
lv_obj_t *scr_main_obj(void);
lv_obj_t *scr_main_overlay(void);   /* the empty PNG seam */

#ifdef __cplusplus
}
#endif
