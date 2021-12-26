/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers
Copyright (C) 2016 Axel Gneiting

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

//r_alias.c -- alias model rendering

#include "quakedef.h"

extern cvar_t r_drawflat, gl_fullbrights, r_lerpmodels, r_lerpmove, r_showtris; //johnfitz
extern cvar_t scr_fov, cl_gun_fovscale;

//up to 16 color translated skins
gltexture_t *playertextures[MAX_SCOREBOARD]; //johnfitz -- changed to an array of pointers

#define NUMVERTEXNORMALS	162

float	r_avertexnormals[NUMVERTEXNORMALS][3] =
{
#include "anorms.h"
};

extern vec3_t	lightcolor; //johnfitz -- replaces "float shadelight" for lit support

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
float	r_avertexnormal_dots[SHADEDOT_QUANT][256] =
{
#include "anorm_dots.h"
};

extern	vec3_t			lightspot;

float	*shadedots = r_avertexnormal_dots[0];
vec3_t	shadevector;

float	entalpha; //johnfitz

qboolean shading = true; //johnfitz -- if false, disable vertex shading for various reasons (fullbright, r_lightmap, showtris, etc)

//johnfitz -- struct for passing lerp information to drawing functions
typedef struct {
	short pose1;
	short pose2;
	float blend;
	vec3_t origin;
	vec3_t angles;
} lerpdata_t;
//johnfitz

typedef struct {
	float model_matrix[16];
	float shade_vector[3];
	float blend_factor;
	float light_color[3];
	float entalpha;
	unsigned int flags;
} aliasubo_t;

typedef struct {
	float model_matrix[16];
	unsigned int st_offset;
} raygen_aliasubo_t;

/*
=============
GLARB_GetXYZOffset

Returns the offset of the first vertex's meshxyz_t.xyz in the vbo for the given
model and pose.
=============
*/
static VkDeviceSize GLARB_GetXYZOffset (aliashdr_t *hdr, int pose)
{
	const int xyzoffs = offsetof (meshxyz_t, xyz);
	return currententity->model->vboxyzofs + (hdr->numverts_vbo * pose * sizeof (meshxyz_t)) + xyzoffs;
}


static void R_Create_Alias_BLAS(aliashdr_t* paliashdr, float transform_mat[16], VkBuffer vertex_buffer, VkDeviceSize vertex_offset, VkBuffer index_buffer) {

	//VkBufferDeviceAddressInfo vertexBufferDeviceAddressInfo;
	//memset(&vertexBufferDeviceAddressInfo, 0, sizeof(VkBufferDeviceAddressInfo));
	//vertexBufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	//vertexBufferDeviceAddressInfo.buffer = vertex_buffer;

	//VkDeviceAddress vertexBufferAddress = vkGetBufferDeviceAddress(vulkan_globals.device, &vertexBufferDeviceAddressInfo);

	//VkDeviceOrHostAddressConstKHR vertexDeviceOrHostAddressConst;
	//memset(&vertexDeviceOrHostAddressConst, 0, sizeof(VkDeviceOrHostAddressConstKHR));
	//vertexDeviceOrHostAddressConst.deviceAddress = vertexBufferAddress + vertex_offset;


	//VkBufferDeviceAddressInfo indexBufferDeviceAddressInfo;
	//memset(&indexBufferDeviceAddressInfo, 0, sizeof(VkBufferDeviceAddressInfo));
	//indexBufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	//indexBufferDeviceAddressInfo.buffer = index_buffer;

	//VkDeviceAddress indexBufferAddress = vkGetBufferDeviceAddress(vulkan_globals.device, &indexBufferDeviceAddressInfo);

	//VkDeviceOrHostAddressConstKHR indexDeviceOrHostAddressConst;
	//memset(&indexDeviceOrHostAddressConst, 0, sizeof(VkDeviceOrHostAddressConstKHR));
	//indexDeviceOrHostAddressConst.deviceAddress = indexBufferAddress;

	//// TODO: See how it affects the transform of the geometry

	//VkTransformMatrixKHR transform;
	//memset(&transform, 0, sizeof(VkTransformMatrixKHR));
	//transform.matrix[0][0] = transform_mat[0];
	//transform.matrix[0][1] = transform_mat[4];
	//transform.matrix[0][2] = transform_mat[8];
	//transform.matrix[0][3] = transform_mat[12];

	//transform.matrix[1][0] = transform_mat[1];
	//transform.matrix[1][1] = transform_mat[5];
	//transform.matrix[1][2] = transform_mat[9];
	//transform.matrix[1][3] = transform_mat[13];

	//transform.matrix[2][0] = transform_mat[2];
	//transform.matrix[2][1] = transform_mat[6];
	//transform.matrix[2][2] = transform_mat[10];
	//transform.matrix[2][3] = transform_mat[14];

	//BufferResource_t transform_buffer_instance;
	//buffer_create(&transform_buffer_instance, sizeof(VkTransformMatrixKHR), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
	//	VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);

	//void* mem_transf = buffer_map(&transform_buffer_instance);
	//memcpy(mem_transf, &transform, transform_buffer_instance.size);
	//buffer_unmap(&transform_buffer_instance);
	//mem_transf = NULL;

	//VkDeviceOrHostAddressConstKHR transform_device_or_host_address_const;
	//memset(&transform_device_or_host_address_const, 0, sizeof(VkDeviceOrHostAddressConstKHR));
	//transform_device_or_host_address_const.deviceAddress = transform_buffer_instance.address;

	//uint32_t max_primitive_count = paliashdr->numtris;
	//uint32_t numverts = paliashdr->numverts_vbo;
	//uint32_t numindices = paliashdr->numindexes;

	//VkAccelerationStructureGeometryTrianglesDataKHR geometry_triangles_data;
	//memset(&geometry_triangles_data, 0, sizeof(VkAccelerationStructureGeometryTrianglesDataKHR));
	//geometry_triangles_data.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	////geometry_triangles_data.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	//geometry_triangles_data.vertexFormat = VK_FORMAT_R8G8B8A8_UNORM;
	//geometry_triangles_data.vertexData = vertexDeviceOrHostAddressConst;
	//geometry_triangles_data.vertexStride = sizeof(meshxyz_t);
	//geometry_triangles_data.maxVertex = numverts - 1;
	//geometry_triangles_data.indexType = VK_INDEX_TYPE_UINT16;
	//geometry_triangles_data.indexData = indexDeviceOrHostAddressConst;
	//geometry_triangles_data.transformData = transform_device_or_host_address_const;

	//// setting up the geometry
	//VkAccelerationStructureGeometryKHR geometry;
	//memset(&geometry, 0, sizeof(VkAccelerationStructureGeometryKHR));
	//geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	//geometry.geometry.triangles = geometry_triangles_data;
	//geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	//geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

	//VkAccelerationStructureBuildGeometryInfoKHR buildInfo;
	//memset(&buildInfo, 0, sizeof(VkAccelerationStructureBuildGeometryInfoKHR));
	//// Prepare build info now, acceleration is filled later
	//buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	//buildInfo.pNext = VK_NULL_HANDLE;
	//buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	//buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	//buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	//buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;
	//buildInfo.dstAccelerationStructure = VK_NULL_HANDLE;
	//buildInfo.geometryCount = 1;
	//buildInfo.pGeometries = &geometry;
	//buildInfo.ppGeometries = VK_NULL_HANDLE;

	//VkAccelerationStructureBuildSizesInfoKHR sizeInfo;
	//memset(&sizeInfo, 0, sizeof(VkAccelerationStructureBuildSizesInfoKHR));
	//sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

	//vulkan_globals.fpGetAccelerationStructureBuildSizesKHR(vulkan_globals.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &buildInfo.geometryCount, &sizeInfo);

	//if (!accel_matches(&vulkan_globals.blas.match, false, numverts, numindices)) {
	//	destroy_accel_struct(&vulkan_globals.blas);

	//	VkAccelerationStructureCreateInfoKHR createInfo;
	//	memset(&createInfo, 0, sizeof(VkAccelerationStructureCreateInfoKHR));
	//	createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	//	createInfo.size = sizeInfo.accelerationStructureSize;
	//	createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

	//	// Create buffer for acceleration
	//	buffer_create(&vulkan_globals.blas.mem, sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	//		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	//	createInfo.buffer = vulkan_globals.blas.mem.buffer;

	//	//creates acceleration structure
	//	VkResult err = vulkan_globals.fpCreateAccelerationStructureKHR(vulkan_globals.device, &createInfo, NULL, &vulkan_globals.blas.accel);
	//	if (err != VK_SUCCESS)
	//		Sys_Error("vkCreateAccelerationStructure failed");
	//};

	//// Scratch buffer
	//BufferResource_t scratch_buffer;
	//buffer_create(&scratch_buffer, sizeInfo.buildScratchSize,
	//	VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	//	VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//VkDeviceOrHostAddressKHR scratchDeviceOrHostAddress;
	//memset(&scratchDeviceOrHostAddress, 0, sizeof(VkDeviceOrHostAddressKHR));
	//scratchDeviceOrHostAddress.deviceAddress = scratch_buffer.address;

	//buildInfo.scratchData = scratchDeviceOrHostAddress;

	//vulkan_globals.blas.match.fast_build = 0;
	//vulkan_globals.blas.match.vertex_count = numverts;
	//vulkan_globals.blas.match.index_count = numindices;
	//vulkan_globals.blas.match.aabb_count = 0;
	//vulkan_globals.blas.match.instance_count = 1;

	//// set where the build lands
	//buildInfo.dstAccelerationStructure = vulkan_globals.blas.accel;

	//// build buildRange
	//VkAccelerationStructureBuildRangeInfoKHR* build_range =
	//	&(VkAccelerationStructureBuildRangeInfoKHR) {
	//	.primitiveCount = max_primitive_count,
	//	.primitiveOffset = 0,
	//	.firstVertex = 0,
	//	.transformOffset = 0
	//};
	//const VkAccelerationStructureBuildRangeInfoKHR** build_range_infos = &build_range;

	//vulkan_globals.fpCmdBuildAccelerationStructuresKHR(vulkan_globals.command_buffer, 1, &buildInfo, build_range_infos);

	////buffer_destroy(&scratch_buffer);
}

static void GL_CreateAliasBLAS(aliashdr_t* paliashdr, lerpdata_t lerpdata, gltexture_t* tx, gltexture_t* fb, float model_matrix[16], float entity_alpha, qboolean alphatest)
{
	float	blend;

	if (lerpdata.pose1 != lerpdata.pose2)
		blend = lerpdata.blend;
	else // poses the same means either 1. the entity has paused its animation, or 2. r_lerpmodels is disabled
		blend = 0;

	VkBuffer uniform_buffer;
	uint32_t uniform_offset;
	VkDescriptorSet ubo_set;
	raygen_aliasubo_t* ubo = (raygen_aliasubo_t*)R_UniformAllocate(sizeof(raygen_aliasubo_t), &uniform_buffer, &uniform_offset, &ubo_set);

	unsigned int vbostoffset = (unsigned)currententity->model->vbostofs;
	memcpy(ubo->model_matrix, model_matrix, 16 * sizeof(float));
	ubo->st_offset = vbostoffset;

	// pose 2 refers to the current frame. method returns offset of vertex buffer which contains current vertices. this offset is added to the vertex buffer address
	VkDeviceSize vertex_offset = GLARB_GetXYZOffset(paliashdr, lerpdata.pose2);

	vulkan_globals.raygen_desc_set_items.vertex_buffer = currententity->model->vertex_buffer;
	vulkan_globals.raygen_desc_set_items.index_buffer = currententity->model->index_buffer;
	vulkan_globals.raygen_desc_set_items.alias_texture_view = tx->image_view;
	if (fb != NULL) {
		vulkan_globals.raygen_desc_set_items.alias_texture_fullbright_view = fb->image_view;
	}
	vulkan_globals.raygen_desc_set_items.alias_uniform_buffer = uniform_buffer;

	R_Create_Alias_BLAS(paliashdr, ubo->model_matrix, currententity->model->vertex_buffer, vertex_offset, currententity->model->index_buffer);
	rs_aliaspasses += paliashdr->numtris;
}

/*
=============
GL_DrawAliasFrame -- ericw

Optimized alias model drawing codepath. This makes 1 draw call,
no vertex data is uploaded (it's already in the r_meshvbo and r_meshindexesvbo
static VBOs), and lerping and lighting is done in the vertex shader.

Supports optional fullbright pixels.

Based on code by MH from RMQEngine
=============
*/
static void GL_DrawAliasFrame (aliashdr_t *paliashdr, lerpdata_t lerpdata, gltexture_t *tx, gltexture_t *fb, float model_matrix[16], float entity_alpha, qboolean alphatest)
{
	float	blend;

	if (lerpdata.pose1 != lerpdata.pose2)
		blend = lerpdata.blend;
	else // poses the same means either 1. the entity has paused its animation, or 2. r_lerpmodels is disabled
		blend = 0;

	vulkan_pipeline_t pipeline;
	if (entity_alpha >= 1.0f)
	{
		if (!alphatest)
			pipeline = vulkan_globals.alias_pipeline;
		else
			pipeline = vulkan_globals.alias_alphatest_pipeline;
	}
	else
	{
		if (!alphatest)
			pipeline = vulkan_globals.alias_blend_pipeline;
		else
			pipeline = vulkan_globals.alias_alphatest_blend_pipeline;
	}

	R_BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	VkBuffer uniform_buffer;
	uint32_t uniform_offset;
	VkDescriptorSet ubo_set;
	aliasubo_t * ubo = (aliasubo_t*)R_UniformAllocate(sizeof(aliasubo_t), &uniform_buffer, &uniform_offset, &ubo_set);

	memcpy(ubo->model_matrix, model_matrix, 16 * sizeof(float));
	memcpy(ubo->shade_vector, shadevector, 3 * sizeof(float));
	ubo->blend_factor = blend;
	memcpy(ubo->light_color, lightcolor, 3 * sizeof(float));
	ubo->flags = (fb != NULL) ? 0x1 : 0x0;
	if (r_fullbright_cheatsafe || r_lightmap_cheatsafe)
		ubo->flags |= 0x2;
	ubo->entalpha = entity_alpha;

	VkDescriptorSet descriptor_sets[3] = { tx->descriptor_set, (fb != NULL) ? fb->descriptor_set : tx->descriptor_set, ubo_set };
	vulkan_globals.vk_cmd_bind_descriptor_sets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.alias_pipeline.layout.handle, 0, 3, descriptor_sets, 1, &uniform_offset);

	VkBuffer vertex_buffers[3] = { currententity->model->vertex_buffer, currententity->model->vertex_buffer, currententity->model->vertex_buffer };
	VkDeviceSize vertex_offsets[3] = { (unsigned)currententity->model->vbostofs, GLARB_GetXYZOffset (paliashdr, lerpdata.pose1), GLARB_GetXYZOffset (paliashdr, lerpdata.pose2) };
	vulkan_globals.vk_cmd_bind_vertex_buffers(vulkan_globals.command_buffer, 0, 3, vertex_buffers, vertex_offsets);
	vulkan_globals.vk_cmd_bind_index_buffer(vulkan_globals.command_buffer, currententity->model->index_buffer, 0, VK_INDEX_TYPE_UINT16);

	vulkan_globals.vk_cmd_draw_indexed(vulkan_globals.command_buffer, paliashdr->numindexes, 1, 0, 0, 0);

	rs_aliaspasses += paliashdr->numtris;
}

/*
=================
R_SetupAliasFrame -- johnfitz -- rewritten to support lerping
=================
*/
void R_SetupAliasFrame (aliashdr_t *paliashdr, int frame, lerpdata_t *lerpdata)
{
	entity_t		*e = currententity;
	int				posenum, numposes;

	if ((frame >= paliashdr->numframes) || (frame < 0))
	{
		Con_DPrintf ("R_AliasSetupFrame: no such frame %d for '%s'\n", frame, e->model->name);
		frame = 0;
	}

	posenum = paliashdr->frames[frame].firstpose;
	numposes = paliashdr->frames[frame].numposes;

	if (numposes > 1)
	{
		e->lerptime = paliashdr->frames[frame].interval;
		posenum += (int)(cl.time / e->lerptime) % numposes;
	}
	else
		e->lerptime = 0.1;

	if (e->lerpflags & LERP_RESETANIM) //kill any lerp in progress
	{
		e->lerpstart = 0;
		e->previouspose = posenum;
		e->currentpose = posenum;
		e->lerpflags -= LERP_RESETANIM;
	}
	else if (e->currentpose != posenum) // pose changed, start new lerp
	{
		if (e->lerpflags & LERP_RESETANIM2) //defer lerping one more time
		{
			e->lerpstart = 0;
			e->previouspose = posenum;
			e->currentpose = posenum;
			e->lerpflags -= LERP_RESETANIM2;
		}
		else
		{
			e->lerpstart = cl.time;
			e->previouspose = e->currentpose;
			e->currentpose = posenum;
		}
	}

	//set up values
	if (r_lerpmodels.value && !(e->model->flags & MOD_NOLERP && r_lerpmodels.value != 2))
	{
		if (e->lerpflags & LERP_FINISH && numposes == 1)
			lerpdata->blend = CLAMP (0, (cl.time - e->lerpstart) / (e->lerpfinish - e->lerpstart), 1);
		else
			lerpdata->blend = CLAMP (0, (cl.time - e->lerpstart) / e->lerptime, 1);

		if (e->currentpose >= paliashdr->numposes || e->currentpose < 0)
		{
			Con_DPrintf ("R_AliasSetupFrame: invalid current pose %d (%d total) for '%s'\n", e->currentpose, paliashdr->numposes, e->model->name);
			e->currentpose = 0;
		}

		if (e->previouspose >= paliashdr->numposes || e->previouspose < 0)
		{
			Con_DPrintf ("R_AliasSetupFrame: invalid prev pose %d (%d total) for '%s'\n", e->previouspose, paliashdr->numposes, e->model->name);
			e->previouspose = e->currentpose;
		}

		lerpdata->pose1 = e->previouspose;
		lerpdata->pose2 = e->currentpose;
	}
	else //don't lerp
	{
		lerpdata->blend = 1;
		lerpdata->pose1 = posenum;
		lerpdata->pose2 = posenum;
	}
}

/*
=================
R_SetupEntityTransform -- johnfitz -- set up transform part of lerpdata
=================
*/
void R_SetupEntityTransform (entity_t *e, lerpdata_t *lerpdata)
{
	float blend;
	vec3_t d;
	int i;

	// if LERP_RESETMOVE, kill any lerps in progress
	if (e->lerpflags & LERP_RESETMOVE)
	{
		e->movelerpstart = 0;
		VectorCopy (e->origin, e->previousorigin);
		VectorCopy (e->origin, e->currentorigin);
		VectorCopy (e->angles, e->previousangles);
		VectorCopy (e->angles, e->currentangles);
		e->lerpflags -= LERP_RESETMOVE;
	}
	else if (!VectorCompare (e->origin, e->currentorigin) || !VectorCompare (e->angles, e->currentangles)) // origin/angles changed, start new lerp
	{
		e->movelerpstart = cl.time;
		VectorCopy (e->currentorigin, e->previousorigin);
		VectorCopy (e->origin,  e->currentorigin);
		VectorCopy (e->currentangles, e->previousangles);
		VectorCopy (e->angles,  e->currentangles);
	}

	//set up values
	if (r_lerpmove.value && e != &cl.viewent && e->lerpflags & LERP_MOVESTEP)
	{
		if (e->lerpflags & LERP_FINISH)
			blend = CLAMP (0, (cl.time - e->movelerpstart) / (e->lerpfinish - e->movelerpstart), 1);
		else
			blend = CLAMP (0, (cl.time - e->movelerpstart) / 0.1, 1);

		//translation
		VectorSubtract (e->currentorigin, e->previousorigin, d);
		lerpdata->origin[0] = e->previousorigin[0] + d[0] * blend;
		lerpdata->origin[1] = e->previousorigin[1] + d[1] * blend;
		lerpdata->origin[2] = e->previousorigin[2] + d[2] * blend;

		//rotation
		VectorSubtract (e->currentangles, e->previousangles, d);
		for (i = 0; i < 3; i++)
		{
			if (d[i] > 180)  d[i] -= 360;
			if (d[i] < -180) d[i] += 360;
		}
		lerpdata->angles[0] = e->previousangles[0] + d[0] * blend;
		lerpdata->angles[1] = e->previousangles[1] + d[1] * blend;
		lerpdata->angles[2] = e->previousangles[2] + d[2] * blend;
	}
	else //don't lerp
	{
		VectorCopy (e->origin, lerpdata->origin);
		VectorCopy (e->angles, lerpdata->angles);
	}
}

/*
=================
R_SetupAliasLighting -- johnfitz -- broken out from R_DrawAliasModel and rewritten
=================
*/
void R_SetupAliasLighting (entity_t	*e)
{
	vec3_t		dist;
	float		add;
	int			i;
	int		quantizedangle;
	float		radiansangle;
	vec3_t		lpos;

	VectorCopy (e->origin, lpos);
	// start the light trace from slightly above the origin
	// this helps with models whose origin is below ground level, but are otherwise visible
	// (e.g. some of the candles in the DOTM start map, which would otherwise appear black)
	lpos[2] += e->model->maxs[2] * 0.5f;
	R_LightPoint (lpos);

	//add dlights
	for (i=0 ; i<MAX_DLIGHTS ; i++)
	{
		if (cl_dlights[i].die >= cl.time)
		{
			VectorSubtract (currententity->origin, cl_dlights[i].origin, dist);
			add = cl_dlights[i].radius - VectorLength(dist);
			if (add > 0)
				VectorMA (lightcolor, add, cl_dlights[i].color, lightcolor);
		}
	}

	// minimum light value on gun (24)
	if (e == &cl.viewent)
	{
		add = 72.0f - (lightcolor[0] + lightcolor[1] + lightcolor[2]);
		if (add > 0.0f)
		{
			lightcolor[0] += add / 3.0f;
			lightcolor[1] += add / 3.0f;
			lightcolor[2] += add / 3.0f;
		}
	}

	// minimum light value on players (8)
	if (currententity > cl.entities && currententity <= cl.entities + cl.maxclients)
	{
		add = 24.0f - (lightcolor[0] + lightcolor[1] + lightcolor[2]);
		if (add > 0.0f)
		{
			lightcolor[0] += add / 3.0f;
			lightcolor[1] += add / 3.0f;
			lightcolor[2] += add / 3.0f;
		}
	}

	// clamp lighting so it doesn't overbright as much (96)
	add = 288.0f / (lightcolor[0] + lightcolor[1] + lightcolor[2]);
	if (add < 1.0f)
		VectorScale(lightcolor, add, lightcolor);

	quantizedangle = ((int)(e->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1);

//ericw -- shadevector is passed to the shader to compute shadedots inside the
//shader, see GLAlias_CreateShaders()
	radiansangle = (quantizedangle / 16.0) * 2.0 * 3.14159;
	shadevector[0] = cos(-radiansangle);
	shadevector[1] = sin(-radiansangle);
	shadevector[2] = 1;
	VectorNormalize(shadevector);
//ericw --

	shadedots = r_avertexnormal_dots[quantizedangle];
	VectorScale (lightcolor, 1.0f / 200.0f, lightcolor);
}

/*
=================
R_DrawAliasModel -- johnfitz -- almost completely rewritten
=================
*/
void R_DrawAliasModel (entity_t *e) //(entity_t *e, qboolean rt)
{
	aliashdr_t	*paliashdr;
	int			i, anim, skinnum;
	gltexture_t	*tx, *fb;
	lerpdata_t	lerpdata;
	qboolean	alphatest = !!(e->model->flags & MF_HOLEY);
	alphatest;

	//
	// setup pose/lerp data -- do it first so we don't miss updates due to culling
	//
	paliashdr = (aliashdr_t *)Mod_Extradata (e->model);
	R_SetupAliasFrame (paliashdr, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);

	//
	// cull it
	//
	if (R_CullModelForEntity(e))
		return;

	//
	// transform it
	//
	float model_matrix[16];
	IdentityMatrix(model_matrix);
	R_RotateForEntity (model_matrix, lerpdata.origin, lerpdata.angles);

	float fovscale = 1.0f;
	if (e == &cl.viewent && scr_fov.value > 90.f && cl_gun_fovscale.value)
	{
		fovscale = tan(scr_fov.value * (0.5f * M_PI / 180.f));
		fovscale = 1.f + (fovscale - 1.f) * cl_gun_fovscale.value;
	}

	float translation_matrix[16];
	TranslationMatrix (translation_matrix, paliashdr->scale_origin[0], paliashdr->scale_origin[1] * fovscale, paliashdr->scale_origin[2] * fovscale);
	MatrixMultiply(model_matrix, translation_matrix);

	// Scale multiplied by 255 because we use UNORM instead of USCALED in the vertex shader
	float scale_matrix[16];
	ScaleMatrix (scale_matrix, paliashdr->scale[0] * 255.0f, paliashdr->scale[1] * fovscale * 255.0f, paliashdr->scale[2] * fovscale * 255.0f);
	MatrixMultiply(model_matrix, scale_matrix);

	//
	// random stuff
	//
	shading = true;

	//
	// set up for alpha blending
	//
	if (r_lightmap_cheatsafe)
		entalpha = 1;
	else
		entalpha = ENTALPHA_DECODE(e->alpha);
	if (entalpha == 0)
		return;

	//
	// set up lighting
	//
	rs_aliaspolys += paliashdr->numtris;
	R_SetupAliasLighting (e);

	//
	// set up textures
	//
	anim = (int)(cl.time*10) & 3;
	skinnum = e->skinnum;
	if ((skinnum >= paliashdr->numskins) || (skinnum < 0))
	{
		Con_DPrintf ("R_DrawAliasModel: no such skin # %d for '%s'\n", skinnum, e->model->name);
		// ericw -- display skin 0 for winquake compatibility
		skinnum = 0;
	}
	tx = paliashdr->gltextures[skinnum][anim];
	fb = paliashdr->fbtextures[skinnum][anim];
	if (e->colormap != vid.colormap && !gl_nocolors.value)
	{
		i = e - cl.entities;
		if (i >= 1 && i<=cl.maxclients )
		    tx = playertextures[i - 1];
	}
	if (!gl_fullbrights.value)
		fb = NULL;

	if (r_fullbright_cheatsafe)
	{
		lightcolor[0] = 0.5f;
		lightcolor[1] = 0.5f;
		lightcolor[2] = 0.5f;
	}
	if (r_lightmap_cheatsafe)
	{
		tx = whitetexture;
		fb = NULL;
		lightcolor[0] = 1.0f;
		lightcolor[1] = 1.0f;
		lightcolor[2] = 1.0f;
	}

	//
	// draw it
	//
	// 
	
	/*if (rt) {
		GL_CreateAliasBLAS(paliashdr, lerpdata, tx, fb, model_matrix, entalpha, alphatest);
	}
	else {
		GL_DrawAliasFrame(paliashdr, lerpdata, tx, fb, model_matrix, entalpha, alphatest);
	}*/
	
}

//johnfitz -- values for shadow matrix
#define SHADOW_SKEW_X -0.7 //skew along x axis. -0.7 to mimic glquake shadows
#define SHADOW_SKEW_Y 0 //skew along y axis. 0 to mimic glquake shadows
#define SHADOW_VSCALE 0 //0=completely flat
#define SHADOW_HEIGHT 0.1 //how far above the floor to render the shadow
//johnfitz

/*
=================
R_DrawAliasModel_ShowTris -- johnfitz
=================
*/
void R_DrawAliasModel_ShowTris (entity_t *e)
{
	aliashdr_t	*paliashdr;
	lerpdata_t	lerpdata;
	float	blend;

	//
	// setup pose/lerp data -- do it first so we don't miss updates due to culling
	//
	paliashdr = (aliashdr_t *)Mod_Extradata (e->model);
	R_SetupAliasFrame (paliashdr, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);

	//
	// cull it
	//
	if (R_CullModelForEntity(e))
		return;

	//
	// transform it
	//
	float model_matrix[16];
	IdentityMatrix(model_matrix);
	R_RotateForEntity (model_matrix, lerpdata.origin, lerpdata.angles);

	float fovscale = 1.0f;
	if (e == &cl.viewent && scr_fov.value > 90.f)
	{
		fovscale = tan(scr_fov.value * (0.5f * M_PI / 180.f));
		fovscale = 1.f + (fovscale - 1.f) * cl_gun_fovscale.value;
	}

	float translation_matrix[16];
	TranslationMatrix (translation_matrix, paliashdr->scale_origin[0], paliashdr->scale_origin[1] * fovscale, paliashdr->scale_origin[2] * fovscale);
	MatrixMultiply(model_matrix, translation_matrix);

	// Scale multiplied by 255 because we use UNORM instead of USCALED in the vertex shader
	float scale_matrix[16];
	ScaleMatrix (scale_matrix, paliashdr->scale[0] * 255.0f, paliashdr->scale[1] * fovscale * 255.0f, paliashdr->scale[2] * fovscale * 255.0f);
	MatrixMultiply(model_matrix, scale_matrix);

	if (r_showtris.value == 1)
		R_BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.alias_showtris_pipeline);
	else
		R_BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.alias_showtris_depth_test_pipeline);

	if (lerpdata.pose1 != lerpdata.pose2)
		blend = lerpdata.blend;
	else // poses the same means either 1. the entity has paused its animation, or 2. r_lerpmodels is disabled
		blend = 0;

	VkBuffer uniform_buffer;
	uint32_t uniform_offset;
	VkDescriptorSet ubo_set;
	aliasubo_t * ubo = (aliasubo_t*)R_UniformAllocate(sizeof(aliasubo_t), &uniform_buffer, &uniform_offset, &ubo_set);
	
	memcpy(ubo->model_matrix, model_matrix, 16 * sizeof(float));
	memset(ubo->shade_vector, 0, 3 * sizeof(float));
	ubo->blend_factor = blend;
	memset(ubo->light_color, 0, 3 * sizeof(float));
	ubo->entalpha = 1.0f;
	ubo->flags = 0;

	VkDescriptorSet descriptor_sets[3] = { nulltexture->descriptor_set, nulltexture->descriptor_set, ubo_set };
	vulkan_globals.vk_cmd_bind_descriptor_sets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.alias_pipeline.layout.handle, 0, 3, descriptor_sets, 1, &uniform_offset);

	VkBuffer vertex_buffers[3] = { currententity->model->vertex_buffer, currententity->model->vertex_buffer, currententity->model->vertex_buffer };
	VkDeviceSize vertex_offsets[3] = { (unsigned)currententity->model->vbostofs, GLARB_GetXYZOffset (paliashdr, lerpdata.pose1), GLARB_GetXYZOffset (paliashdr, lerpdata.pose2) };
	vulkan_globals.vk_cmd_bind_vertex_buffers(vulkan_globals.command_buffer, 0, 3, vertex_buffers, vertex_offsets);
	vulkan_globals.vk_cmd_bind_index_buffer(vulkan_globals.command_buffer, currententity->model->index_buffer, 0, VK_INDEX_TYPE_UINT16);

	vulkan_globals.vk_cmd_draw_indexed(vulkan_globals.command_buffer, paliashdr->numindexes, 1, 0, 0, 0);
}
