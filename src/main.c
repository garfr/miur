/* =====================
 * src/main.c
 * 03/05/2022
 * Entry point for game.
 * ====================
 */

#include <stdlib.h>
#include <stdbool.h>

#include <cwin.h>

#include <miur/gltf.h>
#include <miur/log.h>
#include <miur/render.h>
#include <miur/material.h>
#include <miur/shader.h>
#include <miur/render_graph.h>

#define INIT_SCREEN_WIDTH  960
#define INIT_SCREEN_HEIGHT 720

/* === PROTOTYPES === */

static void *libc_alloc(void *ptr, size_t osz, size_t nsz, void *ud);

/* === PUBLIC FUNCTIONS === */

int main(int argc, char *argv[])
{
  Renderer *render;
  struct cwin_window *window;
  struct cwin_event event;
  bool running = true;
  StaticModel cube;
  enum cwin_error err;

  err = cwin_init();
  if (err)
  {
    MIUR_LOG_INFO("CWIN Error: %d", err);
    return EXIT_FAILURE;
  }

  struct cwin_window_builder window_builder = {
    .name = "Miur Test",
    .height = 300,
    .width = 300,
  };

  err = cwin_create_window(&window, &window_builder);
  if (err)
  {
    MIUR_LOG_INFO("Failed to create window: %d", err);
    return EXIT_FAILURE;
  }

  RendererBuilder renderer_builder = {
    .window = window,
    .name = "Miur Test",
    .version = 1,
    .technique_filename = "../assets/technique.json",
    .effect_filename = "../assets/effect.json",
  };

  render = renderer_create(&renderer_builder);
  if (render == NULL)
  {
    MIUR_LOG_ERR("Failed to create MIUR renderer");
    return EXIT_FAILURE;
  }

  if (!gltf_parse(&cube, "../assets/cube.gltf"))
  {
    MIUR_LOG_ERR("Failed to parser cube.gltf");
    return EXIT_FAILURE;
  }

  if (!renderer_init_static_mesh(render, &cube.meshes[0]))
  {
    return true;
  }

  while (running)
  {
    while (cwin_poll_event(NULL, &event))
    {
      if (event.t == CWIN_EVENT_WINDOW &&
          event.window.t == CWIN_WINDOW_EVENT_CLOSE)
      {
        running = false;
      }
    }
    if (!renderer_draw(render))
    {
      MIUR_LOG_ERR("Failed to draw frame");
      running = false;
    }
  }

  cwin_destroy_window(window);

  renderer_deinit_static_mesh(render, &cube.meshes[0]);

  renderer_destroy(render);
  MIUR_LOG_INFO("Exiting successfully");
  return EXIT_SUCCESS;
}

/* === PRIVATE FUNCTIONS === */

static void *libc_alloc(void *ptr, size_t osz, size_t nsz, void *ud)
{
  if (ptr == NULL)
  {
    return calloc(1, nsz);
  } else if (nsz == 0)
  {
    free(ptr);
    return NULL;
  }
  void *res = realloc(ptr, nsz);
  return res;
}
