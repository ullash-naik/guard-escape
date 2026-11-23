#ifndef GE_RETARGET_H
#define GE_RETARGET_H

#include "raylib.h"

/* Builds a new animation for `target` from `src`, an animation made for
 * `source`. Bones are matched by name. Free it with UnloadModelAnimation. */
/* Puts Mixamo animation frames into the same space as the rest pose.
 * refIndex = a clip whose first frame is the rest pose (the T-pose clip). */
void anim_fix_space(const Model *m, ModelAnimation *anims, int count, int refIndex);

/* Like anim_fix_space but needs no T-pose clip (works out scale from bone
 * lengths and orientation from the body's up/left directions). */
void anim_fix_space_auto(const Model *m, ModelAnimation *anims, int count);

/* Renumbers each mesh's bone ids using a .skinmap file written by
 * tools/unify_skins.ps1 (lines: meshIndex count id0 id1 ...). Returns the
 * number of meshes changed, 0 if the file does not exist. */
int model_apply_skinmap(Model *m, const char *path);

/* Sets every rest-pose bone scale to 1. Call AFTER anim_fix_space. */
void model_normalize_bind_scale(Model *m);

/* Rotates/rescales the mesh so it lines up with its own skeleton (in place). */
void mesh_align_to_skeleton(Model *m);

ModelAnimation retarget_animation(const Model *target, const Model *source, const ModelAnimation *src);

#endif
