/* =====================
 * include/miur/gltf.h
 * 03/06/2022
 * glTF file parser.
 * ====================
 */

#ifndef MIUR_GLTF_H
#define MIUR_GLTF_H

#include <miur/model.h>
#include <miur/membuf.h>

bool gltf_parse(StaticModel *out, const char *filename);

#endif
