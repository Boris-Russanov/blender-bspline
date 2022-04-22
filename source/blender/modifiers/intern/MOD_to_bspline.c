#include "BKE_modifier.h"
#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"	
//extra?
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
#include "BKE_object.h"
#include "BKE_layer.h"
#include "BKE_lib_id.h"
#include "BKE_lib_query.h"
#include "BKE_mesh_runtime.h"
#include "BKE_main.h"

//#inlcude "BKE_to_bspline_Halfedge.h"	//name instead of bspline?
//#inlcude "BKE_to_bspline_helper.h"	//name instead of bspline?

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

#include "intern/CCGSubSurf.h"


//need to add BsplineModifierData struct
/* add this to source/blender/makesdna/ in file DNA_modifier_types.h
typedef struct BsplineModifierData {
  ModifierData modifier;
  //int number;
  //int _pad0; //pad out the struct in muliples of 8 bytes
} BsplineModifierData;
*/ //done
//last left off on the RNA section of the article. 2/9/22
//basic/minimal appears and is recognized by blender 2/23/22
//not accurate anymore, update later 4/8

static void initData(ModifierData *md)
{
  BsplineModifierData *bmd = (BsplineModifierData *)md;

  BLI_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(bmd, modifier));
  
  //errors here due to memcpy giving EXCEPTION_ACCESS_VIOLATION. Must not have completely finished making the modifer %100 in line with blender... -BR 3/12
  //^above resolved, needed to finish default values for modifier data in dna_modifier_defaults.h + link in dna_defaults.c -BR 3/13
  MEMCPY_STRUCT_AFTER(bmd, DNA_struct_default_get(BsplineModifierData), modifier);
}

static void freeData(ModifierData *md) {
	BsplineModifierData *bmd = (BsplineModifierData *)md;
	//DNE, make ur own freeRuntimeData.
	//freeRuntimeData(bmd->modifier.runtime);
}


//supposed entry function
static Mesh *modifyMesh(struct ModifierData *md,
                                 const struct ModifierEvalContext *ctx,
                                 struct Mesh *mesh)
{
	printf("STARTING\n");
    Mesh *result = mesh;
    BMesh *bm;
	BMesh *bmcpy;
    BMIter viter;
    BMVert *v, *v_next;
	BMFace *bf;
	BMLoop *bl;
	BMLoop *looping;
	BMLoop **inp;
	BMEdge *be;
	BMEdge *tmp;
	BsplineModifierData *bmd = (BsplineModifierData*)md;
	
    if (bmd->degree == 1) {
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
	
	//RegPatchConstructor_is_same_type(vert);
	/*	//iter examples.
	BM_ITER_MESH(v, &viter, bm, BM_VERTS_OF_MESH)
	{
		//if (i == number) {
			vNew = v;
			printf("[index: %d] x: %f, y: %f, z: %f\n", i, v->co[0], v->co[1], v->co[2]);
			return;
		//}
		i++;
	}
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
	
	
	
	
	//look into NURBS?
	//consider: BKE_mesh_to_curve or BKE_mesh_to_curve_nurblist for conv mesh to curve
	//consider: mesh_new_from_curve_type_object or BKE_mesh_new_nomain_from_curve to get mesh from curve.
	//-BR 3/16
	
	ListBase nurbslist = {NULL, NULL};
	struct Mesh *me_eval;
	Object *ob_eval = DEG_get_evaluated_object(ctx->depsgraph, ctx->object);
	Scene *scene = DEG_get_evaluated_scene(ctx->depsgraph);
	Main *bmain = DEG_get_bmain(ctx->depsgraph);
	//ViewLayer *view_layer = DEG_get_evaluated_view_layer(ctx->depsgraph);	//DEG_get_input_view_layer(ctx->depsgraph);
	ViewLayer *view_layer = BKE_view_layer_default_view(scene);
	LayerCollection *layer_collection = BKE_layer_collection_get_active(view_layer);
	//layer_collection->collection.
	//printf("name of layer: %s", view_layer->name);
    //me_eval = BKE_object_get_evaluated_mesh(ob_eval);
    //if (me_eval == NULL) {
    //  Scene *scene_eval = (Scene *)DEG_get_evaluated_id(depsgraph, &scene->id);
    //  me_eval = mesh_get_eval_final(depsgraph, scene_eval, ob_eval, &CD_MASK_BAREMESH);
    //}
	printf("hello??\n");
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
	Object *ob = BKE_object_add_only_object(bmain, OB_CURVES_LEGACY, "obj_nurb");	//ctx->object->id.name + 1
	//Object *ob = bmd->obj;			//DEG_add_object_relation(ctx->node, bmd->obj, DEG_OB_COMP_TRANSFORM, "Bspline Modifier");
	//can only read not write bmd->whatever.
	Curve *cu = NULL;
	//if (ob != NULL) {
		//printf("ob is not null\n");
		//bmd->obj = BKE_object_add_only_object(bmain, OB_CURVES_LEGACY, "Bspline_obj");
		//Curve *cu = BKE_curve_add(bmain, "bspline_curve", CU_NURBS);
		//return mesh;
		//return BKE_mesh_new_from_object(ctx->depsgraph, ob, 0, 0);
	//}
	printf("applied %d\n", bmd->applied);
	//ob = BKE_object_add_only_object(bmain, OB_CURVES_LEGACY, "curve_nurb");
	cu = BKE_curve_add(bmain, "bspline_curve", CU_NURBS);
	//ob->data = BKE_object_obdata_add_from_type(bmain, OB_CURVES_LEGACY, ctx->object->id.name + 1);
	//Curve *cu = BKE_curve_add(bmain, "bspline_curve", CU_NURBS);
	//must free curve when added.
    
	cu->flag |= CU_3D;
	cu->type = CU_NURBS; 	//just bc.
	
	//supposedly good from create_object, usd_reader_nurbs.cc
	Nurb* nu;
	BPoint *bp;
	
	//unsure if converstion needed.
	//BKE_mesh_to_curve_nurblist(mesh, &nurbslist, 0);
	//BKE_mesh_to_curve_nurblist(mesh, &nurbslist, 1);
	//id_us_min(&((Mesh *)ob->data)->id);
	
	
	nu = (Nurb *)MEM_callocN(sizeof(Nurb), "spline.new");
	
	//BezTriple *bezt = (BezTriple *)MEM_callocN(sizeof(BezTriple), "spline.new.bezt");
    //bezt->radius = 1.0;
    //nu->bezt = bezt;
	
	bp = (BPoint *)MEM_callocN(4 * sizeof(BPoint), "spline.new.bp"); //add 4 BPoints.
	bp->radius = 1.0f;
	
    nu->bp = bp;
	
	//have more than one point bc u just have one point.
	// 2x2 = square.
	nu->pntsu = 2;
	nu->pntsv = 2;
	//3x3 due to res.
	nu->orderu = nu->orderv = 2;
	nu->resolu = cu->resolu = 2;
	nu->resolv = cu->resolv = 2;
	nu->flag = CU_SMOOTH;
	nu->flagu = CU_NURB_ENDPOINT;	//CU_NURB_ENDPOINT
	nu->flagv = CU_NURB_ENDPOINT;
	nu->type = CU_NURBS; //CU_NURBS; CU_PRIM_PATCH;
	//type nurbs so it can be converted.
	nurbslist.first = nu;
	//nurbslist.last = nu;
	switch (bmd->degree) {
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
	
	//if add.
	//void BKE_curve_nurbs_vert_coords_apply(ListBase *lb, const float (*vert_coords)[3], const bool constrain_2d);
	//float verts[27] = {-1.0f, -1.0f, 1.0f,		-1.0f, 0.0f, 0.0f,		-1.0f, 1.0f, 0.0f,
	//					0.0f, -1.0f, 0.0f,		0.0f, 0.0f, 0.0f,		0.0f, 1.0f, 0.0f,
	//					1.0f, -1.0f, 0.0f,		1.0f, 0.0f, 0.0f,		1.0f, 1.0f, 0.0f,};
	float verts[12] = {1.0f, 1.0f, 1.0f,		-1.0f, 1.0f, -1.0f,		1.0f, -1.0f, -1.0f, 				-1.0f, -1.0f, -1.0f};
	///*
	BM_ITER_MESH_INDEX(v, &viter, bm, BM_VERTS_OF_MESH, counter)
	{
		if (v->co[0] == 0.0f && v->co[1] == 0.0f && v->co[2] == 0.0f) {	//v->co[0] == 0.0f && v->co[1] == 0.0f && v->co[2] == 0.0f RegPatchConstructor_is_same_type(v) == true
			//float** finalMAT = RegPatchConstructor_get_patch(v);
			//BsplinePatch patch = RegPatchConstructor_get_patch(v);
			//float* bspline_coefs_arr = Helper_convert_verts_from_matrix_to_list(finalMAT, 9);
			//BMVert* nb_verts = RegPatchConstructor_get_neighbor_verts(v);
			//for (int i = 0; i < 9; i++) {
			//	for (int j = 0; j < 3; j++) {
			//		if (isnan(nb_verts[i].co[j]) == true || nb_verts[i].co[j] < -10000000 || nb_verts[i].co[j] > 10000000) {
			//			printf("lol thx blender\n");
			//			nb_verts[i].co[j] = 0.0f;
			//		}
			//	}
			//}
			//BMVert* nb_verts = rando_giv(verts);	//MEM_calloc_arrayN(4, sizeof(BMVert), "lol_mem");
			//for (int i = 0; i < 4; i++) {
			//	printf("%f\n", nb_verts[i].co[0]);
			//	printf("%f\n", nb_verts[i].co[1]);
			//	printf("%f\n", nb_verts[i].co[2]);
			//}
			//MEM_freeN(nb_verts);
			//free(nb_verts);
			//printf("alive\n");
			//BKE_curve_nurbs_vert_coords_apply(&nurbslist, bspline_coefs_arr, false);
			//BPoint *bp1 = nu->bp;
			//for (int i = 0; i < 9; i++) {
				//float coords[3] = {0};
				//for (int j = 0; j < 3; j++) {
					//coords[j] = finalMAT[i][j];
					//printf("final[%d][%d] = %f\n", i, j, finalMAT[i][j]);
				//}
				//copy_v3_v3(bp1->vec, coords);
			//}
			//MEM_printmemlist();
			//for (int i = 0; i < 9; i++) {
				//for (int j = 0; j < 3; j++) {
					//nu->bp[i].vec[j] = finalMAT[i][j];
					//printf("nu->bp[%d].vec[%d] = %f\n", i, j, nu->bp[i].vec[j]);
				//}
				//nu->bp[i].vec[3] = 0.0f;
			//}
			//for (int i = 0; i < 9; i++) {
				//MEM_freeN(finalMAT[i]);
			//}
			//MEM_freeN(finalMAT);
			//MEM_freeN(bspline_coefs_arr);
			//BKE_curve_nurbs_vert_coords_apply(&nurbslist, bspline_coefs_arr, NULL); //or 0 for last param.
		}
		//if (v->co[0] == 0.0f && v->co[1] == 0.0f && v->co[2] == 0.0f) {
		//	printf("%d\n", RegPatchConstructor_is_same_type(v));
		//	printf("false case: %d\n", false);
		//}
	}
	//*/
	for (int i = 0; i < mesh->totvert; i++) {
		verts[i*3] = mesh->mvert[i].co[0];
		verts[(3*i)+1] = mesh->mvert[i].co[1];
		verts[(3*i)+2] = mesh->mvert[i].co[2];
	}
	BKE_curve_nurbs_vert_coords_apply(&nurbslist, verts, false);
	//BKE_curve_nurbs_vert_coords_apply(&nurbslist, verts, NULL); //or 0 for last param.
	bp[0].vec[3] = 1.0f;
	bp[1].vec[3] = 1.0f;
	bp[2].vec[3] = 1.0f;
	bp[3].vec[3] = 1.0f;
	//bp[4].vec[3] = 1.0f;
	//bp[5].vec[3] = 1.0f;
	//bp[6].vec[3] = 1.0f;
	//bp[7].vec[3] = 1.0f;
	//bp[8].vec[3] = 1.0f;
	nu->hide = 0;	//show up pls.
	BKE_nurb_knot_calc_u(nu);
    BKE_nurb_knot_calc_v(nu);
	
	cu->nurb = nurbslist;
	
	ob->data = cu;
    ob->type = OB_SURF; //OB_CURVES	OB_CURVES_LEGACY
	
	//added mesh to obj for test..
	//ob = BKE_object_add_only_object(bmain, OB_EMPTY, ctx->object->id.name + 1);
	//BKE_mesh_assign_object(bmain, ob, mesh);
	
	//don't use this.
	//result = BKE_mesh_new_nomain_from_curve(ob);	// BKE_mesh_new_nomain_from_curve_displist(ob, &nurbslist); doesn't quite work
	
	result = BKE_mesh_new_from_object(ctx->depsgraph, ob, false, false);	//works!!
	printf("here?\n");
	//*/
	
	
	
	
	
	
	
	
	
	
	
	//sigh, day 2 of trying to free mem correctly.
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
	//BKE_object_runtime_free_data(ob);	//try to delete added obj
	printf("OR HERE\n");
	
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
	printf("h3\n");
	
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
	printf("End\n");
	
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
    printf("Degree: %d\n", bmd->degree);
    return result;

}

//most likely links it but unsure how it exactly works.
static void foreachIDLink(ModifierData *md, Object *ob, IDWalkFunc walk, void *userData)
{
  BsplineModifierData *bmd = (BsplineModifierData *)md;

  walk(userData, ob, (ID **)&bmd->obj, IDWALK_CB_NOP);
}

//def important, depsgraph always has issues.
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




static void panel_draw(const bContext *UNUSED(C), Panel *panel)
{
    uiLayout *row, *sub;
    uiLayout *layout = panel->layout;

    PointerRNA ob_ptr;
    PointerRNA *ptr = modifier_panel_get_property_pointers(panel, NULL); //second parm can be NULL or &ob_ptr, unsure of purpose atm 3/11 BR
    //hardstuck trying to get modifier menu for this to work for several hours now... 3/11 BR 

    //start
    uiLayoutSetPropSep(layout, true);
    //int lowercase = RNA_enum_get(ptr, "lowercase");
    row = uiLayoutRowWithHeading(layout, true, IFACE_("Degree"));
    uiItemR(row, ptr, "degree", 0, NULL, ICON_NONE);
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

//needed to even have panel...
static void panelRegister(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Bspline, panel_draw);
}

ModifierTypeInfo modifierType_Bspline = {
    /* name */ "Bspline",
    /* structName */ "BsplineModifierData",
    /* structSize */ sizeof(BsplineModifierData),
    /* srna */ &RNA_BsplineModifier,
    /* type */ eModifierTypeType_Constructive,
    /* flags */ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode,
    /* icon */ ICON_MOD_SUBSURF,

    /* copyData */ BKE_modifier_copydata_generic,

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
    /* updateDepsgraph */ updateDepsgraph,
    /* dependsOnTime */ NULL,
    /* dependsOnNormals */ NULL,
    /* foreachIDLink */ foreachIDLink,
    /* foreachTexLink */ NULL,
    /* freeRuntimeData */ NULL,
    /* panelRegister */ panelRegister,
    /* blendWrite */ NULL,
    /* blendRead */ NULL,
};