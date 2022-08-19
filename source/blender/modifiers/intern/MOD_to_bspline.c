#include "BKE_modifier.h"
#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"

//TODO: clear all uneeded #includes -BR 4/21
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "BLI_edgehash.h"
#include "BLI_kdtree.h"
#include "BLI_math.h"
#include "BLI_rand.h"

#include "MEM_guardedalloc.h"

#include "BLI_string.h"
#include "BLI_utildefines.h"
#include "BLI_listbase.h"

#include "BLT_translation.h"

#include "DNA_defaults.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_curve_types.h"

#include "DNA_ID.h"
#include "DNA_collection_types.h"

#include "BKE_bspline.h"	//own bke include
#include "BKE_collection.h"

#include "BKE_context.h"
#include "BKE_editmesh.h"	//BMesh.
#include "BKE_mesh.h"
#include "BKE_scene.h"
#include "BKE_main.h"
#include "BKE_screen.h"
#include "BKE_subdiv.h"
#include "BKE_subdiv_ccg.h"
#include "BKE_subdiv_deform.h"
#include "BKE_subdiv_mesh.h"
#include "BKE_subdiv_modifier.h"
#include "BKE_subsurf.h"
#include "BKE_curve.h"
#include "BKE_curveprofile.h"
#include "BKE_object.h"
#include "BKE_layer.h"
#include "BKE_lib_id.h"
#include "BKE_lib_query.h"
#include "BKE_mesh_runtime.h"
#include "BKE_main.h"
#include "BKE_idtype.h"


#include "UI_interface.h"
#include "UI_resources.h"

#include "RE_engine.h"

#include "RNA_access.h"
#include "DNA_key_types.h"
#include "DNA_material_types.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "intern/rna_internal.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"
#include "DEG_depsgraph_query.h"

#include "MOD_modifiertypes.h"
#include "MOD_ui_common.h"
#include "MOD_util.h"

#include "BLO_read_write.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_object.h"

#include "intern/CCGSubSurf.h"


//need to add BsplineModifierData struct
/* add this to source/blender/makesdna/ in file DNA_modifier_types.h
typedef struct BsplineModifierData {	//struct for Bspline mod data.
  ModifierData modifier;
  //add extra stuff here make sure it's in groups of eight bytes.
  struct Object *obj;
  int degree;
  short applied;
  char _pad[2];
  void *_pad0;
  //always name to pad as: char _pad[#];
} BsplineModifierData;
*/
//updated 4/21
/* LIST OF FILES MODIFIED: '+' == added, '=' == changed
	=source/blender/modifiers/CMakeLists.txt				//added MOD_to_bspline.c so it will compile with "make"
	+source/blender/modifiers/intern/MOD_to_bspline.c		//currently in this file, does all functionality of modier (entry point)
	=source/blender/modifiers/MOD_modifiertypes.h			//add the modifier to global modifier table.
	=source/blender/modifiers/intern/MOD_util.c				//added init of emodifier.
	=source/blender/makesdna/intern/DNA_modifier_types.h	//Defines modifier data and location in list of mods.
	=source/blender/makesdna/intern/DNA_modifier_defaults.h	//declares defualt values for modifer data.
	=source/blender/makesdna/intern/DNA_defaults.c			//links default vals to rna with SDNA_DEFAULT_DECL_STRUCT, etc.
	=source/blender/makesrna/intern/rna_modifier.c			//enables UI and limits for modifier data.
	=source/blender/makesrna/RNA_access.h					//added RNA modifier variant so modifier data can changed.
	=source/blender/blenkernel/CMakeLists.txt				//adds the files with the function definitions for modifer to have functionality. (translated from python)
	+source/blender/blenkernel/bspline.h					//Declares functions for bspline to use.
	+source/blender/blenkernel/intern/bspline.c				//Defines functions for bspline to use.
*/

static void initData(ModifierData *md)
{
  BsplineModifierData *bmd = (BsplineModifierData *)md;

  BLI_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(bmd, modifier));
  
  //errors here due to memcpy giving EXCEPTION_ACCESS_VIOLATION. Must not have completely finished making the modifer %100 in line with blender... -BR 3/12
  //^above resolved, needed to finish default values for modifier data in dna_modifier_defaults.h + link in dna_defaults.c -BR 3/13
  MEMCPY_STRUCT_AFTER(bmd, DNA_struct_default_get(BsplineModifierData), modifier);
  
  //extra
  //bmd->arr = (unsigned int*)MEM_calloc_arrayN(10, sizeof(unsigned int), "alloc_array");
  //printf("once?\n");
}

static void freeData(ModifierData *md) {
	BsplineModifierData *bmd = (BsplineModifierData *)md;
	//printf("free?\n");
	//MEM_freeN(bmd->arr);
	bmd->arr = NULL;
	//printf("done?\n");
	//DNE, make ur own freeRuntimeData.
	//freeRuntimeData(bmd->modifier.runtime);
}


//Entry function, reverted back to const ctx, currently only makes a curved surface of a quad mesh.
static Mesh *modifyMesh(struct ModifierData *md,
                                 const struct ModifierEvalContext *ctx,
                                 struct Mesh *mesh)
{
	//printf("STARTING\n");
    Mesh *result = mesh;
    BMesh *bm;
	BMesh *bmcpy;
    BMIter viter;
	BMIter fiter;
    BMVert *v, *v_next;
	BMFace *bf;
	BMLoop *bl;
	BMLoop *looping;
	BMLoop **inp;
	BMEdge *be;
	BMEdge *tmp;
	BsplineModifierData *bmd = (BsplineModifierData*)md;
	
    if (bmd->resolution == 1) {
        return mesh;
    }
	
    bm = BKE_mesh_to_bmesh_ex(mesh,
                            &(struct BMeshCreateParams){0},
                            &(struct BMeshFromMeshParams){
                                .calc_face_normal = true,
                                .cd_mask_extra = {.vmask = CD_MASK_ORIGINDEX,
                                                  .emask = CD_MASK_ORIGINDEX,
                                                  .pmask = CD_MASK_ORIGINDEX},
                            });
	BMVert *vNew;
	float col[3] = {0};
	//insert testing code.
	int counter = 0;
	
	for (int i = 0; i < 10; i++) {
		//printf("bmd->arr[i] = %d\n", bmd->arr[i]);
	}
	
	for (int i = 0; i < 10; i++) {
		//bmd->arr[i] = i;
		//printf("bmd->arr[i] = %d\n", bmd->arr[i]);
	}
	
	//for the unordered map, need to find way to map verts to the verts of subdivision 1 iter, then map those BMVerts to patches, have center_face/vert
	//also there to be called for update. -5/18
	/*map: vert_list[i] = patchlist[i]
	typedef struct patch {
		unint8_t type;		//1 << 0 == vert	1 << 1 == face //1
		//uint32_t size;		//size of list of verts for this patch
		uint32_t index;		//index of face or vert of the main vert/face in the BMesh to invoke patch construction.
	} patch;
	
	typedef struct patchList {
		uint32_t size;
		patch* plist;
	} patchList;
	*/
	ListBase nurbslist = {NULL, NULL};
	struct Mesh *me_eval;
	
	
	//Object *ob_eval = DEG_get_evaluated_object(ctx->depsgraph, ctx->object);
	//Scene *scene = DEG_get_evaluated_scene(ctx->depsgraph);
	Main *bmain = DEG_get_bmain(ctx->depsgraph);
	//ViewLayer *view_layer = DEG_get_evaluated_view_layer(ctx->depsgraph);	//DEG_get_input_view_layer(ctx->depsgraph);
	//ViewLayer *view_layer = BKE_view_layer_default_view(scene);
	
	//printf("name of layer: %s", view_layer->name);
    //me_eval = BKE_object_get_evaluated_mesh(ob_eval);
    //if (me_eval == NULL) {
    //  Scene *scene_eval = (Scene *)DEG_get_evaluated_id(depsgraph, &scene->id);
    //  me_eval = mesh_get_eval_final(depsgraph, scene_eval, ob_eval, &CD_MASK_BAREMESH);
    //}
	//printf("hello??\n");
	//info stored in obj I think, scene is unused.
	//use new object not ctx->object
	//BKE_mesh_to_curve(bmain, ctx->depsgraph, NULL, ctx->object);
	//Curve *cu = ctx->object->data;
	//cu->nurb == ListBase
	
	//BKE_nurb_makeCurve consider.
    //float projmat[4][4];

    //BKE_mesh_to_curve_nurblist(mesh, &nurbslist, 0); //wire
    //BKE_mesh_to_curve_nurblist(mesh, &nurbslist, 1); //boundary
	//result = BKE_mesh_copy_for_eval(mesh, 0);
	//struct Object *BKE_object_duplicate(struct Main *bmain, struct Object *ob, uint dupflag, uint duplicate_options);
	
	
	//Object *ob = BKE_object_duplicate(bmain, ctx->object, USER_DUP_MESH, LIB_ID_COPY_DEFAULT);		//BKE_object_add_only_object(bmain, OB_CURVES_LEGACY, ctx->object->id.name + 1);
	
	//BKE_mesh_to_curve(bmain, ctx->depsgraph, scene, ob);
	
	//result = BKE_mesh_new_nomain_from_curve(ob);
	
	
	
	
	
	
	
	//first add curve to mesh, then nurb. No need to convert to mesh or back bc u are adding it.
	
	
	//result = BKE_mesh_new_nomain_from_template(mesh, 0, 0, 0, 0, 0);
	
	
	//to make new obj
	//Object *ob = BKE_object_add(bmain, view_layer, OB_EMPTY, ctx->object->id.name + 1);
	
	//b4 making obj check if bspline modifier data's obj is null like:
	/*
	if (bmd->obj == NULL) {
		bmd->obj = BKE_object_add_only_object(bmain, OB_CURVES_LEGACY, "BSpline_Nurb_Obj");
	}	//add extra stuff related to update only versus init, actually look into initData...
	*/
	
	//	struct Main *BKE_main_new(void);
	//void BKE_main_free(struct Main *mainvar);
	//Main *tmpMain = BKE_main_new();
	//BKE_main_free(tmpMain);
	

	
	
	
	
	
	
	///*
	//Object *ob = BKE_object_add_only_object(NULL, OB_CURVES_LEGACY, "obj_nurb");	//ctx->object->id.name + 1
	//Object *ob = BKE_id_new(NULL, ID_OB, NULL);
	//id_us_min(&ob->id);
	//Object *ob = MEM_callocN(sizeof(Object), __func__);
	Object *ob = BKE_id_new_nomain(ID_OB, NULL);
	//Object *ob = bmd->obj;			//DEG_add_object_relation(ctx->node, bmd->obj, DEG_OB_COMP_TRANSFORM, "Bspline Modifier");
	//can only read not write bmd->whatever.
	Curve *cu;
	//if (ob != NULL) {
		//printf("ob is not null\n");
		//bmd->obj = BKE_object_add_only_object(bmain, OB_CURVES_LEGACY, "Bspline_obj");
		//Curve *cu = BKE_curve_add(bmain, "bspline_curve", CU_NURBS);
		//return mesh;
		//return BKE_mesh_new_from_object(ctx->depsgraph, ob, 0, 0);
	//}
	//printf("applied %d\n", bmd->applied);
	//ob = BKE_object_add_only_object(bmain, OB_CURVES_LEGACY, "curve_nurb");
	//cu = BKE_curve_add(NULL, "bspline_curve", CU_NURBS);
	//cu = BKE_id_new(NULL, ID_CV, NULL);	//
	//id_us_min(&cu->id);
	cu = BKE_id_new_nomain(ID_CU_LEGACY, NULL);	//ID_CV is not a good flag, missing some data...
	//cu = MEM_callocN(sizeof(Curve), __func__);
	BKE_curve_init(cu, CU_NURBS);	//tried to do without bmain, not good...
	//ob->data = BKE_object_obdata_add_from_type(bmain, OB_CURVES_LEGACY, ctx->object->id.name + 1);
	//Curve *cu = BKE_curve_add(bmain, "bspline_curve", CU_NURBS);
	//must free curve when added.
    
	cu->flag |= CU_3D;
	cu->type = CU_NURBS;	//redundant but I'll leave it.
	
	//if add.
	//void BKE_curve_nurbs_vert_coords_apply(ListBase *lb, const float (*vert_coords)[3], const bool constrain_2d);
	float verts[27] = {5.0f, 0.0f, 5.0f,		0.0f, 0.0f, 5.0f,		-5.0f, 0.0f, 5.0f,
						5.0f, 0.0f, 0.0f,		0.0f, 0.0f, 0.0f,		-5.0f, 0.0f, 0.0f,
						5.0f, 0.0f, -5.0f,		0.0f, 0.0f, -5.0f,		-5.0f, 0.0f, -5.0f};
	//float verts[12] = {1.0f, 1.0f, 1.0f,		-1.0f, 1.0f, -1.0f,		1.0f, -1.0f, -1.0f, 				-1.0f, -1.0f, -1.0f};
	///*
	//testing, mem leaks don't crash blender, some func calls needed or bad indexing not sure.
	//BMVert* list_verts = rando_giv(bm);
	//for (int e = 0; e < bm->totvert + 1; e++) {
	//	printf("x: %f, y: %f, z: %f\n", list_verts[e].co[0], list_verts[e].co[1], list_verts[e].co[2]);
	//}
	//free(list_verts);
	
	//BM_ITER_MESH(v, &viter, bm, BM_VERTS_OF_MESH)
	//{
	//	printf("x:%f y:%f z:%f\n", v->co[0], v->co[1], v->co[2]);
	//}
	//something wrong with algorithm now, most likely points for matrix are being screwed. consider printing out matrix.
	BM_ITER_MESH_INDEX(v, &viter, bm, BM_VERTS_OF_MESH, counter)
	{	
		if (RegPatchConstructor_is_same_type(v) == true) {	//if (v->co[0] == 0.0f && v->co[1] == 0.0f && v->co[2] == 0.0f) { RegPatchConstructor_is_same_type(v) == true
			//supposedly good from create_object, usd_reader_nurbs.cc
			Nurb* nu;
			BPoint *bp;
	
			nu = (Nurb *)MEM_callocN(sizeof(Nurb), "spline.new");
	
			//BezTriple *bezt = (BezTriple *)MEM_callocN(sizeof(BezTriple), "spline.new.bezt");
			//bezt->radius = 1.0;
			//nu->bezt = bezt;
	
			bp = (BPoint *)MEM_callocN(9 * sizeof(BPoint), "spline.new.bp"); //add 4 BPoints.
			bp->radius = 1.0f;
	
			nu->bp = bp;
	
			nu->pntsu = 3;
			nu->pntsv = 3;
			nu->orderu = nu->orderv = 3;
			nu->resolu = cu->resolu = 2;
			nu->resolv = cu->resolv = 2;
			nu->flag = CU_SMOOTH;
			nu->flagu = 0;	//CU_NURB_ENDPOINT;	//CU_NURB_ENDPOINT //0 for none.
			nu->flagv = 0;	//supposedly is this by default.
			nu->type = CU_NURBS; //CU_NURBS; CU_PRIM_PATCH;
			nu->hide = 0;	//unsure what this does
			//nurbslist.first = nu;	add this with BLI_addtail

			switch (bmd->resolution) {
				case 2:
					nu->resolu = cu->resolu = 2;
					nu->resolv = cu->resolv = 2;
					break;
				case 3:
					nu->resolu = cu->resolu = 4;
					nu->resolv = cu->resolv = 4;
					break;
				case 4:
					nu->resolu = cu->resolu = 6;
					nu->resolv = cu->resolv = 6;
					break;
				case 5:
					nu->resolu = cu->resolu = 8;
					nu->resolv = cu->resolv = 8;
					break;
				case 6:
					nu->resolu = cu->resolu = 10;
					nu->resolv = cu->resolv = 10;
					break;
			}
			float** finalMAT = RegPatchConstructor_get_patch(v);
			for (int i = 0; i < 9; i++) {
				for (int j = 0; j < 3; j++) {
					nu->bp[i].vec[j] = finalMAT[i][j];
					//printf("finalMAT[%d][%d] = %f\n", i, j, finalMAT[i][j]);
					//printf("nu->bp[%d].vec[%d] = %f\n", i, j, nu->bp[i].vec[j]);
				}
				nu->bp[i].vec[3] = 1.0f;
			}
			for (int i = 0; i < 9; i++) {
				MEM_freeN(finalMAT[i]);
			}
			MEM_freeN(finalMAT);
			
			//needs to be called post new verts applied.
			BKE_nurb_knot_calc_u(nu);
			BKE_nurb_knot_calc_v(nu);
			BLI_addtail(&nurbslist, nu);
			//free(bspline_coefs_arr);
			//BKE_curve_nurbs_vert_coords_apply(&nurbslist, verts, NULL); //or 0 for last param.
		} else if (ExtraordinaryPatchConstructor_is_same_type(v) == true) {
			//break;
			//printf("somehow\n");
			int bp_count = BM_vert_edge_count(v);
			if (bp_count == 3) {
				bp_count = 48;
			} else if (bp_count == 5) {
				bp_count = 80;
			} else if (bp_count == 6) {
				bp_count = 384;
			} else if (bp_count == 7) {
				bp_count = 448;
			} else if (bp_count == 8) {
				bp_count = 512;
			}
			//genuine question: how many pnts are on u and v for eop cases? -5/12
			int num_of_patches = bp_count / 16;	//48/16 = 3
			//it's 4x4 points but there are 3 nurbs surfaces, need to find out about breaking them up.
			//look at matrix and the first 16 goes to the first, so on and so forth.
			float** finalMAT = ExtraordinaryPatchConstructor_get_patch(v);
			for (int i = 0; i < num_of_patches; i++) {
				Nurb* nu;
				BPoint *bp;
				nu = (Nurb *)MEM_callocN(sizeof(Nurb), "spline.new");
				
				bp = (BPoint *)MEM_callocN(16 * sizeof(BPoint), "spline.new.bp");
				bp->radius = 1.0f;
				
				nu->bp = bp;
				nu->pntsu = 4;
				nu->pntsv = 4;
				nu->orderu = nu->orderv = 4;
				nu->resolu = cu->resolu = 2;
				nu->resolv = cu->resolv = 2;
				nu->flag = CU_SMOOTH;
				nu->flagu = 0;	//CU_NURB_ENDPOINT;	//CU_NURB_ENDPOINT //0 for none.
				nu->flagv = 0;	//supposedly is this by default.
				nu->type = CU_NURBS; //CU_NURBS; CU_PRIM_PATCH;
				nu->hide = 0;	//unsure what this does
				//nurbslist.first = nu;	add this with BLI_addtail

				switch (bmd->resolution) {
					case 2:
						nu->resolu = cu->resolu = 2;
						nu->resolv = cu->resolv = 2;
						break;
					case 3:
						nu->resolu = cu->resolu = 4;
						nu->resolv = cu->resolv = 4;
						break;
					case 4:
						nu->resolu = cu->resolu = 6;
						nu->resolv = cu->resolv = 6;
						break;
					case 5:
						nu->resolu = cu->resolu = 8;
						nu->resolv = cu->resolv = 8;
						break;
					case 6:
						nu->resolu = cu->resolu = 10;
						nu->resolv = cu->resolv = 10;
						break;
				}
				for (int j = i*16; j < ((1 + i)*16); j++) {
					for (int l = 0; l < 3; l++) {
						nu->bp[j%16].vec[l] = finalMAT[j][l];
						//printf("finalMAT[%d][%d] = %f\n", j, l, finalMAT[j][l]);
						//printf("nu->bp[%d].vec[%d] = %f\n", j%16, l, nu->bp[j%16].vec[l]);
					}
					nu->bp[j%16].vec[3] = 1.0f;
				}
				//needs to be called post new verts applied.
				BKE_nurb_knot_calc_u(nu);
				BKE_nurb_knot_calc_v(nu);
				BLI_addtail(&nurbslist, nu);
			}
			
			for (int i = 0; i < bp_count; i++) {
				MEM_freeN(finalMAT[i]);
			}
			MEM_freeN(finalMAT);
		} else if (PolarPatchConstructor_is_same_type(v) == true) {	//getting garbage, could be bezier to bspline or just bad format b4 hand, most likely ladder, checking it out tmr -BR 8/12
			//printf("called on vert: %f %f %f\n", v->co[0], v->co[1], v->co[2]);
			int bp_count = BM_vert_edge_count(v);	//also only partial vert interaction (moving some verts don't affect patches.); most likely polar settings... rather than convert.
			bp_count = bp_count * 12;	//rows is based on edge count
			int num_of_patches = bp_count / 12;	//it's bc u = 4, v = 3.
			float** finalMAT = PolarPatchConstructor_get_patch(v);
			for (int i = 0; i < num_of_patches; i++) {
				Nurb* nu;
				BPoint *bp;
				nu = (Nurb *)MEM_callocN(sizeof(Nurb), "spline.new");
				
				bp = (BPoint *)MEM_callocN(12 * sizeof(BPoint), "spline.new.bp");
				bp->radius = 1.0f;
				
				nu->bp = bp;
				//odd thing here, in this case it is u = 4 and v = 3, but blender gives odd formatting if I set it that way. Switching the numbers fixes this don't know why it matters
				nu->pntsu = 3;
				nu->pntsv = 4;
				nu->orderu = 3;
				nu->orderv = 4;
				nu->resolu = cu->resolu = 2;
				nu->resolv = cu->resolv = 2;
				nu->flag = CU_SMOOTH;
				nu->flagu = 0;	//CU_NURB_ENDPOINT;	//CU_NURB_ENDPOINT //0 for none.
				nu->flagv = 0;	//supposedly is this by default.
				nu->type = CU_NURBS;
				nu->hide = 0;	//unsure what this does

				switch (bmd->resolution) {
					case 2:
						nu->resolu = cu->resolu = 2;
						nu->resolv = cu->resolv = 2;
						break;
					case 3:
						nu->resolu = cu->resolu = 4;
						nu->resolv = cu->resolv = 4;
						break;
					case 4:
						nu->resolu = cu->resolu = 6;
						nu->resolv = cu->resolv = 6;
						break;
					case 5:
						nu->resolu = cu->resolu = 8;
						nu->resolv = cu->resolv = 8;
						break;
					case 6:
						nu->resolu = cu->resolu = 10;
						nu->resolv = cu->resolv = 10;
						break;
				}
				for (int j = i*12; j < ((1 + i)*12); j++) {
					for (int l = 0; l < 3; l++) {
						nu->bp[j%12].vec[l] = finalMAT[j][l];
						//printf("finalMAT[%d][%d] = %f\n", j, l, finalMAT[j][l]);
					}
					nu->bp[j%12].vec[3] = 1.0f;
				}
				//needs to be called post new verts applied.
				BKE_nurb_knot_calc_u(nu);
				BKE_nurb_knot_calc_v(nu);
				BLI_addtail(&nurbslist, nu);
			}
			
			for (int i = 0; i < bp_count; i++) {
				MEM_freeN(finalMAT[i]);
			}
			MEM_freeN(finalMAT);
		} else if (TwoTrianglesTwoQuadsPatchConstructor_is_same_type(v) == true) {
			//printf("called on vert: %f %f %f\n", v->co[0], v->co[1], v->co[2]);
			int bp_count = 9*1;
			int num_of_patches = bp_count / 9;	//it's bc u = 4, v = 3.
			float** finalMAT = TwoTrianglesTwoQuadsPatchConstructor_get_patch(v);
			for (int i = 0; i < num_of_patches; i++) {
				Nurb* nu;
				BPoint *bp;
				nu = (Nurb *)MEM_callocN(sizeof(Nurb), "spline.new");
				
				bp = (BPoint *)MEM_callocN(9 * sizeof(BPoint), "spline.new.bp");
				bp->radius = 1.0f;
				
				nu->bp = bp;
				//odd thing here, in this case it is u = 4 and v = 3, but blender gives odd formatting if I set it that way. Switching the numbers fixes this don't know why it matters
				nu->pntsu = 3;
				nu->pntsv = 3;
				nu->orderu = nu->orderv = 3;
				nu->resolu = cu->resolu = 2;
				nu->resolv = cu->resolv = 2;
				nu->flag = CU_SMOOTH;
				nu->flagu = 0;	//CU_NURB_ENDPOINT;	//CU_NURB_ENDPOINT //0 for none.
				nu->flagv = 0;	//supposedly is this by default.
				nu->type = CU_NURBS;
				nu->hide = 0;	//unsure what this does

				switch (bmd->resolution) {
					case 2:
						nu->resolu = cu->resolu = 2;
						nu->resolv = cu->resolv = 2;
						break;
					case 3:
						nu->resolu = cu->resolu = 4;
						nu->resolv = cu->resolv = 4;
						break;
					case 4:
						nu->resolu = cu->resolu = 6;
						nu->resolv = cu->resolv = 6;
						break;
					case 5:
						nu->resolu = cu->resolu = 8;
						nu->resolv = cu->resolv = 8;
						break;
					case 6:
						nu->resolu = cu->resolu = 10;
						nu->resolv = cu->resolv = 10;
						break;
				}
				for (int j = i*9; j < ((1 + i)*9); j++) {
					for (int l = 0; l < 3; l++) {
						nu->bp[j%9].vec[l] = finalMAT[j][l];
						//printf("finalMAT[%d][%d] = %f\n", j, l, finalMAT[j][l]);
					}
					nu->bp[j%9].vec[3] = 1.0f;
				}
				//needs to be called post new verts applied.
				BKE_nurb_knot_calc_u(nu);
				BKE_nurb_knot_calc_v(nu);
				BLI_addtail(&nurbslist, nu);
			}
			
			for (int i = 0; i < bp_count; i++) {
				MEM_freeN(finalMAT[i]);
			}
			MEM_freeN(finalMAT);
		}
	}
	
	BM_ITER_MESH_INDEX(bf, &fiter, bm, BM_FACES_OF_MESH, counter)
	{
		if (NGonPatchConstructor_is_same_type(bf) == true) {
			//break;
			int bp_count = Helper_edges_number_of_face(*bf);
			if (bp_count == 3) {
				bp_count = 48;
			} else if (bp_count == 5) {
				bp_count = 80;
			} else if (bp_count == 6) {
				bp_count = 384;
			} else if (bp_count == 7) {
				bp_count = 448;
			} else if (bp_count == 8) {
				bp_count = 512;
			}
			//genuine question: how many pnts are on u and v for eop cases? -5/12
			int num_of_patches = bp_count / 16;	//48/16 = 3
			//it's 4x4 points but there are 3 nurbs surfaces, need to find out about breaking them up.
			//look at matrix and the first 16 goes to the first, so on and so forth.
			float** finalMAT = NGonPatchConstructor_get_patch(bf);
			for (int i = 0; i < num_of_patches; i++) {
				Nurb* nu;
				BPoint *bp;
				nu = (Nurb *)MEM_callocN(sizeof(Nurb), "spline.new");
				
				bp = (BPoint *)MEM_callocN(16 * sizeof(BPoint), "spline.new.bp");
				bp->radius = 1.0f;
				
				nu->bp = bp;
				nu->pntsu = 4;
				nu->pntsv = 4;
				nu->orderu = nu->orderv = 4;
				nu->resolu = cu->resolu = 2;
				nu->resolv = cu->resolv = 2;
				nu->flag = CU_SMOOTH;
				nu->flagu = 0;	//CU_NURB_ENDPOINT;	//CU_NURB_ENDPOINT //0 for none.
				nu->flagv = 0;	//supposedly is this by default.
				nu->type = CU_NURBS; //CU_NURBS; CU_PRIM_PATCH;
				nu->hide = 0;	//unsure what this does
				//nurbslist.first = nu;	add this with BLI_addtail

				switch (bmd->resolution) {
					case 2:
						nu->resolu = cu->resolu = 2;
						nu->resolv = cu->resolv = 2;
						break;
					case 3:
						nu->resolu = cu->resolu = 4;
						nu->resolv = cu->resolv = 4;
						break;
					case 4:
						nu->resolu = cu->resolu = 6;
						nu->resolv = cu->resolv = 6;
						break;
					case 5:
						nu->resolu = cu->resolu = 8;
						nu->resolv = cu->resolv = 8;
						break;
					case 6:
						nu->resolu = cu->resolu = 10;
						nu->resolv = cu->resolv = 10;
						break;
				}
				for (int j = i*16; j < ((1 + i)*16); j++) {
					for (int l = 0; l < 3; l++) {
						nu->bp[j%16].vec[l] = finalMAT[j][l];
						//printf("finalMAT[%d][%d] = %f\n", j, l, finalMAT[j][l]);
						//printf("nu->bp[%d].vec[%d] = %f\n", j%16, l, nu->bp[j%16].vec[l]);
					}
					nu->bp[j%16].vec[3] = 1.0f;
				}
				//needs to be called post new verts applied.
				BKE_nurb_knot_calc_u(nu);
				BKE_nurb_knot_calc_v(nu);
				BLI_addtail(&nurbslist, nu);
			}
			
			for (int i = 0; i < bp_count; i++) {
				MEM_freeN(finalMAT[i]);
			}
			MEM_freeN(finalMAT);
		} else if (T0PatchConstructor_is_same_type(bf) == true) {
			//break;
			int bp_count = 64;
			int num_of_patches = bp_count / 16;	//64/16 = 4
			//it's 4x4 points but there are 3 nurbs surfaces, need to find out about breaking them up.
			//look at matrix and the first 16 goes to the first, so on and so forth.
			float** finalMAT = T0PatchConstructor_get_patch(bf);
			for (int i = 0; i < num_of_patches; i++) {
				Nurb* nu;
				BPoint *bp;
				nu = (Nurb *)MEM_callocN(sizeof(Nurb), "spline.new");
				
				bp = (BPoint *)MEM_callocN(16 * sizeof(BPoint), "spline.new.bp");
				bp->radius = 1.0f;
				
				nu->bp = bp;
				nu->pntsu = 4;
				nu->pntsv = 4;
				nu->orderu = nu->orderv = 4;
				nu->resolu = cu->resolu = 2;
				nu->resolv = cu->resolv = 2;
				nu->flag = CU_SMOOTH;
				nu->flagu = 0;	//CU_NURB_ENDPOINT;	//CU_NURB_ENDPOINT //0 for none.
				nu->flagv = 0;	//supposedly is this by default.
				nu->type = CU_NURBS; //CU_NURBS; CU_PRIM_PATCH;
				nu->hide = 0;	//unsure what this does
				//nurbslist.first = nu;	add this with BLI_addtail

				switch (bmd->resolution) {
					case 2:
						nu->resolu = cu->resolu = 2;
						nu->resolv = cu->resolv = 2;
						break;
					case 3:
						nu->resolu = cu->resolu = 4;
						nu->resolv = cu->resolv = 4;
						break;
					case 4:
						nu->resolu = cu->resolu = 6;
						nu->resolv = cu->resolv = 6;
						break;
					case 5:
						nu->resolu = cu->resolu = 8;
						nu->resolv = cu->resolv = 8;
						break;
					case 6:
						nu->resolu = cu->resolu = 10;
						nu->resolv = cu->resolv = 10;
						break;
				}
				for (int j = i*16; j < ((1 + i)*16); j++) {
					for (int l = 0; l < 3; l++) {
						nu->bp[j%16].vec[l] = finalMAT[j][l];
						//printf("finalMAT[%d][%d] = %f\n", j, l, finalMAT[j][l]);
						//printf("nu->bp[%d].vec[%d] = %f\n", j%16, l, nu->bp[j%16].vec[l]);
					}
					nu->bp[j%16].vec[3] = 1.0f;
				}
				//needs to be called post new verts applied.
				BKE_nurb_knot_calc_u(nu);
				BKE_nurb_knot_calc_v(nu);
				BLI_addtail(&nurbslist, nu);
			}
			
			for (int i = 0; i < bp_count; i++) {
				MEM_freeN(finalMAT[i]);
			}
			MEM_freeN(finalMAT);
		} else if (T1PatchConstructor_is_same_type(bf) == true) {
			//break;
			int bp_count = 128;
			int num_of_patches = bp_count / 16;
			float** finalMAT = T1PatchConstructor_get_patch(bf);
			for (int i = 0; i < num_of_patches; i++) {
				Nurb* nu;
				BPoint *bp;
				nu = (Nurb *)MEM_callocN(sizeof(Nurb), "spline.new");
				
				bp = (BPoint *)MEM_callocN(16 * sizeof(BPoint), "spline.new.bp");
				bp->radius = 1.0f;
				
				nu->bp = bp;
				nu->pntsu = 4;
				nu->pntsv = 4;
				nu->orderu = nu->orderv = 4;
				nu->resolu = cu->resolu = 2;
				nu->resolv = cu->resolv = 2;
				nu->flag = CU_SMOOTH;
				nu->flagu = 0;	//CU_NURB_ENDPOINT;	//CU_NURB_ENDPOINT //0 for none.
				nu->flagv = 0;	//supposedly is this by default.
				nu->type = CU_NURBS; //CU_NURBS; CU_PRIM_PATCH;
				nu->hide = 0;	//unsure what this does
				//nurbslist.first = nu;	add this with BLI_addtail

				switch (bmd->resolution) {
					case 2:
						nu->resolu = cu->resolu = 2;
						nu->resolv = cu->resolv = 2;
						break;
					case 3:
						nu->resolu = cu->resolu = 4;
						nu->resolv = cu->resolv = 4;
						break;
					case 4:
						nu->resolu = cu->resolu = 6;
						nu->resolv = cu->resolv = 6;
						break;
					case 5:
						nu->resolu = cu->resolu = 8;
						nu->resolv = cu->resolv = 8;
						break;
					case 6:
						nu->resolu = cu->resolu = 10;
						nu->resolv = cu->resolv = 10;
						break;
				}
				for (int j = i*16; j < ((1 + i)*16); j++) {
					for (int l = 0; l < 3; l++) {
						nu->bp[j%16].vec[l] = finalMAT[j][l];
						//printf("finalMAT[%d][%d] = %f\n", j, l, finalMAT[j][l]);
						//printf("nu->bp[%d].vec[%d] = %f\n", j%16, l, nu->bp[j%16].vec[l]);
					}
					nu->bp[j%16].vec[3] = 1.0f;
				}
				//needs to be called post new verts applied.
				BKE_nurb_knot_calc_u(nu);
				BKE_nurb_knot_calc_v(nu);
				BLI_addtail(&nurbslist, nu);
			}
			
			for (int i = 0; i < bp_count; i++) {
				MEM_freeN(finalMAT[i]);
			}
			MEM_freeN(finalMAT);
		} else if (T2PatchConstructor_is_same_type(bf) == true) {
			//break;
			int bp_count = 256;
			int num_of_patches = bp_count / 16;
			float** finalMAT = T2PatchConstructor_get_patch(bf);
			for (int i = 0; i < num_of_patches; i++) {
				Nurb* nu;
				BPoint *bp;
				nu = (Nurb *)MEM_callocN(sizeof(Nurb), "spline.new");
				
				bp = (BPoint *)MEM_callocN(16 * sizeof(BPoint), "spline.new.bp");
				bp->radius = 1.0f;
				
				nu->bp = bp;
				nu->pntsu = 4;
				nu->pntsv = 4;
				nu->orderu = nu->orderv = 4;
				nu->resolu = cu->resolu = 2;
				nu->resolv = cu->resolv = 2;
				nu->flag = CU_SMOOTH;
				nu->flagu = 0;	//CU_NURB_ENDPOINT;	//CU_NURB_ENDPOINT //0 for none.
				nu->flagv = 0;	//supposedly is this by default.
				nu->type = CU_NURBS; //CU_NURBS; CU_PRIM_PATCH;
				nu->hide = 0;	//unsure what this does
				//nurbslist.first = nu;	add this with BLI_addtail

				switch (bmd->resolution) {
					case 2:
						nu->resolu = cu->resolu = 2;
						nu->resolv = cu->resolv = 2;
						break;
					case 3:
						nu->resolu = cu->resolu = 4;
						nu->resolv = cu->resolv = 4;
						break;
					case 4:
						nu->resolu = cu->resolu = 6;
						nu->resolv = cu->resolv = 6;
						break;
					case 5:
						nu->resolu = cu->resolu = 8;
						nu->resolv = cu->resolv = 8;
						break;
					case 6:
						nu->resolu = cu->resolu = 10;
						nu->resolv = cu->resolv = 10;
						break;
				}
				for (int j = i*16; j < ((1 + i)*16); j++) {
					for (int l = 0; l < 3; l++) {
						nu->bp[j%16].vec[l] = finalMAT[j][l];
						//printf("finalMAT[%d][%d] = %f\n", j, l, finalMAT[j][l]);
						//printf("nu->bp[%d].vec[%d] = %f\n", j%16, l, nu->bp[j%16].vec[l]);
					}
					nu->bp[j%16].vec[3] = 1.0f;
				}
				//needs to be called post new verts applied.
				BKE_nurb_knot_calc_u(nu);
				BKE_nurb_knot_calc_v(nu);
				BLI_addtail(&nurbslist, nu);
			}
			
			for (int i = 0; i < bp_count; i++) {
				MEM_freeN(finalMAT[i]);
			}
			MEM_freeN(finalMAT);
		}
	}
	
	cu->nurb = nurbslist;
	//to add more nurb surfaces
	//BLI_addtail((add curve here), &nurbslist);	//BKE_curve_nurbs_get(cu)
	
	ob->data = cu;
    ob->type = OB_SURF; //OB_CURVES	OB_CURVES_LEGACY
	
	
	//added mesh to obj for test..
	//ob = BKE_object_add_only_object(bmain, OB_EMPTY, ctx->object->id.name + 1);
	//BKE_mesh_assign_object(bmain, ob, mesh);
	
	//don't use this.
	//result = BKE_mesh_new_nomain_from_curve(ob);	// BKE_mesh_new_nomain_from_curve_displist(ob, &nurbslist); doesn't quite work
	
	result = BKE_mesh_new_from_object(ctx->depsgraph, ob, false, true);
	//result = BKE_mesh_from_object(ob);
	//printf("here?\n");
	//*/
	
	
	
	
	
	
	
	
	
	
	
	//sigh, day 20 of trying to free mem correctly.
	//MEM_freeN(bp);
	//MEM_freeN(nu); //free alloc'd	//MEM_freeN
	//BKE_nurbList_free(&cu->nurb);	//still does not free it.
	//BKE_libblock_free_datablock((ID *)cu, 0);
	//BKE_nurb_free(nu);
	//BKE_curve_editfont_free(cu);
	//BKE_id_free(bmain, (ID *)cu);
	//BKE_object_free_derived_caches(ob);
	//BKE_object_runtime_free_data(ob);
	//BKE_object_shapekey_free(tmpMain, ob);
	
	//BKE_object_free_curve_cache(ob);
	//MEM_freeN(bp);
	//MEM_freeN(nu);
	//BKE_nurbList_free(&cu->nurb);
	//BKE_main_free(tmpMain);
	//MEM_freeN(bp);
	//MEM_freeN(nu);
	//bp = NULL;
	//nu = NULL;
	//BKE_object_free_derived_caches(ob);
	//BKE_nurbList_free(&cu->nurb);
	/*	//does not work for now, prob just iterate through nurblist using macro
	Link *link, *next;
	link = &cu->nurb.first;
	while (link != NULL) {
		next = link->next;
		Nurb* nb = (Nurb*)link;
		if (nb != NULL) {
			if (nb->bp != NULL) {
				MEM_freeN(nb->bp);
				nb->bp = NULL;
				printf("null?\n");
			}
			MEM_freeN(nb);
		}
		nb = NULL;
		link = next;
	}
	*/
	
	//BLI_listbase_clear(&cu->nurb);
	//MEM_freeN(ob);	//ED_object_base_free_and_unlink(bmain, scene, ob);?
	//ED_object_base_free_and_unlink(bmain, scene, ob);
	//BKE_scene_collections_object_remove(bmain, scene, ob, true);
	
	//BKE_id_delete(bmain, cu);
	//BKE_id_delete(bmain, ob);
	//still hardstuck on freeing this data I made... day 3. I genuinely have no idea what to do, no help + no reference. It's amazing I even got this far. gg.
	/*	//does not work either sadly even though it should. -5/13
	LISTBASE_FOREACH(Nurb*, var, &nurbslist) {
		if (var != NULL) {
			if (var->bp != NULL) {
				MEM_freeN(var->bp);
			}
			MEM_freeN(var);
		}
	}
	*/
	Scene *scene = DEG_get_evaluated_scene(ctx->depsgraph);
	ViewLayer *view_layer = DEG_get_evaluated_view_layer(ctx->depsgraph);	//DEG_get_input_view_layer(ctx->depsgraph);
	//ViewLayer *view_layer = BKE_view_layer_default_view(scene);
	//MEM_freeN(cu);
	//MEM_freeN(ob);
	//BKE_nurbList_free(&cu->nurb);
	//BKE_id_free_ex(bmain, ob, 0, false);
	//BKE_id_free_ex(bmain, cu, 0, false);

	//BKE_main_collection_sync_remap(bmain);
	Base* base = BKE_view_layer_base_find(view_layer, ctx->object);
	LayerCollection *layer_collection = BKE_layer_collection_get_active(view_layer);
	Collection* collection = layer_collection->collection;
	//base->flag &= BASE_SELECTED;
	//BKE_scene_set_background(bmain, scene);

	//BKE_view_layer_base_select_and_set_active(view_layer, base);
	//BKE_layer_collection_sync(scene, view_layer);	//does something but only minimum for some reason. only gives outline.
	//BKE_layer_collection_local_sync_all(bmain);
	//these 3 will fix all memory, issue is now the artifact.
	//BKE_nurbList_free(&cu->nurb);
	
	BKE_curve_batch_cache_free(cu);

	BKE_nurbList_free(&cu->nurb);

	if (!cu->edit_data_from_original) {
		BKE_curve_editfont_free(cu);
		BKE_curve_editNurb_free(cu);
	}

	BKE_curveprofile_free(cu->bevel_profile);

	MEM_SAFE_FREE(cu->mat);
	MEM_SAFE_FREE(cu->str);
	MEM_SAFE_FREE(cu->strinfo);
	MEM_SAFE_FREE(cu->tb);
	
	BKE_id_free(NULL, ob);	//flags fail it...
	BKE_id_free(NULL, cu);
	//BKE_id_free_ex(bmain, ob, LIB_ID_FREE_NO_MAIN, false);
	//BKE_id_free_ex(bmain, cu, LIB_ID_FREE_NO_MAIN, false);
	//DEG_id_tag_update(&ob->id, ID_RECALC_SELECT);
	//DEG_relations_tag_update(bmain);
	//WM_main_add_notifier(NC_OBJECT | ND_DRAW | NC_WINDOW | ND_MODIFIER, bmain);
	//BKE_id_free(bmain, cu);
	//BKE_collection_object_remove(bmain, collection, ob, true);
	//BKE_curve_batch_cache_free(cu);
	//BKE_nurbList_free(&cu->nurb);
	//DEG_relations_tag_update(bmain);
	//ED_object_base_select(base, BA_SELECT);
	
	//idk if window manager can help? updating window fixes artifact but i can't find a function to solve it.
	
	//collection_tag_update_parent_recursive(bmain, ctx->collection, ID_RECALC_COPY_ON_WRITE);
	//this does plug up memory leaks but we need to have viewport update somehow...
	//still need to free nurb and bp memory...
	
	//DEG_relations_tag_update(bmain);
    //DEG_id_tag_update(&result->id, ID_RECALC_SELECT);
	
	//Base* base = BKE_view_layer_base_find(view_layer, ctx->object);
	//base->flag &= BASE_SELECTED;
	//BKE_scene_set_background(bmain, scene);

	//BKE_view_layer_base_select_and_set_active(view_layer, base);
	//BKE_view_layer_selected_objects_tag()
	//BKE_main_id_tag_all(bmain, LIB_TAG_DOIT, true);
	//BKE_object_handle_update(ctx->depsgraph, scene, ctx->object);
	//DEG_make_active(ctx->depsgraph);
	//BKE_view_layer_base_deselect_all(view_layer);
	//BKE_object_select_update(ctx->depsgraph, ctx->object);
	//DEG_id_tag_update_ex(bmain, &ctx->object->id, ID_RECALC_BASE_FLAGS);
	//*/
	//BKE_object_runtime_free_data(ob);	//try to delete added obj
	//printf("OR HERE\n");
	
	//if (nurbslist.first) {
    //  Nurb *nutmp = nurbslist.first;
	//  BPoint *bptmp = nutmp->bp;
    //  for (int a = 0; a < nu->pntsu * nu->pntsv; a++, bptmp++) {
	//	printf("[i: %d] x: %f y: %f z: %f w: %f\n", a, bptmp->vec[0], bptmp->vec[1], bptmp->vec[2], bptmp->vec[3]);	//bptmp->vec[3]
    //  }
    //}
	
	//printf("vert: %d\n", result->totvert);
	//printf("faces: %d\n", result->totpoly);
	//for (int t = 0; t < result->totvert; t++) {
    //    printf("[index %i] x: %f, y: %f, z: %f\n", t, result->mvert[t].co[0], result->mvert[t].co[1], result->mvert[t].co[2]);
    //}
	
	//BLI_addtail(BKE_curve_nurbs_get(cu), &nurbslist);
	//cu->nurb = nurbslist;
	//BKE_view_layer_base_deselect_all(view_layer);

	//DEG_id_tag_update_ex(bmain, &ob->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_ANIMATION);

	//LayerCollection *layer_collection = BKE_layer_collection_get_active(view_layer);
	//BKE_collection_object_add(bmain, layer_collection->collection, ob);
	
	

	//Base *base = BKE_view_layer_base_find(view_layer, ob);
	
	//Object *ob = BKE_object_add_only_object(bmain, OB_CURVES_LEGACY, ctx->object->id.name + 4);
	//Object *ob = BKE_object_add_from(bmain, scene, view_layer, OB_CURVES_LEGACY, ctx->object->id.name + 4, ctx->object);
	//BKE_object_to_curve_clear(ob);
	//printf("h1\n");
	
	//Curve *cu = BKE_curve_add(bmain, ctx->object->id.name + 4, OB_CURVES_LEGACY);
	//ob->data = cu;
	//printf("h2\n");
	
	//ctx->object = BKE_object_add_only_object(bmain, OB_CURVES_LEGACY, ctx->object->id.name);
	//must free curve when added.
    //cu->flag |= CU_3D;
	//cu->type = CU_NURBS; 	//just bc.
	
	//supposedly good from create_object, usd_reader_nurbs.cc
	//Nurb* nu;
	//BPoint *bp;
	//BKE_mesh_to_curve_nurblist(mesh, &nurbslist, 0);
	//BKE_mesh_to_curve_nurblist(mesh, &nurbslist, 1);
	//nu = (Nurb *)MEM_callocN(sizeof(Nurb), "spline.new");
	
	//BezTriple *bezt = (BezTriple *)MEM_callocN(sizeof(BezTriple), "spline.new.bezt");
    //bezt->radius = 1.0;
    //nu->bezt = bezt;
	
	//bp = (BPoint *)MEM_callocN(sizeof(BPoint), "spline.new.bp");
	//bp->radius = 1.0f;
    //nu->bp = bp;

	//nu->pntsu = 1;
	//nu->pntsv = 1;

	//nu->orderu = nu->orderv = 4;
	//nu->resolu = cu->resolu;
	//nu->resolv = cu->resolv;
	//nu->flag = CU_SMOOTH;

	//BLI_addtail(BKE_curve_nurbs_get(cu), nu);
	//nurbslist.first = nu;
	//BLI_addtail(BKE_curve_nurbs_get(cu), nu);
	//cu->nurb = nurbslist;
	
	//BKE_object_add
	//ctx->object = BKE_object_add_for_data(bmain, view_layer, OB_CURVES_LEGACY, ctx->object->id.name + 2, &ctx->object->id, true);
	//ctx->object = BKE_object_add_only_object(bmain, OB_CURVES_LEGACY, ctx->object->id.name);
	//ob->data = cu; //above allowed this line.
	//printf("h3\n");
	
	//Collection *collection = (Collection *)ctx->object->id;
	//if (BKE_collection_object_add(bmain, layer_collection->collection, ob)) {
	//	printf("added %s", ob->id.name);
	//}
	//Base *base_new = NULL;
	//base_new = BKE_view_layer_base_find(view_layer, ob);
	//base_new = view_layer->basact;
	//DEG_relations_tag_update(bmain); // added object
	
	
	//struct Object *BKE_object_add_from(struct Main *bmain, struct Scene *scene, struct ViewLayer *view_layer, int type, const char *name, struct Object *ob_src)
	//BKE_object_add_for_data
	
	//BKE_mesh_new_nomain_from_curve_displist supposed convert back but unsure if necessary.
	
	
	
	/*
    if (nurbslist.first) {
      Nurb *nu;
      for (nu = nurbslist.first; nu; nu = nu->next) {
        int a;
        BPoint *bp = nu->bp;
		printf("[i: %d] x: %f y: %f z: %f w: %f", i, bp->vec[0], bp->vec[1], bp->vec[2], bp->vec[3]);
		i++;
      }
    }
	*/

	
	//can use this or other methods...
	//Nurb *nu;
	/*
	Nurb *Niter;
	LISTBASE_FOREACH_INDEX(Nurb*, var, &nurbslist, i) {
		if (i == 0) {
			nu = var;
		}
		//var = var->first;
		do {
			printf("[i: %d] x: %f y: %f z: %f w: %f\n", i, var->bp->vec[0], var->bp->vec[1], var->bp->vec[2], var->bp->vec[3]);
			Niter = var->next;
		} while (nu != Niter);
	}
	*/
	//printf("End\n");
	
	//BKE_nurbList_free(&nurbslist);
	
	//BKE_mesh_to_curve_nurblist(me_eval, &nurbslist, 1);
	//BKE_mesh_new_nomain_from_curve(ob);
	
	//find way to return mesh if it's not the first time being applied... maybe not necessary or important, will leave though
	/*
	BM_ITER_MESH_INDEX(bf, &viter, bm, BM_FACES_OF_MESH, i)
	{
		//BM_elem_index_get(v, 5);
		bl = bf->l_first;
		looping = bl;
		if (i == 0) {
			//consider BM_edge_split
			vNew = bmesh_kernel_split_edge_make_vert(bm, bl->v, bl->e, &tmp);
			BLI_assert(vNew != NULL);
			mid_v3_v3v3(col, bl->next->e->v1->co, bl->next->e->v2->co); //bl->e->v1->co
			col[2] = col[2]*1.25f;
			copy_v3_v3(vNew->co, col);
			bmd->applied = 1;
			//vNew->co[0] = col[0];
			//vNew->co[1] = col[1];
			//vNew->co[2] = col[2];
			//bl->next->v->co[0]
			//bmesh_kernel_split_edge_make_vert returns in 'tmp' the edge going from 'vNew' to 'v_b'.
			//BM_face_split(bm, bl, bl->next, bl->next->next, NULL, false);
		}
		do {
			printf("[index: %d] x: %f, y: %f, z: %f\n", i, looping->v->co[0], looping->v->co[1], looping->v->co[2]);
			looping = looping->next;
		} while(looping != bl);
	}
	*/
	
	//result = BKE_mesh_from_bmesh_for_eval_nomain(bm, NULL, mesh);
	BM_mesh_free(bm);

	//BKE_mesh_normals_tag_dirty(result);
	
	/*
	//mesh, (rest counts of) new tot vert, new tot edge, new tot tesselation?, new tot loop, new tot poly
	result = BKE_mesh_new_nomain_from_template(mesh, mesh->totvert + 1, mesh->totedge + (1*4), 0, mesh->totloop + (4*2), mesh->totpoly + (4-1));
	//^trying to make square with new vertex above square like a triangle above square.
	// Copy original vertex data 
	CustomData_copy_data(&mesh->vdata, &result->vdata, 0, 0, mesh->totvert);
	// Copy original edge/poly/loop data
	CustomData_copy_data(&mesh->edata, &result->edata, 0, 0, mesh->totedge);
	CustomData_copy_data(&mesh->pdata, &result->pdata, 0, 0, mesh->totpoly);
	CustomData_copy_data(&mesh->ldata, &result->ldata, 0, 0, mesh->totloop);
	//does copy and adds extra space after the fact, all init to 0.
	//I assume u simply edit the vert, loop, edge, and poly arrs and it will be good... -BR 3/14
	
	//now add edits
	*/
	
	
	/*
    printf("This object has %d vertices and %d faces.\n", mesh->totvert, mesh->totpoly);
    for (int i = 0; i < result->totvert; i++) {
        printf("[index %i] x: %f, y: %f, z: %f\n", i, result->mvert[i].co[0], result->mvert[i].co[1], result->mvert[i].co[2]);
    }
    for (int i = 0; i < result->totpoly; i++) {
        printf("[face %d]: (start, totloop) %d, %d\n", i, result->mpoly[i].loopstart, result->mpoly[i].totloop);
        
        if (result->mpoly[i].loopstart == 0) {
            for (int j = 0; j < result->mpoly[j].totloop; j++) {
                result->mvert[result->mloop[j].v].co[2] = 0.0f;
            }
        }
        /
    }
	
    //
    for (int i = 0; i < result->totloop; i++) {
        printf("[loop %d]: (vert index, edge index) %d, %d\n", i, result->mloop[i].v, result->mloop[i].e);
    }
	for (int i = 0; i < result->totedge; i++) {
        printf("[edge %i] v1: %d v2: %d\n", i, result->medge[i].v1, result->medge[i].v2);
    }
    //
	*/
    //printf("Resolution: %d\n", bmd->resolution);
    return result;

}

//most likely links object of BsplineModifierData, is not used at the moment.
static void foreachIDLink(ModifierData *md, Object *ob, IDWalkFunc walk, void *userData)
{
  BsplineModifierData *bmd = (BsplineModifierData *)md;

  walk(userData, ob, (ID **)&bmd->obj, IDWALK_CB_NOP);
}

//does nothing useful, can removed. This relates to foreachIDLink
static void updateDepsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
	BsplineModifierData *bmd = (BsplineModifierData *)md;

	if (bmd->obj != NULL) {
		DEG_add_object_relation(ctx->node, bmd->obj, DEG_OB_COMP_TRANSFORM, "Bspline Modifier");
		//DEG_add_object_relation(ctx->node, bmd->object, DEG_OB_COMP_GEOMETRY, "Bspline Modifier");	//maybe need?
		DEG_add_special_eval_flag(ctx->node, &bmd->obj->id, DAG_EVAL_NEED_CURVE_PATH);				//does it tell it needs to be curve?
	}

	DEG_add_modifier_to_transform_relation(ctx->node, "Bspline Modifier");
}



//draws UI 
static void panel_draw(const bContext *UNUSED(C), Panel *panel)
{
    uiLayout *row, *sub;
    uiLayout *layout = panel->layout;

    PointerRNA ob_ptr;
    PointerRNA *ptr = modifier_panel_get_property_pointers(panel, NULL); //second parm can be NULL or &ob_ptr, unsure of purpose atm 3/11 -BR
    //hardstuck trying to get modifier menu for this to work for several hours now... 3/11 -BR

    //start
    uiLayoutSetPropSep(layout, true);
    //int lowercase = RNA_enum_get(ptr, "lowercase");
    row = uiLayoutRowWithHeading(layout, true, IFACE_("Resolution"));
    uiItemR(row, ptr, "resolution", 0, NULL, ICON_NONE);
    //text to the left of settings 
    
    //uiLayout *col = uiLayoutColumn(layout, true);
    //uiItemR(col, ptr, "lowercase", 0, IFACE_("Degree"), ICON_NONE);

    //if we need to do anything else to Bspline
    //also we can get mesh from object
    //BsplineModifierData *bmd = ptr->data;
    //Object *ob = ob_ptr.data;
    //Mesh *mesh = ob->data;

    modifier_panel_end(layout, ptr);
    //end
}

//necessary to even have panel...
static void panelRegister(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Bspline, panel_draw);
}

//attributes here refers to a function call that is defined here, if NULL, either no not used in this modifier or not linked to alternative functions.
ModifierTypeInfo modifierType_Bspline = {	//ones that are "unused" don't do anything meaningful at the moment.
    /* name */ "Bspline",
    /* structName */ "BsplineModifierData",
    /* structSize */ sizeof(BsplineModifierData),
    /* srna */ &RNA_BsplineModifier,
    /* type */ eModifierTypeType_Constructive,
    /* flags */ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /* icon */ ICON_MOD_SUBSURF,

    /* copyData */ BKE_modifier_copydata_generic,	//uses alternative default for copy, could create own but this is fine for now.

    /* deformVerts */ NULL,
    /* deformMatrices */ NULL,
    /* deformVertsEM */ NULL,
    /* deformMatricesEM */ NULL,
    /* modifyMesh */ modifyMesh,
    /* modifyGeometrySet */ NULL,

    /* initData */ initData,
    /* requiredDataMask */ NULL,
    /* freeData */ freeData,
    /* isDisabled */ NULL,
    /* updateDepsgraph */ NULL,	//unused
    /* dependsOnTime */ NULL,
    /* dependsOnNormals */ NULL,
    /* foreachIDLink */ NULL,		//unused
    /* foreachTexLink */ NULL,
    /* freeRuntimeData */ NULL,
    /* panelRegister */ panelRegister,
    /* blendWrite */ NULL,
    /* blendRead */ NULL,
};