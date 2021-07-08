// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2021 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_plane.c
/// \brief Here is a core component: drawing the floors and ceilings,
///        while maintaining a per column clipping list only.
///        Moreover, the sky areas have to be determined.

#include "doomdef.h"
#include "console.h"
#include "g_game.h"
#include "i_video.h"
#include "p_setup.h" // levelflats
#include "p_slopes.h"
#include "r_data.h"
#include "r_textures.h"
#include "r_local.h"
#include "r_state.h"
#include "r_splats.h" // faB(21jan):testing
#include "r_sky.h"
#include "r_portal.h"

#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"
#include "p_tick.h"

//
// opening
//

// Quincunx antialiasing of flats!
//#define QUINCUNX

//SoM: 3/23/2000: Use Boom visplane hashing.

visplane_t *visplanes[MAXVISPLANES];
static visplane_t *freetail;
static visplane_t **freehead = &freetail;

visplane_t *floorplane;
visplane_t *ceilingplane;
static visplane_t *currentplane;

visffloor_t ffloor[MAXFFLOORS];
INT32 numffloors;

//SoM: 3/23/2000: Boom visplane hashing routine.
#define visplane_hash(picnum,lightlevel,height) \
  ((unsigned)((picnum)*3+(lightlevel)+(height)*7) & VISPLANEHASHMASK)

//SoM: 3/23/2000: Use boom opening limit removal
size_t maxopenings;
INT16 *openings, *lastopening; /// \todo free leak

//
// Clip values are the solid pixel bounding the range.
//  floorclip starts out SCREENHEIGHT
//  ceilingclip starts out -1
//
INT16 floorclip[MAXVIDWIDTH], ceilingclip[MAXVIDWIDTH];
fixed_t frontscale[MAXVIDWIDTH];

//
// spanstart holds the start of a plane span
// initialized to 0 at start
//
static INT32 spanstart[MAXVIDHEIGHT];

//
// texture mapping
//
lighttable_t **planezlight;
#ifdef TRUECOLOR
lighttable_u32_t **planezlight_u32;
#endif
static fixed_t planeheight;

//added : 10-02-98: yslopetab is what yslope used to be,
//                yslope points somewhere into yslopetab,
//                now (viewheight/2) slopes are calculated above and
//                below the original viewheight for mouselook
//                (this is to calculate yslopes only when really needed)
//                (when mouselookin', yslope is moving into yslopetab)
//                Check R_SetupFrame, R_SetViewSize for more...
fixed_t yslopetab[MAXVIDHEIGHT*16];
fixed_t *yslope;

fixed_t basexscale, baseyscale;

fixed_t cachedheight[MAXVIDHEIGHT];
fixed_t cacheddistance[MAXVIDHEIGHT];
fixed_t cachedxstep[MAXVIDHEIGHT];
fixed_t cachedystep[MAXVIDHEIGHT];

static fixed_t xoffs, yoffs;
static floatv3_t ds_slope_origin, ds_slope_u, ds_slope_v;

//
// R_InitPlanes
// Only at game startup.
//
void R_InitPlanes(void)
{
	// FIXME: unused
}

//
// Water ripple effect
// Needs the height of the plane, and the vertical position of the span.
// Sets planeripple.xfrac and planeripple.yfrac, added to ds_xfrac and ds_yfrac, if the span is not tilted.
//

struct
{
	INT32 offset;
	fixed_t xfrac, yfrac;
	boolean active;
} planeripple;

// ripples da water texture
static fixed_t R_CalculateRippleOffset(INT32 y)
{
	fixed_t distance = FixedMul(planeheight, yslope[y]);
	const INT32 yay = (planeripple.offset + (distance>>9)) & 8191;
	return FixedDiv(FINESINE(yay), (1<<12) + (distance>>11));
}

static void R_CalculatePlaneRipple(angle_t angle)
{
	angle >>= ANGLETOFINESHIFT;
	angle = (angle + 2048) & 8191; // 90 degrees
	planeripple.xfrac = FixedMul(FINECOSINE(angle), ds_bgofs);
	planeripple.yfrac = FixedMul(FINESINE(angle), ds_bgofs);
}

static void R_UpdatePlaneRipple(void)
{
	ds_waterofs = (leveltime & 1)*16384;
	planeripple.offset = (leveltime * 140);
}

//
// R_MapPlane
//
// Uses global vars:
//  planeheight
//  basexscale
//  baseyscale
//  centerx

static void R_MapPlane(INT32 y, INT32 x1, INT32 x2)
{
	angle_t angle, planecos, planesin;
	fixed_t distance = 0, span;
	size_t pindex;

#ifdef RANGECHECK
	if (x2 < x1 || x1 < 0 || x2 >= viewwidth || y > viewheight)
		I_Error("R_MapPlane: %d, %d at %d", x1, x2, y);
#endif

	if (x1 >= vid.width)
		x1 = vid.width - 1;

	angle = (currentplane->viewangle + currentplane->plangle)>>ANGLETOFINESHIFT;
	planecos = FINECOSINE(angle);
	planesin = FINESINE(angle);

	if (planeheight != cachedheight[y])
	{
		cachedheight[y] = planeheight;
		cacheddistance[y] = distance = FixedMul(planeheight, yslope[y]);
		span = abs(centery - y);

		if (span) // don't divide by zero
		{
			ds_xstep = FixedMul(planesin, planeheight) / span;
			ds_ystep = FixedMul(planecos, planeheight) / span;
		}
		else
		{
			ds_xstep = FixedMul(distance, basexscale);
			ds_ystep = FixedMul(distance, baseyscale);
		}

		cachedxstep[y] = ds_xstep;
		cachedystep[y] = ds_ystep;
	}
	else
	{
		distance = cacheddistance[y];
		ds_xstep = cachedxstep[y];
		ds_ystep = cachedystep[y];
	}

	ds_xfrac = xoffs + FixedMul(planecos, distance) + (x1 - centerx) * ds_xstep;
	ds_yfrac = yoffs - FixedMul(planesin, distance) + (x1 - centerx) * ds_ystep;

	// Water ripple effect
	if (planeripple.active)
	{
		ds_bgofs = R_CalculateRippleOffset(y);

		R_CalculatePlaneRipple(currentplane->viewangle + currentplane->plangle);

		ds_xfrac += planeripple.xfrac;
		ds_yfrac += planeripple.yfrac;
		ds_bgofs >>= FRACBITS;

		if ((y + ds_bgofs) >= viewheight)
			ds_bgofs = viewheight-y-1;
		if ((y + ds_bgofs) < 0)
			ds_bgofs = -y;
	}

	pindex = distance >> LIGHTZSHIFT;
	if (pindex >= MAXLIGHTZ)
		pindex = MAXLIGHTZ - 1;

#ifdef TRUECOLOR
	if (tc_colormaps)
	{
		ds_colormap = (UINT8 *)(planezlight_u32[pindex]);
		ds_colmapstyle = TC_COLORMAPSTYLE_32BPP;
		dp_lighting = TC_CalcScaleLight(planezlight_u32[pindex]);
	}
	else
#endif
	{
		ds_colormap = planezlight[pindex];
		ds_colmapstyle = TC_COLORMAPSTYLE_8BPP;
	}

	if (currentplane->extra_colormap)
	{
#ifdef TRUECOLOR
		dp_extracolormap = currentplane->extra_colormap;
		if (tc_colormaps)
			ds_colormap = (UINT8 *)(currentplane->extra_colormap->colormap_u32 + ((UINT32*)ds_colormap - colormaps_u32));
		else
#endif
			ds_colormap = currentplane->extra_colormap->colormap + (ds_colormap - colormaps);
	}
#ifdef TRUECOLOR
	else
		dp_extracolormap = defaultextracolormap;
#endif

	ds_y = y;
	ds_x1 = x1;
	ds_x2 = x2;

	spanfunc();
}

static void R_MapTiltedPlane(INT32 y, INT32 x1, INT32 x2)
{
#ifdef RANGECHECK
	if (x2 < x1 || x1 < 0 || x2 >= viewwidth || y > viewheight)
		I_Error("R_MapTiltedPlane: %d, %d at %d", x1, x2, y);
#endif

	if (x1 >= vid.width)
		x1 = vid.width - 1;

	// Water ripple effect
	if (planeripple.active)
	{
		ds_bgofs = R_CalculateRippleOffset(y);

		ds_sup = &ds_su[y];
		ds_svp = &ds_sv[y];
		ds_szp = &ds_sz[y];

		ds_bgofs >>= FRACBITS;

		if ((y + ds_bgofs) >= viewheight)
			ds_bgofs = viewheight-y-1;
		if ((y + ds_bgofs) < 0)
			ds_bgofs = -y;
	}

#ifdef TRUECOLOR
	if (tc_colormaps)
	{
		ds_colormap = (lighttable_t*)colormaps_u32;
		ds_colmapstyle = TC_COLORMAPSTYLE_32BPP;
		dp_lighting = 0xFF;
	}
	else
#endif
	{
		ds_colormap = colormaps;
		ds_colmapstyle = TC_COLORMAPSTYLE_8BPP;
	}

	if (currentplane->extra_colormap)
	{
#ifdef TRUECOLOR
		dp_extracolormap = currentplane->extra_colormap;
		if (tc_colormaps)
			ds_colormap = (UINT8 *)(currentplane->extra_colormap->colormap_u32 + ((UINT32*)ds_colormap - colormaps_u32));
		else
#endif
			ds_colormap = currentplane->extra_colormap->colormap + (ds_colormap - colormaps);
	}
#ifdef TRUECOLOR
	else
		dp_extracolormap = defaultextracolormap;
#endif

	ds_y = y;
	ds_x1 = x1;
	ds_x2 = x2;

	spanfunc();
}

void R_ClearFFloorClips (void)
{
	INT32 i, p;

	// opening / clipping determination
	for (i = 0; i < viewwidth; i++)
	{
		for (p = 0; p < MAXFFLOORS; p++)
		{
			ffloor[p].f_clip[i] = (INT16)viewheight;
			ffloor[p].c_clip[i] = -1;
		}
	}

	numffloors = 0;
}

//
// R_ClearPlanes
// At begining of frame.
//
void R_ClearPlanes(void)
{
	INT32 i, p;
	angle_t angle;

	// opening / clipping determination
	for (i = 0; i < viewwidth; i++)
	{
		floorclip[i] = (INT16)viewheight;
		ceilingclip[i] = -1;
		frontscale[i] = INT32_MAX;
		for (p = 0; p < MAXFFLOORS; p++)
		{
			ffloor[p].f_clip[i] = (INT16)viewheight;
			ffloor[p].c_clip[i] = -1;
		}
	}

	for (i = 0; i < MAXVISPLANES; i++)
	for (*freehead = visplanes[i], visplanes[i] = NULL;
		freehead && *freehead ;)
	{
		freehead = &(*freehead)->next;
	}

	lastopening = openings;

	// texture calculation
	memset(cachedheight, 0, sizeof (cachedheight));

	// left to right mapping
	angle = (viewangle-ANGLE_90)>>ANGLETOFINESHIFT;

	// scale will be unit scale at SCREENWIDTH/2 distance
	basexscale = FixedDiv (FINECOSINE(angle),centerxfrac);
	baseyscale = -FixedDiv (FINESINE(angle),centerxfrac);
}

static visplane_t *new_visplane(unsigned hash)
{
	visplane_t *check = freetail;
	if (!check)
	{
		check = calloc(2, sizeof (*check));
		if (check == NULL) I_Error("%s: Out of memory", "new_visplane"); // FIXME: ugly
	}
	else
	{
		freetail = freetail->next;
		if (!freetail)
			freehead = &freetail;
	}
	check->next = visplanes[hash];
	visplanes[hash] = check;
	return check;
}

//
// R_FindPlane: Seek a visplane having the identical values:
//              Same height, same flattexture, same lightlevel.
//              If not, allocates another of them.
//
visplane_t *R_FindPlane(fixed_t height, INT32 picnum, INT32 lightlevel,
	fixed_t xoff, fixed_t yoff, angle_t plangle, extracolormap_t *planecolormap,
	ffloor_t *pfloor, polyobj_t *polyobj, pslope_t *slope)
{
	visplane_t *check;
	unsigned hash;

	if (!slope) // Don't mess with this right now if a slope is involved
	{
		xoff += viewx;
		yoff -= viewy;
		if (plangle != 0)
		{
			// Add the view offset, rotated by the plane angle.
			float ang = ANG2RAD(plangle);
			float x = FixedToFloat(xoff);
			float y = FixedToFloat(yoff);
			xoff = FloatToFixed(x * cos(ang) + y * sin(ang));
			yoff = FloatToFixed(-x * sin(ang) + y * cos(ang));
		}
	}

	if (polyobj)
	{
		if (polyobj->angle != 0)
		{
			angle_t fineshift = polyobj->angle >> ANGLETOFINESHIFT;
			xoff -= FixedMul(FINECOSINE(fineshift), polyobj->centerPt.x)+FixedMul(FINESINE(fineshift), polyobj->centerPt.y);
			yoff -= FixedMul(FINESINE(fineshift), polyobj->centerPt.x)-FixedMul(FINECOSINE(fineshift), polyobj->centerPt.y);
		}
		else
		{
			xoff -= polyobj->centerPt.x;
			yoff += polyobj->centerPt.y;
		}
	}

	// This appears to fix the Nimbus Ruins sky bug.
	if (picnum == skyflatnum && pfloor)
	{
		height = 0; // all skies map together
		lightlevel = 0;
	}

	if (!pfloor)
	{
		hash = visplane_hash(picnum, lightlevel, height);
		for (check = visplanes[hash]; check; check = check->next)
		{
			if (polyobj != check->polyobj)
				continue;
			if (height == check->height && picnum == check->picnum
				&& lightlevel == check->lightlevel
				&& xoff == check->xoffs && yoff == check->yoffs
				&& planecolormap == check->extra_colormap
				&& check->viewx == viewx && check->viewy == viewy && check->viewz == viewz
				&& check->viewangle == viewangle
				&& check->plangle == plangle
				&& check->slope == slope)
			{
				return check;
			}
		}
	}
	else
	{
		hash = MAXVISPLANES - 1;
	}

	check = new_visplane(hash);

	check->height = height;
	check->picnum = picnum;
	check->lightlevel = lightlevel;
	check->minx = vid.width;
	check->maxx = -1;
	check->xoffs = xoff;
	check->yoffs = yoff;
	check->extra_colormap = planecolormap;
	check->ffloor = pfloor;
	check->viewx = viewx;
	check->viewy = viewy;
	check->viewz = viewz;
	check->viewangle = viewangle;
	check->plangle = plangle;
	check->polyobj = polyobj;
	check->slope = slope;

	memset(check->top, 0xff, sizeof (check->top));
	memset(check->bottom, 0x00, sizeof (check->bottom));

	return check;
}

//
// R_CheckPlane: return same visplane or alloc a new one if needed
//
visplane_t *R_CheckPlane(visplane_t *pl, INT32 start, INT32 stop)
{
	INT32 intrl, intrh;
	INT32 unionl, unionh;
	INT32 x;

	if (start < pl->minx)
	{
		intrl = pl->minx;
		unionl = start;
	}
	else
	{
		unionl = pl->minx;
		intrl = start;
	}

	if (stop > pl->maxx)
	{
		intrh = pl->maxx;
		unionh = stop;
	}
	else
	{
		unionh = pl->maxx;
		intrh = stop;
	}

	// 0xff is not equal to -1 with shorts...
	for (x = intrl; x <= intrh; x++)
		if (pl->top[x] != 0xffff || pl->bottom[x] != 0x0000)
			break;

	if (x > intrh) /* Can use existing plane; extend range */
	{
		pl->minx = unionl;
		pl->maxx = unionh;
	}
	else /* Cannot use existing plane; create a new one */
	{
		visplane_t *new_pl;
		if (pl->ffloor)
		{
			new_pl = new_visplane(MAXVISPLANES - 1);
		}
		else
		{
			unsigned hash =
				visplane_hash(pl->picnum, pl->lightlevel, pl->height);
			new_pl = new_visplane(hash);
		}

		new_pl->height = pl->height;
		new_pl->picnum = pl->picnum;
		new_pl->lightlevel = pl->lightlevel;
		new_pl->xoffs = pl->xoffs;
		new_pl->yoffs = pl->yoffs;
		new_pl->extra_colormap = pl->extra_colormap;
		new_pl->ffloor = pl->ffloor;
		new_pl->viewx = pl->viewx;
		new_pl->viewy = pl->viewy;
		new_pl->viewz = pl->viewz;
		new_pl->viewangle = pl->viewangle;
		new_pl->plangle = pl->plangle;
		new_pl->polyobj = pl->polyobj;
		new_pl->slope = pl->slope;
		pl = new_pl;
		pl->minx = start;
		pl->maxx = stop;
		memset(pl->top, 0xff, sizeof pl->top);
		memset(pl->bottom, 0x00, sizeof pl->bottom);
	}
	return pl;
}


//
// R_ExpandPlane
//
// This function basically expands the visplane or I_Errors.
// The reason for this is that when creating 3D floor planes, there is no
// need to create new ones with R_CheckPlane, because 3D floor planes
// are created by subsector and there is no way a subsector can graphically
// overlap.
void R_ExpandPlane(visplane_t *pl, INT32 start, INT32 stop)
{
//	INT32 unionl, unionh;
//	INT32 x;

	// Don't expand polyobject planes here - we do that on our own.
	if (pl->polyobj)
		return;

	if (pl->minx > start) pl->minx = start;
	if (pl->maxx < stop)  pl->maxx = stop;
/*
	if (start < pl->minx)
	{
		unionl = start;
	}
	else
	{
		unionl = pl->minx;
	}

	if (stop > pl->maxx)
	{
		unionh = stop;
	}
	else
	{
		unionh = pl->maxx;
	}
	for (x = start; x <= stop; x++)
		if (pl->top[x] != 0xffff || pl->bottom[x] != 0x0000)
			break;

	if (x <= stop)
		I_Error("R_ExpandPlane: planes in same subsector overlap?!\nminx: %d, maxx: %d, start: %d, stop: %d\n", pl->minx, pl->maxx, start, stop);

	pl->minx = unionl, pl->maxx = unionh;
*/

}

static void R_MakeSpans(INT32 x, INT32 t1, INT32 b1, INT32 t2, INT32 b2)
{
	//    Alam: from r_splats's R_RasterizeFloorSplat
	if (t1 >= vid.height) t1 = vid.height-1;
	if (b1 >= vid.height) b1 = vid.height-1;
	if (t2 >= vid.height) t2 = vid.height-1;
	if (b2 >= vid.height) b2 = vid.height-1;
	if (x-1 >= vid.width) x = vid.width;

	while (t1 < t2 && t1 <= b1)
	{
		R_MapPlane(t1, spanstart[t1], x - 1);
		t1++;
	}
	while (b1 > b2 && b1 >= t1)
	{
		R_MapPlane(b1, spanstart[b1], x - 1);
		b1--;
	}

	while (t2 < t1 && t2 <= b2)
		spanstart[t2++] = x;
	while (b2 > b1 && b2 >= t2)
		spanstart[b2--] = x;
}

static void R_MakeTiltedSpans(INT32 x, INT32 t1, INT32 b1, INT32 t2, INT32 b2)
{
	//    Alam: from r_splats's R_RasterizeFloorSplat
	if (t1 >= vid.height) t1 = vid.height-1;
	if (b1 >= vid.height) b1 = vid.height-1;
	if (t2 >= vid.height) t2 = vid.height-1;
	if (b2 >= vid.height) b2 = vid.height-1;
	if (x-1 >= vid.width) x = vid.width;

	while (t1 < t2 && t1 <= b1)
	{
		R_MapTiltedPlane(t1, spanstart[t1], x - 1);
		t1++;
	}
	while (b1 > b2 && b1 >= t1)
	{
		R_MapTiltedPlane(b1, spanstart[b1], x - 1);
		b1--;
	}

	while (t2 < t1 && t2 <= b2)
		spanstart[t2++] = x;
	while (b2 > b1 && b2 >= t2)
		spanstart[b2--] = x;
}

void R_DrawPlanes(void)
{
	visplane_t *pl;
	INT32 i;

	ds_picfmt = PICFMT_FLAT;
	R_UpdatePlaneRipple();

	for (i = 0; i < MAXVISPLANES; i++, pl++)
	{
		for (pl = visplanes[i]; pl; pl = pl->next)
		{
			if (pl->ffloor != NULL || pl->polyobj != NULL)
				continue;

			R_DrawSinglePlane(pl);
		}
	}
}

// R_DrawSkyPlane
//
// Draws the sky within the plane's top/bottom bounds
// Note: this uses column drawers instead of span drawers, since the sky is always a texture
//
static void R_DrawSkyPlane(visplane_t *pl)
{
	INT32 x;
	INT32 angle;

	// Reset column drawer function (note: couldn't we just call walldrawerfunc directly?)
	// (that is, unless we'll need to switch drawers in future for some reason)
	colfunc = colfuncs[BASEDRAWFUNC];

	// use correct aspect ratio scale
	dc_iscale = skyscale;

#ifdef TRUECOLOR
	dc_picfmt = textures[skytexture]->format;
	dc_colmapstyle = tc_colormaps ? TC_COLORMAPSTYLE_32BPP : TC_COLORMAPSTYLE_8BPP;

	// Sky is always drawn full bright,
	//  i.e. colormaps[0] is used.
	// Because of this hack, sky is not affected
	//  by sector colormaps (INVUL inverse mapping is not implemented in SRB2 so is irrelevant).
	if (tc_colormaps)
		dc_colormap = (UINT8 *)colormaps_u32;
	else
#endif
		dc_colormap = colormaps;

	dc_texturemid = skytexturemid;
	dc_texheight = textureheight[skytexture]
		>>FRACBITS;
	for (x = pl->minx; x <= pl->maxx; x++)
	{
		dc_yl = pl->top[x];
		dc_yh = pl->bottom[x];

		if (dc_yl <= dc_yh)
		{
			angle = (pl->viewangle + xtoviewangle[x])>>ANGLETOSKYSHIFT;
			dc_iscale = FixedMul(skyscale, FINECOSINE(xtoviewangle[x]>>ANGLETOFINESHIFT));
			dc_x = x;
			dc_source =
				R_GetColumn(texturetranslation[skytexture],
					-angle); // get negative of angle for each column to display sky correct way round! --Monster Iestyn 27/01/18
			colfunc();
		}
	}
}

// Returns the height of the sloped plane at (x, y) as a 32.16 fixed_t
static INT64 R_GetSlopeZAt(const pslope_t *slope, fixed_t x, fixed_t y)
{
	INT64 x64 = ((INT64)x - (INT64)slope->o.x);
	INT64 y64 = ((INT64)y - (INT64)slope->o.y);

	x64 = (x64 * (INT64)slope->d.x) / FRACUNIT;
	y64 = (y64 * (INT64)slope->d.y) / FRACUNIT;

	return (INT64)slope->o.z + ((x64 + y64) * (INT64)slope->zdelta) / FRACUNIT;
}

// Sets the texture origin vector of the sloped plane.
static void R_SetSlopePlaneOrigin(pslope_t *slope, fixed_t xpos, fixed_t ypos, fixed_t zpos, fixed_t xoff, fixed_t yoff, fixed_t angle)
{
	floatv3_t *p = &ds_slope_origin;

	INT64 vx = (INT64)xpos + (INT64)xoff;
	INT64 vy = (INT64)ypos - (INT64)yoff;

	float vxf = vx / (float)FRACUNIT;
	float vyf = vy / (float)FRACUNIT;
	float ang = ANG2RAD(ANGLE_270 - angle);

	// p is the texture origin in view space
	// Don't add in the offsets at this stage, because doing so can result in
	// errors if the flat is rotated.
	p->x = vxf * cos(ang) - vyf * sin(ang);
	p->z = vxf * sin(ang) + vyf * cos(ang);
	p->y = (R_GetSlopeZAt(slope, -xoff, yoff) - zpos) / (float)FRACUNIT;
}

// This function calculates all of the vectors necessary for drawing a sloped plane.
void R_SetSlopePlane(pslope_t *slope, fixed_t xpos, fixed_t ypos, fixed_t zpos, fixed_t xoff, fixed_t yoff, angle_t angle, angle_t plangle)
{
	// Potentially override other stuff for now cus we're mean. :< But draw a slope plane!
	// I copied ZDoom's code and adapted it to SRB2... -Red
	floatv3_t *m = &ds_slope_v, *n = &ds_slope_u;
	fixed_t height, temp;
	float ang;

	R_SetSlopePlaneOrigin(slope, xpos, ypos, zpos, xoff, yoff, angle);
	height = P_GetSlopeZAt(slope, xpos, ypos);
	zeroheight = FixedToFloat(height - zpos);

	// m is the v direction vector in view space
	ang = ANG2RAD(ANGLE_180 - (angle + plangle));
	m->x = cos(ang);
	m->z = sin(ang);

	// n is the u direction vector in view space
	n->x = sin(ang);
	n->z = -cos(ang);

	plangle >>= ANGLETOFINESHIFT;
	temp = P_GetSlopeZAt(slope, xpos + FINESINE(plangle), ypos + FINECOSINE(plangle));
	m->y = FixedToFloat(temp - height);
	temp = P_GetSlopeZAt(slope, xpos + FINECOSINE(plangle), ypos - FINESINE(plangle));
	n->y = FixedToFloat(temp - height);
}

// This function calculates all of the vectors necessary for drawing a sloped and scaled plane.
void R_SetScaledSlopePlane(pslope_t *slope, fixed_t xpos, fixed_t ypos, fixed_t zpos, fixed_t xs, fixed_t ys, fixed_t xoff, fixed_t yoff, angle_t angle, angle_t plangle)
{
	floatv3_t *m = &ds_slope_v, *n = &ds_slope_u;
	fixed_t height, temp;

	float xscale = FixedToFloat(xs);
	float yscale = FixedToFloat(ys);
	float ang;

	R_SetSlopePlaneOrigin(slope, xpos, ypos, zpos, xoff, yoff, angle);
	height = P_GetSlopeZAt(slope, xpos, ypos);
	zeroheight = FixedToFloat(height - zpos);

	// m is the v direction vector in view space
	ang = ANG2RAD(ANGLE_180 - (angle + plangle));
	m->x = yscale * cos(ang);
	m->z = yscale * sin(ang);

	// n is the u direction vector in view space
	n->x = xscale * sin(ang);
	n->z = -xscale * cos(ang);

	ang = ANG2RAD(plangle);
	temp = P_GetSlopeZAt(slope, xpos + FloatToFixed(yscale * sin(ang)), ypos + FloatToFixed(yscale * cos(ang)));
	m->y = FixedToFloat(temp - height);
	temp = P_GetSlopeZAt(slope, xpos + FloatToFixed(xscale * cos(ang)), ypos - FloatToFixed(xscale * sin(ang)));
	n->y = FixedToFloat(temp - height);
}

void R_CalculateSlopeVectors(void)
{
	float sfmult = 65536.f;

	// Eh. I tried making this stuff fixed-point and it exploded on me. Here's a macro for the only floating-point vector function I recall using.
#define CROSS(d, v1, v2) \
d->x = (v1.y * v2.z) - (v1.z * v2.y);\
d->y = (v1.z * v2.x) - (v1.x * v2.z);\
d->z = (v1.x * v2.y) - (v1.y * v2.x)
	CROSS(ds_sup, ds_slope_origin, ds_slope_v);
	CROSS(ds_svp, ds_slope_origin, ds_slope_u);
	CROSS(ds_szp, ds_slope_v, ds_slope_u);
#undef CROSS

	ds_sup->z *= focallengthf;
	ds_svp->z *= focallengthf;
	ds_szp->z *= focallengthf;

	// Premultiply the texture vectors with the scale factors
	if (ds_powersoftwo)
		sfmult *= (1 << nflatshiftup);

	ds_sup->x *= sfmult;
	ds_sup->y *= sfmult;
	ds_sup->z *= sfmult;
	ds_svp->x *= sfmult;
	ds_svp->y *= sfmult;
	ds_svp->z *= sfmult;
}

void R_SetTiltedSpan(INT32 span)
{
	if (ds_su == NULL)
		ds_su = Z_Malloc(sizeof(*ds_su) * vid.height, PU_STATIC, NULL);
	if (ds_sv == NULL)
		ds_sv = Z_Malloc(sizeof(*ds_sv) * vid.height, PU_STATIC, NULL);
	if (ds_sz == NULL)
		ds_sz = Z_Malloc(sizeof(*ds_sz) * vid.height, PU_STATIC, NULL);

	ds_sup = &ds_su[span];
	ds_svp = &ds_sv[span];
	ds_szp = &ds_sz[span];
}

static void R_SetSlopePlaneVectors(visplane_t *pl, INT32 y, fixed_t xoff, fixed_t yoff)
{
	R_SetTiltedSpan(y);
	R_SetSlopePlane(pl->slope, pl->viewx, pl->viewy, pl->viewz, xoff, yoff, pl->viewangle, pl->plangle);
	R_CalculateSlopeVectors();
}

static inline void R_AdjustSlopeCoordinates(vector3_t *origin)
{
	const fixed_t modmask = ((1 << (32-nflatshiftup)) - 1);

	fixed_t ox = (origin->x & modmask);
	fixed_t oy = -(origin->y & modmask);

	xoffs &= modmask;
	yoffs &= modmask;

	xoffs -= (origin->x - ox);
	yoffs += (origin->y + oy);
}

static inline void R_AdjustSlopeCoordinatesNPO2(vector3_t *origin)
{
	const fixed_t modmaskw = (ds_flatwidth << FRACBITS);
	const fixed_t modmaskh = (ds_flatheight << FRACBITS);

	fixed_t ox = (origin->x % modmaskw);
	fixed_t oy = -(origin->y % modmaskh);

	xoffs %= modmaskw;
	yoffs %= modmaskh;

	xoffs -= (origin->x - ox);
	yoffs += (origin->y + oy);
}

void R_DrawSinglePlane(visplane_t *pl)
{
	levelflat_t *levelflat;
	INT32 light = 0;
	INT32 x;
	INT32 stop, angle;
	ffloor_t *rover;
	INT32 type;
	INT32 spanfunctype = BASEDRAWFUNC;

	if (!(pl->minx <= pl->maxx))
		return;

	// sky flat
	if (pl->picnum == skyflatnum)
	{
		R_DrawSkyPlane(pl);
		return;
	}

	planeripple.active = false;
	spanfunc = spanfuncs[BASEDRAWFUNC];

	if (pl->polyobj)
	{
		if (pl->polyobj->translucency >= 10)
			return; // Don't even draw it
		else if (pl->polyobj->translucency > 0)
		{
			INT32 transval = pl->polyobj->translucency;

			if (!usetranstables)
			{
				R_SetSpanBlendingFunction(AST_TRANSLUCENT);
				ds_alpha = R_TransnumToAlpha(transval);
			}
			else
				ds_transmap = R_GetTranslucencyTable(transval);

			spanfunctype = (pl->polyobj->flags & POF_SPLAT) ? span_translu_splat : span_translu_splat;
		}
		else if (pl->polyobj->flags & POF_SPLAT) // Opaque, but allow transparent flat pixels
		{
			spanfunctype = SPAN_SPLAT;

#ifdef TRUECOLOR
			R_SetSpanBlendingFunction(AST_COPY);
			ds_alpha = 0xFF;
#endif
		}

		if (pl->polyobj->translucency == 0 || (pl->extra_colormap && (pl->extra_colormap->flags & CMF_FOG)))
			light = (pl->lightlevel >> LIGHTSEGSHIFT);
		else
			light = LIGHTLEVELS-1;
	}
	else
	{
		if (pl->ffloor)
		{
			// Don't draw planes that shouldn't be drawn.
			for (rover = pl->ffloor->target->ffloors; rover; rover = rover->next)
			{
				if ((pl->ffloor->flags & FF_CUTEXTRA) && (rover->flags & FF_EXTRA))
				{
					if (pl->ffloor->flags & FF_EXTRA)
					{
						// The plane is from an extra 3D floor... Check the flags so
						// there are no undesired cuts.
						if (((pl->ffloor->flags & (FF_FOG|FF_SWIMMABLE)) == (rover->flags & (FF_FOG|FF_SWIMMABLE)))
							&& pl->height < *rover->topheight
							&& pl->height > *rover->bottomheight)
							return;
					}
				}
			}

			if (pl->ffloor->flags & FF_TRANSLUCENT)
			{
				spanfunctype = (pl->ffloor->master->flags & ML_EFFECT6) ? span_translu_splat : span_translu;

				if (!usetranstables)
				{
					if (pl->ffloor->alpha >= 255) // Opaque, but allow transparent flat pixels
					{
						spanfunctype = SPAN_SPLAT;
						R_SetSpanBlendingFunction(AST_COPY);
						ds_alpha = 0xFF;
					}
					else if (pl->ffloor->alpha < 1)
						return; // Don't even draw it
					else
					{
						R_SetSpanBlendingFunction(AST_TRANSLUCENT);
						ds_alpha = pl->ffloor->alpha;
					}
				}
				else
				{
					INT32 transnum = R_AlphaToTransnum(pl->ffloor->alpha);
					if (transnum == -1)
						return; // Don't even draw it
					else if (transnum > 0)
						ds_transmap = R_GetTranslucencyTable(transnum);
					else // Opaque, but allow transparent flat pixels
						spanfunctype = SPAN_SPLAT;
				}

				if ((spanfunctype == SPAN_SPLAT) || (pl->extra_colormap && (pl->extra_colormap->flags & CMF_FOG)))
					light = (pl->lightlevel >> LIGHTSEGSHIFT);
				else
					light = LIGHTLEVELS-1;
			}
			else if (pl->ffloor->flags & FF_FOG)
			{
				spanfunctype = SPAN_FOG;
				light = (pl->lightlevel >> LIGHTSEGSHIFT);
			}
			else
				light = (pl->lightlevel >> LIGHTSEGSHIFT);

			if (pl->ffloor->flags & FF_RIPPLE)
			{
				INT32 top, bottom;

				planeripple.active = true;

				if (spanfunctype == span_translu)
				{
					spanfunctype = span_translu_water;

					// Copy the current scene, ugh
					top = pl->high-8;
					bottom = pl->low+8;

					if (top < 0)
						top = 0;
					if (bottom > vid.height)
						bottom = vid.height;

					// Only copy the part of the screen we need
					VID_BlitLinearScreen((splitscreen && viewplayer == &players[secondarydisplayplayer]) ? screens[0] + (top+(vid.height>>1))*vid.rowbytes : screens[0]+((top)*vid.rowbytes), screens[1]+((top)*vid.rowbytes),
										 vid.rowbytes, bottom-top,
										 vid.rowbytes, vid.rowbytes);
				}
			}
		}
		else
			light = (pl->lightlevel >> LIGHTSEGSHIFT);
	}

	currentplane = pl;
	levelflat = &levelflats[pl->picnum];

	/* :james: */
	type = levelflat->type;
	switch (type)
	{
		case LEVELFLAT_NONE:
			return;
		case LEVELFLAT_FLAT:
#if defined(PICTURES_ALLOWDEPTH) && defined(TRUECOLOR)
			if (truecolor)
			{
				ds_source = (UINT8 *)R_GetLevelFlat(levelflat);
				if (!ds_source)
					return;
			}
			else
#endif
				ds_source = (UINT8 *)R_GetFlat(levelflat->u.flat.lumpnum);
			// Raw flats always have dimensions that are powers-of-two numbers.
			R_CheckFlatLength(W_LumpLength(levelflat->u.flat.lumpnum));
			ds_powersoftwo = true;
			break;
		default:
			ds_source = (UINT8 *)R_GetLevelFlat(levelflat);
			if (!ds_source)
				return;
			// Check if this texture or patch has power-of-two dimensions.
			if (R_CheckPowersOfTwo())
				R_CheckFlatLength(ds_flatwidth * ds_flatheight);
	}

	if (!pl->slope // Don't mess with angle on slopes! We'll handle this ourselves later
		&& viewangle != pl->viewangle+pl->plangle)
	{
		memset(cachedheight, 0, sizeof (cachedheight));
		angle = (pl->viewangle+pl->plangle-ANGLE_90)>>ANGLETOFINESHIFT;
		basexscale = FixedDiv(FINECOSINE(angle),centerxfrac);
		baseyscale = -FixedDiv(FINESINE(angle),centerxfrac);
		viewangle = pl->viewangle+pl->plangle;
	}

	xoffs = pl->xoffs;
	yoffs = pl->yoffs;

	if (light >= LIGHTLEVELS)
		light = LIGHTLEVELS-1;

	if (light < 0)
		light = 0;

	if (pl->slope)
	{
		if (!pl->plangle)
		{
			if (ds_powersoftwo)
				R_AdjustSlopeCoordinates(&pl->slope->o);
			else
				R_AdjustSlopeCoordinatesNPO2(&pl->slope->o);
		}

		if (planeripple.active)
		{
			planeheight = abs(P_GetSlopeZAt(pl->slope, pl->viewx, pl->viewy) - pl->viewz);

			R_PlaneBounds(pl);

			for (x = pl->high; x < pl->low; x++)
			{
				ds_bgofs = R_CalculateRippleOffset(x);
				R_CalculatePlaneRipple(pl->viewangle + pl->plangle);
				R_SetSlopePlaneVectors(pl, x, (xoffs + planeripple.xfrac), (yoffs + planeripple.yfrac));
			}
		}
		else
			R_SetSlopePlaneVectors(pl, 0, xoffs, yoffs);

		if (spanfunctype == span_translu_water)
			spanfunctype = span_translu_water_tilted;
		else if (spanfunctype == span_translu)
			spanfunctype = span_translu_tilted;
		else if (spanfunctype == SPAN_SPLAT)
			spanfunctype = SPAN_SPLAT_TILTED;
		else
			spanfunctype = SPAN_TILTED;

#ifdef TRUECOLOR
		if (tc_colormaps)
			planezlight_u32 = scalelight_u32[light];
		else
#endif
			planezlight = scalelight[light];
	}
	else
	{
		planeheight = abs(pl->height - pl->viewz);

#ifdef TRUECOLOR
		if (tc_colormaps)
			planezlight_u32 = zlight_u32[light];
		else
#endif
			planezlight = zlight[light];

	}

	// Use the correct span drawer depending on the powers-of-twoness
	if (!ds_powersoftwo)
	{
		if (spanfuncs_npo2[spanfunctype])
			spanfunc = spanfuncs_npo2[spanfunctype];
		else
			spanfunc = spanfuncs[spanfunctype];
	}
	else
		spanfunc = spanfuncs[spanfunctype];

	// set the maximum value for unsigned
	pl->top[pl->maxx+1] = 0xffff;
	pl->top[pl->minx-1] = 0xffff;
	pl->bottom[pl->maxx+1] = 0x0000;
	pl->bottom[pl->minx-1] = 0x0000;

	stop = pl->maxx + 1;

	if (pl->slope)
	{
		for (x = pl->minx; x <= stop; x++)
			R_MakeTiltedSpans(x, pl->top[x-1], pl->bottom[x-1], pl->top[x], pl->bottom[x]);
	}
	else
	{
		for (x = pl->minx; x <= stop; x++)
			R_MakeSpans(x, pl->top[x-1], pl->bottom[x-1], pl->top[x], pl->bottom[x]);
	}

/*
QUINCUNX anti-aliasing technique (sort of)

Normally, Quincunx antialiasing staggers pixels
in a 5-die pattern like so:

o   o
  o
o   o

To simulate this, we offset the plane by
FRACUNIT/4 in each direction, and draw
at 50% translucency. The result is
a 'smoothing' of the texture while
using the palette colors.
*/
#ifdef QUINCUNX
	if (spanfunc == spanfuncs[BASEDRAWFUNC])
	{
		INT32 i;
		if (usetranstables)
			ds_transmap = R_GetTranslucencyTable(tr_trans50);
		else
			ds_alpha = 128;
		spanfunc = spanfuncs[span_translu];
		for (i=0; i<4; i++)
		{
			xoffs = pl->xoffs;
			yoffs = pl->yoffs;

			switch(i)
			{
				case 0:
					xoffs -= FRACUNIT/4;
					yoffs -= FRACUNIT/4;
					break;
				case 1:
					xoffs -= FRACUNIT/4;
					yoffs += FRACUNIT/4;
					break;
				case 2:
					xoffs += FRACUNIT/4;
					yoffs -= FRACUNIT/4;
					break;
				case 3:
					xoffs += FRACUNIT/4;
					yoffs += FRACUNIT/4;
					break;
			}
			planeheight = abs(pl->height - pl->viewz);

			if (light >= LIGHTLEVELS)
				light = LIGHTLEVELS-1;

			if (light < 0)
				light = 0;

			planezlight = zlight[light];

			// set the maximum value for unsigned
			pl->top[pl->maxx+1] = 0xffff;
			pl->top[pl->minx-1] = 0xffff;
			pl->bottom[pl->maxx+1] = 0x0000;
			pl->bottom[pl->minx-1] = 0x0000;

			stop = pl->maxx + 1;

			for (x = pl->minx; x <= stop; x++)
				R_MakeSpans(x, pl->top[x-1], pl->bottom[x-1],
					pl->top[x], pl->bottom[x]);
		}
	}
#endif
}

void R_PlaneBounds(visplane_t *plane)
{
	INT32 i;
	INT32 hi, low;

	hi = plane->top[plane->minx];
	low = plane->bottom[plane->minx];

	for (i = plane->minx + 1; i <= plane->maxx; i++)
	{
		if (plane->top[i] < hi)
		hi = plane->top[i];
		if (plane->bottom[i] > low)
		low = plane->bottom[i];
	}
	plane->high = hi;
	plane->low = low;
}
