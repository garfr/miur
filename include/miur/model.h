/* =====================
 * include/miur/model.h
 * 03/06/2022
 * 3D Model representation.
 * ====================
 */

#ifndef MIUR_MODEL_H
#define MIUR_MODEL_H

#include <vulkan/vulkan.h>

#include <stdint.h>

#include <miur/material.h>

typedef struct
{
  float *verts_pos;    /* (x,y,z). 3 per vert_count. */
  float *verts_norm;   /* (x,y,z). 3 per vert_count. */
  float *verts_uv;     /* (x,y). 2 per vert_count. */
  uint32_t vert_count; /* Number of vertices in the mesh. */

  uint16_t *indices;
  uint32_t index_count;

  VkBuffer vert_bufs[3];
  VkBuffer index_buf;
  VkDeviceMemory norm_memory;
  VkDeviceMemory pos_memory;
  VkDeviceMemory index_memory;

  Material *material;
} StaticMesh;

typedef struct
{
  StaticMesh *meshes;
  uint32_t mesh_count;
} StaticModel;

#endif
