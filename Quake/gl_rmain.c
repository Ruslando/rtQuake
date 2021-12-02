/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// r_main.c

#include "quakedef.h"

qboolean	r_cache_thrash;		// compatability

vec3_t		modelorg, r_entorigin;
entity_t	*currententity;

int			r_visframecount;	// bumped when going to a new PVS
int			r_framecount;		// used for dlight push checking

mplane_t	frustum[4];

int			render_pass_index;
qboolean	render_warp;
int			render_scale;

//johnfitz -- rendering statistics
unsigned int rs_brushpolys, rs_aliaspolys, rs_skypolys, rs_particles, rs_fogpolys;
unsigned int rs_dynamiclightmaps, rs_brushpasses, rs_aliaspasses, rs_skypasses;
float rs_megatexels;

//
// view origin
//
vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;

float r_fovx, r_fovy; //johnfitz -- rendering fov may be different becuase of r_waterwarp

//
// screen size info
//
refdef_t	r_refdef;

mleaf_t		*r_viewleaf, *r_oldviewleaf;

int		d_lightstylevalue[256];	// 8.8 fraction of base light value


cvar_t	r_drawentities = {"r_drawentities","1",CVAR_NONE};
cvar_t	r_drawviewmodel = {"r_drawviewmodel","1",CVAR_NONE};
cvar_t	r_speeds = {"r_speeds","0",CVAR_NONE};
cvar_t	r_pos = {"r_pos","0",CVAR_NONE};
cvar_t	r_fullbright = {"r_fullbright","0",CVAR_NONE};
cvar_t	r_lightmap = {"r_lightmap","0",CVAR_NONE};
cvar_t	r_wateralpha = {"r_wateralpha","1",CVAR_ARCHIVE};
cvar_t	r_dynamic = {"r_dynamic","1",CVAR_ARCHIVE};
cvar_t	r_novis = {"r_novis","0",CVAR_ARCHIVE};
#if defined(USE_SIMD)
cvar_t	r_simd = {"r_simd","1",CVAR_ARCHIVE};
#endif

cvar_t	gl_finish = {"gl_finish","0",CVAR_NONE};
cvar_t	gl_polyblend = {"gl_polyblend","1",CVAR_NONE};
cvar_t	gl_nocolors = {"gl_nocolors","0",CVAR_NONE};

//johnfitz -- new cvars
cvar_t	r_clearcolor = {"r_clearcolor","2",CVAR_ARCHIVE};
cvar_t	r_fastclear = {"r_fastclear","1",CVAR_ARCHIVE};
cvar_t	r_flatlightstyles = {"r_flatlightstyles", "0", CVAR_NONE};
cvar_t	gl_fullbrights = {"gl_fullbrights", "1", CVAR_ARCHIVE};
cvar_t	gl_farclip = {"gl_farclip", "16384", CVAR_ARCHIVE};
cvar_t	r_oldskyleaf = {"r_oldskyleaf", "0", CVAR_NONE};
cvar_t	r_drawworld = {"r_drawworld", "1", CVAR_NONE};
cvar_t	r_showtris = {"r_showtris", "0", CVAR_NONE};
cvar_t	r_showbboxes = {"r_showbboxes", "0", CVAR_NONE};
cvar_t	r_lerpmodels = {"r_lerpmodels", "1", CVAR_NONE};
cvar_t	r_lerpmove = {"r_lerpmove", "1", CVAR_NONE};
cvar_t	r_nolerp_list = {"r_nolerp_list", "progs/flame.mdl,progs/flame2.mdl,progs/braztall.mdl,progs/brazshrt.mdl,progs/longtrch.mdl,progs/flame_pyre.mdl,progs/v_saw.mdl,progs/v_xfist.mdl,progs/h2stuff/newfire.mdl", CVAR_NONE};

extern cvar_t	r_vfog;
//johnfitz

cvar_t	gl_zfix = {"gl_zfix", "1", CVAR_ARCHIVE}; // QuakeSpasm z-fighting fix

cvar_t	r_lavaalpha = {"r_lavaalpha","0",CVAR_NONE};
cvar_t	r_telealpha = {"r_telealpha","0",CVAR_NONE};
cvar_t	r_slimealpha = {"r_slimealpha","0",CVAR_NONE};

float	map_wateralpha, map_lavaalpha, map_telealpha, map_slimealpha;
float	map_fallbackalpha;

qboolean r_drawworld_cheatsafe, r_fullbright_cheatsafe, r_lightmap_cheatsafe; //johnfitz

cvar_t	r_scale = {"r_scale", "1", CVAR_ARCHIVE};

/*
=================
R_CullBox -- johnfitz -- replaced with new function from lordhavoc

Returns true if the box is completely outside the frustum
=================
*/
qboolean R_CullBox (vec3_t emins, vec3_t emaxs)
{
	int i;
	mplane_t *p;
	byte signbits;
	float vec[3];
	for (i = 0;i < 4;i++)
	{
		p = frustum + i;
		signbits = p->signbits;
		vec[0] = ((signbits % 2)<1) ? emaxs[0] : emins[0];
		vec[1] = ((signbits % 4)<2) ? emaxs[1] : emins[1];
		vec[2] = ((signbits % 8)<4) ? emaxs[2] : emins[2];
		if (p->normal[0]*vec[0] + p->normal[1]*vec[1] + p->normal[2]*vec[2] < p->dist)
			return true;
	}
	return false;
}
/*
===============
R_CullModelForEntity -- johnfitz -- uses correct bounds based on rotation
===============
*/
qboolean R_CullModelForEntity (entity_t *e)
{
	vec3_t mins, maxs;

	if (e->angles[0] || e->angles[2]) //pitch or roll
	{
		VectorAdd (e->origin, e->model->rmins, mins);
		VectorAdd (e->origin, e->model->rmaxs, maxs);
	}
	else if (e->angles[1]) //yaw
	{
		VectorAdd (e->origin, e->model->ymins, mins);
		VectorAdd (e->origin, e->model->ymaxs, maxs);
	}
	else //no rotation
	{
		VectorAdd (e->origin, e->model->mins, mins);
		VectorAdd (e->origin, e->model->maxs, maxs);
	}

	return R_CullBox (mins, maxs);
}

/*
===============
R_RotateForEntity -- johnfitz -- modified to take origin and angles instead of pointer to entity
===============
*/
#define DEG2RAD( a ) ( (a) * M_PI_DIV_180 )
void R_RotateForEntity (float matrix[16], vec3_t origin, vec3_t angles)
{
	float translation_matrix[16];
	TranslationMatrix (translation_matrix, origin[0], origin[1], origin[2]);
	MatrixMultiply (matrix, translation_matrix);

	float rotation_matrix[16];
	RotationMatrix (rotation_matrix, DEG2RAD(angles[1]), 0, 0, 1);
	MatrixMultiply (matrix, rotation_matrix);
	RotationMatrix (rotation_matrix, DEG2RAD(-angles[0]), 0, 1, 0);
	MatrixMultiply (matrix, rotation_matrix);
	RotationMatrix (rotation_matrix, DEG2RAD(angles[2]), 1, 0, 0);
	MatrixMultiply (matrix, rotation_matrix);
}

//==============================================================================
//
// SETUP FRAME
//
//==============================================================================

int SignbitsForPlane (mplane_t *out)
{
	int	bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j=0 ; j<3 ; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1<<j;
	}
	return bits;
}

/*
===============
TurnVector -- johnfitz

turn forward towards side on the plane defined by forward and side
if angle = 90, the result will be equal to side
assumes side and forward are perpendicular, and normalized
to turn away from side, use a negative angle
===============
*/
#define DEG2RAD( a ) ( (a) * M_PI_DIV_180 )
void TurnVector (vec3_t out, const vec3_t forward, const vec3_t side, float angle)
{
	float scale_forward, scale_side;

	scale_forward = cos( DEG2RAD( angle ) );
	scale_side = sin( DEG2RAD( angle ) );

	out[0] = scale_forward*forward[0] + scale_side*side[0];
	out[1] = scale_forward*forward[1] + scale_side*side[1];
	out[2] = scale_forward*forward[2] + scale_side*side[2];
}

/*
===============
R_SetFrustum -- johnfitz -- rewritten
===============
*/
void R_SetFrustum (float fovx, float fovy)
{
	int		i;

	TurnVector(frustum[0].normal, vpn, vright, fovx/2 - 90); //right plane
	TurnVector(frustum[1].normal, vpn, vright, 90 - fovx/2); //left plane
	TurnVector(frustum[2].normal, vpn, vup, 90 - fovy/2); //bottom plane
	TurnVector(frustum[3].normal, vpn, vup, fovy/2 - 90); //top plane

	for (i=0 ; i<4 ; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal); //FIXME: shouldn't this always be zero?
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}

/*
=============
GL_FrustumMatrix
=============
*/
#define NEARCLIP 4
static void GL_FrustumMatrix(float matrix[16], float fovx, float fovy)
{
	const float w = 1.0f / tanf(fovx * 0.5f);
	const float h = 1.0f / tanf(fovy * 0.5f);

	// reduce near clip distance at high FOV's to avoid seeing through walls
	const float d = 12.f * q_min(w, h);
	const float n = CLAMP(0.5f, d, NEARCLIP);
	const float f = gl_farclip.value;

	memset(matrix, 0, 16 * sizeof(float));

	// First column
	matrix[0*4 + 0] = w;

	// Second column
	matrix[1*4 + 1] = -h;
	
	// Third column
	matrix[2*4 + 2] = f / (f - n) - 1.0f;
	matrix[2*4 + 3] = -1.0f;

	// Fourth column
	matrix[3*4 + 2] = (n * f) / (f - n);
}

/*
=============
R_SetupMatrix_RTX
=============
*/
void R_SetupMatrix_RTX()
{
	// Projection matrix
	GL_FrustumMatrix(vulkan_globals.projection_matrix, DEG2RAD(r_fovx), DEG2RAD(r_fovy));

	// View matrix
	float rotation_matrix[16];
	RotationMatrix(vulkan_globals.view_matrix, -M_PI / 2.0f, 1.0f, 0.0f, 0.0f);
	RotationMatrix(rotation_matrix, M_PI / 2.0f, 0.0f, 0.0f, 1.0f);
	MatrixMultiply(vulkan_globals.view_matrix, rotation_matrix);
	RotationMatrix(rotation_matrix, DEG2RAD(-r_refdef.viewangles[2]), 1.0f, 0.0f, 0.0f);
	MatrixMultiply(vulkan_globals.view_matrix, rotation_matrix);
	RotationMatrix(rotation_matrix, DEG2RAD(-r_refdef.viewangles[0]), 0.0f, 1.0f, 0.0f);
	MatrixMultiply(vulkan_globals.view_matrix, rotation_matrix);
	RotationMatrix(rotation_matrix, DEG2RAD(-r_refdef.viewangles[1]), 0.0f, 0.0f, 1.0f);
	MatrixMultiply(vulkan_globals.view_matrix, rotation_matrix);

	float translation_matrix[16];
	TranslationMatrix(translation_matrix, -r_refdef.vieworg[0], -r_refdef.vieworg[1], -r_refdef.vieworg[2]);
	MatrixMultiply(vulkan_globals.view_matrix, translation_matrix);

	static camera_pushconstants_t inverse_matrices;

	InverseMatrix(vulkan_globals.view_matrix, inverse_matrices.view_inverse);
	InverseMatrix(vulkan_globals.projection_matrix, inverse_matrices.proj_inverse);

	/*R_BindPipeline(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vulkan_globals.raygen_pipeline);
	R_PushConstants(VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR, 0, sizeof(inverse_matrices), &inverse_matrices);*/
}

/*
=============
R_SetupMatrix
=============
*/
void R_SetupMatrix (void)
{
	GL_Viewport(glx + r_refdef.vrect.x,
				gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height,
				r_refdef.vrect.width,
				r_refdef.vrect.height,
				0.0f, 1.0f);

	// Projection matrix
	GL_FrustumMatrix(vulkan_globals.projection_matrix, DEG2RAD(r_fovx), DEG2RAD(r_fovy));

	// View matrix
	float rotation_matrix[16];
	RotationMatrix(vulkan_globals.view_matrix, -M_PI / 2.0f, 1.0f, 0.0f, 0.0f);
	RotationMatrix(rotation_matrix,  M_PI / 2.0f, 0.0f, 0.0f, 1.0f);
	MatrixMultiply(vulkan_globals.view_matrix, rotation_matrix);
	RotationMatrix(rotation_matrix, DEG2RAD(-r_refdef.viewangles[2]), 1.0f, 0.0f, 0.0f);
	MatrixMultiply(vulkan_globals.view_matrix, rotation_matrix);
	RotationMatrix(rotation_matrix, DEG2RAD(-r_refdef.viewangles[0]), 0.0f, 1.0f, 0.0f);
	MatrixMultiply(vulkan_globals.view_matrix, rotation_matrix);
	RotationMatrix(rotation_matrix, DEG2RAD(-r_refdef.viewangles[1]), 0.0f, 0.0f, 1.0f);
	MatrixMultiply(vulkan_globals.view_matrix, rotation_matrix);
	
	float translation_matrix[16];
	TranslationMatrix(translation_matrix, -r_refdef.vieworg[0], -r_refdef.vieworg[1], -r_refdef.vieworg[2]);
	MatrixMultiply(vulkan_globals.view_matrix, translation_matrix);

	// View projection matrix
	memcpy(vulkan_globals.view_projection_matrix, vulkan_globals.projection_matrix, 16 * sizeof(float));
	MatrixMultiply(vulkan_globals.view_projection_matrix, vulkan_globals.view_matrix);

	R_BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_blend_pipeline[render_pass_index]);
	R_PushConstants(VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16 * sizeof(float), vulkan_globals.view_projection_matrix);
}

/*
===============
R_SetupScene
===============
*/
void R_SetupScene (void)
{
	render_pass_index = 0;
	qboolean screen_effects = render_warp || (render_scale >= 2);
	vkCmdBeginRenderPass(vulkan_globals.command_buffer, &vulkan_globals.main_render_pass_begin_infos[screen_effects ? 1 : 0], VK_SUBPASS_CONTENTS_INLINE);

	R_SetupMatrix ();
}

/*
===============
R_SetupView
===============
*/
void R_SetupView_RTX(void)
{
	R_PushDlights();
	R_AnimateLight();
	r_framecount++;

	// build the transformation matrix for the given view angles
	VectorCopy(r_refdef.vieworg, r_origin);
	AngleVectors(r_refdef.viewangles, vpn, vright, vup);

	// current viewleaf
	r_oldviewleaf = r_viewleaf;
	r_viewleaf = Mod_PointInLeaf(r_origin, cl.worldmodel);

	V_SetContentsColor(r_viewleaf->contents);
	V_CalcBlend();

	r_cache_thrash = false;

	//johnfitz -- calculate r_fovx and r_fovy here
	r_fovx = r_refdef.fov_x;
	r_fovy = r_refdef.fov_y;
	render_warp = false;
	render_scale = (int)r_scale.value;

	R_SetFrustum(r_fovx, r_fovy); //johnfitz -- use r_fov* vars

	R_MarkSurfaces(); //johnfitz -- create texture chains from PVS

	//johnfitz -- cheat-protect some draw modes
	r_fullbright_cheatsafe = false;
	r_lightmap_cheatsafe = false;
	r_drawworld_cheatsafe = true;
	if (cl.maxclients == 1)
	{
		if (!r_drawworld.value) r_drawworld_cheatsafe = false;
		if (r_fullbright.value) r_fullbright_cheatsafe = true;
		else if (r_lightmap.value) r_lightmap_cheatsafe = true;
	}
}

/*
===============
R_SetupView
===============
*/
void R_SetupView (void)
{
	// Need to do those early because we now update dynamic light maps during R_MarkSurfaces
	R_PushDlights ();
	R_AnimateLight ();
	r_framecount++;

	Fog_SetupFrame (); //johnfitz

// build the transformation matrix for the given view angles
	VectorCopy (r_refdef.vieworg, r_origin);
	AngleVectors (r_refdef.viewangles, vpn, vright, vup);

// current viewleaf
	r_oldviewleaf = r_viewleaf;
	r_viewleaf = Mod_PointInLeaf (r_origin, cl.worldmodel);

	V_SetContentsColor (r_viewleaf->contents);
	V_CalcBlend ();

	r_cache_thrash = false;

	//johnfitz -- calculate r_fovx and r_fovy here
	r_fovx = r_refdef.fov_x;
	r_fovy = r_refdef.fov_y;
	render_warp = false;
	render_scale = (int)r_scale.value;

	if (r_waterwarp.value)
	{
		int contents = Mod_PointInLeaf (r_origin, cl.worldmodel)->contents;
		if (contents == CONTENTS_WATER || contents == CONTENTS_SLIME || contents == CONTENTS_LAVA)
		{
			if (r_waterwarp.value == 1)
				render_warp = true;
			else
			{
				//variance is a percentage of width, where width = 2 * tan(fov / 2) otherwise the effect is too dramatic at high FOV and too subtle at low FOV.  what a mess!
				r_fovx = atan(tan(DEG2RAD(r_refdef.fov_x) / 2) * (0.97 + sin(cl.time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
				r_fovy = atan(tan(DEG2RAD(r_refdef.fov_y) / 2) * (1.03 - sin(cl.time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
			}
		}
	}
	//johnfitz

	R_SetFrustum (r_fovx, r_fovy); //johnfitz -- use r_fov* vars

	R_MarkSurfaces (); //johnfitz -- create texture chains from PVS

	R_UpdateWarpTextures (); //johnfitz -- do this before R_Clear

	//johnfitz -- cheat-protect some draw modes
	r_fullbright_cheatsafe = false;
	r_lightmap_cheatsafe = false;
	r_drawworld_cheatsafe = true;
	if (cl.maxclients == 1)
	{
		if (!r_drawworld.value) r_drawworld_cheatsafe = false;
		if (r_fullbright.value) r_fullbright_cheatsafe = true;
		else if (r_lightmap.value) r_lightmap_cheatsafe = true;
	}
	//johnfitz
}

//==============================================================================
//
// RENDER VIEW
//
//==============================================================================

/*
=============
R_DrawEntitiesOnList
=============
*/
void R_DrawEntitiesOnList (qboolean alphapass) //johnfitz -- added parameter
{
	int		i;

	if (!r_drawentities.value)
		return;

	R_BeginDebugUtilsLabel (alphapass ? "Entities Alpha Pass" : "Entities" );
	//johnfitz -- sprites are not a special case
	for (i=0 ; i<cl_numvisedicts ; i++)
	{
		currententity = cl_visedicts[i];

		//johnfitz -- if alphapass is true, draw only alpha entites this time
		//if alphapass is false, draw only nonalpha entities this time
		if ((ENTALPHA_DECODE(currententity->alpha) < 1 && !alphapass) ||
			(ENTALPHA_DECODE(currententity->alpha) == 1 && alphapass))
			continue;

		//johnfitz -- chasecam
		if (currententity == &cl.entities[cl.viewentity])
			currententity->angles[0] *= 0.3;
		//johnfitz

		//spike -- this would be more efficient elsewhere, but its more correct here.
		if (currententity->eflags & EFLAGS_EXTERIORMODEL)
			continue;

		switch (currententity->model->type)
		{
			case mod_alias:
				R_DrawAliasModel (currententity);
				break;
			case mod_brush:
				R_DrawBrushModel (currententity);
				break;
			case mod_sprite:
				R_DrawSpriteModel (currententity);
				break;
		}
	}
	R_EndDebugUtilsLabel();
}

/*
=============
R_DrawViewModel -- johnfitz -- gutted
=============
*/
void R_DrawViewModel (void)
{
	if (!r_drawviewmodel.value || !r_drawentities.value || chase_active.value)
		return;

	if (cl.items & IT_INVISIBILITY || cl.stats[STAT_HEALTH] <= 0)
		return;

	currententity = &cl.viewent;
	if (!currententity->model)
		return;

	//johnfitz -- this fixes a crash
	if (currententity->model->type != mod_alias)
		return;
	//johnfitz

	R_BeginDebugUtilsLabel ("View Model");

	// hack the depth range to prevent view model from poking into walls
	/*GL_Viewport(glx + r_refdef.vrect.x,
				gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height,
				r_refdef.vrect.width,
				r_refdef.vrect.height,
				0.7f, 1.0f);*/
	
	R_DrawAliasModel (currententity);

	/*GL_Viewport(glx + r_refdef.vrect.x,
				gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height,
				r_refdef.vrect.width,
				r_refdef.vrect.height,
				0.0f, 1.0f);*/

	R_EndDebugUtilsLabel ();
}

/*
================
R_EmitWirePoint -- johnfitz -- draws a wireframe cross shape for point entities
================
*/
void R_EmitWirePoint (vec3_t origin)
{
	VkBuffer vertex_buffer;
	VkDeviceSize vertex_buffer_offset;
	basicvertex_t * vertices = (basicvertex_t*)R_VertexAllocate(6 * sizeof(basicvertex_t), &vertex_buffer, &vertex_buffer_offset);
	int size=8;

	vertices[0].position[0] = origin[0]-size;
	vertices[0].position[1] = origin[1];
	vertices[0].position[2] = origin[2];
	vertices[1].position[0] = origin[0]+size;
	vertices[1].position[1] = origin[1];
	vertices[1].position[2] = origin[2];
	vertices[2].position[0] = origin[0];
	vertices[2].position[1] = origin[1]-size;
	vertices[2].position[2] = origin[2];
	vertices[3].position[0] = origin[0];
	vertices[3].position[1] = origin[1]+size;
	vertices[3].position[2] = origin[2];
	vertices[4].position[0] = origin[0];
	vertices[4].position[1] = origin[1];
	vertices[4].position[2] = origin[2]-size;
	vertices[5].position[0] = origin[0];
	vertices[5].position[1] = origin[1];
	vertices[5].position[2] = origin[2]+size;

	vulkan_globals.vk_cmd_bind_vertex_buffers(vulkan_globals.command_buffer, 0, 1, &vertex_buffer, &vertex_buffer_offset);
	vulkan_globals.vk_cmd_draw(vulkan_globals.command_buffer, 6, 1, 0, 0);
}

/*
================
R_EmitWireBox -- johnfitz -- draws one axis aligned bounding box
================
*/
void R_EmitWireBox (vec3_t mins, vec3_t maxs, VkBuffer box_index_buffer, VkDeviceSize box_index_buffer_offset)
{
	VkBuffer vertex_buffer;
	VkDeviceSize vertex_buffer_offset;
	basicvertex_t * vertices = (basicvertex_t*)R_VertexAllocate(8 * sizeof(basicvertex_t), &vertex_buffer, &vertex_buffer_offset);

	for (int i = 0; i < 8; ++i)
	{
		vertices[i].position[0] = ((i % 2) < 1) ? mins[0] : maxs[0];
		vertices[i].position[1] = ((i % 4) < 2) ? mins[1] : maxs[1];
		vertices[i].position[2] = ((i % 8) < 4) ? mins[2] : maxs[2];
	}

	vulkan_globals.vk_cmd_bind_index_buffer(vulkan_globals.command_buffer, box_index_buffer, box_index_buffer_offset, VK_INDEX_TYPE_UINT16);
	vulkan_globals.vk_cmd_bind_vertex_buffers(vulkan_globals.command_buffer, 0, 1, &vertex_buffer, &vertex_buffer_offset);
	vulkan_globals.vk_cmd_draw_indexed(vulkan_globals.command_buffer, 24, 1, 0, 0, 0);
}

static uint16_t box_indices[24] =
{
	0, 1, 2, 3, 4, 5, 6, 7,
	0, 4, 1, 5, 2, 6, 3, 7,
	0, 2, 1, 3, 4, 6, 5, 7
};

/*
================
R_ShowBoundingBoxes -- johnfitz

draw bounding boxes -- the server-side boxes, not the renderer cullboxes
================
*/
void R_ShowBoundingBoxes (void)
{
	extern		edict_t *sv_player;
	vec3_t		mins,maxs;
	edict_t		*ed;
	int			i;

	if (!r_showbboxes.value || cl.maxclients > 1 || !r_drawentities.value || !sv.active)
		return;

	R_BeginDebugUtilsLabel ("show bboxes");
	R_BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.showbboxes_pipeline);

	VkBuffer box_index_buffer;
	VkDeviceSize box_index_buffer_offset;
	uint16_t * indices = (uint16_t *)R_IndexAllocate(24 * sizeof(uint16_t), &box_index_buffer, &box_index_buffer_offset);
	memcpy(indices, box_indices, 24 * sizeof(uint16_t));

	PR_SwitchQCVM(&sv.qcvm);
	for (i=0, ed=NEXT_EDICT(qcvm->edicts) ; i<qcvm->num_edicts ; i++, ed=NEXT_EDICT(ed))
	{
		if (ed == sv_player)
			continue; //don't draw player's own bbox

		if (ed->v.mins[0] == ed->v.maxs[0] && ed->v.mins[1] == ed->v.maxs[1] && ed->v.mins[2] == ed->v.maxs[2])
		{
			//point entity
			R_EmitWirePoint (ed->v.origin);
		}
		else
		{
			//box entity
			VectorAdd (ed->v.mins, ed->v.origin, mins);
			VectorAdd (ed->v.maxs, ed->v.origin, maxs);
			R_EmitWireBox (mins, maxs, box_index_buffer, box_index_buffer_offset);
		}
	}
	PR_SwitchQCVM(NULL);

	Sbar_Changed (); //so we don't get dots collecting on the statusbar
	R_EndDebugUtilsLabel ();
}

/*
================
R_ShowTris -- johnfitz
================
*/
void R_ShowTris(void)
{
	extern cvar_t r_particles;
	int i;

	if (r_showtris.value < 1 || r_showtris.value > 2 || cl.maxclients > 1 || !vulkan_globals.non_solid_fill)
		return;

	R_BeginDebugUtilsLabel ("show tris");
	if (r_drawworld.value)
		R_DrawWorld_ShowTris();

	if (r_drawentities.value)
	{
		for (i=0 ; i<cl_numvisedicts ; i++)
		{
			currententity = cl_visedicts[i];

			if (currententity == &cl.entities[cl.viewentity]) // chasecam
				currententity->angles[0] *= 0.3;

			switch (currententity->model->type)
			{
			case mod_brush:
				R_DrawBrushModel_ShowTris (currententity);
				break;
			case mod_alias:
				R_DrawAliasModel_ShowTris (currententity);
				break;
			case mod_sprite:
				R_DrawSpriteModel_ShowTris (currententity);
				break;
			default:
				break;
			}
		}

		// viewmodel
		currententity = &cl.viewent;
		if (r_drawviewmodel.value
			&& !chase_active.value
			&& cl.stats[STAT_HEALTH] > 0
			&& !(cl.items & IT_INVISIBILITY)
			&& currententity->model
			&& currententity->model->type == mod_alias)
		{
			R_DrawAliasModel_ShowTris (currententity);
		}
	}

	if (r_particles.value)
	{
		R_DrawParticles_ShowTris();
#ifdef PSET_SCRIPT
		PScript_DrawParticles_ShowTris();
#endif
	}

	Sbar_Changed(); //so we don't get dots collecting on the statusbar
	R_EndDebugUtilsLabel ();
}

void R_SetupRaytracing(void)
{
	R_BindPipeline(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vulkan_globals.raygen_pipeline);
	// Probably have to include other descriptor sets too
	vulkan_globals.vk_cmd_bind_descriptor_sets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vulkan_globals.raygen_pipeline.layout.handle, 0, 1, &vulkan_globals.raygen_desc_set, 0, VK_NULL_HANDLE);

	vulkan_globals.fpCmdTraceRaysKHR(vulkan_globals.command_buffer, &vulkan_globals.rt_gen_region, &vulkan_globals.rt_miss_region, &vulkan_globals.rt_hit_region, &vulkan_globals.rt_call_region,
		1280, 720, 1);
}

// accel_struct_t* blas, qmodel_t* model, const aliashdr_t* header
void R_Create_BLAS() {

	VkDeviceOrHostAddressConstKHR vertex_device_or_host_address_const;
	memset(&vertex_device_or_host_address_const, 0, sizeof(VkDeviceOrHostAddressConstKHR));
	vertex_device_or_host_address_const.deviceAddress = vulkan_globals.vert_buffer;

	VkDeviceOrHostAddressConstKHR index_device_or_host_address_const;
	memset(&index_device_or_host_address_const, 0, sizeof(VkDeviceOrHostAddressConstKHR));
	index_device_or_host_address_const.deviceAddress = vulkan_globals.ind_buffer;

	VkDeviceOrHostAddressConstKHR transform_device_or_host_address_const;
	memset(&transform_device_or_host_address_const, 0, sizeof(VkDeviceOrHostAddressConstKHR));
	transform_device_or_host_address_const.deviceAddress = vulkan_globals.mem_transf;

	VkAccelerationStructureGeometryTrianglesDataKHR geometry_triangles_data;
	memset(&geometry_triangles_data, 0, sizeof(VkAccelerationStructureGeometryTrianglesDataKHR));
	geometry_triangles_data.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	geometry_triangles_data.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	geometry_triangles_data.vertexData = vertex_device_or_host_address_const;
	geometry_triangles_data.vertexStride = sizeof(float) * 3;
	geometry_triangles_data.maxVertex = 3;
	geometry_triangles_data.indexType = VK_INDEX_TYPE_UINT32;
	geometry_triangles_data.indexData = index_device_or_host_address_const;
	geometry_triangles_data.transformData = transform_device_or_host_address_const;

	// setting up the geometry (in this case mesh geometry_triangles_data)
	VkAccelerationStructureGeometryKHR geometry;
	memset(&geometry, 0, sizeof(VkAccelerationStructureGeometryKHR));
	geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry.geometry.triangles = geometry_triangles_data;
	geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

	VkAccelerationStructureBuildGeometryInfoKHR buildInfo;
	memset(&buildInfo, 0, sizeof(VkAccelerationStructureBuildGeometryInfoKHR));
	// Prepare build info now, acceleration is filled later
	buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	buildInfo.pNext = VK_NULL_HANDLE;
	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;
	buildInfo.dstAccelerationStructure = VK_NULL_HANDLE;
	buildInfo.geometryCount = 1;
	buildInfo.pGeometries = &geometry;
	buildInfo.ppGeometries = VK_NULL_HANDLE;
	//// Create BLAS size information
	//uint32_t max_primitive_count = header->numtris;

	VkAccelerationStructureBuildSizesInfoKHR sizeInfo;
	memset(&sizeInfo, 0, sizeof(VkAccelerationStructureBuildSizesInfoKHR));
	sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

	vulkan_globals.fpGetAccelerationStructureBuildSizesKHR(vulkan_globals.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &buildInfo.geometryCount, &sizeInfo);

	if (!accel_matches(&vulkan_globals.blas.match, false, 9, 3)) {
		destroy_accel_struct(&vulkan_globals.blas);

		VkAccelerationStructureCreateInfoKHR createInfo;
		memset(&createInfo, 0, sizeof(VkAccelerationStructureCreateInfoKHR));
		createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
		createInfo.size = sizeInfo.accelerationStructureSize;
		createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

		// Create buffer for acceleration
		buffer_create(&vulkan_globals.blas.mem, sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		createInfo.buffer = vulkan_globals.blas.mem.buffer;

		//creates acceleration structure
		VkResult err = vulkan_globals.fpCreateAccelerationStructureKHR(vulkan_globals.device, &createInfo, NULL, &vulkan_globals.blas.accel);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateAccelerationStructure failed");
	};

	// Scratch buffer
	BufferResource_t scratch_buffer;
	buffer_create(&scratch_buffer, sizeInfo.buildScratchSize,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkDeviceOrHostAddressKHR scratchDeviceOrHostAddress;
	memset(&scratchDeviceOrHostAddress, 0, sizeof(VkDeviceOrHostAddressKHR));
	scratchDeviceOrHostAddress.deviceAddress = scratch_buffer.address;

	buildInfo.scratchData = scratchDeviceOrHostAddress;

	vulkan_globals.blas.match.fast_build = 0;
	vulkan_globals.blas.match.vertex_count = 9;
	vulkan_globals.blas.match.index_count = 3;
	vulkan_globals.blas.match.aabb_count = 0;
	vulkan_globals.blas.match.instance_count = 1;

	// set where the build lands
	buildInfo.dstAccelerationStructure = vulkan_globals.blas.accel;

	// build buildRange
	VkAccelerationStructureBuildRangeInfoKHR* build_range =
		&(VkAccelerationStructureBuildRangeInfoKHR) {
		.primitiveCount = 1,
		.primitiveOffset = 0,
		.firstVertex = 0,
		.transformOffset = 0
	};
	const VkAccelerationStructureBuildRangeInfoKHR** build_range_infos = &build_range;

	vulkan_globals.fpCmdBuildAccelerationStructuresKHR(vulkan_globals.command_buffer, 1, &buildInfo, build_range_infos);

	buffer_destroy(&scratch_buffer);
}

void R_Create_TLAS() {

	uint32_t num_instances = 1;

	VkTransformMatrixKHR transformMatrix;
	memset(&transformMatrix, 0, sizeof(VkTransformMatrixKHR));
	transformMatrix.matrix[0][0] = 1.0;
	transformMatrix.matrix[1][1] = 1.0;
	transformMatrix.matrix[2][2] = 1.0;

	VkAccelerationStructureInstanceKHR geometryInstance;
	memset(&geometryInstance, 0, sizeof(VkAccelerationStructureInstanceKHR));
	geometryInstance.transform = transformMatrix;
	geometryInstance.instanceCustomIndex = 0;
	geometryInstance.mask = 0xFF;
	geometryInstance.instanceShaderBindingTableRecordOffset = 0;
	geometryInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
	geometryInstance.accelerationStructureReference = vulkan_globals.blas.mem.address;

	VkDeviceSize geometryInstanceBufferSize = sizeof(VkAccelerationStructureInstanceKHR);

	BufferResource_t geometry_buffer_instance;
	buffer_create(&geometry_buffer_instance, geometryInstanceBufferSize, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
		, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	void* instance_data = buffer_map(&geometry_buffer_instance);
	memcpy(instance_data, &geometryInstance, geometryInstanceBufferSize);
	buffer_unmap(&geometry_buffer_instance);

	// Build the TLAS
	VkAccelerationStructureGeometryDataKHR geometry = {
		.instances = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
			.data = {.deviceAddress = geometry_buffer_instance.address}
		}
	};

	VkAccelerationStructureGeometryKHR topASGeometry;
	memset(&topASGeometry, 0, sizeof(VkAccelerationStructureGeometryKHR));
	topASGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	topASGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	topASGeometry.geometry = geometry;
	topASGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

	// Find size to build on the device
	VkAccelerationStructureBuildGeometryInfoKHR buildInfo;
	memset(&buildInfo, 0, sizeof(VkAccelerationStructureBuildGeometryInfoKHR));
	buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR,
		buildInfo.geometryCount = 1;
	buildInfo.pGeometries = &topASGeometry;
	buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

	VkAccelerationStructureBuildSizesInfoKHR sizeInfo;
	memset(&sizeInfo, 0, sizeof(VkAccelerationStructureBuildSizesInfoKHR));
	sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

	vulkan_globals.fpGetAccelerationStructureBuildSizesKHR(vulkan_globals.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &num_instances, &sizeInfo);

	if(!accel_matches_top_level(&vulkan_globals.tlas.match, 1, 1)) {
		// Create the buffer for the acceleration structure
		buffer_create(&vulkan_globals.tlas.mem, sizeInfo.accelerationStructureSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		// Create TLAS
		// Create acceleration structure
		VkAccelerationStructureCreateInfoKHR createInfo = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
			.size = sizeInfo.accelerationStructureSize,
			.buffer = vulkan_globals.tlas.mem.buffer
		};

		// Create the acceleration structure
		VkResult err = vulkan_globals.fpCreateAccelerationStructureKHR(vulkan_globals.device, &createInfo, NULL, &vulkan_globals.tlas.accel);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateAccelerationStructure failed");
	}
	

	vulkan_globals.tlas.match.fast_build = 1;
	vulkan_globals.tlas.match.index_count = 0;
	vulkan_globals.tlas.match.vertex_count = 0;
	vulkan_globals.tlas.match.aabb_count = 0;
	vulkan_globals.tlas.match.instance_count = 1;

	// scratch buffer creation
	BufferResource_t scratch_buffer;
	buffer_create(&scratch_buffer, sizeInfo.buildScratchSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// Update build information
	buildInfo.dstAccelerationStructure = vulkan_globals.tlas.accel;
	buildInfo.scratchData.deviceAddress = scratch_buffer.address;

	//// build buildRange
	VkAccelerationStructureBuildRangeInfoKHR* build_range =
		&(VkAccelerationStructureBuildRangeInfoKHR) {
		.primitiveCount = 1,
			.primitiveOffset = 0,
			.firstVertex = 0,
			.transformOffset = 0
	};
	const VkAccelerationStructureBuildRangeInfoKHR** build_range_infos = &build_range;

	vulkan_globals.fpCmdBuildAccelerationStructuresKHR(vulkan_globals.command_buffer, 1, &buildInfo, build_range_infos);

	buffer_destroy(&scratch_buffer);
	buffer_destroy(&geometry_buffer_instance);
}

VkResult R_UpdateRaygenDescriptorSet()
{
	if (vulkan_globals.raygen_desc_set != VK_NULL_HANDLE) {

		VkDescriptorImageInfo pt_output_image_info;
		memset(&pt_output_image_info, 0, sizeof(VkDescriptorImageInfo));
		// give access to image buffer view from vidsl 
		pt_output_image_info.imageView = vulkan_globals.color_buffers_view;
		//pt_output_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		pt_output_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		//pt_output_image_info.sampler = VK_IMAGE_LAYOUT_GENERAL;

		VkWriteDescriptorSetAccelerationStructureKHR desc_accel_struct;
		memset(&desc_accel_struct, 0, sizeof(VkWriteDescriptorSetAccelerationStructureKHR));
		desc_accel_struct.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
		desc_accel_struct.accelerationStructureCount = 1;
		desc_accel_struct.pAccelerationStructures = &vulkan_globals.tlas.accel;

		VkWriteDescriptorSet raygen_writes[2];
		memset(&raygen_writes, 0, 2 * sizeof(VkWriteDescriptorSet));
		raygen_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		raygen_writes[0].pNext = &desc_accel_struct;
		raygen_writes[0].dstBinding = 0;
		raygen_writes[0].descriptorCount = 1;
		raygen_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		raygen_writes[0].dstSet = vulkan_globals.raygen_desc_set;

		raygen_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		raygen_writes[1].dstBinding = 1;
		raygen_writes[1].descriptorCount = 1;
		raygen_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		raygen_writes[1].dstSet = vulkan_globals.raygen_desc_set;
		raygen_writes[1].pImageInfo = &pt_output_image_info;

		vkUpdateDescriptorSets(vulkan_globals.device, 2, raygen_writes, 0, NULL);
	}

	return VK_SUCCESS;
}

void R_SetupTestAS_RTX(void)
{
	static accel_struct_t blas;
	static accel_struct_t tlas;

	VkMemoryBarrier memoryBarrier;
	memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memoryBarrier.pNext = VK_NULL_HANDLE;
	memoryBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

	R_Create_BLAS(&blas);
	vkCmdPipelineBarrier(vulkan_globals.command_buffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &memoryBarrier, 0, 0, 0, 0);
	R_Create_TLAS(&blas, &tlas);
	vkCmdPipelineBarrier(vulkan_globals.command_buffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &memoryBarrier, 0, 0, 0, 0);
	R_UpdateRaygenDescriptorSet();
}

/*
================
R_RenderScene_RTX
================
*/
void R_RenderScene_RTX(void)
{
	static entity_t r_worldentity;	//so we can make sure currententity is valid
	currententity = &r_worldentity;

	R_SetupMatrix_RTX();

	//R_DrawViewModel();

	R_SetupTestAS_RTX();

	R_SetupRaytracing();

	S_ExtraUpdate();
}

/*
================
R_RenderScene
================
*/
void R_RenderScene (void)
{
	static entity_t r_worldentity;	//so we can make sure currententity is valid
	currententity = &r_worldentity;
	R_SetupScene (); //johnfitz -- this does everything that should be done once per call to RenderScene

	Fog_EnableGFog (); //johnfitz

	R_DrawWorld ();
	currententity = NULL;

	S_ExtraUpdate (); // don't let sound get messed up if going slow

	R_DrawEntitiesOnList (false); //johnfitz -- false means this is the pass for nonalpha entities

	Sky_DrawSky (); //johnfitz

	R_DrawWorld_Water (); //johnfitz -- drawn here since they might have transparency

	R_DrawEntitiesOnList (true); //johnfitz -- true means this is the pass for alpha entities

	R_DrawParticles ();
#ifdef PSET_SCRIPT
	PScript_DrawParticles();
#endif

	Fog_DisableGFog (); //johnfitz

	R_DrawViewModel (); //johnfitz -- moved here from R_RenderView
	
	R_ShowTris(); //johnfitz

	R_ShowBoundingBoxes (); //johnfitz
}

/*
================
R_RenderView_RTX
================
*/
void R_RenderView_RTX (void)
{	
	double	time1, time2;

	if (!cl.worldmodel)
		Sys_Error("R_RenderView: NULL worldmodel");

	time1 = 0; /* avoid compiler warning */
	if (r_speeds.value)
	{
		time1 = Sys_DoubleTime();

		//johnfitz -- rendering statistics
		rs_brushpolys = rs_aliaspolys = rs_skypolys = rs_particles = rs_fogpolys = rs_megatexels =
			rs_dynamiclightmaps = rs_aliaspasses = rs_skypasses = rs_brushpasses = 0;
	}

	R_SetupView_RTX();

	R_RenderScene_RTX();


	//johnfitz -- modified r_speeds output
	time2 = Sys_DoubleTime();
	if (r_pos.value)
		Con_Printf("x %i y %i z %i (pitch %i yaw %i roll %i)\n",
			(int)cl.entities[cl.viewentity].origin[0],
			(int)cl.entities[cl.viewentity].origin[1],
			(int)cl.entities[cl.viewentity].origin[2],
			(int)cl.viewangles[PITCH],
			(int)cl.viewangles[YAW],
			(int)cl.viewangles[ROLL]);
	else if (r_speeds.value == 2)
		Con_Printf("%6.3f ms  %4u/%4u wpoly %4u/%4u epoly %3u lmap %4u/%4u sky\n",
			(time2 - time1) * 1000.0,
			rs_brushpolys,
			rs_brushpasses,
			rs_aliaspolys,
			rs_aliaspasses,
			rs_dynamiclightmaps,
			rs_skypolys,
			rs_skypasses);
	else if (r_speeds.value)
		Con_Printf("%3i ms  %4i wpoly %4i epoly %3i lmap\n",
			(int)((time2 - time1) * 1000),
			rs_brushpolys,
			rs_aliaspolys,
			rs_dynamiclightmaps);
}


/*
================
R_RenderView
================
*/
void R_RenderView (void)
{
	double	time1, time2;

	if (!cl.worldmodel)
		Sys_Error ("R_RenderView: NULL worldmodel");

	time1 = 0; /* avoid compiler warning */
	if (r_speeds.value)
	{
		time1 = Sys_DoubleTime ();

		//johnfitz -- rendering statistics
		rs_brushpolys = rs_aliaspolys = rs_skypolys = rs_particles = rs_fogpolys = rs_megatexels =
		rs_dynamiclightmaps = rs_aliaspasses = rs_skypasses = rs_brushpasses = 0;
	}

	R_SetupView (); //johnfitz -- this does everything that should be done once per frame

	R_RenderScene ();

	//johnfitz

	//johnfitz -- modified r_speeds output
	time2 = Sys_DoubleTime ();
	if (r_pos.value)
		Con_Printf ("x %i y %i z %i (pitch %i yaw %i roll %i)\n",
			(int)cl.entities[cl.viewentity].origin[0],
			(int)cl.entities[cl.viewentity].origin[1],
			(int)cl.entities[cl.viewentity].origin[2],
			(int)cl.viewangles[PITCH],
			(int)cl.viewangles[YAW],
			(int)cl.viewangles[ROLL]);
	else if (r_speeds.value == 2)
		Con_Printf ("%6.3f ms  %4u/%4u wpoly %4u/%4u epoly %3u lmap %4u/%4u sky\n",
					(time2-time1)*1000.0,
					rs_brushpolys,
					rs_brushpasses,
					rs_aliaspolys,
					rs_aliaspasses,
					rs_dynamiclightmaps,
					rs_skypolys,
					rs_skypasses);
	else if (r_speeds.value)
		Con_Printf ("%3i ms  %4i wpoly %4i epoly %3i lmap\n",
					(int)((time2-time1)*1000),
					rs_brushpolys,
					rs_aliaspolys,
					rs_dynamiclightmaps);
	//johnfitz
}

