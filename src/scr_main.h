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
lv_obj_t *scr_main_room(void);      /* room_layer - mess sprites live here */
void      scr_main_hud_refresh(void);
void      scr_main_egg_refresh(void);   /* egg UI + hatch countdown */

/* START, as one implementation. Resolves both Surprise choices, persists
 * them immediately with a forced save, and arms the hatch timer `secs` from
 * now. The START button passes EGG_HATCH_SEC; the console test command
 * passes 10.
 *
 * It is shared BECAUSE the console used to set egg_hatch_ts directly, which
 * skipped the resolution entirely - so the one command anyone would reach
 * for to test the hatch was exercising a path the product does not have, and
 * produced an egg whose identity had never been resolved. A test that drives
 * a different code path from the button proves nothing about the button. */
void      scr_main_egg_start(uint32_t secs);

/* Virtual room darkness. Dims the pet scene ONLY - never the menus, and
 * never the sleeping Zs, which are drawn above it. */
void      scr_main_set_room_dark(bool dark);
void      scr_main_sleep_fx(void);      /* floating Zs while asleep */

#ifdef __cplusplus
}
#endif
