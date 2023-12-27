// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2023 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  f_finale.h
/// \brief Title screen, intro, game evaluation, and credits.

#ifndef __F_FINALE__
#define __F_FINALE__

#include "doomtype.h"
#include "d_event.h"
#include "p_mobj.h"

//
// FINALE
//

// Called by main loop.
boolean F_IntroResponder(event_t *ev);
boolean F_CutsceneResponder(event_t *ev);
boolean F_CreditResponder(event_t *ev);

// Called by main loop.
void F_GameEndTicker(void);
void F_IntroTicker(void);
void F_TitleScreenTicker(boolean run);
void F_CutsceneTicker(void);
void F_TitleDemoTicker(void);
void F_TextPromptTicker(void);

// Called by main loop.
void F_GameEndDrawer(void);
void F_IntroDrawer(void);
void F_TitleScreenDrawer(void);
void F_SkyScroll(const char *patchname);

void F_GameEvaluationDrawer(void);
void F_StartGameEvaluation(void);
void F_GameEvaluationTicker(void);

void F_EndingTicker(void);
void F_EndingDrawer(void);

void F_CreditTicker(void);
void F_CreditDrawer(void);

void F_StartCustomCutscene(INT32 cutscenenum, boolean precutscene, boolean resetplayer, boolean FLS);
void F_CutsceneDrawer(void);
void F_EndCutScene(void);

void F_StartTextPrompt(INT32 promptnum, INT32 pagenum, mobj_t *mo, UINT16 postexectag, boolean blockcontrols, boolean freezerealtime);
void F_GetPromptPageByNamedTag(const char *tag, INT32 *promptnum, INT32 *pagenum);
void F_TextPromptDrawer(void);
void F_EndTextPrompt(boolean forceexec, boolean noexec);
boolean F_GetPromptHideHudAll(void);
boolean F_GetPromptHideHud(fixed_t y);

void F_StartGameEnd(void);
void F_StartIntro(void);
void F_StartTitleScreen(void);
void F_StartEnding(void);
void F_StartCredits(void);

boolean F_ContinueResponder(event_t *event);
void F_StartContinue(void);
void F_ContinueTicker(void);
void F_ContinueDrawer(void);

void F_InitTitleScreen(void);

void F_StartWaitingPlayers(void);
void F_WaitingPlayersTicker(void);
void F_WaitingPlayersDrawer(void);

extern INT32 finalecount;
extern INT32 titlescrollxspeed;
extern INT32 titlescrollyspeed;

extern INT32 intro_scenenum;

enum
{
	INTRO_STJR      = 0,
	INTRO_FIRST     = 1,
	INTRO_ASTEROID  = 4,
	INTRO_RADAR     = 5,
	INTRO_GRASS1    = 6,
	INTRO_GRASS2    = 7,
	INTRO_SKYRUNNER = 10,
	INTRO_SONICDO1  = 14,
	INTRO_SONICDO2  = 15,
	INTRO_LAST      = 16
};

typedef enum
{
	TTMODE_NONE = 0,
	TTMODE_OLD,
	TTMODE_ALACROIX,
	TTMODE_USER
} ttmode_enum;

#define TTMAX_ALACROIX 30 // max frames for SONIC typeface, plus one for NULL terminating entry
#define TTMAX_USER 100

extern ttmode_enum ttmode;
extern UINT8 ttscale;
// ttmode user vars
extern char ttname[9];
extern INT16 ttx;
extern INT16 tty;
extern INT16 ttloop;
extern UINT16 tttics;
extern boolean ttavailable[6];

typedef enum
{
	TITLEMAP_OFF = 0,
	TITLEMAP_LOADING,
	TITLEMAP_RUNNING
} titlemap_enum;

// Current menu parameters
extern mobj_t *titlemapcameraref;
extern char curbgname[9];
extern SINT8 curfadevalue;
extern INT32 curbgcolor;
extern INT32 curbgxspeed;
extern INT32 curbgyspeed;
extern boolean curbghide;
extern boolean hidetitlemap;

extern boolean curhidepics;
extern ttmode_enum curttmode;
extern UINT8 curttscale;

// ttmode user vars
extern char curttname[9];
extern INT16 curttx;
extern INT16 curtty;
extern INT16 curttloop;
extern UINT16 curtttics;

#define TITLEBACKGROUNDACTIVE (curfadevalue >= 0 || curbgname[0])

void F_InitMenuPresValues(void);
void F_MenuPresTicker(void);

#endif
