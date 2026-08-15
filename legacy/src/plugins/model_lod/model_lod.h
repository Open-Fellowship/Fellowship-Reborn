/* model_lod.h: pin models to their finest level of detail.
 *
 * A preference, not a bug fix, and it costs frame rate. It is here because the engine's LOD
 * thresholds were chosen for 1024x768 on 2002 hardware, so at 4K a model swaps to its coarse
 * form while it is still large enough on screen for the change to be obvious.
 */
#ifndef MODEL_LOD_H
#define MODEL_LOD_H

void model_lod_install(void);

#endif /* MODEL_LOD_H */
