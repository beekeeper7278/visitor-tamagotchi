#pragma once
/* ===========================================================================
 * assets.h - PNG image declarations  [SPEC section 8]
 *
 * DELIBERATELY EMPTY IN PHASE 2.
 *
 * The hybrid renderer draws the pet's body and face from LVGL primitives and
 * reserves PNG for what primitives cannot express: ears, tails, accessories,
 * food, effects and icons. The Baby form has ear_style NONE and tail_style
 * NONE, so it needs no art at all - and shipping placeholder art that nobody
 * intends to keep is how placeholder art ends up in the final product.
 *
 * The seam is wired instead: scr_main_overlay() is the parent, this header
 * is the declaration point, and tools/convert_assets.py is the generator.
 * Adding the first real asset is a manifest entry plus one lv_img_create().
 *
 * WORKFLOW (section 8):
 *   1. Author RGBA8888 PNGs at FINAL pixel size in assets/src/ - no runtime
 *      scaling, which on this panel would cost both quality and bandwidth.
 *   2. tools/convert_assets.py reads assets/manifest.json and emits C arrays
 *      as LV_IMG_CF_TRUE_COLOR_ALPHA with LV_COLOR_16_SWAP=0 to match
 *      lv_conf.h.
 *   3. Generated files land in src/assets/img_<name>.c and are declared here.
 *
 * Budget: ~40 icons at 48x48 plus 3 food sprites at 96x96 is roughly 350 KB
 * of 16 MB flash. A non-issue; INDEXED_8BIT would cut it 4x if it ever were.
 * ======================================================================== */

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* No assets yet. Generated declarations will appear below this line, e.g.
 *   LV_IMG_DECLARE(img_ear_floppy);
 *   LV_IMG_DECLARE(img_sparkle);
 */

#ifdef __cplusplus
}
#endif
