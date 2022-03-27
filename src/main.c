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

int main(int argc, char *argv[])
{
  Renderer *render;
  struct cwin_window *window;
  struct cwin_event event;
  bool running = true;
  StaticModel cube;
  enum cwin_error err;
  MIUR_LOG_INFO("Running MIUR");

  err = cwin_init();
  if (err)
  {
    MIUR_LOG_INFO("CWIN Error: %d", err);
    return EXIT_FAILURE;
  }

  struct cwin_window_builder window_builder = {
    .name = "Miur Test",
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
    renderer_draw(render);
  }

  renderer_deinit_static_mesh(render, &cube.meshes[0]);

  renderer_destroy(render);
  MIUR_LOG_INFO("Exiting successfully");
  return EXIT_SUCCESS;
}

