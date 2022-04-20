/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2018 Blender Foundation. All rights reserved. */

/** \file
 * \ingroup bke
 */

#pragma once

#include "BLI_compiler_compat.h"
#include "BLI_sys_types.h"
#include "DNA_mesh_types.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "BLI_listbase.h"
#ifdef __cplusplus
extern "C" {
#endif

struct BMesh;
struct Mesh;
struct BPoint;
struct BMVert;
struct BMFace;
struct BMLoop;
struct BMEdge;
struct Nurb;
struct Curve;
struct Depsgraph;
struct Main;
struct Mesh;
struct Object;
struct Scene;

typedef enum MASK_SELECTOR {
  MASK9X9 = 1,
  MASK_bi2_bb2b = 2,
  MASK_bi3_bb2b = 3,
  MASK_bi4_bb2b = 4
} MASK_SELECTOR;

typedef struct BsplinePatch {
	int deg_u;
	int deg_v;
	float** bspline_coefs;
} BsplinePatch;

//extern?
//extern float bi2_bb2b[3][3];
//extern float bi3_bb2b[4][4];
//extern float bi4_bb2b[5][5];
///*

//*/

//halfEdge
struct BMVert* halfEdgeGetVerts(struct BMLoop* halfEdge, uint8_t commands[]);
struct BMVert halfEdge_get_single_vert(struct BMLoop* halfEdge, uint8_t commands[]);
struct BMVert* halfEdge_get_verts_repeatN_times(struct BMLoop* halfEdge, uint8_t commands[], uint8_t repeat_times, uint16_t* get_vert_order, uint32_t num_verts_reserved);

//Helper
struct BPoint Helper_add_a_dimesion_to_vector(struct BMVert vec, float weighting);
struct BPoint Helper_convert_3d_vector_to_4d_coord(struct BMVert vec, float weighting);
struct BPoint* Helper_convert_3d_vectors_to_4d_coords(struct BMVert* vecs, float weighting, uint16_t size);
int* Helper_get_verts_id(struct BMVert* verts, uint16_t size);
float** Helper_convert_verts_from_list_to_matrix(struct BMVert* verts, uint16_t size);
float** Helper_split_list(float* list, uint16_t size, uint16_t numb_of_pieces);
float* Helper_convert_verts_from_matrix_to_list(float** mat, uint16_t size);
float** Helper_normalize_each_row(enum MASK_SELECTOR mask, uint16_t x, uint16_t y);
float** Helper_apply_mask_on_neighbor_verts(enum MASK_SELECTOR mask, struct BMVert* nbverts, uint16_t row, uint16_t col);
int edges_number_of_face(struct BMFace face);	//int **arr == int arr[][]	//func(int n, int m, arr[m][n])
bool is_pentagon(struct BMFace face);
bool is_quad(struct BMFace face);
bool is_triangle(struct BMFace face);
bool is_hexagon(struct BMFace face);
struct BMVert* Helper_reorder_list(struct BMVert* unordered_verts, uint16_t* get_vert_order, uint32_t num_verts_reserved);
struct BMFace* Helper_init_neighbor_faces(struct BMFace* face);
bool Helper_are_faces_all_quad(struct BMFace* faces, uint16_t count);

//RegPatchConstructor
bool RegPatchConstructor_is_same_type(struct BMVert* vert);
struct BMVert* RegPatchConstructor_get_neighbor_verts(struct BMVert* vert);
float** RegPatchConstructor_get_patch(struct BMVert* vert);

//BezierBsplineConverter
float** BezierBsplineConverter_bezier_to_bspline(float** bezier_coefs_mat, int deg_u, int deg_v, int total_bezier_coefs);
float** BezierBsplineConverter_bb2b_mask_selector(int deg);

//additional
float** getMask(enum MASK_SELECTOR type);
struct BMVert* rando_giv(float* verts);

#ifdef __cplusplus
}
#endif


