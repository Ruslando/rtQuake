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
// r_light.c

#include "quakedef.h"

int	r_dlightframecount;

const int MAX_LIGHT_ENTITIES = 512;
const int MAX_VISIBLE_LIGHT_ENTITIES = 128;


extern cvar_t r_flatlightstyles; //johnfitz

/*
==================
R_AnimateLight
==================
*/
void R_AnimateLight (void)
{
	int			i,j,k;

//
// light animations
// 'm' is normal light, 'a' is no light, 'z' is double bright
	i = (int)(cl.time*10);
	for (j=0 ; j<MAX_LIGHTSTYLES ; j++)
	{
		if (!cl_lightstyle[j].length)
		{
			d_lightstylevalue[j] = 256;
			continue;
		}
		//johnfitz -- r_flatlightstyles
		if (r_flatlightstyles.value == 2)
			k = cl_lightstyle[j].peak - 'a';
		else if (r_flatlightstyles.value == 1)
			k = cl_lightstyle[j].average - 'a';
		else
		{
			k = i % cl_lightstyle[j].length;
			k = cl_lightstyle[j].map[k] - 'a';
		}
		d_lightstylevalue[j] = k*22;
		//johnfitz
	}
}

/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights -- johnfitz -- rewritten to use LordHavoc's lighting speedup
=============
*/
void R_MarkLights (dlight_t *light, int num, mnode_t *node)
{
	mplane_t		*splitplane;
	msurface_t		*surf;
	vec3_t			impact;
	float			dist, l, maxdist;
	unsigned int	i;
	int				j, s, t;

start:

	if (node->contents < 0)
		return;

	splitplane = node->plane;
	if (splitplane->type < 3)
		dist = light->origin[splitplane->type] - splitplane->dist;
	else
		dist = DotProduct (light->origin, splitplane->normal) - splitplane->dist;

	if (dist > light->radius)
	{
		node = node->children[0];
		goto start;
	}
	if (dist < -light->radius)
	{
		node = node->children[1];
		goto start;
	}

	maxdist = light->radius*light->radius;
// mark the polygons
	surf = cl.worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		for (j=0 ; j<3 ; j++)
			impact[j] = light->origin[j] - surf->plane->normal[j]*dist;
		// clamp center of light to corner and check brightness
		l = DotProduct (impact, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3] - surf->texturemins[0];
		s = l+0.5;if (s < 0) s = 0;else if (s > surf->extents[0]) s = surf->extents[0];
		s = l - s;
		l = DotProduct (impact, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3] - surf->texturemins[1];
		t = l+0.5;if (t < 0) t = 0;else if (t > surf->extents[1]) t = surf->extents[1];
		t = l - t;
		// compare to minimum light
		if ((s*s+t*t+dist*dist) < maxdist)
		{
			if (surf->dlightframe != r_dlightframecount) // not dynamic until now
			{
				surf->dlightbits[num >> 5] = 1U << (num & 31);
				surf->dlightframe = r_dlightframecount;
			}
			else // already dynamic
				surf->dlightbits[num >> 5] |= 1U << (num & 31);
		}
	}

	if (node->children[0]->contents >= 0)
		R_MarkLights (light, num, node->children[0]);
	if (node->children[1]->contents >= 0)
		R_MarkLights (light, num, node->children[1]);
}

/*
=============
R_PushDlights
=============
*/
void R_PushDlights(void)
{
	int		i;
	dlight_t	*l;

	r_dlightframecount = r_framecount + 1;	// because the count hasn't
											//  advanced yet for this frame
	l = cl_dlights;

	for (i = 0; i<MAX_DLIGHTS; i++, l++)
	{
		if (l->die < cl.time || !l->radius)
			continue;
		R_MarkLights(l, i, cl.worldmodel->nodes);
	}
}

/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

mplane_t		*lightplane;
vec3_t			lightspot;
vec3_t			lightcolor; //johnfitz -- lit support via lordhavoc

/*
=============
RecursiveLightPoint -- johnfitz -- replaced entire function for lit support via lordhavoc
=============
*/
int RecursiveLightPoint (vec3_t color, mnode_t *node, vec3_t rayorg, vec3_t start, vec3_t end, float *maxdist)
{
	float		front, back, frac;
	vec3_t		mid;

loc0:
	if (node->contents < 0)
		return false;		// didn't hit anything

// calculate mid point
	if (node->plane->type < 3)
	{
		front = start[node->plane->type] - node->plane->dist;
		back = end[node->plane->type] - node->plane->dist;
	}
	else
	{
		front = DotProduct(start, node->plane->normal) - node->plane->dist;
		back = DotProduct(end, node->plane->normal) - node->plane->dist;
	}

	// LordHavoc: optimized recursion
	if ((back < 0) == (front < 0))
//		return RecursiveLightPoint (color, node->children[front < 0], rayorg, start, end, maxdist);
	{
		node = node->children[front < 0];
		goto loc0;
	}

	frac = front / (front-back);
	mid[0] = start[0] + (end[0] - start[0])*frac;
	mid[1] = start[1] + (end[1] - start[1])*frac;
	mid[2] = start[2] + (end[2] - start[2])*frac;

// go down front side
	if (RecursiveLightPoint (color, node->children[front < 0], rayorg, start, mid, maxdist))
		return true;	// hit something
	else
	{
		unsigned int i;
		int ds, dt;
		msurface_t *surf;
	// check for impact on this node
		VectorCopy (mid, lightspot);
		lightplane = node->plane;

		surf = cl.worldmodel->surfaces + node->firstsurface;
		for (i = 0;i < node->numsurfaces;i++, surf++)
		{
			float sfront, sback, dist;
			vec3_t raydelta;

			if (surf->flags & SURF_DRAWTILED)
				continue;	// no lightmaps

		// ericw -- added double casts to force 64-bit precision.
		// Without them the zombie at the start of jam3_ericw.bsp was
		// incorrectly being lit up in SSE builds.
			ds = (int) ((double) DoublePrecisionDotProduct (mid, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3]);
			dt = (int) ((double) DoublePrecisionDotProduct (mid, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3]);

			if (ds < surf->texturemins[0] || dt < surf->texturemins[1])
				continue;

			ds -= surf->texturemins[0];
			dt -= surf->texturemins[1];

			if (ds > surf->extents[0] || dt > surf->extents[1])
				continue;

			if (surf->plane->type < 3)
			{
				sfront = rayorg[surf->plane->type] - surf->plane->dist;
				sback = end[surf->plane->type] - surf->plane->dist;
			}
			else
			{
				sfront = DotProduct(rayorg, surf->plane->normal) - surf->plane->dist;
				sback = DotProduct(end, surf->plane->normal) - surf->plane->dist;
			}
			VectorSubtract(end, rayorg, raydelta);
			dist = sfront / (sfront - sback) * VectorLength(raydelta);

			if (!surf->samples)
			{
				// We hit a surface that is flagged as lightmapped, but doesn't have actual lightmap info.
				// Instead of just returning black, we'll keep looking for nearby surfaces that do have valid samples.
				// This fixes occasional pitch-black models in otherwise well-lit areas in DOTM (e.g. mge1m1, mge4m1)
				// caused by overlapping surfaces with mixed lighting data.
				const float nearby = 8.f;
				dist += nearby;
				*maxdist = q_min(*maxdist, dist);
				continue;
			}

			if (dist < *maxdist)
			{
				// LordHavoc: enhanced to interpolate lighting
				byte *lightmap;
				int maps, line3, dsfrac = ds & 15, dtfrac = dt & 15, r00 = 0, g00 = 0, b00 = 0, r01 = 0, g01 = 0, b01 = 0, r10 = 0, g10 = 0, b10 = 0, r11 = 0, g11 = 0, b11 = 0;
				float scale;
				line3 = ((surf->extents[0]>>4)+1)*3;

				lightmap = surf->samples + ((dt>>4) * ((surf->extents[0]>>4)+1) + (ds>>4))*3; // LordHavoc: *3 for color

				for (maps = 0;maps < MAXLIGHTMAPS && surf->styles[maps] != 255;maps++)
				{
					scale = (float) d_lightstylevalue[surf->styles[maps]] * 1.0 / 256.0;
					r00 += (float) lightmap[      0] * scale;g00 += (float) lightmap[      1] * scale;b00 += (float) lightmap[2] * scale;
					r01 += (float) lightmap[      3] * scale;g01 += (float) lightmap[      4] * scale;b01 += (float) lightmap[5] * scale;
					r10 += (float) lightmap[line3+0] * scale;g10 += (float) lightmap[line3+1] * scale;b10 += (float) lightmap[line3+2] * scale;
					r11 += (float) lightmap[line3+3] * scale;g11 += (float) lightmap[line3+4] * scale;b11 += (float) lightmap[line3+5] * scale;
					lightmap += ((surf->extents[0]>>4)+1) * ((surf->extents[1]>>4)+1)*3; // LordHavoc: *3 for colored lighting
				}

				color[0] += (float) ((int) ((((((((r11-r10) * dsfrac) >> 4) + r10)-((((r01-r00) * dsfrac) >> 4) + r00)) * dtfrac) >> 4) + ((((r01-r00) * dsfrac) >> 4) + r00)));
				color[1] += (float) ((int) ((((((((g11-g10) * dsfrac) >> 4) + g10)-((((g01-g00) * dsfrac) >> 4) + g00)) * dtfrac) >> 4) + ((((g01-g00) * dsfrac) >> 4) + g00)));
				color[2] += (float) ((int) ((((((((b11-b10) * dsfrac) >> 4) + b10)-((((b01-b00) * dsfrac) >> 4) + b00)) * dtfrac) >> 4) + ((((b01-b00) * dsfrac) >> 4) + b00)));
			}
			return true; // success
		}

	// go down back side
		return RecursiveLightPoint (color, node->children[front >= 0], rayorg, mid, end, maxdist);
	}
}

/*
=============
R_LightPoint -- johnfitz -- replaced entire function for lit support via lordhavoc
=============
*/
int R_LightPoint (vec3_t p)
{
	vec3_t		end;
	float		maxdist = 8192.f; //johnfitz -- was 2048

	if (!cl.worldmodel->lightdata)
	{
		lightcolor[0] = lightcolor[1] = lightcolor[2] = 255;
		return 255;
	}

	end[0] = p[0];
	end[1] = p[1];
	end[2] = p[2] - maxdist;

	lightcolor[0] = lightcolor[1] = lightcolor[2] = 0;
	RecursiveLightPoint (lightcolor, cl.worldmodel->nodes, p, p, end, &maxdist);
	return ((lightcolor[0] + lightcolor[1] + lightcolor[2]) * (1.0f / 3.0f));
}

//rt_light_entity_t light_entities[512];
//int light_entities_count = 0;

void R_InitWorldLightEntities(void) {

	if (vulkan_globals.rt_light_entities_buffer.buffer == NULL) {
		buffer_create(&vulkan_globals.rt_light_entities_buffer, MAX_LIGHT_ENTITIES * sizeof(rt_light_entity_shader_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

		buffer_create(&vulkan_globals.rt_light_entities_list_buffer, MAX_VISIBLE_LIGHT_ENTITIES * sizeof(uint16_t), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

		vulkan_globals.rt_light_entities = (rt_light_entity_t*) malloc(MAX_LIGHT_ENTITIES * sizeof(rt_light_entity_t));
		vulkan_globals.rt_light_entities_count = 0;
	}
	else {
		memset(vulkan_globals.rt_light_entities, 0, MAX_LIGHT_ENTITIES * sizeof(rt_light_entity_t));
		vulkan_globals.rt_light_entities_count = 0;
	};

}

void R_AddWorldLightEntity(float x, float y, float z, float radius, int lightStyle, float r, float g, float b) {
	rt_light_entity_t rt_light_ent;
	rt_light_ent.origin_radius[0] = x;
	rt_light_ent.origin_radius[1] = y;
	rt_light_ent.origin_radius[2] = z;
	rt_light_ent.origin_radius[3] = radius;

	rt_light_ent.lightStyle = lightStyle;

	rt_light_ent.absmin[0] = x;
	rt_light_ent.absmin[1] = y;
	rt_light_ent.absmin[2] = z;

	rt_light_ent.absmax[0] = x;
	rt_light_ent.absmax[1] = y;
	rt_light_ent.absmax[2] = z;

	rt_light_ent.light_color[0] = r;
	rt_light_ent.light_color[1] = g;
	rt_light_ent.light_color[2] = b;

	rt_light_ent.index = vulkan_globals.rt_light_entities_count;

	vulkan_globals.rt_light_entities[vulkan_globals.rt_light_entities_count] = rt_light_ent;
	vulkan_globals.rt_light_entities_count++;
}

void R_CopyLightEntitiesToBuffer(void)
{
	rt_light_entity_shader_t* light_shader = (rt_light_entity_shader_t*)malloc(vulkan_globals.rt_light_entities_count * sizeof(rt_light_entity_shader_t));
	for (int i = 0; i < vulkan_globals.rt_light_entities_count; i++) {
		light_shader[i].origin_radius[0] = vulkan_globals.rt_light_entities[i].origin_radius[0]; light_shader[i].origin_radius[1] = vulkan_globals.rt_light_entities[i].origin_radius[1];
		light_shader[i].origin_radius[2] = vulkan_globals.rt_light_entities[i].origin_radius[2]; light_shader[i].origin_radius[3] = vulkan_globals.rt_light_entities[i].origin_radius[3];

		light_shader[i].light_color[0] = vulkan_globals.rt_light_entities[i].light_color[0]; light_shader[i].light_color[1] = vulkan_globals.rt_light_entities[i].light_color[1];
		light_shader[i].light_color[2] = vulkan_globals.rt_light_entities[i].light_color[2]; light_shader[i].light_color[3] = vulkan_globals.rt_light_entities[i].light_color[3];

		light_shader[i].light_clamp[0] = vulkan_globals.rt_light_entities[i].light_clamp[0]; light_shader[i].light_clamp[1] = vulkan_globals.rt_light_entities[i].light_clamp[1];
		light_shader[i].light_clamp[2] = vulkan_globals.rt_light_entities[i].light_clamp[2]; light_shader[i].light_clamp[3] = vulkan_globals.rt_light_entities[i].light_clamp[3];
	}

	if (vulkan_globals.rt_light_entities_count > 0) {
		void* data = buffer_map(&vulkan_globals.rt_light_entities_buffer);
		memcpy(data, light_shader, vulkan_globals.rt_light_entities_count * sizeof(rt_light_entity_shader_t));
		buffer_unmap(&vulkan_globals.rt_light_entities_buffer);
	}
}

int lightSort(const void* a, const void* b) {
	rt_light_entity_t light1 = *((rt_light_entity_t*)a);
	rt_light_entity_t light2 = *((rt_light_entity_t*)b);

	return light1.distance - light2.distance;
}

void R_CreateLightEntitiesList(vec3_t viewpos) {

	if (vulkan_globals.rt_light_entities_buffer.buffer != NULL) {
		int numVisLights = 0;

		for (int i = 0; i < vulkan_globals.rt_light_entities_count; i++) {
			if (numVisLights >= MAX_VISIBLE_LIGHT_ENTITIES) { break; }

			rt_light_entity_t light_entity = vulkan_globals.rt_light_entities[i];

			vec3_t distance_vec;
			VectorSubtract(viewpos, light_entity.origin_radius, distance_vec);
			vec_t distance = VectorLength(distance_vec);
			vulkan_globals.rt_light_entities[i].distance = distance;
		}
	}

	qsort(vulkan_globals.rt_light_entities, vulkan_globals.rt_light_entities_count, sizeof(rt_light_entity_t), lightSort);
	
	rt_light_entity_t light_entity1 = vulkan_globals.rt_light_entities[0];
	rt_light_entity_t light_entity2 = vulkan_globals.rt_light_entities[1];
	rt_light_entity_t light_entity3 = vulkan_globals.rt_light_entities[2];
	light_entity1;
	light_entity2;
	light_entity3;

	uint16_t* visible_light_entity_indices = (uint16_t*)malloc(MAX_VISIBLE_LIGHT_ENTITIES * sizeof(uint16_t));

	for (int i = 0; i < MAX_VISIBLE_LIGHT_ENTITIES; i++) {
		visible_light_entity_indices[i] = vulkan_globals.rt_light_entities[i].index;
	}

	void* entities_list_data = buffer_map(&vulkan_globals.rt_light_entities_list_buffer);
	memcpy(entities_list_data, visible_light_entity_indices, MAX_VISIBLE_LIGHT_ENTITIES * sizeof(uint16_t));
	buffer_unmap(&vulkan_globals.rt_light_entities_list_buffer);

	free(visible_light_entity_indices);
}

