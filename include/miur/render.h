/* =====================
 * include/miur/render.h
 * 03/05/2022
 * Rendering interface.
 * ====================
 */

#ifndef MIUR_RENDER_H
#define MIUR_RENDER_H

#include <stdint.h>

#include <miur/model.h>
#include <cwin.h>

typedef struct Renderer Renderer;

typedef struct
{
  struct cwin_window *window;
  const char *name;
  uint32_t version;
  const char *technique_filename;
  const char *effect_filename;
} RendererBuilder;

typedef struct
{
  uint32_t width, height;
} RendererConfigure;

typedef uint64_t GPUTechnique;

Renderer *renderer_create(RendererBuilder *builder);
void renderer_destroy(Renderer *render);
bool renderer_draw(Renderer *render);
void renderer_configure(Renderer *render, RendererConfigure *cofigure);

bool renderer_init_static_mesh(Renderer *render, StaticMesh *mesh);
void renderer_deinit_static_mesh(Renderer *render, StaticMesh *mesh);

#endif
