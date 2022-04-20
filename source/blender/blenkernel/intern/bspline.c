	/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2022 Blender Foundation. All rights reserved. */

/** \file
 * \ingroup bke
 */

#include "BKE_bspline.h"
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "MEM_guardedalloc.h"

#include "BLI_alloca.h"
#include "BLI_ghash.h"
#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"
#include "BLT_translation.h"

#include "DNA_defaults.h"

#include "DNA_customdata_types.h"
#include "DNA_key_types.h"
#include "DNA_material_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"

#include "BKE_mesh.h"
#include "BKE_editmesh.h"	//BMesh.
#include "BKE_modifier.h"
#include "BKE_action.h"
#include "BKE_anim_data.h"
#include "BKE_anim_visualization.h"
#include "BKE_armature.h"
#include "BKE_constraint.h"
#include "BKE_curve.h"
#include "BKE_idprop.h"
#include "BKE_idtype.h"
#include "BKE_lib_id.h"
#include "BKE_lib_query.h"
#include "BKE_main.h"
#include "BKE_object.h"
#include "BKE_scene.h"

#include "DEG_depsgraph_build.h"
#include "DEG_depsgraph_query.h"

#include "BIK_api.h"

#include "BLO_read_write.h"

#include "CLG_log.h"
#include "BLI_utildefines.h"

#include "BKE_modifier.h"
#include "DNA_mesh_types.h"
#include "DNA_curve_types.h"

//Notes:
//for mem alloc consider MEM_calloc_arrayN since we use arrs a lot.
//you can loop through vert of face with this: BM_ITER_ELEM (f, &iter, initial_vertex, BM_FACES_OF_VERT)
static float MASK[9][9] = {
	{ 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f },
	{ 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f },
	{ 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f },
	{ 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f },
	{ 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f },
	{ 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f }
};

static float bi2_bb2b[3][3] = {
	{2.0f, -1.0f, 0.0f},
	{0.0f, 1.0f, 0.0f},
	{0.0f, -1.0f, 2.0f}
};

static float bi3_bb2b[4][4] = {
	{6.0f, -7.0f, 2.0f, 0.0f},
	{0.0f, 2.0f, -1.0f, 0.0f},
	{0.0f, -1.0f, 2.0f, 0.0f},
	{0.0f, 2.0f, -7.0f, 6.0f}
};

static float bi4_bb2b[5][5] = {
	{24.0f, -46.0f, 29.0f, -6.0f, 0.0f},
	{0.0f, 6.0f, -7.0f, 2.0f, 0.0f},
	{0.0f, -2.0f, 5.0f, -2.0f, 0.0f},
	{0.0f, 2.0f, -7.0f, 6.0f, 0.0f},
	{0.0f, -6.0f, 29.0f, -46.0f, 24.0f}
};


//halfEdge functions
struct BMVert* halfEdgeGetVerts(struct BMLoop* halfEdge, uint8_t commands[])	//uint8_t commandCount - could be extra or find it since it is not dynamic
{
	uint8_t commandCount = sizeof(commands)/sizeof(commands[0]);
	uint8_t index = 0;
	uint8_t size = 0;
	for (int i = 0; i < commandCount; i++) {
		if (commands[i] == 4) {
			size++;
		}
	}
	BMVert* vertList = (BMVert *)MEM_calloc_arrayN(size, sizeof(BMVert), "part_vertList");
	//static BMvert vertList[size];	//can't do this need to dynamically allocate memory.
	BMLoop* loop = halfEdge;
	//uint8_t 
	for (int i = 0; i < commandCount; i++) {
		switch (commands[i]) {
		case 1:
			loop = loop->next;
			break;
		case 2:
			loop = loop->prev;
			break;
		case 3:
			loop = loop->radial_next;
			break;
		default:
			vertList[index] = *loop->v;
			index++;
			break;
		}
	}
	//halfEdge (BMLoop) pointer changes to current pointer. So inputted BMloop changes if commands are put in.
	halfEdge = loop;
	return vertList;
}

BMVert halfEdge_get_single_vert(BMLoop* halfEdge, uint8_t commands[])
{
	BMVert* vert = halfEdgeGetVerts(halfEdge, commands);
	//BMVert* vert_single = (BMVert *)MEM_callocN(sizeof(BMVert), "BMVert");
	BMVert vert_single = vert[0];
	MEM_freeN(vert);
	return vert_single;
}

BMVert* halfEdge_get_verts_repeatN_times(BMLoop* halfEdge, uint8_t commands[], uint8_t repeat_times, uint16_t* get_vert_order, uint32_t num_verts_reserved)
{
	uint8_t commandCount = sizeof(commands)/sizeof(commands[0]);	//this mult by repeat_times should equal to num_verts_reserved
	uint32_t index = 0;	//unsure how big index / size will get
	uint32_t size = 0;	//command count shouldn't be more than 20, I assume.
	for (int i = 0; i < commandCount; i++) {
		if (commands[i] == 4) {
			size++;
		}
	}
	int tot_size = size * repeat_times;	//while we know the size, and it is "known", not possible to make different sizes in a function has to be int literal.
	//BMvert unordered_verts[tot_size]; //can't, needs dynamic since unknown/dynamic size.
	BMVert* unordered_verts = (BMVert *)MEM_malloc_arrayN(tot_size, sizeof(BMVert), "verts_listed");
    for (int i = 0; i < repeat_times; i++) {
		BMVert* vert_list = halfEdgeGetVerts(halfEdge, commands);	//get list part
		for (int j = 0; j < size; j++) {	//extend(vert_list);
			unordered_verts[index] = vert_list[j];	//gets dereference, therefore no lost info post free.
			index++;
		}
		MEM_freeN(vert_list);	//free list part.
	}	//TODO: add new vert_list to the end of unordered verts. Then make reorder list and other helper functions in seperate file.
    return Helper_reorder_list(unordered_verts, get_vert_order, num_verts_reserved);
	//what is vert order array exactly? somewhat know but confirm point...
}		//also add the prev three functions in seperate file. in source/blender/blenkernel for .h, source/blender/blenkernel/intern for .c
//HelperReorderList does not exist yet. Also don't forget to dup mem in HelperReorderList and delete b4 returning.





//Helper functions
BPoint Helper_add_a_dimesion_to_vector(BMVert vec, float weighting)
{
	//BPoint* new_vec = (BPoint *)MEM_callocN(sizeof(BPoint), "Bpoint");
	BPoint new_vec;
	new_vec.vec[0] = vec.co[0];
	new_vec.vec[1] = vec.co[1];
	new_vec.vec[2] = vec.co[2];
	new_vec.vec[3] = weighting;	//i'd assume it will always be 1.0f or some const number for uniform weighting...
	return new_vec;	//think about freeing passed in vec...free prob in looped func. (nw about anymore)
}

BPoint Helper_convert_3d_vector_to_4d_coord(BMVert vec, float weighting)
{
	return Helper_add_a_dimesion_to_vector(vec, weighting);
}

BPoint* Helper_convert_3d_vectors_to_4d_coords(BMVert* vecs, float weighting, uint16_t size)	//needs size param or null val at end to indicate that
{
	BPoint* list = (BPoint *)MEM_malloc_arrayN(size, sizeof(BPoint), "BPoints");
	BPoint point;
	for (int i = 0; i < size; i++) {
		point = Helper_convert_3d_vector_to_4d_coord(vecs[i], weighting);
		list[i] = point;
		//unneeded due to making one at a time so we can use stack.
		//MEM_freeN(point);	//if I did this, it would work since it does get recongnized in memory due to list[i] getting dereferenced BPoint. (actually gets copied)
	}
	return list;	//also needs to be freed later.
}

int* Helper_get_verts_id(BMVert* verts, uint16_t size)	//once again need size / null val to indicate end
{
	int* ids = (int *)MEM_malloc_arrayN(size, sizeof(int), "vert_ids");
	for (int i = 0; i < size; i++) {
		ids[i] = verts[i].head.index;
	}
	return ids;	//needs to be freed later.
}

//different row size
float** Helper_convert_verts_from_list_to_matrix(BMVert* verts, uint16_t size)
{
	float** MATSx3 = (float **)MEM_malloc_arrayN(size, sizeof(float*), "sizeX3_mat");
	for (int i = 0; i < size; i++) {
		MATSx3[i] = (float *)MEM_malloc_arrayN(3, sizeof(float), __func__);
	}
	for (int i = 0; i < size; i++) {
		MATSx3[i][0] = verts[i].co[0];
		MATSx3[i][1] = verts[i].co[1];
		MATSx3[i][2] = verts[i].co[2];
	}
	MEM_freeN(verts);	//unsure if needed can remove whenever.
	return MATSx3;	//since this is double I assume we can just free it like MEM_freeN(MATSx3) due to one dimension known. (nope can't, c limited.)
}


float** Helper_split_list(float* list, uint16_t size, uint16_t numb_of_pieces)
{
	if (numb_of_pieces <= 0) {
		printf("Cannot split by zero\n");
		return NULL;
	}
	int num_of_element_per_chunk = (int)(size / numb_of_pieces);
	if (size % num_of_element_per_chunk > 0) { //remainder means need another block of remainer size or element size but with null vals.
		return NULL;	//temporary since I can malloc another block but maybe it should be illegal to divide list with uneven piece count.
	}	// remainder = size % num_of_element_per_chunk
	//alloc 2D array
	float** Arr2D = (float **)MEM_malloc_arrayN(numb_of_pieces, sizeof(float*), "Arr2D");	//numb_of_pieces is number of chunks
	for (int i = 0; i < numb_of_pieces; i++) {
		Arr2D[i] = (float *)MEM_malloc_arrayN(num_of_element_per_chunk, sizeof(float), __func__);
	}
	int index = 0; //i jumps between gaps, j starts at i and reads in the gaps (gap size).
	int jndex = 0; //index/jndex needed for actual output arr indicies.
	for (int i = 0; i < size; i+=num_of_element_per_chunk) {
		jndex = 0;
		for (int j = i; j < (i + num_of_element_per_chunk); j++) {
			printf("index: %d, jndex: %d, j: %d\n", index, jndex, j);
			Arr2D[index][jndex] = list[j];
			jndex++;
		}
		index++;
	}
	MEM_freeN(list);
	return Arr2D;	//still need to free 2d arr T_T
}

//I will assume it wants it in float form. //different row size
float* Helper_convert_verts_from_matrix_to_list(float** mat, uint16_t size)	//like before I will assume it's 9x3, unsure if always 9 though
{
	for (int i = 0; i < size; i++) {
		for (int j = 0; j < 3; j++) {
			printf("matconvert[%d][%d] = %f\n", i, j, mat[i][j]);
		}
	}
	int SIZE = size * 3;
	printf("size is %d\n", SIZE);
	float* list = NULL;
	list = (float*)MEM_malloc_arrayN(SIZE, sizeof(float), "MAT_2_list");				//(float *)MEM_callocN(sizeof(float) * (SIZE), "mat_to_list");	//(float(*)[3])MEM_calloc_arrayN(number_of, sizeof(float[3]), "mdisp swap");
	for (int i = 0; i < size*3; i+=3) {
		printf("mat_[%d] = %f\n", i, mat[i/3][0]);
		printf("mat_[%d] = %f\n", i+1, mat[i/3][1]);
		printf("mat_[%d] = %f\n", i+2, mat[i/3][2]);
		list[i] = mat[i/3][0];
		list[i+1] = mat[i/3][1];
		list[i+2] = mat[i/3][2];
	}
	for (int i = 0; i < size; i++) {
		MEM_freeN(mat[i]);
	}
	MEM_freeN(mat);		//unsure if I can free the 2d arr like this but since columns are know, I'd say yes.
	return list;	//needs freeing later
}


//for 1d arrs, float* parm or float parm[] is fine.
//however, 2d arrs need to be float parm[][number], float** is reserved for only allocated mem.
//different row size, column size
float** Helper_normalize_each_row(MASK_SELECTOR mask, uint16_t x, uint16_t y)	//don't need to return unless ... we need a whole new mat?
{	//currently, it edits the current mat, but I can make a new if needed.
	float* row_sums = (float *)MEM_malloc_arrayN(x, sizeof(float), "tmp_summation");
	float** maskcpy = (float **)MEM_malloc_arrayN(x, sizeof(float*), "mat_tmp_main");					//(float **)malloc(sizeof(float*) * x);//(float **)MEM_callocN(sizeof(float*) * x, "mat_tmp_main");
	for (int i = 0; i < x; i++) {
		maskcpy[i] = (float *)MEM_malloc_arrayN(x, sizeof(float), __func__);	//(float *)MEM_callocN(sizeof(float) * x, "mat_tmp_sub");
	}
	float** mat = getMask(mask);
	for (int i = 0; i < x; i++) {
		for (int j = 0; j < x; j++) {
			//printf("new mat: i: %d, j: %d, %f\n", i, j, maskcpy[i][j]);
			//printf("const mat: i: %d, j: %d, %f\n", i, j, mat[i][j]);
			//maskcpy[i][j] = mat[i][j];
		}
	}
	
	for (int i = 0; i < x; i++) {
		for (int j = 0; j < x; j++) {
			row_sums[i] += mat[i][j];
		}
	}
	for (int i = 0; i < x; i++) {
		for (int j = 0; j < x; j++) {
			maskcpy[i][j] = mat[i][j]/row_sums[i];
		}
	}
	for (int i = 0; i < x; i++) {
		MEM_freeN(mat[i]);
	}
	MEM_freeN(mat);
	MEM_freeN(row_sums);
	return maskcpy;
}	//better have zoom calls to check after many changes.


/*
def normalize_each_row(mat):
        row_sums = np.array(mat).sum(axis=1)	//sum on horizontal.
        return mat / row_sums[:, np.newaxis]	//div mat by vertical vector of row_sums since we need same size mat. (9x3)/(3x3)
*/

//different row size, col size diff
float** Helper_apply_mask_on_neighbor_verts(MASK_SELECTOR mask, BMVert* nbverts, uint16_t row, uint16_t col)
{	//9, 3
	float** maskcpy = NULL;
	float** nbmat = Helper_convert_verts_from_list_to_matrix(nbverts, row);
	maskcpy = Helper_normalize_each_row(mask, row, row);
	float** retMat = (float **)MEM_calloc_arrayN(row, sizeof(float*), "ret_mat");
	for (int i = 0; i < row; i++) {
		retMat[i] = (float *)MEM_calloc_arrayN(col, sizeof(float), __func__);
	}
	//dot product 9x9 mat and 9x3
	for (int i = 0; i < row; i++) {	//x = row, y = col. should be 9x3
		//retMat[i][j] = 0.0f;
		for (int j = 0; j < col; j++) {
			for (int k = 0; k < row; k++) {	//pretty sure this is dot product calc
				retMat[i][j] += maskcpy[i][k] * nbmat[k][j];
			}
			printf("retMat[%d][%d] = %f\n", i, j, retMat[i][j]);
		}
	}
	//free it all
	for (int i = 0; i < row; i++) {
		MEM_freeN(maskcpy[i]);
		MEM_freeN(nbmat[i]);
	}
	MEM_freeN(maskcpy);
	MEM_freeN(nbmat);
	return retMat;
}

int edges_number_of_face(BMFace face)	//can make pointer but idk purpose
{
	return face.len;
}

bool is_pentagon(BMFace face)
{
	if (edges_number_of_face(face) == 5) {
		return true;
	} else {
		return false;
	}
}

bool is_quad(BMFace face)
{
	if (edges_number_of_face(face) == 4) {
		return true;
	} else {
		return false;
	}
}

bool is_triangle(BMFace face)
{
	if (edges_number_of_face(face) == 3) {
		return true;
	} else {
		return false;
	}
}

bool is_hexagon(BMFace face)
{
	if (edges_number_of_face(face) == 6) {
		return true;
	} else {
		return false;
	}
}

BMVert* Helper_reorder_list(BMVert* unordered_verts, uint16_t* get_vert_order, uint32_t num_verts_reserved)
{
	BMVert* ordered_verts = (BMVert *)MEM_malloc_arrayN(num_verts_reserved, sizeof(BMVert), "verts_ordered_listed");
    for(int i = 0; i < num_verts_reserved; i++)	{
        ordered_verts[get_vert_order[i]] = unordered_verts[i];
    }
	MEM_freeN(unordered_verts);
	return ordered_verts;
}

BMFace* Helper_init_neighbor_faces(BMFace* face)
{
	//BM_elem_index_get(face->l_first->vert->head.index); //not good
	//face->l_first;	//the BMLoop.
	uint16_t size = 2;
	uint16_t index = 0;
	BMFace* faces = (BMFace *)MEM_callocN(sizeof(BMFace) * size, "faces_neighbors");
	BMLoop* loop_iter = face->l_first;	//face->vert->e->l;	//loop of the edge.
	BMLoop* loop_start = loop_iter;
	BMLoop* loop_iter2 = loop_iter;
	BMLoop* loop_start2 = loop_iter;
	BMVert* vert = loop_iter->v;
	BMEdge* edge_start = vert->e;
	BMEdge* iter_edge = vert->e;		//iter_edge = iter_edge.v1_disk_link->next;
	while (loop_iter != loop_start) {	//consider checking if v does not change through loops?
		iter_edge = loop_iter->e;		//no it should be fine since diff loops guarantee diff verts.
		edge_start = loop_iter->e;
		while (iter_edge != edge_start) {
			loop_iter2 = iter_edge->l;
			loop_start2 = iter_edge->l;
			while (loop_iter2 != loop_start2) {
				faces[index] = *loop_iter2->f;	//copied, can edit no issue.
				index++;
				if ((index) == size) {
					size *= 2;
					faces = (BMFace *)MEM_reallocN(faces, sizeof(BMFace) * size);
				}
				loop_iter2 = loop_iter2->radial_next;
			}
			if (loop_iter->v == iter_edge->v1) {
				iter_edge = iter_edge->v1_disk_link.next;	//I do not think v1 changes here.
			} else {
				iter_edge = iter_edge->v2_disk_link.next;
			}
		}
		loop_iter = loop_iter->next;
	}
	//loop through edges with v1_disk_link, v2_disk_link, next, prev (edges)
	//3,2,3 || 2,3 -> ...
	//there are dups that need to be removed, then realloc size.
	BMFace* faces_list = (BMFace *)MEM_callocN(sizeof(BMFace) * size, "faces_neighbors_new");
	int new_size = 0;
	for (int i = 0; i < index; i++) {	//brute force remove dups.
		for (int j = i+1; j < index; j++) {
			if (faces[i].head.index == faces[j].head.index) {	//if both index's are the same in bmesh, then they are the same.
				faces[j].len = -1;
			}
		}
		if (faces[i].head.index == face->head.index) {
			faces[i].len = -1;
		}
		if (faces[i].len != -1) {
			faces_list[new_size++] = faces[i];
		}
	}
	
	//after removing dups find all NULLs reorder and realloc size. (might not be necessary, lack of dups)
	faces_list = (BMFace *)MEM_reallocN(faces_list, sizeof(BMFace) * new_size);
	MEM_freeN(faces);
	return faces;	//need to free this too...
}

bool Helper_are_faces_all_quad(BMFace* faces, uint16_t count)
{
	for (int i = 0; i < count; i++) {
		if (!is_quad(faces[i])) {
			return false;
		}
	}
	return true;
}

//RegPatchConstructor functions

//vert.link_faces -> look at vert and see all faces in common. (all faces for one vert)
bool RegPatchConstructor_is_same_type(BMVert* vert)
{
	if (!is_quad(*vert->e->l->f)) {
		return false;
	}
	BMEdge* iter_edge = vert->e;
	BMEdge* edge_start = vert->e;
	BMLoop* loop_iter = NULL;
	BMLoop* loop_start = NULL;
	while (iter_edge != edge_start) {
		loop_iter = iter_edge->l;
		loop_start = iter_edge->l;
		while (loop_iter != loop_start) {
			if (!is_quad(*loop_iter->f)) {
				return false;
			}
			loop_iter = loop_iter->radial_next;
		}
		if (vert == iter_edge->v1) {
				iter_edge = iter_edge->v1_disk_link.next;	//I do not think v1 changes here.
		} else {
				iter_edge = iter_edge->v2_disk_link.next;
		}
	}
	/*
	BMLoop* l_iter = vert->e->l;
	BMLoop* start = l_iter;
	for (l_iter = vert->e->l; l_iter != start; l_iter=l_iter->radial_next) {
		if (!is_quad(l_iter->f)) {
			return false;
		}
	}
	*/
	return true;
}

BMVert* RegPatchConstructor_get_neighbor_verts(BMVert* vert)
{
	BMLoop* l_iter = vert->e->l;
	uint8_t commands[] = {1, 4, 1, 4, 1, 3};
	uint16_t get_vert_order[] = {1, 0, 3, 6, 7, 8, 5, 2};
	BMVert* nb_verts = halfEdge_get_verts_repeatN_times(l_iter, commands, 4, get_vert_order, 9);
	nb_verts[4] = *vert;
	for (int i = 0; i < 9; i++) {
		printf("vert %d, %f %f %f\n",i, nb_verts[i].co[0], nb_verts[i].co[1], nb_verts[i].co[2]);
	}
	return nb_verts;
}

float** RegPatchConstructor_get_patch(BMVert* vert)	//always 9x3 since regular construct, others are diff.
{	//bi2_bb2b == 3x3, used for 1x3 x 3x3 == 1x3. (1x4 x 4x3 = 1x3) Others vary
	BsplinePatch patch = {0, 0, NULL};
	patch.deg_u = 2;
	patch.deg_v = 2;
	BMVert* nb_verts = RegPatchConstructor_get_neighbor_verts(vert);
	for (int i = 0; i < 9; i++) {
		for (int j = 0; j < 3; j++) {
			if (isnan(nb_verts[i].co[j]) == true || nb_verts[i].co[j] < -10000000 || nb_verts[i].co[j] > 10000000) {
				printf("lol thx blender\n");
				nb_verts[i].co[j] = 0.0f;
			}
		}
	}
	float** bezier_coefs = Helper_apply_mask_on_neighbor_verts(1, nb_verts, 9, 3);	//9x3
	float** bspline_coefficients = BezierBsplineConverter_bezier_to_bspline(bezier_coefs, patch.deg_u, patch.deg_v, 9);
	float* bspline_coefs_arr = Helper_convert_verts_from_matrix_to_list(bspline_coefficients, 9);
	for (int i = 0; i < 27; i++) {
		printf("bspline_coefs_arr[%d] = %f\n", i, bspline_coefs_arr[i]);
	}
	//some functions above are too be implemented.
	int num_of_coef_per_patch = (patch.deg_u + 1) * (patch.deg_v + 1);
	int num_of_patches = 27 / num_of_coef_per_patch;	//9x3
	patch.deg_u++;
	patch.deg_v++;
	float** retMat = Helper_split_list(bspline_coefs_arr, 27, 9); //size: 27, num_of_patches: 9
	//patch.bspline_coefs = retMat;
	for (int i = 0; i < 9; i++) {
		for (int j = 0; j < 3; j++)
		printf("retMat[%d][%d] = %f\n", i, j, retMat[i][j]);
	}
	for (int i = 0; i < 9; i++) {	//can clear after BezierBsplineConverter_bezier_to_bspline is called (we get bspline_coefs).
		MEM_freeN(bezier_coefs[i]);
	}
	MEM_freeN(bezier_coefs);
	return retMat;
	//MEM_freeN(nb_verts);	//also ask if Helper_apply_mask_on_neighbor_verts is always #x3 and not #x#, make sure columns are not different. (doesn't matter but freeing double arr is annoying)
	//return patch;		//might need to consider other frees, ask if we use data after function calls.
}


//BezierBsplineConverter
//is dynamic, could be 9x3, 16x3, 25x3 etc. (2x2) (3x3) (4x4)
float** BezierBsplineConverter_bezier_to_bspline(float** bezier_coefs_mat, int deg_u, int deg_v, int total_bezier_coefs)	//add extra param for mat size.
{
	//int total_bezier_coefs = 0; == number of rows.
	int vert_dim = 3;
	if (deg_u != deg_v) {
		//throw error.
		return NULL;
	}

	int orderu = deg_u + 1;
	int orderv = deg_v + 1;
	int bezier_coef_per_patch = (deg_u + 1) * (deg_v + 1);
	float** mask_v = getMask(deg_v);	//BezierBsplineConverter_bb2b_mask_selector(deg_v);	//bad matrix matrix pointer redo function.
	float** mask_u = getMask(deg_u);	//BezierBsplineConverter_bb2b_mask_selector(deg_u);
	float** tmp_bc = (float **)MEM_malloc_arrayN(total_bezier_coefs, sizeof(float*), "tmp_bc");
	float** bspline_coefs = (float **)MEM_malloc_arrayN(total_bezier_coefs, sizeof(float*), "bspline_coefs");
	for (int i = 0; i < total_bezier_coefs; i++) {
		tmp_bc[i] = (float *)MEM_malloc_arrayN(3, sizeof(float), __func__);
		bspline_coefs[i] = (float *)MEM_malloc_arrayN(3, sizeof(float), __func__);
	}
	for (int i = 0; i < total_bezier_coefs; i++) {
		for (int j = 0; j < 3; j++) {
			tmp_bc[i][j] = 0.0f;
			bspline_coefs[i][j] = 0.0f;
		}
	}
	//v direction.
	for (int i = 0; i < total_bezier_coefs; i+=orderv) {
		for (int j = 0; j < orderv; j++) {			//after this we need to get our 1x3 for each row.
			for (int k = 0; k < vert_dim; k++) {	//always 3.
				for (int l = 0; l < orderv; l++) {	//variable: 3,4,5
					printf("i:%d, j:%d, k:%d, l:%d\n", i, j, k, l);
					tmp_bc[i+j][k] += mask_v[j][l] * bezier_coefs_mat[i+l][k];
				}
			}
		}
	}
	
	//u direction. //need to add extra due to multiple patches.
	//extra loop here. m?
	for (int m = 0; m < total_bezier_coefs; m+=bezier_coef_per_patch) {
		for (int i = 0; i < orderu; i++) {
			for (int j = 0; j < orderv; j++) {			//variable
				for (int k = 0; k < vert_dim; k++) {	//3
					for (int l = 0; l < orderu; l++) {	//orderu this time. tmp_bc[index] = 0,3,6 | 1,4,7, etc
					printf("p2: m:%d, i:%d, j:%d, k:%d, l:%d\n", m, i, j, k, l);
						bspline_coefs[m + (i*orderv) + j][k] += mask_u[i][l] * tmp_bc[m + j + (l*orderu)][k];
					}	//bspline_coefs[(i*orderv) + j][k] += mask_u[i][l] * tmp_bc[j + (l*orderu)][k];	//old
				}
			}
		}
	}
	printf("orderu: %d\n", orderu);
	printf("orderv: %d\n", orderv);
	for (int i = 0; i < deg_v; i++) {
		MEM_freeN(mask_v[i]);
		MEM_freeN(mask_u[i]);
	}
	for (int i = 0; i < total_bezier_coefs; i++) {
		MEM_freeN(tmp_bc[i]);
	}
	MEM_freeN(tmp_bc);
	MEM_freeN(mask_v);
	MEM_freeN(mask_u);
	return bspline_coefs; //bb2b_mask_selector should just return a const pointer to the masks for u and v, no malloc needed.
}

float** BezierBsplineConverter_bb2b_mask_selector(int deg)
{
	switch (deg) {
	case 2:
		return bi2_bb2b;
		break;
	case 3:
		return bi3_bb2b;
		break;
	case 4:
		return bi4_bb2b;
		break;
	default:
		//throw error.
		break;
	}
}

//additional func.
float** getMask(MASK_SELECTOR type)
{
	float** matrix = NULL;
	switch (type) {
	case MASK9X9:
		matrix = (float **)MEM_malloc_arrayN(9, sizeof(float*), "mat_tmp_main1");
		for (int i = 0; i < 9; i++) {
			matrix[i] = (float *)MEM_malloc_arrayN(9, sizeof(float), __func__);
		}
		printf("making new matrix\n");
		for (int i = 0; i < 9; i++) {
			for (int j = 0; j < 9; j++) {
				matrix[i][j] = MASK[i][j];
				printf("matrix[%d][%d] = %f\n", i, j, matrix[i][j]);
			}
		}
		return matrix;
		break;
	case MASK_bi2_bb2b:
		matrix = (float **)MEM_malloc_arrayN(3, sizeof(float*), "mat_tmp_main2");
		for (int i = 0; i < 3; i++) {
			matrix[i] = (float *)MEM_malloc_arrayN(3, sizeof(float), __func__);
		}
		printf("making new matrix 3x3\n");
		for (int i = 0; i < 3; i++) {
			for (int j = 0; j < 3; j++) {
				matrix[i][j] = bi2_bb2b[i][j];
				printf("matrix[%d][%d] = %f\n", i, j, matrix[i][j]);
			}
		}
		return matrix;
		break;
	case MASK_bi3_bb2b:
		matrix = (float **)MEM_malloc_arrayN(4, sizeof(float*), "mat_tmp_main3");
		for (int i = 0; i < 4; i++) {
			matrix[i] = (float *)MEM_malloc_arrayN(4, sizeof(float), __func__);
		}
		printf("making new matrix 4x4\n");
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				matrix[i][j] = bi3_bb2b[i][j];
				printf("matrix[%d][%d] = %f\n", i, j, matrix[i][j]);
			}
		}
		return matrix;
		break;
	case MASK_bi4_bb2b:
		matrix = (float **)MEM_malloc_arrayN(5, sizeof(float*), "mat_tmp_main4");
		for (int i = 0; i < 5; i++) {
			matrix[i] = (float *)MEM_malloc_arrayN(5, sizeof(float), __func__);
		}
		printf("making new matrix 5x5\n");
		for (int i = 0; i < 5; i++) {
			for (int j = 0; j < 5; j++) {
				matrix[i][j] = bi4_bb2b[i][j];
				printf("matrix[%d][%d] = %f\n", i, j, matrix[i][j]);
			}
		}
		return matrix;
		break;
	default:
		return NULL;
		break;
	}
	return NULL;
}

BMVert* rando_giv(float* verts)
{
	//l = (BMVert* )MEM_calloc_arrayN(4, sizeof(BMVert), "lol_mem");
	BMVert* list = (BMVert* )malloc(sizeof(BMVert) * 4);
	for (int i = 0; i < 4; i++) {
		list[i].co[0] = verts[i*3];
		list[i].co[1] = verts[i*3 + 1];
		list[i].co[2] = verts[i*3 + 2];
	}
	return list;
	//l = nb_verts;
}
