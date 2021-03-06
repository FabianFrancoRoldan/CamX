/*
 *  Source machine generated by GadToolsBox V2.0b
 *  which is (c) Copyright 1991-1993 Jaba Development
 *
 *  GUI Designed by : Martin Endres
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/imageclass.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <graphics/displayinfo.h>
#include <graphics/gfxbase.h>
#include <clib/exec_protos.h>
#include <clib/intuition_protos.h>
#include <clib/gadtools_protos.h>
#include <clib/graphics_protos.h>
#include <clib/utility_protos.h>
#include <string.h>
#include <clib/diskfont_protos.h>

#include "scoregui.h"

far    struct Screen         *Scr = NULL;
far    UBYTE                 *PubScreenName = "Workbench";
far    APTR                   VisualInfo = NULL;
far    struct Window         *SoWnd = NULL;
far    struct Gadget         *SoGList = NULL;
far    struct TextFont       *Font       = NULL;
far    struct Gadget         *SoGadgets[6];
far    UWORD                  SoLeft = 12;
far    UWORD                  SoTop = 20;
far    UWORD                  SoWidth = 753;
far    UWORD                  SoHeight = 559;
far    UBYTE                 *SoWdt = (UBYTE *)"Scorefont";

far    UBYTE *zoomi0Labels[] = {
        (UBYTE *)"Raster1",
        (UBYTE *)"Raster2",
        (UBYTE *)"Raster3",
        (UBYTE *)"Raster4",
        NULL };

far    UBYTE *zorro0Labels[] = {
        (UBYTE *)"Zoom20",
        (UBYTE *)"Zoom18",
        (UBYTE *)"Zoom16",
        (UBYTE *)"Zoom14",
        (UBYTE *)"Zoom12",
        (UBYTE *)"Zoom10",
        (UBYTE *)"Zoom8",
        (UBYTE *)"Zoom6",
        (UBYTE *)"Zoom4",
        (UBYTE *)"Zoom3",
        NULL };

far    UBYTE *edscale0Labels[] = {
        (UBYTE *)"Edit",
        (UBYTE *)"Scale",
        NULL };

far    struct TextAttr XHelvetica11 = {
        ( STRPTR )"XHelvetica.font", 11, 0x00, 0x20 };

far    UWORD SoGTypes[] = {
        BUTTON_KIND,
        BUTTON_KIND,
        CYCLE_KIND,
        BUTTON_KIND,
        CYCLE_KIND,
        CYCLE_KIND
};

far    struct NewGadget SoNGad[] = {
        7, 533, 48, 19, (UBYTE *)"-", NULL, GD_sub, PLACETEXT_IN, NULL, NULL,
        59, 533, 48, 19, (UBYTE *)"+", NULL, GD_add, PLACETEXT_IN, NULL, NULL,
        427, 532, 64, 20, NULL, NULL, GD_zoomi, 0, NULL, NULL,
        713, 536, 27, 17, (UBYTE *)"Test", NULL, GD_test, PLACETEXT_IN, NULL, NULL,
        255, 533, 85, 19, NULL, NULL, GD_zorro, 0, NULL, NULL,
        611, 532, 64, 21, NULL, NULL, GD_edscale, 0, NULL, NULL
};

far    ULONG SoGTags[] = {
        (TAG_DONE),
        (TAG_DONE),
        (GTCY_Labels), (ULONG)&zoomi0Labels[ 0 ], (TAG_DONE),
        (TAG_DONE),
        (GTCY_Labels), (ULONG)&zorro0Labels[ 0 ], (GTCY_Active), 7, (TAG_DONE),
        (GTCY_Labels), (ULONG)&edscale0Labels[ 0 ], (TAG_DONE)
};

int SetupSoScreen( void )
{
        if ( ! ( Font = OpenDiskFont( &XHelvetica11 )))
                return( 5L );

        if ( ! ( Scr = LockPubScreen( PubScreenName )))
                return( 1L );

        if ( ! ( VisualInfo = GetVisualInfo( Scr, TAG_DONE )))
                return( 2L );

        return( 0L );
}

void CloseSoDownScreen( void )
{
        if ( VisualInfo ) {
                FreeVisualInfo( VisualInfo );
                VisualInfo = NULL;
        }

        if ( Scr        ) {
                UnlockPubScreen( NULL, Scr );
                Scr = NULL;
        }

        if ( Font       ) {
                CloseFont( Font );
                Font = NULL;
        }
}

int OpenSoWindow( void )
{
        struct NewGadget        ng;
        struct Gadget   *g;
        UWORD           lc, tc;
        UWORD           offx = Scr->WBorLeft, offy = Scr->WBorTop + Scr->RastPort.TxHeight + 1;

        if ( ! ( g = CreateContext( &SoGList )))
                return( 1L );

        for( lc = 0, tc = 0; lc < So_CNT; lc++ ) {

                CopyMem((char * )&SoNGad[ lc ], (char * )&ng, (long)sizeof( struct NewGadget ));

                ng.ng_VisualInfo = VisualInfo;
                ng.ng_TextAttr   = &XHelvetica11;
                ng.ng_LeftEdge  += offx;
                ng.ng_TopEdge   += offy;

                SoGadgets[ lc ] = g = CreateGadgetA((ULONG)SoGTypes[ lc ], g, &ng, ( struct TagItem * )&SoGTags[ tc ] );

                while( SoGTags[ tc ] ) tc += 2;
                tc++;

                if ( NOT g )
                        return( 2L );
        }

        if ( ! ( SoWnd = OpenWindowTags( NULL,
                                WA_Left,        SoLeft,
                                WA_Top,         SoTop,
                                WA_Width,       SoWidth,
                                WA_Height,      SoHeight + offy,
                                WA_IDCMP,       BUTTONIDCMP|CYCLEIDCMP|IDCMP_MOUSEBUTTONS|IDCMP_CLOSEWINDOW|IDCMP_REFRESHWINDOW,
                                WA_Flags,       WFLG_DRAGBAR|WFLG_DEPTHGADGET|WFLG_CLOSEGADGET|WFLG_SMART_REFRESH|WFLG_REPORTMOUSE|WFLG_ACTIVATE|WFLG_RMBTRAP,
                                WA_Gadgets,     SoGList,
                                WA_Title,       SoWdt,
                                TAG_DONE )))
        return( 4L );

        GT_RefreshWindow( SoWnd, NULL );

        return( 0L );
}

void CloseSoWindow( void )
{
        if ( SoWnd        ) {
                CloseWindow( SoWnd );
                SoWnd = NULL;
        }

        if ( SoGList      ) {
                FreeGadgets( SoGList );
                SoGList = NULL;
        }
}

