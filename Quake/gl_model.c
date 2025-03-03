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
// models.c -- model loading and caching

// models are the only shared resource between a client and server running
// on the same machine.

#include "quakedef.h"

qmodel_t	*loadmodel;
char	loadname[32];	// for hunk tags

static void Mod_LoadSpriteModel (qmodel_t *mod, void *buffer);
static void Mod_LoadBrushModel (qmodel_t *mod, void *buffer);
static void Mod_LoadAliasModel (qmodel_t *mod, void *buffer);
qmodel_t *Mod_LoadModel (qmodel_t *mod, qboolean crash);

cvar_t	external_ents = {"external_ents", "1", CVAR_ARCHIVE};
cvar_t	external_vis = {"external_vis", "1", CVAR_ARCHIVE};

static byte	*mod_novis;
static int	mod_novis_capacity;

static byte	*mod_decompressed;
static int	mod_decompressed_capacity;

#define	MAX_MOD_KNOWN	2048 /*johnfitz -- was 512 */
qmodel_t	mod_known[MAX_MOD_KNOWN];
int		mod_numknown;

texture_t	*r_notexture_mip; //johnfitz -- moved here from r_main.c
texture_t	*r_notexture_mip2; //johnfitz -- used for non-lightmapped surfs with a missing texture

/*
===============
ReadShortUnaligned
===============
*/
static short ReadShortUnaligned(byte * ptr)
{
	short temp;
	memcpy(&temp, ptr, sizeof(short));
	return LittleShort(temp);
}

/*
===============
ReadLongUnaligned
===============
*/
static int ReadLongUnaligned(byte * ptr)
{
	int temp;
	memcpy(&temp, ptr, sizeof(int));
	return LittleLong(temp);
}

/*
===============
ReadFloatUnaligned
===============
*/
static float ReadFloatUnaligned(byte * ptr)
{
	float temp;
	memcpy(&temp, ptr, sizeof(float));
	return LittleFloat(temp);
}

/*
===============
Mod_Init
===============
*/
void Mod_Init (void)
{
	Cvar_RegisterVariable (&gl_subdivide_size);
	Cvar_RegisterVariable (&external_vis);
	Cvar_RegisterVariable (&external_ents);

	//johnfitz -- create notexture miptex
	r_notexture_mip = (texture_t *) Hunk_AllocName (sizeof(texture_t), "r_notexture_mip");
	strcpy (r_notexture_mip->name, "notexture");
	r_notexture_mip->height = r_notexture_mip->width = 32;

	r_notexture_mip2 = (texture_t *) Hunk_AllocName (sizeof(texture_t), "r_notexture_mip2");
	strcpy (r_notexture_mip2->name, "notexture2");
	r_notexture_mip2->height = r_notexture_mip2->width = 32;
	//johnfitz
}

/*
===============
Mod_Extradata

Caches the data if needed
===============
*/
void *Mod_Extradata (qmodel_t *mod)
{
	void	*r;

	r = Cache_Check (&mod->cache);
	if (r)
		return r;

	Mod_LoadModel (mod, true);

	if (!mod->cache.data)
		Sys_Error ("Mod_Extradata: caching failed");
	return mod->cache.data;
}

/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *Mod_PointInLeaf (float *p, qmodel_t *model)
{
	mnode_t		*node;
	float		d;
	mplane_t	*plane;

	if (!model || !model->nodes)
		Sys_Error ("Mod_PointInLeaf: bad model");

	node = model->nodes;
	while (1)
	{
		if (node->contents < 0)
			return (mleaf_t *)node;
		plane = node->plane;
		d = DotProduct (p,plane->normal) - plane->dist;
		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}

	return NULL;	// never reached
}


/*
===================
Mod_DecompressVis
===================
*/
byte *Mod_DecompressVis (byte *in, qmodel_t *model)
{
	int		c;
	byte	*out;
	byte	*outend;
	int		row;

	row = (model->numleafs+7)>>3;
	if (mod_decompressed == NULL || row > mod_decompressed_capacity)
	{
		mod_decompressed_capacity = row;
		mod_decompressed = (byte *) realloc (mod_decompressed, mod_decompressed_capacity);
		if (!mod_decompressed)
			Sys_Error ("Mod_DecompressVis: realloc() failed on %d bytes", mod_decompressed_capacity);
	}
	out = mod_decompressed;
	outend = mod_decompressed + row;

	if (!in)
	{	// no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}
		return mod_decompressed;
	}

	do
	{
		if (*in)
		{
			*out++ = *in++;
			continue;
		}

		c = in[1];
		in += 2;
		if (c > row - (out - mod_decompressed))
			c = row - (out - mod_decompressed);	//now that we're dynamically allocating pvs buffers, we have to be more careful to avoid heap overflows with buggy maps.
		while (c)
		{
			if (out == outend)
			{
				if(!model->viswarn) {
					model->viswarn = true;
					Con_Warning("Mod_DecompressVis: output overrun on model \"%s\"\n", model->name);
				}
				return mod_decompressed;
			}
			*out++ = 0;
			c--;
		}
	} while (out - mod_decompressed < row);

	return mod_decompressed;
}

byte *Mod_LeafPVS (mleaf_t *leaf, qmodel_t *model)
{
	if (leaf == model->leafs)
		return Mod_NoVisPVS (model);
	return Mod_DecompressVis (leaf->compressed_vis, model);
}

byte *Mod_NoVisPVS (qmodel_t *model)
{
	int pvsbytes;
 
	pvsbytes = (model->numleafs+7)>>3;
	if (mod_novis == NULL || pvsbytes > mod_novis_capacity)
	{
		mod_novis_capacity = pvsbytes;
		mod_novis = (byte *) realloc (mod_novis, mod_novis_capacity);
		if (!mod_novis)
			Sys_Error ("Mod_NoVisPVS: realloc() failed on %d bytes", mod_novis_capacity);
		
		memset(mod_novis, 0xff, mod_novis_capacity);
	}
	return mod_novis;
}

/*
===================
Mod_ClearAll
===================
*/
void Mod_ClearAll (void)
{
	int		i;
	qmodel_t	*mod;

	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
		if (mod->type != mod_alias)
		{
			mod->needload = true;
			TexMgr_FreeTexturesForOwner (mod); //johnfitz
		}

	InvalidateTraceLineCache();
}

void Mod_ResetAll (void)
{
	int		i;
	qmodel_t	*mod;

	//ericw -- free alias model VBOs
	GLMesh_DeleteVertexBuffers ();
	
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (!mod->needload) //otherwise Mod_ClearAll() did it already
			TexMgr_FreeTexturesForOwner (mod);
		memset(mod, 0, sizeof(qmodel_t));
	}
	mod_numknown = 0;
}

/*
==================
Mod_FindName

==================
*/
qmodel_t *Mod_FindName (const char *name)
{
	int		i;
	qmodel_t	*mod;

	if (!name[0])
		Sys_Error ("Mod_FindName: NULL name"); //johnfitz -- was "Mod_ForName"

//
// search the currently loaded models
//
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
		if (!strcmp (mod->name, name) )
			break;

	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
			Sys_Error ("mod_numknown == MAX_MOD_KNOWN");
		q_strlcpy (mod->name, name, MAX_QPATH);
		mod->needload = true;
		mod_numknown++;
		InvalidateTraceLineCache();
	}

	return mod;
}

/*
==================
Mod_TouchModel

==================
*/
void Mod_TouchModel (const char *name)
{
	qmodel_t	*mod;

	mod = Mod_FindName (name);

	if (!mod->needload)
	{
		if (mod->type == mod_alias)
			Cache_Check (&mod->cache);
	}
}

/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/
qmodel_t *Mod_LoadModel (qmodel_t *mod, qboolean crash)
{
	byte	*buf;
	byte	stackbuf[1024];		// avoid dirtying the cache heap
	int	mod_type;

	if (!mod->needload)
	{
		if (mod->type == mod_alias)
		{
			if (Cache_Check (&mod->cache))
				return mod;
		}
		else
			return mod;		// not cached at all
	}

	InvalidateTraceLineCache();

//
// load the file
//
	buf = COM_LoadStackFile (mod->name, stackbuf, sizeof(stackbuf), & mod->path_id);
	if (!buf)
	{
		if (crash)
			Host_Error ("Mod_LoadModel: %s not found", mod->name); //johnfitz -- was "Mod_NumForName"
		return NULL;
	}

//
// allocate a new model
//
	COM_FileBase (mod->name, loadname, sizeof(loadname));

	loadmodel = mod;

//
// fill it in
//

// call the apropriate loader
	mod->needload = false;

	mod_type = (buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));
	switch (mod_type)
	{
	case IDPOLYHEADER:
		Mod_LoadAliasModel (mod, buf);
		break;

	case IDSPRITEHEADER:
		Mod_LoadSpriteModel (mod, buf);
		break;

	default:
		Mod_LoadBrushModel (mod, buf);
		break;
	}

	return mod;
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
qmodel_t *Mod_ForName (const char *name, qboolean crash)
{
	qmodel_t	*mod;

	mod = Mod_FindName (name);

	return Mod_LoadModel (mod, crash);
}


/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/

byte	*mod_base;

/*
=================
Mod_CheckFullbrights -- johnfitz
=================
*/
qboolean Mod_CheckFullbrights (byte *pixels, int count)
{
	int i;
	for (i = 0; i < count; i++)
		if (*pixels++ > 223)
			return true;
	return false;
}

/*
=================
Mod_CheckAnimTextureArrayQ64

Quake64 bsp
Check if we have any missing textures in the array
=================
*/
qboolean Mod_CheckAnimTextureArrayQ64(texture_t *anims[], int numTex)
{
	int i;

	for (i = 0; i < numTex; i++)
	{
		if (!anims[i])
			return false;
	}
	return true;
}

/*
=================
Mod_LoadTextures
=================
*/
void Mod_LoadTextures (lump_t *l)
{
	int		i, j, pixels, num, maxanim, altmax;
	miptex_t mt;
	texture_t	*tx, *tx2;
	texture_t	*anims[10];
	texture_t	*altanims[10];
	byte		*m;
	byte 		*pixels_p;
	char		texturename[64];
	int			nummiptex;
	int 		dataofs;
	src_offset_t		offset;
	int			mark, fwidth, fheight;
	char		filename[MAX_OSPATH], filename2[MAX_OSPATH], mapname[MAX_OSPATH];
	byte		*data;
	extern byte *hunk_base;

	//johnfitz -- don't return early if no textures; still need to create dummy texture
	if (!l->filelen)
	{
		Con_Printf ("Mod_LoadTextures: no textures in bsp file\n");
		nummiptex = 0;
		m = NULL; // avoid bogus compiler warning
	}
	else
	{
		m = mod_base + l->fileofs;
		nummiptex = ReadLongUnaligned(m + offsetof(dmiptexlump_t, nummiptex));
	}
	//johnfitz

	loadmodel->numtextures = nummiptex + 2; //johnfitz -- need 2 dummy texture chains for missing textures
	loadmodel->textures = (texture_t **) Hunk_AllocName (loadmodel->numtextures * sizeof(*loadmodel->textures) , loadname);

	for (i=0 ; i<nummiptex ; i++)
	{
		dataofs = ReadLongUnaligned(m + offsetof(dmiptexlump_t, dataofs[i]));
		if (dataofs == -1)
			continue;
		memcpy(&mt, m + dataofs, sizeof(miptex_t));
		mt.width = LittleLong (mt.width);
		mt.height = LittleLong (mt.height);
		for (j=0 ; j<MIPLEVELS ; j++)
			mt.offsets[j] = LittleLong (mt.offsets[j]);

		if ( (mt.width & 15) || (mt.height & 15) )
		{
			if (loadmodel->bspversion != BSPVERSION_QUAKE64)
				Sys_Error ("Texture %s is not 16 aligned", mt.name);
		}

		pixels = mt.width*mt.height/64*85;
		tx = (texture_t *) Hunk_AllocName (sizeof(texture_t) +pixels, loadname );
		loadmodel->textures[i] = tx;

		memcpy (tx->name, mt.name, sizeof(tx->name));
		tx->width = mt.width;
		tx->height = mt.height;
		for (j=0 ; j<MIPLEVELS ; j++)
			tx->offsets[j] = mt.offsets[j] + sizeof(texture_t) - sizeof(miptex_t);
		// the pixels immediately follow the structures

		// ericw -- check for pixels extending past the end of the lump.
		// appears in the wild; e.g. jam2_tronyn.bsp (func_mapjam2),
		// kellbase1.bsp (quoth), and can lead to a segfault if we read past
		// the end of the .bsp file buffer
		pixels_p = m + dataofs + sizeof(miptex_t);
		if ((pixels_p + pixels) > (mod_base + l->fileofs + l->filelen))
		{
			Con_DPrintf("Texture %s extends past end of lump\n", mt.name);
			pixels = q_max(0, (mod_base + l->fileofs + l->filelen) - pixels_p);
		}

		tx->update_warp = false; //johnfitz
		tx->warpimage = NULL; //johnfitz
		tx->fullbright = NULL; //johnfitz
		tx->shift = 0;	// Q64 only

		if (loadmodel->bspversion != BSPVERSION_QUAKE64)
		{
			memcpy ( tx+1, pixels_p, pixels);
		}
		else
		{ // Q64 bsp
			tx->shift = ReadLongUnaligned(m + dataofs + offsetof(miptex64_t, shift));
			memcpy ( tx+1, m + dataofs + sizeof(miptex64_t), pixels);
		}

		//johnfitz -- lots of changes
		if (!isDedicated) //no texture uploading for dedicated server
		{
			if (!q_strncasecmp(tx->name,"sky",3)) //sky texture //also note -- was Q_strncmp, changed to match qbsp
			{
				if (loadmodel->bspversion == BSPVERSION_QUAKE64)
					Sky_LoadTextureQ64 (tx);
				else
					Sky_LoadTexture (tx);
			}
			else if (tx->name[0] == '*') //warping texture
			{
				//external textures -- first look in "textures/mapname/" then look in "textures/"
				mark = Hunk_LowMark();
				COM_StripExtension (loadmodel->name + 5, mapname, sizeof(mapname));
				q_snprintf (filename, sizeof(filename), "textures/%s/#%s", mapname, tx->name+1); //this also replaces the '*' with a '#'
				data = Image_LoadImage (filename, &fwidth, &fheight);
				if (!data)
				{
					q_snprintf (filename, sizeof(filename), "textures/#%s", tx->name+1);
					data = Image_LoadImage (filename, &fwidth, &fheight);
				}

				//now load whatever we found
				if (data) //load external image
				{
					q_strlcpy (texturename, filename, sizeof(texturename));
					tx->gltexture = TexMgr_LoadImage (loadmodel, texturename, fwidth, fheight,
						SRC_RGBA, data, filename, 0, TEXPREF_NONE);
				}
				else //use the texture from the bsp file
				{
					q_snprintf (texturename, sizeof(texturename), "%s:%s", loadmodel->name, tx->name);
					offset = (src_offset_t)(pixels_p) - (src_offset_t)mod_base;
					tx->gltexture = TexMgr_LoadImage (loadmodel, texturename, tx->width, tx->height,
						SRC_INDEXED, (byte *)(tx+1), loadmodel->name, offset, TEXPREF_NONE);
				}

				//now create the warpimage, using dummy data from the hunk to create the initial image
				Hunk_Alloc (WARPIMAGESIZE*WARPIMAGESIZE*4); //make sure hunk is big enough so we don't reach an illegal address
				Hunk_FreeToLowMark (mark);
				q_snprintf (texturename, sizeof(texturename), "%s_warp", texturename);
				tx->warpimage = TexMgr_LoadImage (loadmodel, texturename, WARPIMAGESIZE,
					WARPIMAGESIZE, SRC_RGBA, hunk_base, "", (src_offset_t)hunk_base, TEXPREF_NOPICMIP | TEXPREF_WARPIMAGE);
				tx->update_warp = true;
			}
			else //regular texture
			{
				// ericw -- fence textures
				int	extraflags;

				extraflags = 0;
				if (tx->name[0] == '{')
					extraflags |= TEXPREF_ALPHA;
				// ericw

				//external textures -- first look in "textures/mapname/" then look in "textures/"
				mark = Hunk_LowMark ();
				COM_StripExtension (loadmodel->name + 5, mapname, sizeof(mapname));
				q_snprintf (filename, sizeof(filename), "textures/%s/%s", mapname, tx->name);
				data = Image_LoadImage (filename, &fwidth, &fheight);
				if (!data)
				{
					q_snprintf (filename, sizeof(filename), "textures/%s", tx->name);
					data = Image_LoadImage (filename, &fwidth, &fheight);
				}

				//now load whatever we found
				if (data) //load external image
				{
					tx->gltexture = TexMgr_LoadImage (loadmodel, filename, fwidth, fheight,
						SRC_RGBA, data, filename, 0, TEXPREF_MIPMAP | extraflags );

					//now try to load glow/luma image from the same place
					Hunk_FreeToLowMark (mark);
					q_snprintf (filename2, sizeof(filename2), "%s_glow", filename);
					data = Image_LoadImage (filename2, &fwidth, &fheight);
					if (!data)
					{
						q_snprintf (filename2, sizeof(filename2), "%s_luma", filename);
						data = Image_LoadImage (filename2, &fwidth, &fheight);
					}

					if (data)
						tx->fullbright = TexMgr_LoadImage (loadmodel, filename2, fwidth, fheight,
							SRC_RGBA, data, filename, 0, TEXPREF_MIPMAP | extraflags );
				}
				else //use the texture from the bsp file
				{
					q_snprintf (texturename, sizeof(texturename), "%s:%s", loadmodel->name, tx->name);
					offset = (src_offset_t)(pixels_p) - (src_offset_t)mod_base;
					if (Mod_CheckFullbrights ((byte *)(tx+1), pixels))
					{
						tx->gltexture = TexMgr_LoadImage (loadmodel, texturename, tx->width, tx->height,
							SRC_INDEXED, (byte *)(tx+1), loadmodel->name, offset, TEXPREF_MIPMAP | TEXPREF_NOBRIGHT | extraflags);
						q_snprintf (texturename, sizeof(texturename), "%s:%s_glow", loadmodel->name, tx->name);
						tx->fullbright = TexMgr_LoadImage (loadmodel, texturename, tx->width, tx->height,
							SRC_INDEXED, (byte *)(tx+1), loadmodel->name, offset, TEXPREF_MIPMAP | TEXPREF_FULLBRIGHT | extraflags);
					}
					else
					{
						tx->gltexture = TexMgr_LoadImage (loadmodel, texturename, tx->width, tx->height,
							SRC_INDEXED, (byte *)(tx+1), loadmodel->name, offset, TEXPREF_MIPMAP | extraflags);
					}
				}
				Hunk_FreeToLowMark (mark);
			}
		}
		//johnfitz
	}

	//johnfitz -- last 2 slots in array should be filled with dummy textures
	loadmodel->textures[loadmodel->numtextures-2] = r_notexture_mip; //for lightmapped surfs
	loadmodel->textures[loadmodel->numtextures-1] = r_notexture_mip2; //for SURF_DRAWTILED surfs

//
// sequence the animations
//
	for (i=0 ; i<nummiptex ; i++)
	{
		tx = loadmodel->textures[i];
		if (!tx || tx->name[0] != '+')
			continue;
		if (tx->anim_next)
			continue;	// allready sequenced

	// find the number of frames in the animation
		memset (anims, 0, sizeof(anims));
		memset (altanims, 0, sizeof(altanims));

		maxanim = tx->name[1];
		altmax = 0;
		if (maxanim >= 'a' && maxanim <= 'z')
			maxanim -= 'a' - 'A';
		if (maxanim >= '0' && maxanim <= '9')
		{
			maxanim -= '0';
			altmax = 0;
			anims[maxanim] = tx;
			maxanim++;
		}
		else if (maxanim >= 'A' && maxanim <= 'J')
		{
			altmax = maxanim - 'A';
			maxanim = 0;
			altanims[altmax] = tx;
			altmax++;
		}
		else
			Sys_Error ("Bad animating texture %s", tx->name);

		for (j=i+1 ; j<nummiptex ; j++)
		{
			tx2 = loadmodel->textures[j];
			if (!tx2 || tx2->name[0] != '+')
				continue;
			if (strcmp (tx2->name+2, tx->name+2))
				continue;

			num = tx2->name[1];
			if (num >= 'a' && num <= 'z')
				num -= 'a' - 'A';
			if (num >= '0' && num <= '9')
			{
				num -= '0';
				anims[num] = tx2;
				if (num+1 > maxanim)
					maxanim = num + 1;
			}
			else if (num >= 'A' && num <= 'J')
			{
				num = num - 'A';
				altanims[num] = tx2;
				if (num+1 > altmax)
					altmax = num+1;
			}
			else
				Sys_Error ("Bad animating texture %s", tx->name);
		}

		if (loadmodel->bspversion == BSPVERSION_QUAKE64 && !Mod_CheckAnimTextureArrayQ64(anims, maxanim))
			continue; // Just pretend this is a normal texture

#define	ANIM_CYCLE	2
	// link them all together
		for (j=0 ; j<maxanim ; j++)
		{
			tx2 = anims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s",j, tx->name);
			tx2->anim_total = maxanim * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = anims[ (j+1)%maxanim ];
			if (altmax)
				tx2->alternate_anims = altanims[0];
		}
		for (j=0 ; j<altmax ; j++)
		{
			tx2 = altanims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s",j, tx->name);
			tx2->anim_total = altmax * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = altanims[ (j+1)%altmax ];
			if (maxanim)
				tx2->alternate_anims = anims[0];
		}
	}
}

/*
=================
Mod_LoadLighting -- johnfitz -- replaced with lit support code via lordhavoc
=================
*/
void Mod_LoadLighting (lump_t *l)
{
	int i, mark;
	byte *in, *out, *data;
	byte d, q64_b0, q64_b1;
	char litfilename[MAX_OSPATH];
	unsigned int path_id;

	loadmodel->lightdata = NULL;
	// LordHavoc: check for a .lit file
	q_strlcpy(litfilename, loadmodel->name, sizeof(litfilename));
	COM_StripExtension(litfilename, litfilename, sizeof(litfilename));
	q_strlcat(litfilename, ".lit", sizeof(litfilename));
	mark = Hunk_LowMark();
	data = (byte*) COM_LoadHunkFile (litfilename, &path_id);
	if (data)
	{
		// use lit file only from the same gamedir as the map
		// itself or from a searchpath with higher priority.
		if (path_id < loadmodel->path_id)
		{
			Hunk_FreeToLowMark(mark);
			Con_DPrintf("ignored %s from a gamedir with lower priority\n", litfilename);
		}
		else
		if (data[0] == 'Q' && data[1] == 'L' && data[2] == 'I' && data[3] == 'T')
		{
			i = ReadLongUnaligned(data + sizeof(int));
			if (i == 1)
			{
				if (8+l->filelen*3 == com_filesize)
				{
					Con_DPrintf2("%s loaded\n", litfilename);
					loadmodel->lightdata = data + 8;
					return;
				}
				Hunk_FreeToLowMark(mark);
				Con_Printf("Outdated .lit file (%s should be %u bytes, not %u)\n", litfilename, 8+l->filelen*3, com_filesize);
			}
			else
			{
				Hunk_FreeToLowMark(mark);
				Con_Printf("Unknown .lit file version (%d)\n", i);
			}
		}
		else
		{
			Hunk_FreeToLowMark(mark);
			Con_Printf("Corrupt .lit file (old version?), ignoring\n");
		}
	}
	// LordHavoc: no .lit found, expand the white lighting data to color
	if (!l->filelen)
		return;

	// Quake64 bsp lighmap data
	if (loadmodel->bspversion == BSPVERSION_QUAKE64)
	{
		// RGB lightmap samples are packed in 16bits.
		// RRRRR GGGGG BBBBBB

		loadmodel->lightdata = (byte *) Hunk_AllocName ( (l->filelen / 2)*3, litfilename);
		in = mod_base + l->fileofs;
		out = loadmodel->lightdata;

		for (i = 0;i < (l->filelen / 2) ;i++)
		{
			q64_b0 = *in++;
			q64_b1 = *in++;

			*out++ = q64_b0 & 0xf8;/* 0b11111000 */
			*out++ = ((q64_b0 & 0x07) << 5) + ((q64_b1 & 0xc0) >> 5);/* 0b00000111, 0b11000000 */
			*out++ = (q64_b1 & 0x3f) << 2;/* 0b00111111 */
		}
		return;
	}

	loadmodel->lightdata = (byte *) Hunk_AllocName ( l->filelen*3, litfilename);
	in = loadmodel->lightdata + l->filelen*2; // place the file at the end, so it will not be overwritten until the very last write
	out = loadmodel->lightdata;
	memcpy (in, mod_base + l->fileofs, l->filelen);
	for (i = 0;i < l->filelen;i++)
	{
		d = *in++;
		*out++ = d;
		*out++ = d;
		*out++ = d;
	}
}


/*
=================
Mod_LoadVisibility
=================
*/
void Mod_LoadVisibility (lump_t *l)
{
	loadmodel->viswarn = false;
	if (!l->filelen)
	{
		loadmodel->visdata = NULL;
		return;
	}
	loadmodel->visdata = (byte *) Hunk_AllocName ( l->filelen, loadname);
	memcpy (loadmodel->visdata, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadEntities
=================
*/
void Mod_LoadEntities (lump_t *l)
{
	char	basemapname[MAX_QPATH];
	char	entfilename[MAX_QPATH];
	char		*ents;
	int		mark;
	unsigned int	path_id;
	unsigned int	crc = 0;

	if (! external_ents.value)
		goto _load_embedded;

	mark = Hunk_LowMark();
	if (l->filelen > 0) {
		crc = CRC_Block(mod_base + l->fileofs, l->filelen - 1);
	}

	q_strlcpy(basemapname, loadmodel->name, sizeof(basemapname));
	COM_StripExtension(basemapname, basemapname, sizeof(basemapname));

	q_snprintf(entfilename, sizeof(entfilename), "%s@%04x.ent", basemapname, crc);
	Con_DPrintf2("trying to load %s\n", entfilename);
	ents = (char *) COM_LoadHunkFile (entfilename, &path_id);

	if (!ents)
	{
		q_snprintf(entfilename, sizeof(entfilename), "%s.ent", basemapname);
		Con_DPrintf2("trying to load %s\n", entfilename);
		ents = (char *) COM_LoadHunkFile (entfilename, &path_id);
	}

	if (ents)
	{
		// use ent file only from the same gamedir as the map
		// itself or from a searchpath with higher priority.
		if (path_id < loadmodel->path_id)
		{
			Hunk_FreeToLowMark(mark);
			Con_DPrintf("ignored %s from a gamedir with lower priority\n", entfilename);
		}
		else
		{
			loadmodel->entities = ents;
			Con_DPrintf("Loaded external entity file %s\n", entfilename);
			return;
		}
	}

_load_embedded:
	if (!l->filelen)
	{
		loadmodel->entities = NULL;
		return;
	}
	loadmodel->entities = (char *) Hunk_AllocName ( l->filelen, loadname);
	memcpy (loadmodel->entities, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadVertexes
=================
*/
void Mod_LoadVertexes (lump_t *l)
{
	byte	*in;
	mvertex_t	*out;
	int			i, count;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof(dvertex_t))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(dvertex_t);
	out = (mvertex_t *) Hunk_AllocName ( count*sizeof(*out), loadname);

	loadmodel->vertexes = out;
	loadmodel->numvertexes = count;

	for (i=0 ; i<count ; i++, in += sizeof(dvertex_t), out++)
	{
		out->position[0] = ReadFloatUnaligned (in + offsetof(dvertex_t, point[0]));
		out->position[1] = ReadFloatUnaligned (in + offsetof(dvertex_t, point[1]));
		out->position[2] = ReadFloatUnaligned (in + offsetof(dvertex_t, point[2]));
	}
}

/*
=================
Mod_LoadEdges
=================
*/
void Mod_LoadEdges (lump_t *l, int bsp2)
{
	medge_t *out;
	int 	i, count;

	if (bsp2)
	{
		byte *in = mod_base + l->fileofs;

		if (l->filelen % sizeof(dledge_t))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(dledge_t);
		out = (medge_t *) Hunk_AllocName ( (count + 1) * sizeof(*out), loadname);

		loadmodel->edges = out;
		loadmodel->numedges = count;

		for (i=0 ; i<count ; i++, in += sizeof(dledge_t), out++)
		{
			out->v[0] = ReadLongUnaligned(in + offsetof(dledge_t, v[0]));
			out->v[1] = ReadLongUnaligned(in + offsetof(dledge_t, v[1]));
		}
	}
	else
	{
		byte *in = mod_base + l->fileofs;

		if (l->filelen % sizeof(dsedge_t))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(dsedge_t);
		out = (medge_t *) Hunk_AllocName ( (count + 1) * sizeof(*out), loadname);

		loadmodel->edges = out;
		loadmodel->numedges = count;

		for (i=0 ; i<count ; i++, in += sizeof(dsedge_t), out++)
		{
			out->v[0] = (unsigned short)ReadShortUnaligned(in + offsetof(dsedge_t, v[0]));
			out->v[1] = (unsigned short)ReadShortUnaligned(in + offsetof(dsedge_t, v[1]));
		}
	}
}

/*
=================
Mod_LoadTexinfo
=================
*/
void Mod_LoadTexinfo (lump_t *l)
{
	byte *in;
	mtexinfo_t *out;
	int	i, j, count, miptex;
	int missing = 0; //johnfitz

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof(texinfo_t))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(texinfo_t);
	out = (mtexinfo_t *) Hunk_AllocName ( count*sizeof(*out), loadname);

	loadmodel->texinfo = out;
	loadmodel->numtexinfo = count;

	for (i=0 ; i<count ; i++, in += sizeof(texinfo_t), out++)
	{
		for (j=0 ; j<4 ; j++)
		{
			out->vecs[0][j] = ReadFloatUnaligned (in + offsetof(texinfo_t, vecs[0][j]));
			out->vecs[1][j] = ReadFloatUnaligned (in + offsetof(texinfo_t, vecs[1][j]));
		}

		miptex = ReadLongUnaligned (in + offsetof(texinfo_t, miptex));
		out->flags = ReadLongUnaligned (in + offsetof(texinfo_t, flags));

		//johnfitz -- rewrote this section
		if (miptex >= loadmodel->numtextures-1 || !loadmodel->textures[miptex])
		{
			if (out->flags & TEX_SPECIAL)
				out->texture = loadmodel->textures[loadmodel->numtextures-1];
			else
				out->texture = loadmodel->textures[loadmodel->numtextures-2];
			out->flags |= TEX_MISSING;
			missing++;
		}
		else
		{
			out->texture = loadmodel->textures[miptex];
		}
		//johnfitz
	}

	//johnfitz: report missing textures
	if (missing && loadmodel->numtextures > 1)
		Con_Printf ("Mod_LoadTexinfo: %d texture(s) missing from BSP file\n", missing);
	//johnfitz
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
void CalcSurfaceExtents (msurface_t *s)
{
	float	mins[2], maxs[2], val;
	int		i,j, e;
	mvertex_t	*v;
	mtexinfo_t	*tex;
	int		bmins[2], bmaxs[2];

	mins[0] = mins[1] = FLT_MAX;
	maxs[0] = maxs[1] = -FLT_MAX;

	tex = s->texinfo;

	for (i=0 ; i<s->numedges ; i++)
	{
		e = loadmodel->surfedges[s->firstedge+i];
		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];

		for (j=0 ; j<2 ; j++)
		{
			/* The following calculation is sensitive to floating-point
			 * precision.  It needs to produce the same result that the
			 * light compiler does, because R_BuildLightMap uses surf->
			 * extents to know the width/height of a surface's lightmap,
			 * and incorrect rounding here manifests itself as patches
			 * of "corrupted" looking lightmaps.
			 * Most light compilers are win32 executables, so they use
			 * x87 floating point.  This means the multiplies and adds
			 * are done at 80-bit precision, and the result is rounded
			 * down to 32-bits and stored in val.
			 * Adding the casts to double seems to be good enough to fix
			 * lighting glitches when Quakespasm is compiled as x86_64
			 * and using SSE2 floating-point.  A potential trouble spot
			 * is the hallway at the beginning of mfxsp17.  -- ericw
			 */
			val =	((double)v->position[0] * (double)tex->vecs[j][0]) +
				((double)v->position[1] * (double)tex->vecs[j][1]) +
				((double)v->position[2] * (double)tex->vecs[j][2]) +
				(double)tex->vecs[j][3];

			if (val < mins[j])
				mins[j] = val;
			if (val > maxs[j])
				maxs[j] = val;
		}
	}

	for (i=0 ; i<2 ; i++)
	{
		bmins[i] = floor(mins[i]/16);
		bmaxs[i] = ceil(maxs[i]/16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;

		if ( !(tex->flags & TEX_SPECIAL) && s->extents[i] > 2000) //johnfitz -- was 512 in glquake, 256 in winquake
			Sys_Error ("Bad surface extents");
	}
}

/*
================
Mod_PolyForUnlitSurface -- johnfitz -- creates polys for unlightmapped surfaces (sky and water)

TODO: merge this into BuildSurfaceDisplayList?
================
*/
void Mod_PolyForUnlitSurface (msurface_t *fa)
{
	vec3_t		verts[64];
	int			numverts, i, lindex;
	float		*vec;
	glpoly_t	*poly;
	float 		*poly_vert;
	float		texscale;

	if (fa->flags & (SURF_DRAWTURB | SURF_DRAWSKY))
		texscale = (1.0/128.0); //warp animation repeats every 128
	else
		texscale = (1.0/32.0); //to match r_notexture_mip

	// convert edges back to a normal polygon
	numverts = 0;
	for (i=0 ; i<fa->numedges ; i++)
	{
		lindex = loadmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
			vec = loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position;
		else
			vec = loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position;
		VectorCopy (vec, verts[numverts]);
		numverts++;
	}

	//create the poly
	poly = (glpoly_t *) Hunk_Alloc (sizeof(glpoly_t) + (numverts-4) * VERTEXSIZE*sizeof(float));
	poly->next = NULL;
	fa->polys = poly;
	poly->numverts = numverts;
	for (i=0, vec=(float *)verts; i<numverts; i++, vec+= 3)
	{
		poly_vert = &poly->verts[0][0] + (i * VERTEXSIZE);
		VectorCopy (vec, poly_vert);
		poly_vert[3] = DotProduct(vec, fa->texinfo->vecs[0]) * texscale;
		poly_vert[4] = DotProduct(vec, fa->texinfo->vecs[1]) * texscale;
	}
}

/*
=================
Mod_LoadFaces
=================
*/
void Mod_LoadFaces (lump_t *l, qboolean bsp2)
{
	byte	*ins;
	byte	*inl;
	msurface_t 	*out;
	int			i, count, surfnum, lofs;
	int			planenum, side, texinfon;

	if (bsp2)
	{
		ins = NULL;
		inl = mod_base + l->fileofs;
		if (l->filelen % sizeof(dlface_t))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
		count = l->filelen / sizeof(dlface_t);
	}
	else
	{
		ins = mod_base + l->fileofs;
		inl = NULL;
		if (l->filelen % sizeof(dsface_t))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
		count = l->filelen / sizeof(dsface_t);
	}
	out = (msurface_t *)Hunk_AllocName ( count*sizeof(*out), loadname);

	//johnfitz -- warn mappers about exceeding old limits
	if (count > 32767 && !bsp2)
		Con_DWarning ("%i faces exceeds standard limit of 32767.\n", count);
	//johnfitz

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	for (surfnum=0 ; surfnum<count ; surfnum++, out++)
	{
		if (bsp2)
		{
			out->firstedge = ReadLongUnaligned(inl + offsetof(dlface_t, firstedge));
			out->numedges = ReadLongUnaligned(inl + offsetof(dlface_t, numedges));
			planenum = ReadLongUnaligned(inl + offsetof(dlface_t, planenum));
			side = ReadLongUnaligned(inl + offsetof(dlface_t, side));
			texinfon = ReadLongUnaligned (inl + offsetof(dlface_t, texinfo));
			for (i=0 ; i<MAXLIGHTMAPS ; i++)
				out->styles[i] = *(inl + offsetof(dlface_t, styles[i]));
			lofs = ReadLongUnaligned(inl + offsetof(dlface_t, lightofs));
			inl += sizeof(dlface_t);
		}
		else
		{
			out->firstedge = ReadLongUnaligned(ins + offsetof(dsface_t, firstedge));
			out->numedges = ReadShortUnaligned(ins + offsetof(dsface_t, numedges));
			planenum = ReadShortUnaligned(ins + offsetof(dsface_t, planenum));
			side = ReadShortUnaligned(ins + offsetof(dsface_t, side));
			texinfon = ReadShortUnaligned (ins + offsetof(dsface_t, texinfo));
			for (i=0 ; i<MAXLIGHTMAPS ; i++)
				out->styles[i] = *(ins + offsetof(dsface_t, styles[i]));
			lofs = ReadLongUnaligned(ins + offsetof(dsface_t, lightofs));
			ins += sizeof(dsface_t);
		}

		out->flags = 0;

		if (side)
			out->flags |= SURF_PLANEBACK;

		out->plane = loadmodel->planes + planenum;

		out->texinfo = loadmodel->texinfo + texinfon;

		CalcSurfaceExtents (out);

	// lighting info
		if (loadmodel->bspversion == BSPVERSION_QUAKE64)
			lofs /= 2; // Q64 samples are 16bits instead 8 in normal Quake 

		if (lofs == -1)
			out->samples = NULL;
		else
			out->samples = loadmodel->lightdata + (lofs * 3); //johnfitz -- lit support via lordhavoc (was "+ i")

		//johnfitz -- this section rewritten
		if (!q_strncasecmp(out->texinfo->texture->name,"sky",3)) // sky surface //also note -- was Q_strncmp, changed to match qbsp
		{
			out->flags |= (SURF_DRAWSKY | SURF_DRAWTILED);
			Mod_PolyForUnlitSurface (out); //no more subdivision
		}
		else if (out->texinfo->texture->name[0] == '*') // warp surface
		{
			out->flags |= (SURF_DRAWTURB | SURF_DRAWTILED);

		// detect special liquid types
			if (!strncmp (out->texinfo->texture->name, "*lava", 5))
				out->flags |= SURF_DRAWLAVA;
			else if (!strncmp (out->texinfo->texture->name, "*slime", 6))
				out->flags |= SURF_DRAWSLIME;
			else if (!strncmp (out->texinfo->texture->name, "*tele", 5))
				out->flags |= SURF_DRAWTELE;
			else out->flags |= SURF_DRAWWATER;

			Mod_PolyForUnlitSurface (out);
			GL_SubdivideSurface (out);
		}
		else if (out->texinfo->texture->name[0] == '{') // ericw -- fence textures
		{
			out->flags |= SURF_DRAWFENCE;
		}
		else if (out->texinfo->flags & TEX_MISSING) // texture is missing from bsp
		{
			if (out->samples) //lightmapped
			{
				out->flags |= SURF_NOTEXTURE;
			}
			else // not lightmapped
			{
				out->flags |= (SURF_NOTEXTURE | SURF_DRAWTILED);
				Mod_PolyForUnlitSurface (out);
			}
		}
		//johnfitz
	}
}


/*
=================
Mod_SetParent
=================
*/
void Mod_SetParent (mnode_t *node, mnode_t *parent)
{
	node->parent = parent;
	if (node->contents < 0)
		return;
	Mod_SetParent (node->children[0], node);
	Mod_SetParent (node->children[1], node);
}

/*
=================
Mod_LoadNodes
=================
*/
void Mod_LoadNodes_S (lump_t *l)
{
	int			i, j, count, p;
	byte		*in;
	mnode_t		*out;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof(dsnode_t))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(dsnode_t);
	out = (mnode_t *) Hunk_AllocName ( count*sizeof(*out), loadname);

	//johnfitz -- warn mappers about exceeding old limits
	if (count > 32767)
		Con_DWarning ("%i nodes exceeds standard limit of 32767.\n", count);
	//johnfitz

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for (i=0 ; i<count ; i++, in += sizeof(dsnode_t), out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = ReadShortUnaligned (in + offsetof(dsnode_t, mins[j]));
			out->minmaxs[3+j] = ReadShortUnaligned (in + offsetof(dsnode_t, maxs[j]));
		}

		p = ReadLongUnaligned(in + offsetof(dsnode_t, planenum));
		out->plane = loadmodel->planes + p;

		out->firstsurface = (unsigned short)ReadShortUnaligned (in + offsetof(dsnode_t, firstface)); //johnfitz -- explicit cast as unsigned short
		out->numsurfaces = (unsigned short)ReadShortUnaligned (in + offsetof(dsnode_t, numfaces)); //johnfitz -- explicit cast as unsigned short

		for (j=0 ; j<2 ; j++)
		{
			//johnfitz -- hack to handle nodes > 32k, adapted from darkplaces
			p = (unsigned short)ReadShortUnaligned(in + offsetof(dsnode_t, children[j]));
			if (p < count)
				out->children[j] = loadmodel->nodes + p;
			else
			{
				p = 65535 - p; //note this uses 65535 intentionally, -1 is leaf 0
				if (p < loadmodel->numleafs)
					out->children[j] = (mnode_t *)(loadmodel->leafs + p);
				else
				{
					Con_Printf("Mod_LoadNodes: invalid leaf index %i (file has only %i leafs)\n", p, loadmodel->numleafs);
					out->children[j] = (mnode_t *)(loadmodel->leafs); //map it to the solid leaf
				}
			}
			//johnfitz
		}
	}
}

void Mod_LoadNodes_L1 (lump_t *l)
{
	int			i, j, count, p;
	byte	*in;
	mnode_t		*out;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof(dl1node_t))
		Sys_Error ("Mod_LoadNodes: funny lump size in %s",loadmodel->name);

	count = l->filelen / sizeof(dl1node_t);
	out = (mnode_t *)Hunk_AllocName ( count*sizeof(*out), loadname);

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for (i=0 ; i<count ; i++, in += sizeof(dl1node_t), out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = ReadShortUnaligned (in + offsetof(dl1node_t, mins[j]));
			out->minmaxs[3+j] = ReadShortUnaligned (in + offsetof(dl1node_t, maxs[j]));
		}

		p = ReadLongUnaligned(in + offsetof(dl1node_t, planenum));
		out->plane = loadmodel->planes + p;

		out->firstsurface = ReadLongUnaligned (in + offsetof(dl1node_t, firstface)); //johnfitz -- explicit cast as unsigned short
		out->numsurfaces = ReadLongUnaligned (in + offsetof(dl1node_t, numfaces)); //johnfitz -- explicit cast as unsigned short

		for (j=0 ; j<2 ; j++)
		{
			//johnfitz -- hack to handle nodes > 32k, adapted from darkplaces
			p = ReadLongUnaligned(in + offsetof(dl1node_t, children[j]));
			if (p >= 0 && p < count)
				out->children[j] = loadmodel->nodes + p;
			else
			{
				p = 0xffffffff - p; //note this uses 65535 intentionally, -1 is leaf 0
				if (p >= 0 && p < loadmodel->numleafs)
					out->children[j] = (mnode_t *)(loadmodel->leafs + p);
				else
				{
					Con_Printf("Mod_LoadNodes: invalid leaf index %i (file has only %i leafs)\n", p, loadmodel->numleafs);
					out->children[j] = (mnode_t *)(loadmodel->leafs); //map it to the solid leaf
				}
			}
			//johnfitz
		}
	}
}

void Mod_LoadNodes_L2 (lump_t *l)
{
	int			i, j, count, p;
	byte	*in;
	mnode_t		*out;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof(dl2node_t))
		Sys_Error ("Mod_LoadNodes: funny lump size in %s",loadmodel->name);

	count = l->filelen / sizeof(dl2node_t);
	out = (mnode_t *)Hunk_AllocName ( count*sizeof(*out), loadname);

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for (i=0 ; i<count ; i++, in += sizeof(dl2node_t), out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = ReadFloatUnaligned (in + offsetof(dl2node_t, mins[j]));
			out->minmaxs[3+j] = ReadFloatUnaligned (in + offsetof(dl2node_t, maxs[j]));
		}

		p = ReadLongUnaligned(in + offsetof(dl2node_t, planenum));
		out->plane = loadmodel->planes + p;

		out->firstsurface = ReadLongUnaligned (in + offsetof(dl2node_t, firstface)); //johnfitz -- explicit cast as unsigned short
		out->numsurfaces = ReadLongUnaligned (in + offsetof(dl2node_t, numfaces)); //johnfitz -- explicit cast as unsigned short

		for (j=0 ; j<2 ; j++)
		{
			//johnfitz -- hack to handle nodes > 32k, adapted from darkplaces
			p = ReadLongUnaligned(in + offsetof(dl2node_t, children[j]));
			if (p > 0 && p < count)
				out->children[j] = loadmodel->nodes + p;
			else
			{
				p = 0xffffffff - p; //note this uses 65535 intentionally, -1 is leaf 0
				if (p >= 0 && p < loadmodel->numleafs)
					out->children[j] = (mnode_t *)(loadmodel->leafs + p);
				else
				{
					Con_Printf("Mod_LoadNodes: invalid leaf index %i (file has only %i leafs)\n", p, loadmodel->numleafs);
					out->children[j] = (mnode_t *)(loadmodel->leafs); //map it to the solid leaf
				}
			}
			//johnfitz
		}
	}
}

void Mod_LoadNodes (lump_t *l, int bsp2)
{
	if (bsp2 == 2)
		Mod_LoadNodes_L2(l);
	else if (bsp2)
		Mod_LoadNodes_L1(l);
	else
		Mod_LoadNodes_S(l);

	Mod_SetParent (loadmodel->nodes, NULL);	// sets nodes and leafs
}

void Mod_ProcessLeafs_S (byte *in, int filelen)
{
	mleaf_t		*out;
	int			i, j, count, p;

	if (filelen % sizeof(dsleaf_t))
		Sys_Error ("Mod_ProcessLeafs: funny lump size in %s", loadmodel->name);
	count = filelen / sizeof(dsleaf_t);
	out = (mleaf_t *) Hunk_AllocName ( count*sizeof(*out), loadname);

	//johnfitz
	if (count > 32767)
		Host_Error ("Mod_LoadLeafs: %i leafs exceeds limit of 32767.\n", count);
	//johnfitz

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for (i=0 ; i<count ; i++, in += sizeof(dsleaf_t), out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = ReadShortUnaligned (in + offsetof(dsleaf_t, mins[j]));
			out->minmaxs[3+j] = ReadShortUnaligned (in + offsetof(dsleaf_t, maxs[j]));
		}

		p = ReadLongUnaligned(in + offsetof(dsleaf_t, contents));
		out->contents = p;

		out->firstmarksurface = loadmodel->marksurfaces + (unsigned short)ReadShortUnaligned(in + offsetof(dsleaf_t, firstmarksurface)); //johnfitz -- unsigned short
		out->nummarksurfaces = (unsigned short)ReadShortUnaligned(in + offsetof(dsleaf_t, nummarksurfaces)); //johnfitz -- unsigned short

		p = ReadLongUnaligned(in + offsetof(dsleaf_t, visofs));
		if (p == -1)
			out->compressed_vis = NULL;
		else
			out->compressed_vis = (loadmodel->visdata != NULL) ? (loadmodel->visdata + p) : NULL;
		out->efrags = NULL;

		for (j=0 ; j<4 ; j++)
			out->ambient_sound_level[j] = *(in + offsetof(dsleaf_t, ambient_level[j]));

		//johnfitz -- removed code to mark surfaces as SURF_UNDERWATER
	}
}

void Mod_ProcessLeafs_L1 (byte *in, int filelen)
{
	mleaf_t		*out;
	int			i, j, count, p;

	if (filelen % sizeof(dl1leaf_t))
		Sys_Error ("Mod_ProcessLeafs: funny lump size in %s", loadmodel->name);

	count = filelen / sizeof(dl1leaf_t);

	out = (mleaf_t *) Hunk_AllocName (count * sizeof(*out), loadname);

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for (i=0 ; i<count ; i++, in += sizeof(dl1leaf_t), out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = ReadShortUnaligned (in + offsetof(dl1leaf_t, mins[j]));
			out->minmaxs[3+j] = ReadShortUnaligned (in + offsetof(dl1leaf_t, maxs[j]));
		}

		p = ReadLongUnaligned(in + offsetof(dl1leaf_t, contents));
		out->contents = p;

		out->firstmarksurface = loadmodel->marksurfaces + ReadLongUnaligned(in + offsetof(dl1leaf_t, firstmarksurface)); //johnfitz -- unsigned short
		out->nummarksurfaces = ReadLongUnaligned(in + offsetof(dl1leaf_t, nummarksurfaces)); //johnfitz -- unsigned short

		p = ReadLongUnaligned(in + offsetof(dl1leaf_t, visofs));
		if (p == -1)
			out->compressed_vis = NULL;
		else
			out->compressed_vis = loadmodel->visdata + p;
		out->efrags = NULL;

		for (j=0 ; j<4 ; j++)
			out->ambient_sound_level[j] = *(in + offsetof(dl1leaf_t, ambient_level[j]));

		//johnfitz -- removed code to mark surfaces as SURF_UNDERWATER
	}
}

void Mod_ProcessLeafs_L2 (byte *in, int filelen)
{
	mleaf_t		*out;
	int			i, j, count, p;

	if (filelen % sizeof(dl2leaf_t))
		Sys_Error ("Mod_ProcessLeafs: funny lump size in %s", loadmodel->name);

	count = filelen / sizeof(dl2leaf_t);

	out = (mleaf_t *) Hunk_AllocName (count * sizeof(*out), loadname);

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for (i=0 ; i<count ; i++, in += sizeof(dl2leaf_t), out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = ReadFloatUnaligned (in + offsetof(dl2leaf_t, mins[j]));
			out->minmaxs[3+j] = ReadFloatUnaligned (in + offsetof(dl2leaf_t, maxs[j]));
		}

		p = ReadLongUnaligned(in + offsetof(dl2leaf_t, contents));
		out->contents = p;

		out->firstmarksurface = loadmodel->marksurfaces + ReadLongUnaligned(in + offsetof(dl2leaf_t, firstmarksurface)); //johnfitz -- unsigned short
		out->nummarksurfaces = ReadLongUnaligned(in + offsetof(dl2leaf_t, nummarksurfaces)); //johnfitz -- unsigned short

		p = ReadLongUnaligned(in + offsetof(dl2leaf_t, visofs));
		if (p == -1)
			out->compressed_vis = NULL;
		else
			out->compressed_vis = loadmodel->visdata + p;
		out->efrags = NULL;

		for (j=0 ; j<4 ; j++)
			out->ambient_sound_level[j] = *(in + offsetof(dl2leaf_t, ambient_level[j]));

		//johnfitz -- removed code to mark surfaces as SURF_UNDERWATER
	}
}

/*
=================
Mod_LoadLeafs
=================
*/
void Mod_LoadLeafs (lump_t *l, int bsp2)
{
	void *in = (void *)(mod_base + l->fileofs);

	if (bsp2 == 2)
		Mod_ProcessLeafs_L2 (in, l->filelen);
	else if (bsp2)
		Mod_ProcessLeafs_L1 (in, l->filelen);
	else
		Mod_ProcessLeafs_S  (in, l->filelen);
}

/*
=================
Mod_CheckWaterVis
=================
*/
void Mod_CheckWaterVis(void)
{
	mleaf_t		*leaf, *other;
	msurface_t * surf;
	int i, j, k;
	int numclusters = loadmodel->submodels[0].visleafs;
	int contentfound = 0;
	int contenttransparent = 0;
	int contenttype;
	unsigned hascontents = 0;

	if (r_novis.value)
	{	//all can be
		loadmodel->contentstransparent = (SURF_DRAWWATER|SURF_DRAWTELE|SURF_DRAWSLIME|SURF_DRAWLAVA);
		return;
	}

	//pvs is 1-based. leaf 0 sees all (the solid leaf).
	//leaf 0 has no pvs, and does not appear in other leafs either, so watch out for the biases.
	for (i=0,leaf=loadmodel->leafs+1 ; i<numclusters ; i++, leaf++)
	{
		byte *vis;
		if (leaf->contents < 0)	//err... wtf?
			hascontents = 0;
		if (leaf->contents == CONTENTS_WATER)
		{
			if ((contenttransparent & (SURF_DRAWWATER|SURF_DRAWTELE))==(SURF_DRAWWATER|SURF_DRAWTELE))
				continue;
			//this check is somewhat risky, but we should be able to get away with it.
			for (contenttype = 0, i = 0; i < leaf->nummarksurfaces; i++)
			{
				surf = &loadmodel->surfaces[leaf->firstmarksurface[i]];
				if (surf->flags & (SURF_DRAWWATER|SURF_DRAWTELE))
				{
					contenttype = surf->flags & (SURF_DRAWWATER|SURF_DRAWTELE);
					break;
				}
			}
			//its possible that this leaf has absolutely no surfaces in it, turb or otherwise.
			if (contenttype == 0)
				continue;
		}
		else if (leaf->contents == CONTENTS_SLIME)
			contenttype = SURF_DRAWSLIME;
		else if (leaf->contents == CONTENTS_LAVA)
			contenttype = SURF_DRAWLAVA;
		//fixme: tele
		else
			continue;
		if (contenttransparent & contenttype)
		{
			nextleaf:
			continue;	//found one of this type already
		}
		contentfound |= contenttype;
		vis = Mod_DecompressVis(leaf->compressed_vis, loadmodel);
		for (j = 0; j < (numclusters+7)/8; j++)
		{
			if (vis[j])
			{
				for (k = 0; k < 8; k++)
				{
					if (vis[j] & (1u<<k))
					{
						other = &loadmodel->leafs[(j<<3)+k+1];
						if (leaf->contents != other->contents)
						{
//							Con_Printf("%p:%i sees %p:%i\n", leaf, leaf->contents, other, other->contents);
							contenttransparent |= contenttype;
							goto nextleaf;
						}
					}
				}
			}
		}
	}

	if (!contenttransparent)
	{	//no water leaf saw a non-water leaf
		//but only warn when there's actually water somewhere there...
		if (hascontents & ((1<<-CONTENTS_WATER)
						|  (1<<-CONTENTS_SLIME)
						|  (1<<-CONTENTS_LAVA)))
			Con_DPrintf("%s is not watervised\n", loadmodel->name);
	}
	else
	{
		Con_DPrintf2("%s is vised for transparent", loadmodel->name);
		if (contenttransparent & SURF_DRAWWATER)
			Con_DPrintf2(" water");
		if (contenttransparent & SURF_DRAWTELE)
			Con_DPrintf2(" tele");
		if (contenttransparent & SURF_DRAWLAVA)
			Con_DPrintf2(" lava");
		if (contenttransparent & SURF_DRAWSLIME)
			Con_DPrintf2(" slime");
		Con_DPrintf2("\n");
	}
	//any types that we didn't find are assumed to be transparent.
	//this allows submodels to work okay (eg: ad uses func_illusionary teleporters for some reason).
	loadmodel->contentstransparent = contenttransparent | (~contentfound & (SURF_DRAWWATER|SURF_DRAWTELE|SURF_DRAWSLIME|SURF_DRAWLAVA));
}

/*
=================
SoA_FillBoxLane
=================
*/
void SoA_FillBoxLane(soa_aabb_t *boxes, int index, vec3_t mins, vec3_t maxs)
{
	float *dst = boxes[index >> 3];
	index &= 7;
	dst[index +  0] = mins[0];
	dst[index +  8] = maxs[0];
	dst[index + 16] = mins[1];
	dst[index + 24] = maxs[1];
	dst[index + 32] = mins[2];
	dst[index + 40] = maxs[2];
}

/*
=================
SoA_FillPlaneLane
=================
*/
void SoA_FillPlaneLane(soa_plane_t *planes, int index, mplane_t *src, qboolean flip)
{
	float side = flip ? -1.0f : 1.0f;
	float *dst = planes[index >> 3];
	index &= 7;
	dst[index +  0] = side * src->normal[0];
	dst[index +  8] = side * src->normal[1];
	dst[index + 16] = side * src->normal[2];
	dst[index + 24] = side * src->dist;
}

/*
=================
Mod_PrepareSIMDData
=================
*/
void Mod_PrepareSIMDData (void)
{
#ifdef USE_SIMD
	int i;

	loadmodel->soa_leafbounds = Hunk_Alloc(6 * sizeof(float) * ((loadmodel->numleafs + 7) & ~7));
	loadmodel->surfvis        = Hunk_Alloc((loadmodel->numsurfaces + 7) >> 3);
	loadmodel->soa_surfplanes = Hunk_Alloc(4 * sizeof(float) * ((loadmodel->numsurfaces + 7) & ~7));

	for (i = 0; i < loadmodel->numleafs; ++i)
	{
		mleaf_t *leaf = &loadmodel->leafs[i + 1];
		SoA_FillBoxLane(loadmodel->soa_leafbounds, i, leaf->minmaxs, leaf->minmaxs + 3);
	}

	for (i = 0; i < loadmodel->numsurfaces; ++i)
	{
		msurface_t *surf = &loadmodel->surfaces[i];
		SoA_FillPlaneLane(loadmodel->soa_surfplanes, i, surf->plane, surf->flags & SURF_PLANEBACK);
	}
#endif // def USE_SIMD
}

/*
=================
Mod_LoadClipnodes
=================
*/
void Mod_LoadClipnodes (lump_t *l, qboolean bsp2)
{
	byte *ins;
	byte *inl;

	mclipnode_t *out; //johnfitz -- was dclipnode_t
	int			i, count;
	hull_t		*hull;

	if (bsp2)
	{
		ins = NULL;
		inl = mod_base + l->fileofs;
		if (l->filelen % sizeof(dlclipnode_t))
			Sys_Error ("Mod_LoadClipnodes: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(dlclipnode_t);
	}
	else
	{
		ins = mod_base + l->fileofs;
		inl = NULL;
		if (l->filelen % sizeof(dsclipnode_t))
			Sys_Error ("Mod_LoadClipnodes: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(dsclipnode_t);
	}
	out = (mclipnode_t *) Hunk_AllocName ( count*sizeof(*out), loadname);

	//johnfitz -- warn about exceeding old limits
	if (count > 32767 && !bsp2)
		Con_DWarning ("%i clipnodes exceeds standard limit of 32767.\n", count);
	//johnfitz

	loadmodel->clipnodes = out;
	loadmodel->numclipnodes = count;

	hull = &loadmodel->hulls[1];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -16;
	hull->clip_mins[1] = -16;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 16;
	hull->clip_maxs[1] = 16;
	hull->clip_maxs[2] = 32;

	hull = &loadmodel->hulls[2];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -32;
	hull->clip_mins[1] = -32;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 32;
	hull->clip_maxs[1] = 32;
	hull->clip_maxs[2] = 64;

	if (bsp2)
	{
		for (i=0 ; i<count ; i++, out++, inl += sizeof(dlclipnode_t))
		{
			out->planenum = ReadLongUnaligned(inl + offsetof(dlclipnode_t, planenum));

			//johnfitz -- bounds check
			if (out->planenum < 0 || out->planenum >= loadmodel->numplanes)
				Host_Error ("Mod_LoadClipnodes: planenum out of bounds");
			//johnfitz

			out->children[0] = ReadLongUnaligned(inl + offsetof(dlclipnode_t, children[0]));
			out->children[1] = ReadLongUnaligned(inl + offsetof(dlclipnode_t, children[1]));
			//Spike: FIXME: bounds check
		}
	}
	else
	{
		for (i=0 ; i<count ; i++, out++, ins += sizeof(dsclipnode_t))
		{
			out->planenum = ReadLongUnaligned(ins + offsetof(dsclipnode_t, planenum));

			//johnfitz -- bounds check
			if (out->planenum < 0 || out->planenum >= loadmodel->numplanes)
				Host_Error ("Mod_LoadClipnodes: planenum out of bounds");
			//johnfitz

			//johnfitz -- support clipnodes > 32k
			out->children[0] = (unsigned short)ReadShortUnaligned(ins + offsetof(dsclipnode_t, children[0]));
			out->children[1] = (unsigned short)ReadShortUnaligned(ins + offsetof(dsclipnode_t, children[1]));

			if (out->children[0] >= count)
				out->children[0] -= 65536;
			if (out->children[1] >= count)
				out->children[1] -= 65536;
			//johnfitz
		}
	}
}

/*
=================
Mod_MakeHull0

Duplicate the drawing hull structure as a clipping hull
=================
*/
void Mod_MakeHull0 (void)
{
	mnode_t		*in, *child;
	mclipnode_t *out; //johnfitz -- was dclipnode_t
	int			i, j, count;
	hull_t		*hull;

	hull = &loadmodel->hulls[0];

	in = loadmodel->nodes;
	count = loadmodel->numnodes;
	out = (mclipnode_t *) Hunk_AllocName ( count*sizeof(*out), loadname);

	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;

	for (i=0 ; i<count ; i++, out++, in++)
	{
		out->planenum = in->plane - loadmodel->planes;
		for (j=0 ; j<2 ; j++)
		{
			child = in->children[j];
			if (child->contents < 0)
				out->children[j] = child->contents;
			else
				out->children[j] = child - loadmodel->nodes;
		}
	}
}

/*
=================
Mod_LoadMarksurfaces
=================
*/
void Mod_LoadMarksurfaces (lump_t *l, int bsp2)
{
	int		i, j, count;
	int		*out;
	if (bsp2)
	{
		byte *in = mod_base + l->fileofs;

		if (l->filelen % sizeof(unsigned int))
			Host_Error ("Mod_LoadMarksurfaces: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(unsigned int);
		out = (int*)Hunk_AllocName ( count*sizeof(*out), loadname);

		loadmodel->marksurfaces = out;
		loadmodel->nummarksurfaces = count;

		for (i=0 ; i<count ; i++)
		{
			j = ReadLongUnaligned(in + (i * sizeof(int)));
			if (j >= loadmodel->numsurfaces)
				Host_Error ("Mod_LoadMarksurfaces: bad surface number");
			out[i] = j;
		}
	}
	else
	{
		byte *in = mod_base + l->fileofs;

		if (l->filelen % sizeof(short))
			Host_Error ("Mod_LoadMarksurfaces: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(short);
		out = (int*)Hunk_AllocName ( count*sizeof(*out), loadname);

		loadmodel->marksurfaces = out;
		loadmodel->nummarksurfaces = count;

		//johnfitz -- warn mappers about exceeding old limits
		if (count > 32767)
			Con_DWarning ("%i marksurfaces exceeds standard limit of 32767.\n", count);
		//johnfitz

		for (i=0 ; i<count ; i++)
		{
			j = (unsigned short)ReadShortUnaligned(in + (i * sizeof(short))); //johnfitz -- explicit cast as unsigned short
			if (j >= loadmodel->numsurfaces)
				Sys_Error ("Mod_LoadMarksurfaces: bad surface number");
			out[i] = j;
		}
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/
void Mod_LoadSurfedges (lump_t *l)
{
	int		i, count;
	byte	*in;
	int		*out;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof(int))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(int);
	out = (int *) Hunk_AllocName ( count*sizeof(int), loadname);

	loadmodel->surfedges = out;
	loadmodel->numsurfedges = count;

	for (i=0 ; i<count ; i++)
	{
		out[i] = ReadLongUnaligned(in + (i * sizeof(int)));
	}
}


/*
=================
Mod_LoadPlanes
=================
*/
void Mod_LoadPlanes (lump_t *l)
{
	int			i, j;
	mplane_t	*out;
	byte 		*in;
	int			count;
	int			bits;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof(dplane_t))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(dplane_t);
	out = (mplane_t *) Hunk_AllocName ( count*2*sizeof(*out), loadname);

	loadmodel->planes = out;
	loadmodel->numplanes = count;

	for (i=0 ; i<count ; i++, in += sizeof(dplane_t), out++)
	{
		bits = 0;
		for (j=0 ; j<3 ; j++)
		{
			out->normal[j] = ReadFloatUnaligned (in + offsetof(dplane_t, normal[j]));
			if (out->normal[j] < 0)
				bits |= 1<<j;
		}

		out->dist = ReadFloatUnaligned (in + offsetof(dplane_t, dist));
		out->type = ReadLongUnaligned (in + offsetof(dplane_t, type));
		out->signbits = bits;
	}
}

/*
=================
RadiusFromBounds
=================
*/
float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	int		i;
	vec3_t	corner;

	for (i=0 ; i<3 ; i++)
	{
		corner[i] = fabs(mins[i]) > fabs(maxs[i]) ? fabs(mins[i]) : fabs(maxs[i]);
	}

	return VectorLength (corner);
}

/*
=================
Mod_LoadSubmodels
=================
*/
void Mod_LoadSubmodels (lump_t *l)
{
	byte	*in;
	dmodel_t	*out;
	int			i, j, count;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof(dmodel_t))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(dmodel_t);
	out = (dmodel_t *) Hunk_AllocName ( count*sizeof(*out), loadname);

	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;

	for (i=0 ; i<count ; i++, in += sizeof(dmodel_t), out++)
	{
		for (j=0 ; j<3 ; j++)
		{	// spread the mins / maxs by a pixel
			out->mins[j] = ReadFloatUnaligned (in + offsetof(dmodel_t, mins[j])) - 1;
			out->maxs[j] = ReadFloatUnaligned (in + offsetof(dmodel_t, maxs[j])) + 1;
			out->origin[j] = ReadFloatUnaligned (in + offsetof(dmodel_t, origin[j]));
		}
		for (j=0 ; j<MAX_MAP_HULLS ; j++)
		{
			out->headnode[j] = ReadLongUnaligned (in + offsetof(dmodel_t, headnode[j]));
		}
		out->visleafs = ReadLongUnaligned (in + offsetof(dmodel_t, visleafs));
		out->firstface = ReadLongUnaligned (in + offsetof(dmodel_t, firstface));
		out->numfaces = ReadLongUnaligned (in + offsetof(dmodel_t, numfaces));
	}

	// johnfitz -- check world visleafs -- adapted from bjp
	out = loadmodel->submodels;

	if (out->visleafs > 8192)
		Con_DWarning ("%i visleafs exceeds standard limit of 8192.\n", out->visleafs);
	//johnfitz
}

/*
====================
Mod_ParseEdictForLightData

Parses an edict out of the given string, returning possible light data
====================
*/
const char* Mod_ParseEdictForLightData(const char* data, edict_light_t* ent)
{
	//ddef_t* key;
	char		keyname[256];
	qboolean	init;
	int		n;

	init = false;

	// go through all the dictionary pairs
	while (1)
	{
		// parse key
		data = COM_Parse(data);
		if (com_token[0] == '}')
			break;
		if (!data)
			Host_Error("ED_ParseEntity: EOF without closing brace");


		// FIXME: change light to _light to get rid of this hack
		if (!strcmp(com_token, "light"))
			strcpy(com_token, "light_lev");	// hack for single light def

		q_strlcpy(keyname, com_token, sizeof(keyname));

		// another hack to fix keynames with trailing spaces
		n = strlen(keyname);
		while (n && keyname[n - 1] == ' ')
		{
			keyname[n - 1] = 0;
			n--;
		}

		// parse value
		data = COM_Parse(data);
		if (!data)
			Host_Error("ED_ParseEntity: EOF without closing brace");

		if (com_token[0] == '}')
			Host_Error("ED_ParseEntity: closing brace without data");

		init = true;

		// keynames with a leading underscore are used for utility comments,
		// and are immediately discarded by quake
		if (keyname[0] == '_')
			continue;

		if (!strcmp(keyname, "classname")) {
			//ent->light = atof(com_token);
			strcpy(ent->clientClassName, com_token);
		}

		if (!strcmp(keyname, "origin")) {
			char* v, * w;
			char* end;
			char	string[128];
			q_strlcpy(string, com_token, sizeof(string));
			end = (char*)string + strlen(string);
			v = string;
			w = string;

			for (int i = 0; i < 3 && (w <= end); i++) // ericw -- added (w <= end) check
			{
				// set v to the next space (or 0 byte), and change that char to a 0 byte
				while (*v && *v != ' ')
					v++;
				*v = 0;
				ent->clientOrigin[i] = atof(w);
				w = v = v + 1;
			}
		}

		if (!strcmp(keyname, "light_lev")) {
			ent->light = atof(com_token);
		}

		if (!strcmp(keyname, "style")) {
			ent->light_style = atoi(com_token);
		}
	}

	return data;
}

/*
=================
Mod_LoadLightEntities
=================
*/
void Mod_LoadLightEntities(void) {

	R_InitWorldLightEntities();

	qboolean rtLightsFile = false;	// do not support rt files right now
	// Load light entities copied from quakertx dx12 project
	const char* data = loadmodel->entities;

	edict_light_t ent;
	while (1)
	{
		memset(&ent, 0, sizeof(ent));

		// parse the opening brace
		data = COM_Parse(data);
		if (!data)
			break;
		if (com_token[0] != '{')
			Host_Error("ED_LoadFromFile: found %s when expecting {", com_token);

		data = Mod_ParseEdictForLightData(data, &ent);

		if (!rtLightsFile)
		{
			const char* entityName = ent.clientClassName;
			if (strstr(entityName, "light")) {

				// strcmp returns 0 when true, so we have to negate it. this choice is confusing
				if (!strcmp(entityName, "light_flame_large_yellow")) {
					ent.clientOrigin[2] += 20;

					if (ent.light == 0) {
						ent.light = 300;
					}

					R_AddWorldLightEntity(ent.clientOrigin[0], ent.clientOrigin[1], ent.clientOrigin[2], ent.light, ent.light_style, 1.0f, 1.0f, 0.0f);
				}

				if (!strcmp(entityName, "light_flame_small_yellow")) {
					ent.clientOrigin[2] += 20;

					if (ent.light == 0) {
						ent.light = 200;
					}

					R_AddWorldLightEntity(ent.clientOrigin[0], ent.clientOrigin[1], ent.clientOrigin[2], ent.light, ent.light_style, 1.0f, 1.0f, 0.0f);
				}

				if (!strcmp(entityName, "light_flame_small_white")) {
					ent.clientOrigin[2] += 20;

					if (ent.light == 0) {
						ent.light = 200;
					}

					R_AddWorldLightEntity(ent.clientOrigin[0], ent.clientOrigin[1], ent.clientOrigin[2], ent.light, ent.light_style, 1.0f, 1.0f, 1.0f);
				}

				if (!strcmp(entityName, "light_torch_small_walltorch")) {
					ent.clientOrigin[2] += 20;

					if (ent.light == 0) {
						ent.light = 200;
					}

					R_AddWorldLightEntity(ent.clientOrigin[0], ent.clientOrigin[1], ent.clientOrigin[2], ent.light, ent.light_style, 1.0f, 1.0f, 0.0f);
				}

			}
		}
	}

	R_CopyLightEntitiesToBuffer();
}

/*
=================
Mod_BoundsFromClipNode -- johnfitz

update the model's clipmins and clipmaxs based on each node's plane.

This works because of the way brushes are expanded in hull generation.
Each brush will include all six axial planes, which bound that brush.
Therefore, the bounding box of the hull can be constructed entirely
from axial planes found in the clipnodes for that hull.
=================
*/
void Mod_BoundsFromClipNode (qmodel_t *mod, int hull, int nodenum)
{
	mplane_t	*plane;
	mclipnode_t	*node;

	if (nodenum < 0)
		return; //hit a leafnode

	node = &mod->clipnodes[nodenum];
	plane = mod->hulls[hull].planes + node->planenum;
	switch (plane->type)
	{

	case PLANE_X:
		if (plane->signbits == 1)
			mod->clipmins[0] = q_min(mod->clipmins[0], -plane->dist - mod->hulls[hull].clip_mins[0]);
		else
			mod->clipmaxs[0] = q_max(mod->clipmaxs[0], plane->dist - mod->hulls[hull].clip_maxs[0]);
		break;
	case PLANE_Y:
		if (plane->signbits == 2)
			mod->clipmins[1] = q_min(mod->clipmins[1], -plane->dist - mod->hulls[hull].clip_mins[1]);
		else
			mod->clipmaxs[1] = q_max(mod->clipmaxs[1], plane->dist - mod->hulls[hull].clip_maxs[1]);
		break;
	case PLANE_Z:
		if (plane->signbits == 4)
			mod->clipmins[2] = q_min(mod->clipmins[2], -plane->dist - mod->hulls[hull].clip_mins[2]);
		else
			mod->clipmaxs[2] = q_max(mod->clipmaxs[2], plane->dist - mod->hulls[hull].clip_maxs[2]);
		break;
	default:
		//skip nonaxial planes; don't need them
		break;
	}

	Mod_BoundsFromClipNode (mod, hull, node->children[0]);
	Mod_BoundsFromClipNode (mod, hull, node->children[1]);
}

/* EXTERNAL VIS FILE SUPPORT:
 */
typedef struct vispatch_s
{
	char	mapname[32];
	int	filelen;	// length of data after header (VIS+Leafs)
} vispatch_t;
#define VISPATCH_HEADER_LEN 36

static FILE *Mod_FindVisibilityExternal(void)
{
	vispatch_t header;
	char visfilename[MAX_QPATH];
	const char* shortname;
	unsigned int path_id;
	FILE *f;
	long pos;
	size_t r;

	q_snprintf(visfilename, sizeof(visfilename), "maps/%s.vis", loadname);
	if (COM_FOpenFile(visfilename, &f, &path_id) < 0)
	{
		Con_DPrintf("%s not found, trying ", visfilename);
		q_snprintf(visfilename, sizeof(visfilename), "%s.vis", COM_SkipPath(com_gamedir));
		Con_DPrintf("%s\n", visfilename);
		if (COM_FOpenFile(visfilename, &f, &path_id) < 0)
		{
			Con_DPrintf("external vis not found\n");
			return NULL;
		}
	}
	if (path_id < loadmodel->path_id)
	{
		fclose(f);
		Con_DPrintf("ignored %s from a gamedir with lower priority\n", visfilename);
		return NULL;
	}

	Con_DPrintf("Found external VIS %s\n", visfilename);

	shortname = COM_SkipPath(loadmodel->name);
	pos = 0;
	while ((r = fread(&header, 1, VISPATCH_HEADER_LEN, f)) == VISPATCH_HEADER_LEN)
	{
		header.filelen = LittleLong(header.filelen);
		if (header.filelen <= 0) {	/* bad entry -- don't trust the rest. */
			fclose(f);
			return NULL;
		}
		if (!q_strcasecmp(header.mapname, shortname))
			break;
		pos += header.filelen + VISPATCH_HEADER_LEN;
		fseek(f, pos, SEEK_SET);
	}
	if (r != VISPATCH_HEADER_LEN) {
		fclose(f);
		Con_DPrintf("%s not found in %s\n", shortname, visfilename);
		return NULL;
	}

	return f;
}

static byte *Mod_LoadVisibilityExternal(FILE* f)
{
	int	filelen;
	byte*	visdata;

	filelen = 0;
	if (fread(&filelen, 1, 4, f) != 4)
		return NULL;
	filelen = LittleLong(filelen);
	if (filelen <= 0) return NULL;
	Con_DPrintf("...%d bytes visibility data\n", filelen);
	visdata = (byte *) Hunk_AllocName(filelen, "EXT_VIS");
	if (fread(visdata, filelen, 1, f) != filelen)
		return NULL;
	return visdata;
}

static void Mod_LoadLeafsExternal(FILE* f)
{
	int	filelen;
	void*	in;

	filelen = 0;
	if (fread(&filelen, 1, 4, f) != 4)
		Sys_Error("Invalid leaf");
	filelen = LittleLong(filelen);
	if (filelen <= 0) return;
	Con_DPrintf("...%d bytes leaf data\n", filelen);
	in = Hunk_AllocName(filelen, "EXT_LEAF");
	if (fread(in, filelen, 1, f) != filelen)
		return;
	Mod_ProcessLeafs_S((byte*)in, filelen);
}

/*
=================
Mod_LoadBrushModel
=================
*/
static void Mod_LoadBrushModel (qmodel_t *mod, void *buffer)
{
	int			i, j;
	int			bsp2;
	dheader_t	*header;
	dmodel_t 	*bm;
	float		radius; //johnfitz

	loadmodel->type = mod_brush;

	header = (dheader_t *)buffer;

	mod->bspversion = LittleLong (header->version);

	switch(mod->bspversion)
	{
	case BSPVERSION:
		bsp2 = false;
		break;
	case BSP2VERSION_2PSB:
		bsp2 = 1;	//first iteration
		break;
	case BSP2VERSION_BSP2:
		bsp2 = 2;	//sanitised revision
		break;
	case BSPVERSION_QUAKE64:
		bsp2 = false;
		break;
	default:
		Sys_Error ("Mod_LoadBrushModel: %s has unsupported version number (%i)", mod->name, mod->bspversion);
		break;
	}

// swap all the lumps
	mod_base = (byte *)header;

	for (i = 0; i < (int) sizeof(dheader_t) / 4; i++)
		((int *)header)[i] = LittleLong ( ((int *)header)[i]);

// load into heap

	Mod_LoadVertexes (&header->lumps[LUMP_VERTEXES]);
	Mod_LoadEdges (&header->lumps[LUMP_EDGES], bsp2);
	Mod_LoadSurfedges (&header->lumps[LUMP_SURFEDGES]);
	Mod_LoadTextures (&header->lumps[LUMP_TEXTURES]);
	Mod_LoadLighting (&header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes (&header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo (&header->lumps[LUMP_TEXINFO]);
	Mod_LoadFaces (&header->lumps[LUMP_FACES], bsp2);
	Mod_LoadMarksurfaces (&header->lumps[LUMP_MARKSURFACES], bsp2);

	if (mod->bspversion == BSPVERSION && external_vis.value && sv.modelname[0] && !q_strcasecmp(loadname, sv.name))
	{
		FILE* fvis;
		Con_DPrintf("trying to open external vis file\n");
		fvis = Mod_FindVisibilityExternal();
		if (fvis) {
			int mark = Hunk_LowMark();
			loadmodel->leafs = NULL;
			loadmodel->numleafs = 0;
			Con_DPrintf("found valid external .vis file for map\n");
			loadmodel->visdata = Mod_LoadVisibilityExternal(fvis);
			if (loadmodel->visdata) {
				Mod_LoadLeafsExternal(fvis);
			}
			fclose(fvis);
			if (loadmodel->visdata && loadmodel->leafs && loadmodel->numleafs) {
				goto visdone;
			}
			Hunk_FreeToLowMark(mark);
			Con_DPrintf("External VIS data failed, using standard vis.\n");
		}
	}

	Mod_LoadVisibility (&header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs (&header->lumps[LUMP_LEAFS], bsp2);
visdone:
	Mod_LoadNodes (&header->lumps[LUMP_NODES], bsp2);
	Mod_LoadClipnodes (&header->lumps[LUMP_CLIPNODES], bsp2);
	Mod_LoadEntities (&header->lumps[LUMP_ENTITIES]);
	Mod_LoadSubmodels (&header->lumps[LUMP_MODELS]);
	Mod_LoadLightEntities();

	Mod_PrepareSIMDData ();
	Mod_MakeHull0 ();

	mod->numframes = 2;		// regular and alternate animation

	Mod_CheckWaterVis ();

//
// set up the submodels (FIXME: this is confusing)
//

	// johnfitz -- okay, so that i stop getting confused every time i look at this loop, here's how it works:
	// we're looping through the submodels starting at 0.  Submodel 0 is the main model, so we don't have to
	// worry about clobbering data the first time through, since it's the same data.  At the end of the loop,
	// we create a new copy of the data to use the next time through.
	for (i=0 ; i<mod->numsubmodels ; i++)
	{
		bm = &mod->submodels[i];

		if (i > 0) {
			for (int d = 0; d < bm->numfaces; d++)
			{
				mod->surfaces[bm->firstface + d].bmodelindex = i + 1;
			}
		}
		
		mod->hulls[0].firstclipnode = bm->headnode[0];
		for (j=1 ; j<MAX_MAP_HULLS ; j++)
		{
			mod->hulls[j].firstclipnode = bm->headnode[j];
			mod->hulls[j].lastclipnode = mod->numclipnodes-1;
		}

		mod->firstmodelsurface = bm->firstface;
		mod->nummodelsurfaces = bm->numfaces;

		VectorCopy (bm->maxs, mod->maxs);
		VectorCopy (bm->mins, mod->mins);

		//johnfitz -- calculate rotate bounds and yaw bounds
		radius = RadiusFromBounds (mod->mins, mod->maxs);
		mod->rmaxs[0] = mod->rmaxs[1] = mod->rmaxs[2] = mod->ymaxs[0] = mod->ymaxs[1] = mod->ymaxs[2] = radius;
		mod->rmins[0] = mod->rmins[1] = mod->rmins[2] = mod->ymins[0] = mod->ymins[1] = mod->ymins[2] = -radius;
		//johnfitz

		//johnfitz -- correct physics cullboxes so that outlying clip brushes on doors and stuff are handled right
		if (i > 0 || strcmp(mod->name, sv.modelname) != 0) //skip submodel 0 of sv.worldmodel, which is the actual world
		{
			// start with the hull0 bounds
			VectorCopy (mod->maxs, mod->clipmaxs);
			VectorCopy (mod->mins, mod->clipmins);

			// process hull1 (we don't need to process hull2 becuase there's
			// no such thing as a brush that appears in hull2 but not hull1)
			//Mod_BoundsFromClipNode (mod, 1, mod->hulls[1].firstclipnode); // (disabled for now becuase it fucks up on rotating models)
		}
		//johnfitz

		mod->numleafs = bm->visleafs;

		if (i < mod->numsubmodels-1)
		{	// duplicate the basic information
			char	name[12];

			sprintf (name, "*%i", i+1);
			loadmodel = Mod_FindName (name);
			*loadmodel = *mod;
			strcpy (loadmodel->name, name);
#ifdef PSET_SCRIPT
			// Need to NULL this otherwise we double delete in PScript_ClearSurfaceParticles
			loadmodel->skytrimem = NULL;
#endif
			mod = loadmodel;
		}
	}
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

aliashdr_t	*pheader;

stvert_t	stverts[MAXALIASVERTS];
mtriangle_t	triangles[MAXALIASTRIS];

// a pose is a single set of vertexes.  a frame may be
// an animating sequence of poses
trivertx_t	*poseverts[MAXALIASFRAMES];
int			posenum;

byte		**player_8bit_texels_tbl;
byte		*player_8bit_texels;

/*
=================
Mod_LoadAliasFrame
=================
*/
void * Mod_LoadAliasFrame (void * pin, maliasframedesc_t *frame)
{
	trivertx_t		*pinframe;
	int				i;
	daliasframe_t	*pdaliasframe;

	if (posenum >= MAXALIASFRAMES)
		Sys_Error ("posenum >= MAXALIASFRAMES");

	pdaliasframe = (daliasframe_t *)pin;

	strcpy (frame->name, pdaliasframe->name);
	frame->firstpose = posenum;
	frame->numposes = 1;

	for (i=0 ; i<3 ; i++)
	{
		// these are byte values, so we don't have to worry about
		// endianness
		frame->bboxmin.v[i] = pdaliasframe->bboxmin.v[i];
		frame->bboxmax.v[i] = pdaliasframe->bboxmax.v[i];
	}


	pinframe = (trivertx_t *)(pdaliasframe + 1);

	poseverts[posenum] = pinframe;
	posenum++;

	pinframe += pheader->numverts;

	return (void *)pinframe;
}


/*
=================
Mod_LoadAliasGroup
=================
*/
void *Mod_LoadAliasGroup (void * pin,  maliasframedesc_t *frame)
{
	daliasgroup_t		*pingroup;
	int					i, numframes;
	daliasinterval_t	*pin_intervals;
	void				*ptemp;

	pingroup = (daliasgroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	frame->firstpose = posenum;
	frame->numposes = numframes;

	for (i=0 ; i<3 ; i++)
	{
		// these are byte values, so we don't have to worry about endianness
		frame->bboxmin.v[i] = pingroup->bboxmin.v[i];
		frame->bboxmax.v[i] = pingroup->bboxmax.v[i];
	}


	pin_intervals = (daliasinterval_t *)(pingroup + 1);

	frame->interval = LittleFloat (pin_intervals->interval);

	pin_intervals += numframes;

	ptemp = (void *)pin_intervals;

	for (i=0 ; i<numframes ; i++)
	{
		if (posenum >= MAXALIASFRAMES)
			Sys_Error ("posenum >= MAXALIASFRAMES");

		poseverts[posenum] = (trivertx_t *)((daliasframe_t *)ptemp + 1);
		posenum++;

		ptemp = (trivertx_t *)((daliasframe_t *)ptemp + 1) + pheader->numverts;
	}

	return ptemp;
}

//=========================================================


/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes - Ed
=================
*/

typedef struct
{
	short		x, y;
} floodfill_t;

// must be a power of 2
#define	FLOODFILL_FIFO_SIZE		0x1000
#define	FLOODFILL_FIFO_MASK		(FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP( off, dx, dy )				\
do {								\
	if (pos[off] == fillcolor)				\
	{							\
		pos[off] = 255;					\
		fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
		inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;	\
	}							\
	else if (pos[off] != 255) fdc = pos[off];		\
} while (0)

void Mod_FloodFillSkin( byte *skin, int skinwidth, int skinheight )
{
	byte		fillcolor = *skin; // assume this is the pixel to fill
	floodfill_t	fifo[FLOODFILL_FIFO_SIZE];
	int			inpt = 0, outpt = 0;
	int			filledcolor = -1;
	int			i;

	if (filledcolor == -1)
	{
		filledcolor = 0;
		// attempt to find opaque black
		for (i = 0; i < 256; ++i)
			if (d_8to24table[i] == (255 << 0)) // alpha 1.0
			{
				filledcolor = i;
				break;
			}
	}

	// can't fill to filled color or to transparent color (used as visited marker)
	if ((fillcolor == filledcolor) || (fillcolor == 255))
	{
		//printf( "not filling skin from %d to %d\n", fillcolor, filledcolor );
		return;
	}

	fifo[inpt].x = 0, fifo[inpt].y = 0;
	inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

	while (outpt != inpt)
	{
		int			x = fifo[outpt].x, y = fifo[outpt].y;
		int			fdc = filledcolor;
		byte		*pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

		if (x > 0)				FLOODFILL_STEP( -1, -1, 0 );
		if (x < skinwidth - 1)	FLOODFILL_STEP( 1, 1, 0 );
		if (y > 0)				FLOODFILL_STEP( -skinwidth, 0, -1 );
		if (y < skinheight - 1)	FLOODFILL_STEP( skinwidth, 0, 1 );
		skin[x + skinwidth * y] = fdc;
	}
}

/*
===============
Mod_LoadAllSkins
===============
*/
void *Mod_LoadAllSkins (int numskins, byte *pskintype)
{
	int			i, j, k, size, groupskins;
	char			name[MAX_QPATH];
	byte			*skin, *texels;
	byte			*pinskingroup;
	byte			*pinskinintervals;
	char			fbr_mask_name[MAX_QPATH]; //johnfitz -- added for fullbright support
	src_offset_t		offset; //johnfitz
	unsigned int		texflags = TEXPREF_PAD;

	if (numskins < 1 || numskins > MAX_SKINS)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of skins: %d\n", numskins);

	size = pheader->skinwidth * pheader->skinheight;

	if (loadmodel->flags & MF_HOLEY)
		texflags |= TEXPREF_ALPHA;

	for (i=0 ; i<numskins ; i++)
	{
		if (ReadLongUnaligned(pskintype + offsetof(daliasskintype_t, type)) == ALIAS_SKIN_SINGLE)
		{
			skin = pskintype + sizeof(daliasskintype_t);
			Mod_FloodFillSkin( skin, pheader->skinwidth, pheader->skinheight );

			// save 8 bit texels for the player model to remap
			texels = (byte *) Hunk_AllocName(size, loadname);
			pheader->texels[i] = texels - (byte *)pheader;
			memcpy (texels, skin, size);

			//johnfitz -- rewritten
			q_snprintf (name, sizeof(name), "%s:frame%i", loadmodel->name, i);
			offset = (src_offset_t)(skin) - (src_offset_t)mod_base;
			if (Mod_CheckFullbrights (skin, size))
			{
				pheader->gltextures[i][0] = TexMgr_LoadImage (loadmodel, name, pheader->skinwidth, pheader->skinheight,
					SRC_INDEXED, skin, loadmodel->name, offset, texflags | TEXPREF_NOBRIGHT);
				q_snprintf (fbr_mask_name, sizeof(fbr_mask_name), "%s:frame%i_glow", loadmodel->name, i);
				pheader->fbtextures[i][0] = TexMgr_LoadImage (loadmodel, fbr_mask_name, pheader->skinwidth, pheader->skinheight,
					SRC_INDEXED, skin, loadmodel->name, offset, texflags | TEXPREF_FULLBRIGHT);
			}
			else
			{
				pheader->gltextures[i][0] = TexMgr_LoadImage (loadmodel, name, pheader->skinwidth, pheader->skinheight,
					SRC_INDEXED, skin, loadmodel->name, offset, texflags);
				pheader->fbtextures[i][0] = NULL;
			}

			pheader->gltextures[i][3] = pheader->gltextures[i][2] = pheader->gltextures[i][1] = pheader->gltextures[i][0];
			pheader->fbtextures[i][3] = pheader->fbtextures[i][2] = pheader->fbtextures[i][1] = pheader->fbtextures[i][0];
			//johnfitz

			pskintype += sizeof(daliasskintype_t) + size;
		}
		else
		{
			// animating skin group.  yuck.
			pinskingroup = pskintype + sizeof(daliasskintype_t);
			groupskins = ReadLongUnaligned (pinskingroup + offsetof(daliasskingroup_t, numskins));
			pinskinintervals = pinskingroup + sizeof(daliasskingroup_t);
			skin = pinskinintervals + (groupskins * sizeof(daliasskininterval_t));

			for (j=0 ; j<groupskins ; j++)
			{
				Mod_FloodFillSkin (skin, pheader->skinwidth, pheader->skinheight);
				if (j == 0) {
					texels = (byte *) Hunk_AllocName(size, loadname);
					pheader->texels[i] = texels - (byte *)pheader;
					memcpy (texels, skin, size);
				}

				//johnfitz -- rewritten
				q_snprintf (name, sizeof(name), "%s:frame%i_%i", loadmodel->name, i,j);
				offset = (src_offset_t)(skin) - (src_offset_t)mod_base; //johnfitz
				if (Mod_CheckFullbrights (skin, size))
				{
					pheader->gltextures[i][j&3] = TexMgr_LoadImage (loadmodel, name, pheader->skinwidth, pheader->skinheight,
						SRC_INDEXED, skin, loadmodel->name, offset, texflags | TEXPREF_NOBRIGHT);
					q_snprintf (fbr_mask_name, sizeof(fbr_mask_name), "%s:frame%i_%i_glow", loadmodel->name, i,j);
					pheader->fbtextures[i][j&3] = TexMgr_LoadImage (loadmodel, fbr_mask_name, pheader->skinwidth, pheader->skinheight,
						SRC_INDEXED, skin, loadmodel->name, offset, texflags | TEXPREF_FULLBRIGHT);
				}
				else
				{
					pheader->gltextures[i][j&3] = TexMgr_LoadImage (loadmodel, name, pheader->skinwidth, pheader->skinheight,
						SRC_INDEXED, skin, loadmodel->name, offset, texflags);
					pheader->fbtextures[i][j&3] = NULL;
				}
				//johnfitz

				skin += size;
			}
			k = j;
			for (/**/; j < 4; j++)
				pheader->gltextures[i][j&3] = pheader->gltextures[i][j - k];

			pskintype = skin;
		}
	}

	return (void *)pskintype;
}

//=========================================================================

/*
=================
Mod_CalcAliasBounds -- johnfitz -- calculate bounds of alias model for nonrotated, yawrotated, and fullrotated cases
=================
*/
void Mod_CalcAliasBounds (aliashdr_t *a)
{
	int			i,j,k;
	float		dist, yawradius, radius;
	vec3_t		v;

	//clear out all data
	for (i=0; i<3;i++)
	{
		loadmodel->mins[i] = loadmodel->ymins[i] = loadmodel->rmins[i] = FLT_MAX;
		loadmodel->maxs[i] = loadmodel->ymaxs[i] = loadmodel->rmaxs[i] = -FLT_MAX;
		radius = yawradius = 0;
	}

	//process verts
	for (i=0 ; i<a->numposes; i++)
		for (j=0; j<a->numverts; j++)
		{
			for (k=0; k<3;k++)
				v[k] = poseverts[i][j].v[k] * pheader->scale[k] + pheader->scale_origin[k];

			for (k=0; k<3;k++)
			{
				loadmodel->mins[k] = q_min(loadmodel->mins[k], v[k]);
				loadmodel->maxs[k] = q_max(loadmodel->maxs[k], v[k]);
			}
			dist = v[0] * v[0] + v[1] * v[1];
			if (yawradius < dist)
				yawradius = dist;
			dist += v[2] * v[2];
			if (radius < dist)
				radius = dist;
		}

	//rbounds will be used when entity has nonzero pitch or roll
	radius = sqrt(radius);
	loadmodel->rmins[0] = loadmodel->rmins[1] = loadmodel->rmins[2] = -radius;
	loadmodel->rmaxs[0] = loadmodel->rmaxs[1] = loadmodel->rmaxs[2] = radius;

	//ybounds will be used when entity has nonzero yaw
	yawradius = sqrt(yawradius);
	loadmodel->ymins[0] = loadmodel->ymins[1] = -yawradius;
	loadmodel->ymaxs[0] = loadmodel->ymaxs[1] = yawradius;
	loadmodel->ymins[2] = loadmodel->mins[2];
	loadmodel->ymaxs[2] = loadmodel->maxs[2];
}

static qboolean
nameInList(const char *list, const char *name)
{
	const char *s;
	char tmp[MAX_QPATH];
	int i;

	s = list;

	while (*s)
	{
		// make a copy until the next comma or end of string
		i = 0;
		while (*s && *s != ',')
		{
			if (i < MAX_QPATH - 1)
				tmp[i++] = *s;
			s++;
		}
		tmp[i] = '\0';
		//compare it to the model name
		if (!strcmp(name, tmp))
		{
			return true;
		}
		//search forwards to the next comma or end of string
		while (*s && *s == ',')
			s++;
	}
	return false;
}

/*
=================
Mod_SetExtraFlags -- johnfitz -- set up extra flags that aren't in the mdl
=================
*/
void Mod_SetExtraFlags (qmodel_t *mod)
{
	extern cvar_t r_nolerp_list;

	if (!mod)
		return;

	mod->flags &= (0xFF | MF_HOLEY); //only preserve first byte, plus MF_HOLEY

	if (mod->type == mod_alias)
	{
		// nolerp flag
		if (nameInList(r_nolerp_list.string, mod->name))
			mod->flags |= MOD_NOLERP;

	// fullbright hack (TODO: make this a cvar list)
	if (!strcmp (mod->name, "progs/flame2.mdl") ||
		!strcmp (mod->name, "progs/flame.mdl") ||
		!strcmp (mod->name, "progs/boss.mdl"))
		mod->flags |= MOD_FBRIGHTHACK;
	}

#ifdef PSET_SCRIPT
	PScript_UpdateModelEffects(mod);
#endif
}

/*
=================
Mod_LoadAliasModel
=================
*/
static void Mod_LoadAliasModel (qmodel_t *mod, void *buffer)
{
	int					i, j;
	byte			*pinstverts;
	byte			*pintriangles;
	int					version, numframes;
	int					size;
	byte	*pframetype;
	byte	*pskintype;
	int					start, end, total;

	start = Hunk_LowMark ();

	mod_base = (byte *)buffer; //johnfitz

	version = ReadLongUnaligned (mod_base + offsetof(mdl_t, version));
	if (version != ALIAS_VERSION)
		Sys_Error ("%s has wrong version number (%i should be %i)",
				 mod->name, version, ALIAS_VERSION);

//
// allocate space for a working header, plus all the data except the frames,
// skin and group info
//
	size	= sizeof(aliashdr_t) +
		 (ReadLongUnaligned (mod_base + offsetof(mdl_t, numframes)) - 1) * sizeof (pheader->frames[0]);
	pheader = (aliashdr_t *) Hunk_AllocName (size, loadname);

	mod->flags = ReadLongUnaligned (mod_base + offsetof(mdl_t, flags));

//
// endian-adjust and copy the data, starting with the alias model header
//
	pheader->boundingradius = ReadLongUnaligned (mod_base + offsetof(mdl_t, boundingradius));
	pheader->numskins = ReadLongUnaligned (mod_base + offsetof(mdl_t, numskins));
	pheader->skinwidth = ReadLongUnaligned (mod_base + offsetof(mdl_t, skinwidth));
	pheader->skinheight = ReadLongUnaligned (mod_base + offsetof(mdl_t, skinheight));

	if (pheader->skinheight > MAX_LBM_HEIGHT)
		Con_DWarning ("model %s has a skin taller than %d", mod->name,
				   MAX_LBM_HEIGHT);

	pheader->numverts = ReadLongUnaligned (mod_base + offsetof(mdl_t, numverts));

	if (pheader->numverts <= 0)
		Sys_Error ("model %s has no vertices", mod->name);

	if (pheader->numverts > MAXALIASVERTS)
		Sys_Error ("model %s has too many vertices (%d; max = %d)", mod->name, pheader->numverts, MAXALIASVERTS);

	pheader->numtris = ReadLongUnaligned (mod_base + offsetof(mdl_t, numtris));

	if (pheader->numtris <= 0)
		Sys_Error ("model %s has no triangles", mod->name);

	if (pheader->numtris > MAXALIASTRIS)
		Sys_Error ("model %s has too many triangles (%d; max = %d)", mod->name, pheader->numtris, MAXALIASTRIS);

	pheader->numframes = ReadLongUnaligned (mod_base + offsetof(mdl_t, numframes));
	numframes = pheader->numframes;
	if (numframes < 1)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of frames: %d\n", numframes);

	pheader->size = ReadFloatUnaligned (mod_base + offsetof(mdl_t, size)) * ALIAS_BASE_SIZE_RATIO;
	mod->synctype = (synctype_t) ReadLongUnaligned (mod_base + offsetof(mdl_t, synctype));
	mod->numframes = pheader->numframes;

	for (i=0 ; i<3 ; i++)
	{
		pheader->scale[i] = ReadFloatUnaligned (mod_base + offsetof(mdl_t, scale[i]));
		pheader->scale_origin[i] = ReadFloatUnaligned (mod_base + offsetof(mdl_t, scale_origin[i]));
		pheader->eyeposition[i] = ReadFloatUnaligned (mod_base + offsetof(mdl_t, eyeposition[i]));
	}


//
// load the skins
//
	pskintype = mod_base + sizeof(mdl_t);
	pskintype = Mod_LoadAllSkins (pheader->numskins, pskintype);

//
// load base s and t vertices
//
	pinstverts = pskintype;

	for (i=0 ; i<pheader->numverts ; i++)
	{
		stverts[i].onseam = ReadLongUnaligned (pinstverts + offsetof(stvert_t, onseam));
		stverts[i].s = ReadLongUnaligned (pinstverts + offsetof(stvert_t, s));
		stverts[i].t = ReadLongUnaligned (pinstverts + offsetof(stvert_t, t));
		pinstverts += sizeof(stvert_t);
	}

//
// load triangle lists
//
	pintriangles = pinstverts;

	for (i=0 ; i<pheader->numtris ; i++)
	{
		triangles[i].facesfront = ReadLongUnaligned (pintriangles + offsetof(dtriangle_t, facesfront));

		for (j=0 ; j<3 ; j++)
		{
			triangles[i].vertindex[j] =
					ReadLongUnaligned (pintriangles + offsetof(dtriangle_t, vertindex[j]));
		}
		pintriangles += sizeof(dtriangle_t);
	}

//
// load the frames
//
	posenum = 0;
	pframetype = pintriangles;

	for (i=0 ; i<numframes ; i++)
	{
		aliasframetype_t	frametype;
		frametype = (aliasframetype_t) ReadLongUnaligned (pframetype + offsetof(daliasframetype_t, type));
		if (frametype == ALIAS_SINGLE)
			pframetype = Mod_LoadAliasFrame (pframetype + sizeof(daliasframetype_t), &pheader->frames[i]);
		else
			pframetype = Mod_LoadAliasGroup (pframetype + sizeof(daliasframetype_t), &pheader->frames[i]);
	}

	pheader->numposes = posenum;

	mod->type = mod_alias;

	Mod_SetExtraFlags (mod); //johnfitz

	Mod_CalcAliasBounds (pheader); //johnfitz

	//
	// build the draw lists
	//

	GL_MakeAliasModelDisplayLists (mod, pheader);

//
// move the complete, relocatable alias model to the cache
//
	end = Hunk_LowMark ();
	total = end - start;

	Cache_Alloc (&mod->cache, total, loadname);
	if (!mod->cache.data)
		return;
	memcpy (mod->cache.data, pheader, total);

	Hunk_FreeToLowMark (start);
}

//=============================================================================

/*
=================
Mod_LoadSpriteFrame
=================
*/
void * Mod_LoadSpriteFrame (void * pin, mspriteframe_t **ppframe, int framenum)
{
	dspriteframe_t		*pinframe;
	mspriteframe_t		*pspriteframe;
	int					width, height, size, origin[2];
	char				name[64];
	src_offset_t			offset; //johnfitz

	pinframe = (dspriteframe_t *)pin;

	width = LittleLong (pinframe->width);
	height = LittleLong (pinframe->height);
	size = width * height;

	pspriteframe = (mspriteframe_t *) Hunk_AllocName (sizeof (mspriteframe_t),loadname);
	*ppframe = pspriteframe;

	pspriteframe->width = width;
	pspriteframe->height = height;
	origin[0] = LittleLong (pinframe->origin[0]);
	origin[1] = LittleLong (pinframe->origin[1]);

	pspriteframe->up = origin[1];
	pspriteframe->down = origin[1] - height;
	pspriteframe->left = origin[0];
	pspriteframe->right = width + origin[0];

	//johnfitz -- image might be padded
	pspriteframe->smax = (float)width/(float)TexMgr_PadConditional(width);
	pspriteframe->tmax = (float)height/(float)TexMgr_PadConditional(height);
	//johnfitz

	q_snprintf (name, sizeof(name), "%s:frame%i", loadmodel->name, framenum);
	offset = (src_offset_t)(pinframe+1) - (src_offset_t)mod_base; //johnfitz
	pspriteframe->gltexture =
		TexMgr_LoadImage (loadmodel, name, width, height, SRC_INDEXED,
				  (byte *)(pinframe + 1), loadmodel->name, offset,
				  TEXPREF_PAD | TEXPREF_ALPHA | TEXPREF_NOPICMIP); //johnfitz -- TexMgr

	return (void *)((byte *)pinframe + sizeof (dspriteframe_t) + size);
}


/*
=================
Mod_LoadSpriteGroup
=================
*/
void * Mod_LoadSpriteGroup (void * pin, mspriteframe_t **ppframe, int framenum)
{
	dspritegroup_t		*pingroup;
	mspritegroup_t		*pspritegroup;
	int					i, numframes;
	dspriteinterval_t	*pin_intervals;
	float				*poutintervals;
	void				*ptemp;

	pingroup = (dspritegroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	pspritegroup = (mspritegroup_t *) Hunk_AllocName (sizeof (mspritegroup_t) +
				(numframes - 1) * sizeof (pspritegroup->frames[0]), loadname);

	pspritegroup->numframes = numframes;

	*ppframe = (mspriteframe_t *)pspritegroup;

	pin_intervals = (dspriteinterval_t *)(pingroup + 1);

	poutintervals = (float *) Hunk_AllocName (numframes * sizeof (float), loadname);

	pspritegroup->intervals = poutintervals;

	for (i=0 ; i<numframes ; i++)
	{
		*poutintervals = LittleFloat (pin_intervals->interval);
		if (*poutintervals <= 0.0)
			Sys_Error ("Mod_LoadSpriteGroup: interval<=0");

		poutintervals++;
		pin_intervals++;
	}

	ptemp = (void *)pin_intervals;

	for (i=0 ; i<numframes ; i++)
	{
		ptemp = Mod_LoadSpriteFrame (ptemp, &pspritegroup->frames[i], framenum * 100 + i);
	}

	return ptemp;
}


/*
=================
Mod_LoadSpriteModel
=================
*/
static void Mod_LoadSpriteModel (qmodel_t *mod, void *buffer)
{
	int					i;
	int					version;
	dsprite_t			*pin;
	msprite_t			*psprite;
	int					numframes;
	int					size;
	dspriteframetype_t	*pframetype;

	pin = (dsprite_t *)buffer;
	mod_base = (byte *)buffer; //johnfitz

	version = LittleLong (pin->version);
	if (version != SPRITE_VERSION)
		Sys_Error ("%s has wrong version number "
				 "(%i should be %i)", mod->name, version, SPRITE_VERSION);

	numframes = LittleLong (pin->numframes);

	size = sizeof (msprite_t) + (numframes - 1) * sizeof (psprite->frames);

	psprite = (msprite_t *) Hunk_AllocName (size, loadname);

	mod->cache.data = psprite;

	psprite->type = LittleLong (pin->type);
	psprite->maxwidth = LittleLong (pin->width);
	psprite->maxheight = LittleLong (pin->height);
	psprite->beamlength = LittleFloat (pin->beamlength);
	mod->synctype = (synctype_t) LittleLong (pin->synctype);
	psprite->numframes = numframes;

	mod->mins[0] = mod->mins[1] = -psprite->maxwidth/2;
	mod->maxs[0] = mod->maxs[1] = psprite->maxwidth/2;
	mod->mins[2] = -psprite->maxheight/2;
	mod->maxs[2] = psprite->maxheight/2;

//
// load the frames
//
	if (numframes < 1)
		Sys_Error ("Mod_LoadSpriteModel: Invalid # of frames: %d\n", numframes);

	mod->numframes = numframes;

	pframetype = (dspriteframetype_t *)(pin + 1);

	for (i=0 ; i<numframes ; i++)
	{
		spriteframetype_t	frametype;

		frametype = (spriteframetype_t) LittleLong (pframetype->type);
		psprite->frames[i].type = frametype;

		if (frametype == SPR_SINGLE)
		{
			pframetype = (dspriteframetype_t *)
					Mod_LoadSpriteFrame (pframetype + 1, &psprite->frames[i].frameptr, i);
		}
		else
		{
			pframetype = (dspriteframetype_t *)
					Mod_LoadSpriteGroup (pframetype + 1, &psprite->frames[i].frameptr, i);
		}
	}

	mod->type = mod_sprite;
}

//=============================================================================

/*
================
Mod_Print
================
*/
void Mod_Print (void)
{
	int		i;
	qmodel_t	*mod;

	Con_SafePrintf ("Cached models:\n"); //johnfitz -- safeprint instead of print
	for (i=0, mod=mod_known ; i < mod_numknown ; i++, mod++)
	{
		Con_SafePrintf ("%8p : %s\n", mod->cache.data, mod->name); //johnfitz -- safeprint instead of print
	}
	Con_Printf ("%i models\n",mod_numknown); //johnfitz -- print the total too
}

