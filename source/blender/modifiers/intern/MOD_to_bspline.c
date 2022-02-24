#include "BKE_modifier.h"
#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"	
//extra?
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "DNA_defaults.h"
#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"

#include "BKE_context.h"
#include "BKE_editmesh.h"
#include "BKE_mesh.h"
#include "BKE_scene.h"
#include "BKE_screen.h"
#include "BKE_subdiv.h"
#include "BKE_subdiv_ccg.h"
#include "BKE_subdiv_deform.h"
#include "BKE_subdiv_mesh.h"
#include "BKE_subdiv_modifier.h"
#include "BKE_subsurf.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "RE_engine.h"

#include "RNA_access.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "MOD_modifiertypes.h"
#include "MOD_ui_common.h"

#include "BLO_read_write.h"

#include "intern/CCGSubSurf.h"

//need to add BplineModifierData struct
/* add this to source/blender/makesdna/ in file DNA_modifier_types.h
typedef struct BsplineModifierData {
  ModifierData modifier;
  //int number;
  //int _pad0; //pad out the struct in muliples of 8 bytes
} BsplineModifierData;
*/ //done
//last left off on the RNA section of the article. 2/9/22 (3:10 AM)
//basic/minimal appears and is recognized by blender 2/23/22

//supposed entry function
static Mesh *modifyMesh(struct ModifierData *md,
                                 const struct ModifierEvalContext *ctx,
                                 struct Mesh *mesh)
{
	//BsplineModifierData *bmd = (BsplineModifierData*)md;
	//Mesh *returner = BKE_mesh_new_nomain(bmd.out_totverts, 0, bmd.out_totfaces, 0, 0);
	//for (int i = 0; i < bmd.out_totverts; i++)
	
    //printf("This object has " + &mesh->out_totverts + " vertices and " + &mesh->out_totfaces" faces.");
	printf("Hello World\n");
    return mesh;
}

ModifierTypeInfo modifierType_Bspline = {
    /* name */ "Bspline",
    /* structName */ "BsplineModifierData",
    /* structSize */ sizeof(BsplineModifierData),
    /* srna */ &RNA_BsplineModifier,
    /* type */ eModifierTypeType_Constructive,
    /* flags */ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsMapping |
        eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode |
        eModifierTypeFlag_AcceptsCVs,
    /* icon */ NULL,

    /* copyData */ NULL,

    /* deformVerts */ NULL,
    /* deformMatrices */ NULL,
    /* deformVertsEM */ NULL,
    /* deformMatricesEM */ NULL,
    /* modifyMesh */ modifyMesh,
    /* modifyHair */ NULL,
    /* modifyGeometrySet */ NULL,

    /* initData */ NULL,
    /* requiredDataMask */ NULL,
    /* freeData */ NULL,
    /* isDisabled */ NULL,
    /* updateDepsgraph */ NULL,
    /* dependsOnTime */ NULL,
    /* dependsOnNormals */ NULL,
    /* foreachIDLink */ NULL,
    /* foreachTexLink */ NULL,
    /* freeRuntimeData */ NULL,
    /* panelRegister */ NULL,
    /* blendWrite */ NULL,
    /* blendRead */ NULL,
};