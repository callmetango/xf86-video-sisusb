/* $XFree86$ */
/* $XdotOrg$ */
/*
 * Xv driver for SiS 315 USB
 *
 * Copyright (C) 2001-2005 by Thomas Winischhofer, Vienna, Austria.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1) Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3) The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author:    Thomas Winischhofer <thomas@winischhofer.net>
 *
 */

#include "sisusb.h"
#include "xf86fbman.h"
#include "regionstr.h"

#include "xf86xv.h"
#include "Xv.h"
#include "dixstruct.h"
#include "fourcc.h"

#include "sisusb_regs.h"

static 		XF86VideoAdaptorPtr SISUSBSetupImageVideo(ScreenPtr);
static int 	SISUSBSetPortAttribute(ScrnInfoPtr, Atom, INT32, pointer);
static int 	SISUSBGetPortAttribute(ScrnInfoPtr, Atom ,INT32 *, pointer);
static void 	SISUSBQueryBestSize(ScrnInfoPtr, Bool, short, short, short,
			short, unsigned int *,unsigned int *, pointer);
static int 	SISUSBQueryImageAttributes(ScrnInfoPtr,
    			int, UShort *, UShort *, int *, int *);
static void 	SISUSBStopVideo(ScrnInfoPtr, pointer, Bool);
#ifdef SIS_ENABLEXV
static int 	SISUSBPutImage( ScrnInfoPtr,
    			short, short, short, short, short, short, short, short,
    			int, UChar *, short, short, Bool, RegionPtr, pointer);
static void 	SISUSBVideoTimerCallback(ScrnInfoPtr pScrn, Time now);
static void     SISUSBInitOffscreenImages(ScreenPtr pScrn);
FBLinearPtr     SISUSBAllocateOverlayMemory(ScrnInfoPtr pScrn, FBLinearPtr linear, int size);
#endif

#define OFF_DELAY   	200    /* milliseconds */
#define FREE_DELAY  	30000

#define OFF_TIMER   	0x01
#define FREE_TIMER  	0x02
#define CLIENT_VIDEO_ON 0x04

#define TIMER_MASK      (OFF_TIMER | FREE_TIMER)

#define WATCHDOG_DELAY  200000 /* Watchdog counter for Vertical Restrace waiting */

#define MAKE_ATOM(a) MakeAtom(a, sizeof(a) - 1, TRUE)

#define IMAGE_MIN_WIDTH         32  	/* Minimum and maximum source image sizes */
#define IMAGE_MIN_HEIGHT        24
#define IMAGE_MAX_WIDTH_315   1920
#define IMAGE_MAX_HEIGHT_315  1080
#define IMAGE_MAX_WIDTH_340   1920	/* ? */

#define OVERLAY_MIN_WIDTH       32  	/* Minimum overlay sizes */
#define OVERLAY_MIN_HEIGHT      24

#define DISPMODE_SINGLE1 0x1  		/* CRT1 only */
#define DISPMODE_SINGLE2 0x2  		/* CRT2 only */
#define DISPMODE_MIRROR  0x4  		/* CRT1 + CRT2 MIRROR (see note below) */

#define LINEBUFLIMIT1    384		/* Limits at which line buffers must be merged */
#define LINEBUFLIMIT2    720
#define LINEBUFLIMIT3    576
#define LINEBUFLIMIT4   1280		/* 340 */

#define NUM_FORMATS 3

static XF86VideoFormatRec SISUSBFormats[NUM_FORMATS] =
{
   { 8, PseudoColor},
   {16, TrueColor},
   {24, TrueColor}
};

static char sisxvcolorkey[] 				= "XV_COLORKEY";
static char sisxvbrightness[] 				= "XV_BRIGHTNESS";
static char sisxvcontrast[] 				= "XV_CONTRAST";
static char sisxvsaturation[] 				= "XV_SATURATION";
static char sisxvhue[] 					= "XV_HUE";
static char sisxvautopaintcolorkey[] 			= "XV_AUTOPAINT_COLORKEY";
static char sisxvsetdefaults[] 				= "XV_SET_DEFAULTS";
static char sisxvswitchcrt[] 				= "XV_SWITCHCRT";
static char sisxvtvxposition[] 				= "XV_TVXPOSITION";
static char sisxvtvyposition[] 				= "XV_TVYPOSITION";
static char sisxvgammared[] 				= "XV_GAMMA_RED";
static char sisxvgammagreen[] 				= "XV_GAMMA_GREEN";
static char sisxvgammablue[] 				= "XV_GAMMA_BLUE";
static char sisxvdisablegfx[] 				= "XV_DISABLE_GRAPHICS";
static char sisxvdisablegfxlr[] 			= "XV_DISABLE_GRAPHICS_LR";
static char sisxvdisablecolorkey[] 			= "XV_DISABLE_COLORKEY";
static char sisxvusechromakey[] 			= "XV_USE_CHROMAKEY";
static char sisxvinsidechromakey[] 			= "XV_INSIDE_CHROMAKEY";
static char sisxvyuvchromakey[] 			= "XV_YUV_CHROMAKEY";
static char sisxvchromamin[] 				= "XV_CHROMAMIN";
static char sisxvchromamax[] 				= "XV_CHROMAMAX";
static char sisxvqueryvbflags[] 			= "XV_QUERYVBFLAGS";
static char sisxvsdgetdriverversion[] 			= "XV_SD_GETDRIVERVERSION";
static char sisxvsdgethardwareinfo[]			= "XV_SD_GETHARDWAREINFO";
static char sisxvsdgetbusid[] 				= "XV_SD_GETBUSID";
static char sisxvsdqueryvbflagsversion[] 		= "XV_SD_QUERYVBFLAGSVERSION";
static char sisxvsdgetsdflags[] 			= "XV_SD_GETSDFLAGS";
static char sisxvsdgetsdflags2[] 			= "XV_SD_GETSDFLAGS2";
static char sisxvsdunlocksisdirect[] 			= "XV_SD_UNLOCKSISDIRECT";
static char sisxvsdsetvbflags[] 			= "XV_SD_SETVBFLAGS";
static char sisxvsdquerydetecteddevices[] 		= "XV_SD_QUERYDETECTEDDEVICES";
static char sisxvsdcrt1status[] 			= "XV_SD_CRT1STATUS";
static char sisxvsdcheckmodeindexforcrt2[] 		= "XV_SD_CHECKMODEINDEXFORCRT2";
static char sisxvsdresultcheckmodeindexforcrt2[] 	= "XV_SD_RESULTCHECKMODEINDEXFORCRT2";
static char sisxvsdredetectcrt2[]			= "XV_SD_REDETECTCRT2DEVICES";
static char sisxvsdsisantiflicker[] 			= "XV_SD_SISANTIFLICKER";
static char sisxvsdsissaturation[] 			= "XV_SD_SISSATURATION";
static char sisxvsdsisedgeenhance[] 			= "XV_SD_SISEDGEENHANCE";
static char sisxvsdsiscolcalibf[] 			= "XV_SD_SISCOLCALIBF";
static char sisxvsdsiscolcalibc[] 			= "XV_SD_SISCOLCALIBC";
static char sisxvsdsiscfilter[] 			= "XV_SD_SISCFILTER";
static char sisxvsdsisyfilter[] 			= "XV_SD_SISYFILTER";
static char sisxvsdchcontrast[] 			= "XV_SD_CHCONTRAST";
static char sisxvsdchtextenhance[] 			= "XV_SD_CHTEXTENHANCE";
static char sisxvsdchchromaflickerfilter[] 		= "XV_SD_CHCHROMAFLICKERFILTER";
static char sisxvsdchlumaflickerfilter[] 		= "XV_SD_CHLUMAFLICKERFILTER";
static char sisxvsdchcvbscolor[] 			= "XV_SD_CHCVBSCOLOR";
static char sisxvsdchoverscan[]				= "XV_SD_CHOVERSCAN";
static char sisxvsdenablegamma[]			= "XV_SD_ENABLEGAMMA";
static char sisxvsdtvxscale[] 				= "XV_SD_TVXSCALE";
static char sisxvsdtvyscale[] 				= "XV_SD_TVYSCALE";
static char sisxvsdgetscreensize[] 			= "XV_SD_GETSCREENSIZE";
static char sisxvsdstorebrir[] 				= "XV_SD_STOREDGAMMABRIR";
static char sisxvsdstorebrig[] 				= "XV_SD_STOREDGAMMABRIG";
static char sisxvsdstorebrib[] 				= "XV_SD_STOREDGAMMABRIB";
static char sisxvsdstorepbrir[] 			= "XV_SD_STOREDGAMMAPBRIR";
static char sisxvsdstorepbrig[] 			= "XV_SD_STOREDGAMMAPBRIG";
static char sisxvsdstorepbrib[] 			= "XV_SD_STOREDGAMMAPBRIB";
static char sisxvsdstorebrir2[]				= "XV_SD_STOREDGAMMABRIR2";
static char sisxvsdstorebrig2[]				= "XV_SD_STOREDGAMMABRIG2";
static char sisxvsdstorebrib2[]				= "XV_SD_STOREDGAMMABRIB2";
static char sisxvsdstorepbrir2[] 			= "XV_SD_STOREDGAMMAPBRIR2";
static char sisxvsdstorepbrig2[] 			= "XV_SD_STOREDGAMMAPBRIG2";
static char sisxvsdstorepbrib2[] 			= "XV_SD_STOREDGAMMAPBRIB2";
static char sisxvsdstoregarc2[]				= "XV_SD_GAMMACRT2R";
static char sisxvsdstoregagc2[]				= "XV_SD_GAMMACRT2G";
static char sisxvsdstoregabc2[]				= "XV_SD_GAMMACRT2B";
static char sisxvsdstorebrirc2[]			= "XV_SD_STOREDGAMMABRIRC2";
static char sisxvsdstorebrigc2[]			= "XV_SD_STOREDGAMMABRIGC2";
static char sisxvsdstorebribc2[]			= "XV_SD_STOREDGAMMABRIBC2";
static char sisxvsdstorepbrirc2[] 			= "XV_SD_STOREDGAMMAPBRIRC2";
static char sisxvsdstorepbrigc2[] 			= "XV_SD_STOREDGAMMAPBRIGC2";
static char sisxvsdstorepbribc2[] 			= "XV_SD_STOREDGAMMAPBRIBC2";
static char sisxvsdhidehwcursor[] 			= "XV_SD_HIDEHWCURSOR";
static char sisxvsdpanelmode[] 				= "XV_SD_PANELMODE";
#ifdef TWDEBUG
static char sisxvsetreg[]				= "XV_SD_SETREG";
#endif

/***********************************************/
/*               OVERLAY ADAPTOR               */
/***********************************************/

#define GET_PORT_PRIVATE(pScrn) \
   (SISUSBPortPrivPtr)((SISUSBPTR(pScrn))->adaptor->pPortPrivates[0].ptr)

/* client libraries expect an encoding */
static XF86VideoEncodingRec DummyEncoding =
{
   0,
   "XV_IMAGE",
   0, 0,		/* Will be filled in */
   {1, 1}
};

#ifdef TWDEBUG
#define NUM_ATTRIBUTES_315 75
#else
#define NUM_ATTRIBUTES_315 74
#endif

static XF86AttributeRec SISUSBAttributes_315[NUM_ATTRIBUTES_315] =
{
   {XvSettable | XvGettable, 0, (1 << 24) - 1, sisxvcolorkey},
   {XvSettable | XvGettable, -128, 127,        sisxvbrightness},
   {XvSettable | XvGettable, 0, 7,             sisxvcontrast},
   {XvSettable | XvGettable, -7, 7,            sisxvsaturation},
   {XvSettable | XvGettable, -8, 7,            sisxvhue},
   {XvSettable | XvGettable, 0, 1,             sisxvautopaintcolorkey},
   {XvSettable             , 0, 0,             sisxvsetdefaults},
   {XvSettable | XvGettable, -32, 32,          sisxvtvxposition},
   {XvSettable | XvGettable, -32, 32,          sisxvtvyposition},
   {XvSettable | XvGettable, 100, 10000,       sisxvgammared},
   {XvSettable | XvGettable, 100, 10000,       sisxvgammagreen},
   {XvSettable | XvGettable, 100, 10000,       sisxvgammablue},
   {XvSettable | XvGettable, 0, 1,             sisxvdisablegfx},
   {XvSettable | XvGettable, 0, 1,             sisxvdisablegfxlr},
   {XvSettable | XvGettable, 0, 1,             sisxvdisablecolorkey},
   {XvSettable | XvGettable, 0, 1,             sisxvusechromakey},
   {XvSettable | XvGettable, 0, 1,             sisxvinsidechromakey},
   {XvSettable | XvGettable, 0, (1 << 24) - 1, sisxvchromamin},
   {XvSettable | XvGettable, 0, (1 << 24) - 1, sisxvchromamax},
   {             XvGettable, 0, -1,    	       sisxvqueryvbflags},
   {             XvGettable, 0, -1,    	       sisxvsdgetdriverversion},
   {             XvGettable, 0, -1,    	       sisxvsdgethardwareinfo},
   {             XvGettable, 0, -1,    	       sisxvsdgetbusid},
   {             XvGettable, 0, -1,    	       sisxvsdqueryvbflagsversion},
   {             XvGettable, 0, -1,   	       sisxvsdgetsdflags},
   {             XvGettable, 0, -1,   	       sisxvsdgetsdflags2},
   {XvSettable | XvGettable, 0, -1,   	       sisxvsdunlocksisdirect},
   {XvSettable             , 0, -1,   	       sisxvsdsetvbflags},
   {             XvGettable, 0, -1,    	       sisxvsdquerydetecteddevices},
   {XvSettable | XvGettable, 0, 1,    	       sisxvsdcrt1status},
   {XvSettable             , 0, -1,    	       sisxvsdcheckmodeindexforcrt2},
   {             XvGettable, 0, -1,   	       sisxvsdresultcheckmodeindexforcrt2},
   {XvSettable             , 0, 0,             sisxvsdredetectcrt2},
   {XvSettable | XvGettable, 0, 4,             sisxvsdsisantiflicker},
   {XvSettable | XvGettable, 0, 15,            sisxvsdsissaturation},
   {XvSettable | XvGettable, 0, 15,            sisxvsdsisedgeenhance},
   {XvSettable | XvGettable, -128, 127,        sisxvsdsiscolcalibf},
   {XvSettable | XvGettable, -120, 120,        sisxvsdsiscolcalibc},
   {XvSettable | XvGettable, 0, 1,             sisxvsdsiscfilter},
   {XvSettable | XvGettable, 0, 8,             sisxvsdsisyfilter},
   {XvSettable | XvGettable, 0, 15,            sisxvsdchcontrast},
   {XvSettable | XvGettable, 0, 15,            sisxvsdchtextenhance},
   {XvSettable | XvGettable, 0, 15,            sisxvsdchchromaflickerfilter},
   {XvSettable | XvGettable, 0, 15,            sisxvsdchlumaflickerfilter},
   {XvSettable | XvGettable, 0, 1,             sisxvsdchcvbscolor},
   {XvSettable | XvGettable, 0, 3,             sisxvsdchoverscan},
   {XvSettable | XvGettable, 0, 7,             sisxvsdenablegamma},
   {XvSettable | XvGettable, -16, 16,          sisxvsdtvxscale},
   {XvSettable | XvGettable, -4, 3,            sisxvsdtvyscale},
   {             XvGettable, 0, -1,   	       sisxvsdgetscreensize},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstorebrir},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstorebrig},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstorebrib},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstorepbrir},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstorepbrig},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstorepbrib},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstorebrir2},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstorebrig2},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstorebrib2},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstorepbrir2},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstorepbrig2},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstorepbrib2},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstoregarc2},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstoregagc2},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstoregabc2},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstorebrirc2},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstorebrigc2},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstorebribc2},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstorepbrirc2},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstorepbrigc2},
   {XvSettable | XvGettable, 100, 10000,       sisxvsdstorepbribc2},
   {XvSettable | XvGettable, 0, 1,             sisxvsdhidehwcursor},
   {XvSettable | XvGettable, 0, 15,            sisxvsdpanelmode},
#ifdef TWDEBUG
   {XvSettable             , 0, -1,   	       sisxvsetreg},
#endif
   {XvSettable | XvGettable, 0, 1,             sisxvswitchcrt},
};

#define NUM_IMAGES_315 7	    /* basically NV12 only - but does not work */

#define PIXEL_FMT_YV12 FOURCC_YV12  /* 0x32315659 */
#define PIXEL_FMT_UYVY FOURCC_UYVY  /* 0x59565955 */
#define PIXEL_FMT_YUY2 FOURCC_YUY2  /* 0x32595559 */
#define PIXEL_FMT_I420 FOURCC_I420  /* 0x30323449 */
#define PIXEL_FMT_RGB5 0x35315652
#define PIXEL_FMT_RGB6 0x36315652
#define PIXEL_FMT_YVYU 0x55595659   /* 315/330+ only */
#define PIXEL_FMT_NV12 0x3231564e   /* 330+ only */
#define PIXEL_FMT_NV21 0x3132564e   /* 330+ only */

/* TODO: */
#define PIXEL_FMT_RAW8 0x38574152

static XF86ImageRec SISUSBImages[NUM_IMAGES_315] =
{
    XVIMAGE_YUY2, /* If order is changed, SISOffscreenImages must be adapted */
    XVIMAGE_YV12,
    XVIMAGE_UYVY,
    XVIMAGE_I420
    ,
    { /* RGB 555 */
      PIXEL_FMT_RGB5,
      XvRGB,
      LSBFirst,
      {'R','V','1','5',
       0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
      16,
      XvPacked,
      1,
      15, 0x7C00, 0x03E0, 0x001F,
      0, 0, 0,
      0, 0, 0,
      0, 0, 0,
      {'R', 'V', 'B',0,
       0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
      XvTopToBottom
    },
    { /* RGB 565 */
      PIXEL_FMT_RGB6,
      XvRGB,
      LSBFirst,
      {'R','V','1','6',
       0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
      16,
      XvPacked,
      1,
      16, 0xF800, 0x07E0, 0x001F,
      0, 0, 0,
      0, 0, 0,
      0, 0, 0,
      {'R', 'V', 'B',0,
       0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
      XvTopToBottom
    },
    {  /* YVYU */
      PIXEL_FMT_YVYU, \
      XvYUV, \
      LSBFirst, \
      {'Y','V','Y','U',
	0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71},
      16,
      XvPacked,
      1,
      0, 0, 0, 0,
      8, 8, 8,
      1, 2, 2,
      1, 1, 1,
      {'Y','V','Y','U',
       0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
      XvTopToBottom
   }
};

typedef struct {
    FBLinearPtr  linear;
    CARD32       bufAddr[2];

    UChar currentBuf;

    short drw_x, drw_y, drw_w, drw_h;
    short src_x, src_y, src_w, src_h;
    int id;
    short srcPitch, height;

    char         brightness;
    UChar        contrast;
    char 	 hue;
    short        saturation;

    RegionRec    clip;
    CARD32       colorKey;
    Bool 	 autopaintColorKey;

    Bool 	 disablegfx;
    Bool	 disablegfxlr;

    Bool         usechromakey;
    Bool	 insidechromakey, yuvchromakey;
    CARD32	 chromamin, chromamax;

    CARD32       videoStatus;
    Bool	 overlayStatus;
    Time         offTime;
    Time         freeTime;

    CARD32       displayMode;
    Bool	 bridgeIsSlave;

    Bool         hasTwoOverlays;   /* Chipset has two overlays */
    Bool         dualHeadMode;     /* We're running in DHM */

    Bool  	 NoOverlay;
    Bool	 PrevOverlay;

    Bool	 AllowSwitchCRT;
    int 	 crtnum;	   /* 0=CRT1, 1=CRT2 */

    Bool         needToScale;      /* Need to scale video */

    int          shiftValue;       /* 315/330 series need word addr/pitch, 300 series double word */

    short  	 linebufMergeLimit;
    CARD8        linebufmask;

    short        oldx1, oldx2, oldy1, oldy2;
    int          mustwait;

    Bool         grabbedByV4L;	   /* V4L stuff */
    int          pitch;
    int          offset;

    int 	 modeflags;	   /* Flags field of current display mode */

    Bool	 is340;

} SISUSBPortPrivRec, *SISUSBPortPrivPtr;

typedef struct {
    int pixelFormat;

    CARD16  pitch;
    CARD16  origPitch;

    CARD8   keyOP;
    CARD16  HUSF;
    CARD16  VUSF;
    CARD8   IntBit;
    CARD8   wHPre;

    CARD16  srcW;
    CARD16  srcH;

    BoxRec  dstBox;

    CARD32  PSY;
    CARD32  PSV;
    CARD32  PSU;

    CARD16  SCREENheight;

    CARD16  lineBufSize;

    DisplayModePtr  currentmode;

    CARD8   bobEnable;

    CARD8   planar;
    CARD8   planar_shiftpitch;

    CARD8   contrastCtrl;
    CARD8   contrastFactor;

    CARD16  oldLine, oldtop;

    CARD8   (*VBlankActiveFunc)(SISUSBPtr, SISUSBPortPrivPtr);
#if 0
    CARD32  (*GetScanLineFunc)(SISUSBPtr pSiSUSB);
#endif

} SISUSBOverlayRec, *SISUSBOverlayPtr;

/****************************************************************************
 * Raw register access : These routines directly interact with the sis's
 *                       control aperature.  Must not be called until after
 *                       the board's pci memory has been mapped.
 ****************************************************************************/

#ifdef SIS_ENABLEXV
#if 0
static CARD32 _sisread(SISUSBPtr pSiSUSB, CARD32 reg)
{
    return *(pSiSUSB->IOBase + reg);
}

static void _siswrite(SISUSBPtr pSiSUSB, CARD32 reg, CARD32 data)
{
    *(pSiSUSB->IOBase + reg) = data;
}
#endif

static CARD8 getsrreg(SISUSBPtr pSiSUSB, CARD8 reg)
{
    CARD8 ret;
    inSISIDXREG(pSiSUSB, SISSR, reg, ret);
    return(ret);
}

static CARD8 getvideoreg(SISUSBPtr pSiSUSB, CARD8 reg)
{
    CARD8 ret;
    inSISIDXREG(pSiSUSB, SISVID, reg, ret);
    return(ret);
}

static __inline void setvideoreg(SISUSBPtr pSiSUSB, CARD8 reg, CARD8 data)
{
    outSISIDXREG(pSiSUSB, SISVID, reg, data);
}

static __inline void setvideoregmask(SISUSBPtr pSiSUSB, CARD8 reg, CARD8 data, CARD8 mask)
{
    setSISIDXREGmask(pSiSUSB, SISVID, reg, data, mask);
}

static void setsrregmask(SISUSBPtr pSiSUSB, CARD8 reg, CARD8 data, CARD8 mask)
{
    setSISIDXREGmask(pSiSUSB, SISSR, reg, data, mask);
}

/* VBlank */
static CARD8 vblank_active_CRT1(SISUSBPtr pSiSUSB, SISUSBPortPrivPtr pPriv)
{
    return(inSISREG(pSiSUSB, SISINPSTAT) & 0x08); /* Verified */
}

/* Scanline - unused */
#if 0
static CARD16 get_scanline_CRT1(SISUSBPtr pSiSUSB)
{
    CARD32 line;

    _siswrite(pSiSUSB, REG_PRIM_CRT_COUNTER, 0x00000001);
    line = _sisread(pSiSUSB, REG_PRIM_CRT_COUNTER);

    return((CARD16)((line >> 16) & 0x07FF));
}
#endif
#endif /* SIS_ENABLEXV */

#ifdef SIS_ENABLEXV
static void
SiSUSBComputeXvGamma(SISUSBPtr pSiSUSB)
{
    int num = 255, i;
    double red = 1.0 / (double)((double)pSiSUSB->XvGammaRed / 1000);
    double green = 1.0 / (double)((double)pSiSUSB->XvGammaGreen / 1000);
    double blue = 1.0 / (double)((double)pSiSUSB->XvGammaBlue / 1000);

    for(i = 0; i <= num; i++) {
        pSiSUSB->XvGammaRampRed[i] =
	    (red == 1.0) ? i : (CARD8)(pow((double)i / (double)num, red) * (double)num + 0.5);

	pSiSUSB->XvGammaRampGreen[i] =
	    (green == 1.0) ? i : (CARD8)(pow((double)i / (double)num, green) * (double)num + 0.5);

	pSiSUSB->XvGammaRampBlue[i] =
	    (blue == 1.0) ? i : (CARD8)(pow((double)i / (double)num, blue) * (double)num + 0.5);
    }
}

static void
SiSUSBSetXvGamma(SISUSBPtr pSiSUSB)
{
    int i;
    UChar backup = getsrreg(pSiSUSB, 0x1f);
    setsrregmask(pSiSUSB, 0x1f, 0x08, 0x18);
    for(i = 0; i <= 255; i++) {
       SIS_MMIO_OUT32(pSiSUSB, pSiSUSB->IOBase, 0x8570,
       			(i << 24)     |
			(pSiSUSB->XvGammaRampBlue[i] << 16) |
			(pSiSUSB->XvGammaRampGreen[i] << 8) |
			pSiSUSB->XvGammaRampRed[i]);
    }
    setsrregmask(pSiSUSB, 0x1f, backup, 0xff);
}

static void
SiSUSBUpdateXvGamma(SISUSBPtr pSiSUSB, SISUSBPortPrivPtr pPriv)
{
    UChar sr7 = getsrreg(pSiSUSB, 0x07);

    if(!pSiSUSB->XvGamma) return;
    if(!(pSiSUSB->MiscFlags & MISC_CRT1OVERLAYGAMMA)) return;

    if(!(sr7 & 0x04)) return;

    SiSUSBComputeXvGamma(pSiSUSB);
    SiSUSBSetXvGamma(pSiSUSB);
}

static void
SISUSBResetXvGamma(ScrnInfoPtr pScrn)
{
    SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);
    SISUSBPortPrivPtr pPriv = GET_PORT_PRIVATE(pScrn);

    SiSUSBUpdateXvGamma(pSiSUSB, pPriv);
}
#endif

void SISUSBInitVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    XF86VideoAdaptorPtr *adaptors, *newAdaptors = NULL;
    XF86VideoAdaptorPtr newAdaptor = NULL;
    int num_adaptors;

    newAdaptor = SISUSBSetupImageVideo(pScreen);

#ifdef SIS_ENABLEXV
    if(newAdaptor) {
       SISUSBInitOffscreenImages(pScreen);
    }
#endif

    num_adaptors = xf86XVListGenericAdaptors(pScrn, &adaptors);

    if(newAdaptor) {
       int size = num_adaptors;

       if(newAdaptor) size++;

       newAdaptors = xalloc(size * sizeof(XF86VideoAdaptorPtr*));
       if(newAdaptors) {
          if(num_adaptors) {
             memcpy(newAdaptors, adaptors, num_adaptors * sizeof(XF86VideoAdaptorPtr));
	  }
	  if(newAdaptor) {
             newAdaptors[num_adaptors] = newAdaptor;
             num_adaptors++;
          }
	  adaptors = newAdaptors;
       }
    }

    if(num_adaptors) {
       xf86XVScreenInit(pScreen, adaptors, num_adaptors);
    }

    if(newAdaptors) {
       xfree(newAdaptors);
    }
}

static void
SISUSBSetPortDefaults(ScrnInfoPtr pScrn, SISUSBPortPrivPtr pPriv)
{
    SISUSBPtr    pSiSUSB = SISUSBPTR(pScrn);

    pPriv->colorKey    = pSiSUSB->colorKey = 0x000101fe;
    pPriv->brightness  = pSiSUSB->XvDefBri;
    pPriv->contrast    = pSiSUSB->XvDefCon;
    pPriv->hue         = pSiSUSB->XvDefHue;
    pPriv->saturation  = pSiSUSB->XvDefSat;
    pPriv->autopaintColorKey = TRUE;
    pPriv->disablegfx  = pSiSUSB->XvDefDisableGfx;
    pPriv->disablegfxlr= pSiSUSB->XvDefDisableGfxLR;
    pSiSUSB->disablecolorkeycurrent = pSiSUSB->XvDisableColorKey;
    pPriv->usechromakey    = pSiSUSB->XvUseChromaKey;
    pPriv->insidechromakey = pSiSUSB->XvInsideChromaKey;
    pPriv->yuvchromakey    = pSiSUSB->XvYUVChromaKey;
    pPriv->chromamin       = pSiSUSB->XvChromaMin;
    pPriv->chromamax       = pSiSUSB->XvChromaMax;
    pPriv->crtnum = 0;

    pSiSUSB->XvGammaRed = pSiSUSB->XvGammaRedDef;
    pSiSUSB->XvGammaGreen = pSiSUSB->XvGammaGreenDef;
    pSiSUSB->XvGammaBlue = pSiSUSB->XvGammaBlueDef;
#ifdef SIS_ENABLEXV
    SiSUSBUpdateXvGamma(pSiSUSB, pPriv);
#endif
}

#ifdef SIS_ENABLEXV
static void
SISUSBResetVideo(ScrnInfoPtr pScrn)
{
    SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);
    SISUSBPortPrivPtr pPriv = GET_PORT_PRIVATE(pScrn);

    /* Unlock registers */
#ifdef UNLOCK_ALWAYS
    sisusbSaveUnlockExtRegisterLock(pSiSUSB, NULL, NULL);
#endif
    if(getvideoreg(pSiSUSB, Index_VI_Passwd) != 0xa1) {
        setvideoreg(pSiSUSB, Index_VI_Passwd, 0x86);
        if(getvideoreg(pSiSUSB, Index_VI_Passwd) != 0xa1)
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Xv: Video password could not unlock registers\n");
    }

    /* Initialize first overlay (CRT1) ------------------------------- */

    /* This bit has obviously a different meaning on 315 series (linebuffer-related) */

    /* Select overlay 2, clear all linebuffer related bits */
    setvideoregmask(pSiSUSB, Index_VI_Control_Misc2,      0x00, 0xb1);

    /* Disable overlay */
    setvideoregmask(pSiSUSB, Index_VI_Control_Misc0,         0x00, 0x02);

    /* Disable bob de-interlacer and some strange bit */
    setvideoregmask(pSiSUSB, Index_VI_Control_Misc1,         0x00, 0x82);

    /* Reset scale control and contrast */
    /* (Enable DDA (interpolation)) */
    setvideoregmask(pSiSUSB, Index_VI_Scale_Control,         0x60, 0x60);
    setvideoregmask(pSiSUSB, Index_VI_Contrast_Enh_Ctrl,     0x04, 0x1F);

    setvideoreg(pSiSUSB, Index_VI_Disp_Y_Buf_Preset_Low,     0x00);
    setvideoreg(pSiSUSB, Index_VI_Disp_Y_Buf_Preset_Middle,  0x00);
    setvideoreg(pSiSUSB, Index_VI_UV_Buf_Preset_Low,         0x00);
    setvideoreg(pSiSUSB, Index_VI_UV_Buf_Preset_Middle,      0x00);
    setvideoreg(pSiSUSB, Index_VI_Disp_Y_UV_Buf_Preset_High, 0x00);
    setvideoreg(pSiSUSB, Index_VI_Play_Threshold_Low,        0x00);
    setvideoreg(pSiSUSB, Index_VI_Play_Threshold_High,       0x00);

    /* Reset top window position for scanline check */
    setvideoreg(pSiSUSB, Index_VI_Win_Ver_Disp_Start_Low, 0x00);
    setvideoreg(pSiSUSB, Index_VI_Win_Ver_Over, 0x00);

    /* set default properties for overlay 1 (CRT1) -------------------------- */
    setvideoregmask(pSiSUSB, Index_VI_Control_Misc2,         0x00, 0x01);
    setvideoregmask(pSiSUSB, Index_VI_Contrast_Enh_Ctrl,     0x04, 0x07);
    setvideoreg(pSiSUSB, Index_VI_Brightness,                0x20);
    setvideoreg(pSiSUSB, Index_VI_Hue,          	  	  0x00);
    setvideoreg(pSiSUSB, Index_VI_Saturation,             	  0x00);

    /* Reset Xv gamma correction */
    SiSUSBUpdateXvGamma(pSiSUSB, pPriv);
}
#endif

/* Set display mode (single CRT1/CRT2, mirror).
 * MIRROR mode is only available on chipsets with two overlays.
 * On the other chipsets, if only CRT1 or only CRT2 are used,
 * the correct display CRT is chosen automatically. If both
 * CRT1 and CRT2 are connected, the user can choose between CRT1 and
 * CRT2 by using the option XvOnCRT2.
 */
#ifdef SIS_ENABLEXV
static void
set_dispmode(ScrnInfoPtr pScrn, SISUSBPortPrivPtr pPriv)
{
    pPriv->dualHeadMode = pPriv->bridgeIsSlave = FALSE;
    pPriv->displayMode = DISPMODE_SINGLE1;    /* CRT1 only */
}

static void
set_disptype_regs(ScrnInfoPtr pScrn, SISUSBPortPrivPtr pPriv)
{
    SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);

#ifdef UNLOCK_ALWAYS
    sisusbSaveUnlockExtRegisterLock(pSiSUSB, NULL, NULL);
#endif

    setsrregmask(pSiSUSB, 0x06, 0x00, 0xc0);  /* only overlay -> CRT1 */
    setsrregmask(pSiSUSB, 0x32, 0x00, 0xc0);
}
#endif

static void
set_allowswitchcrt(SISUSBPtr pSiSUSB, SISUSBPortPrivPtr pPriv)
{
    pPriv->AllowSwitchCRT = FALSE;
}

static void
set_maxencoding(SISUSBPtr pSiSUSB, SISUSBPortPrivPtr pPriv)
{
    DummyEncoding.width = IMAGE_MAX_WIDTH_315;
    DummyEncoding.height = IMAGE_MAX_HEIGHT_315;
}

static XF86VideoAdaptorPtr
SISUSBSetupImageVideo(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);
    XF86VideoAdaptorPtr adapt;
    SISUSBPortPrivPtr pPriv;

    if(!(adapt = xcalloc(1, sizeof(XF86VideoAdaptorRec) +
                            sizeof(SISUSBPortPrivRec) +
                            sizeof(DevUnion))))
    	return NULL;

    adapt->type = XvWindowMask | XvInputMask | XvImageMask;
    adapt->flags = VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT;
    adapt->name = "SIS 300/315/330 series Video Overlay";
    adapt->nEncodings = 1;
    adapt->pEncodings = &DummyEncoding;

    adapt->nFormats = NUM_FORMATS;
    adapt->pFormats = SISUSBFormats;
    adapt->nPorts = 1;
    adapt->pPortPrivates = (DevUnion*)(&adapt[1]);

    pPriv = (SISUSBPortPrivPtr)(&adapt->pPortPrivates[1]);

    /* Setup chipset type helpers */

    pPriv->hasTwoOverlays = FALSE;
    pPriv->AllowSwitchCRT = FALSE;

    set_allowswitchcrt(pSiSUSB, pPriv);

    adapt->pPortPrivates[0].ptr = (pointer)(pPriv);

    adapt->nImages = NUM_IMAGES_315;
    adapt->pAttributes = SISUSBAttributes_315;
    adapt->nAttributes = NUM_ATTRIBUTES_315;
    if(pPriv->hasTwoOverlays) adapt->nAttributes--;

    adapt->pImages = SISUSBImages;
    adapt->PutVideo = NULL;
    adapt->PutStill = NULL;
    adapt->GetVideo = NULL;
    adapt->GetStill = NULL;
    adapt->StopVideo = SISUSBStopVideo;
#ifdef SIS_ENABLEXV
    adapt->PutImage  = SISUSBPutImage;
#else
    adapt->PutImage  = NULL;
#endif
    adapt->SetPortAttribute = SISUSBSetPortAttribute;
    adapt->GetPortAttribute = SISUSBGetPortAttribute;
    adapt->QueryBestSize = SISUSBQueryBestSize;
    adapt->QueryImageAttributes = SISUSBQueryImageAttributes;

    pPriv->videoStatus = 0;
    pPriv->currentBuf  = 0;
    pPriv->linear      = NULL;
    pPriv->grabbedByV4L= FALSE;
    pPriv->NoOverlay   = FALSE;
    pPriv->PrevOverlay = FALSE;
    pPriv->is340       = FALSE;

    /* gotta uninit this someplace */
#if defined(REGION_NULL)
    REGION_NULL(pScreen, &pPriv->clip);
#else
    REGION_INIT(pScreen, &pPriv->clip, NullBox, 0);
#endif

    pSiSUSB->adaptor = adapt;

    pSiSUSB->xvBrightness = MAKE_ATOM(sisxvbrightness);
    pSiSUSB->xvContrast   = MAKE_ATOM(sisxvcontrast);
    pSiSUSB->xvColorKey   = MAKE_ATOM(sisxvcolorkey);
    pSiSUSB->xvSaturation = MAKE_ATOM(sisxvsaturation);
    pSiSUSB->xvHue        = MAKE_ATOM(sisxvhue);
    pSiSUSB->xvSwitchCRT  = MAKE_ATOM(sisxvswitchcrt);
    pSiSUSB->xvAutopaintColorKey = MAKE_ATOM(sisxvautopaintcolorkey);
    pSiSUSB->xvSetDefaults       = MAKE_ATOM(sisxvsetdefaults);
    pSiSUSB->xvDisableGfx        = MAKE_ATOM(sisxvdisablegfx);
    pSiSUSB->xvDisableGfxLR      = MAKE_ATOM(sisxvdisablegfxlr);
    pSiSUSB->xvTVXPosition       = MAKE_ATOM(sisxvtvxposition);
    pSiSUSB->xvTVYPosition       = MAKE_ATOM(sisxvtvyposition);
    pSiSUSB->xvGammaRed  	 = MAKE_ATOM(sisxvgammared);
    pSiSUSB->xvGammaGreen 	 = MAKE_ATOM(sisxvgammagreen);
    pSiSUSB->xvGammaBlue  	 = MAKE_ATOM(sisxvgammablue);
    pSiSUSB->xvDisableColorkey   = MAKE_ATOM(sisxvdisablecolorkey);
    pSiSUSB->xvUseChromakey      = MAKE_ATOM(sisxvusechromakey);
    pSiSUSB->xvInsideChromakey   = MAKE_ATOM(sisxvinsidechromakey);
    pSiSUSB->xvYUVChromakey      = MAKE_ATOM(sisxvyuvchromakey);
    pSiSUSB->xvChromaMin	 = MAKE_ATOM(sisxvchromamin);
    pSiSUSB->xvChromaMax         = MAKE_ATOM(sisxvchromamax);
    pSiSUSB->xv_QVF              = MAKE_ATOM(sisxvqueryvbflags);
    pSiSUSB->xv_GDV	      = MAKE_ATOM(sisxvsdgetdriverversion);
    pSiSUSB->xv_GHI	      = MAKE_ATOM(sisxvsdgethardwareinfo);
    pSiSUSB->xv_GBI	      = MAKE_ATOM(sisxvsdgetbusid);
    pSiSUSB->xv_QVV           = MAKE_ATOM(sisxvsdqueryvbflagsversion);
    pSiSUSB->xv_GSF           = MAKE_ATOM(sisxvsdgetsdflags);
    pSiSUSB->xv_GSF2          = MAKE_ATOM(sisxvsdgetsdflags2);
    pSiSUSB->xv_USD           = MAKE_ATOM(sisxvsdunlocksisdirect);
    pSiSUSB->xv_SVF           = MAKE_ATOM(sisxvsdsetvbflags);
    pSiSUSB->xv_QDD	      = MAKE_ATOM(sisxvsdquerydetecteddevices);
    pSiSUSB->xv_CT1	      = MAKE_ATOM(sisxvsdcrt1status);
    pSiSUSB->xv_CMD	      = MAKE_ATOM(sisxvsdcheckmodeindexforcrt2);
    pSiSUSB->xv_CMDR	      = MAKE_ATOM(sisxvsdresultcheckmodeindexforcrt2);
    pSiSUSB->xv_RDT	      = MAKE_ATOM(sisxvsdredetectcrt2);
    pSiSUSB->xv_TAF	      = MAKE_ATOM(sisxvsdsisantiflicker);
    pSiSUSB->xv_TSA	      = MAKE_ATOM(sisxvsdsissaturation);
    pSiSUSB->xv_TEE	      = MAKE_ATOM(sisxvsdsisedgeenhance);
    pSiSUSB->xv_COC	      = MAKE_ATOM(sisxvsdsiscolcalibc);
    pSiSUSB->xv_COF	      = MAKE_ATOM(sisxvsdsiscolcalibf);
    pSiSUSB->xv_CFI	      = MAKE_ATOM(sisxvsdsiscfilter);
    pSiSUSB->xv_YFI	      = MAKE_ATOM(sisxvsdsisyfilter);
    pSiSUSB->xv_TCO	      = MAKE_ATOM(sisxvsdchcontrast);
    pSiSUSB->xv_TTE	      = MAKE_ATOM(sisxvsdchtextenhance);
    pSiSUSB->xv_TCF	      = MAKE_ATOM(sisxvsdchchromaflickerfilter);
    pSiSUSB->xv_TLF	      = MAKE_ATOM(sisxvsdchlumaflickerfilter);
    pSiSUSB->xv_TCC	      = MAKE_ATOM(sisxvsdchcvbscolor);
    pSiSUSB->xv_OVR	      = MAKE_ATOM(sisxvsdchoverscan);
    pSiSUSB->xv_SGA	      = MAKE_ATOM(sisxvsdenablegamma);
    pSiSUSB->xv_TXS	      = MAKE_ATOM(sisxvsdtvxscale);
    pSiSUSB->xv_TYS	      = MAKE_ATOM(sisxvsdtvyscale);
    pSiSUSB->xv_GSS	      = MAKE_ATOM(sisxvsdgetscreensize);
    pSiSUSB->xv_BRR	      = MAKE_ATOM(sisxvsdstorebrir);
    pSiSUSB->xv_BRG	      = MAKE_ATOM(sisxvsdstorebrig);
    pSiSUSB->xv_BRB	      = MAKE_ATOM(sisxvsdstorebrib);
    pSiSUSB->xv_PBR	      = MAKE_ATOM(sisxvsdstorepbrir);
    pSiSUSB->xv_PBG	      = MAKE_ATOM(sisxvsdstorepbrig);
    pSiSUSB->xv_PBB	      = MAKE_ATOM(sisxvsdstorepbrib);
    pSiSUSB->xv_BRR2	      = MAKE_ATOM(sisxvsdstorebrir2);
    pSiSUSB->xv_BRG2	      = MAKE_ATOM(sisxvsdstorebrig2);
    pSiSUSB->xv_BRB2	      = MAKE_ATOM(sisxvsdstorebrib2);
    pSiSUSB->xv_PBR2	      = MAKE_ATOM(sisxvsdstorepbrir2);
    pSiSUSB->xv_PBG2	      = MAKE_ATOM(sisxvsdstorepbrig2);
    pSiSUSB->xv_PBB2	      = MAKE_ATOM(sisxvsdstorepbrib2);
    pSiSUSB->xv_GARC2	      = MAKE_ATOM(sisxvsdstoregarc2);
    pSiSUSB->xv_GAGC2	      = MAKE_ATOM(sisxvsdstoregagc2);
    pSiSUSB->xv_GABC2	      = MAKE_ATOM(sisxvsdstoregabc2);
    pSiSUSB->xv_BRRC2	      = MAKE_ATOM(sisxvsdstorebrirc2);
    pSiSUSB->xv_BRGC2	      = MAKE_ATOM(sisxvsdstorebrigc2);
    pSiSUSB->xv_BRBC2	      = MAKE_ATOM(sisxvsdstorebribc2);
    pSiSUSB->xv_PBRC2	      = MAKE_ATOM(sisxvsdstorepbrirc2);
    pSiSUSB->xv_PBGC2	      = MAKE_ATOM(sisxvsdstorepbrigc2);
    pSiSUSB->xv_PBBC2	      = MAKE_ATOM(sisxvsdstorepbribc2);
    pSiSUSB->xv_SHC	      = MAKE_ATOM(sisxvsdhidehwcursor);
    pSiSUSB->xv_PMD	      = MAKE_ATOM(sisxvsdpanelmode);
#ifdef TWDEBUG
    pSiSUSB->xv_STR	      = MAKE_ATOM(sisxvsetreg);
#endif

    pSiSUSB->xv_sisdirectunlocked = 0;
    pSiSUSB->xv_sd_result = 0;

    pPriv->shiftValue = 1;

    /* Set displayMode according to VBFlags */
#ifdef SIS_ENABLEXV
    set_dispmode(pScrn, pPriv);
#endif

    pPriv->linebufMergeLimit = LINEBUFLIMIT1;

    set_maxencoding(pSiSUSB, pPriv);

    pPriv->linebufmask = 0xb1;
    if(!(pPriv->hasTwoOverlays)) {
       /* On machines with only one overlay, the linebuffers are
        * generally larger, so our merging-limit is higher, too.
	*/
       pPriv->linebufMergeLimit = LINEBUFLIMIT2;
    }

    /* Reset the properties to their defaults */
    SISUSBSetPortDefaults(pScrn, pPriv);

#ifdef SIS_ENABLEXV
    /* Set SR(06, 32) registers according to DISPMODE */
    set_disptype_regs(pScrn, pPriv);

    SISUSBResetVideo(pScrn);
    pSiSUSB->ResetXv = SISUSBResetVideo;
    pSiSUSB->ResetXvGamma = SISUSBResetXvGamma;
#endif

    return adapt;
}

#ifdef SIS_ENABLEXV
#if XF86_VERSION_CURRENT < XF86_VERSION_NUMERIC(4,3,99,3,0)
static Bool
RegionsEqual(RegionPtr A, RegionPtr B)
{
    int *dataA, *dataB;
    int num;

    num = REGION_NUM_RECTS(A);
    if(num != REGION_NUM_RECTS(B))
    return FALSE;

    if((A->extents.x1 != B->extents.x1) ||
       (A->extents.x2 != B->extents.x2) ||
       (A->extents.y1 != B->extents.y1) ||
       (A->extents.y2 != B->extents.y2))
    return FALSE;

    dataA = (int*)REGION_RECTS(A);
    dataB = (int*)REGION_RECTS(B);

    while(num--) {
      if((dataA[0] != dataB[0]) || (dataA[1] != dataB[1]))
        return FALSE;
      dataA += 2;
      dataB += 2;
    }

    return TRUE;
}
#endif
#endif

static int
SISUSBSetPortAttribute(ScrnInfoPtr pScrn, Atom attribute,
  		    INT32 value, pointer data)
{
  SISUSBPortPrivPtr pPriv = (SISUSBPortPrivPtr)data;
  SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);


  if(attribute == pSiSUSB->xvBrightness) {
     if((value < -128) || (value > 127))
        return BadValue;
     pPriv->brightness = value;
  } else if(attribute == pSiSUSB->xvContrast) {
     if((value < 0) || (value > 7))
        return BadValue;
     pPriv->contrast = value;
  } else if(attribute == pSiSUSB->xvColorKey) {
     pPriv->colorKey = pSiSUSB->colorKey = value;
     REGION_EMPTY(pScrn->pScreen, &pPriv->clip);
  } else if(attribute == pSiSUSB->xvAutopaintColorKey) {
     if((value < 0) || (value > 1))
        return BadValue;
     pPriv->autopaintColorKey = value;
  } else if(attribute == pSiSUSB->xvSetDefaults) {
     SISUSBSetPortDefaults(pScrn, pPriv);
  } else if(attribute == pSiSUSB->xvDisableGfx) {
     if((value < 0) || (value > 1))
        return BadValue;
     pPriv->disablegfx = value;
  } else if(attribute == pSiSUSB->xvDisableGfxLR) {
     if((value < 0) || (value > 1))
        return BadValue;
     pPriv->disablegfxlr = value;
  } else if(attribute == pSiSUSB->xvTVXPosition) {

  } else if(attribute == pSiSUSB->xvTVYPosition) {

  } else if(attribute == pSiSUSB->xvDisableColorkey) {
     if((value < 0) || (value > 1))
        return BadValue;
     pSiSUSB->disablecolorkeycurrent = value;
  } else if(attribute == pSiSUSB->xvUseChromakey) {
     if((value < 0) || (value > 1))
        return BadValue;
     pPriv->usechromakey = value;
  } else if(attribute == pSiSUSB->xvInsideChromakey) {
     if((value < 0) || (value > 1))
        return BadValue;
     pPriv->insidechromakey = value;
  } else if(attribute == pSiSUSB->xvYUVChromakey) {
     if((value < 0) || (value > 1))
        return BadValue;
     pPriv->yuvchromakey = value;
  } else if(attribute == pSiSUSB->xvChromaMin) {
     pPriv->chromamin = value;
  } else if(attribute == pSiSUSB->xvChromaMax) {
     pPriv->chromamax = value;
  } else if(attribute == pSiSUSB->xv_USD) {
     if(pSiSUSB->enablesisctrl) {
        if(value == SIS_DIRECTKEY) {
	   pSiSUSB->xv_sisdirectunlocked++;
	} else if(pSiSUSB->xv_sisdirectunlocked) {
	   pSiSUSB->xv_sisdirectunlocked--;
	}
     } else {
     	pSiSUSB->xv_sisdirectunlocked = 0;
     }
  } else if(attribute == pSiSUSB->xv_SVF) {
        if(pSiSUSB->xv_sisdirectunlocked) {
#ifdef SIS_ENABLEXV
	   set_dispmode(pScrn, pPriv);
#endif
	   set_allowswitchcrt(pSiSUSB, pPriv);
	   set_maxencoding(pSiSUSB, pPriv);
        }
  } else if(attribute == pSiSUSB->xv_CT1) {
        if(pSiSUSB->xv_sisdirectunlocked) {
#ifdef SIS_ENABLEXV
	   set_dispmode(pScrn, pPriv);
#endif
	   set_allowswitchcrt(pSiSUSB, pPriv);
	   set_maxencoding(pSiSUSB, pPriv);
        }
  } else if(attribute == pSiSUSB->xv_RDT) {
        /* nil */
  } else if(attribute == pSiSUSB->xv_TAF) {
     	/* nil */
  } else if(attribute == pSiSUSB->xv_TSA) {
     	/* nil */
  } else if(attribute == pSiSUSB->xv_TEE) {

  } else if(attribute == pSiSUSB->xv_CFI) {

  } else if(attribute == pSiSUSB->xv_YFI) {

  } else if(attribute == pSiSUSB->xv_COC) {

  } else if(attribute == pSiSUSB->xv_COF) {

  } else if(attribute == pSiSUSB->xv_TCO) {

  } else if(attribute == pSiSUSB->xv_TTE) {

  } else if(attribute == pSiSUSB->xv_TCF) {

  } else if(attribute == pSiSUSB->xv_TLF) {

  } else if(attribute == pSiSUSB->xv_TCC) {

  } else if(attribute == pSiSUSB->xv_OVR) {

  } else if(attribute == pSiSUSB->xv_CMD) {
     if(pSiSUSB->xv_sisdirectunlocked) {
        pSiSUSB->xv_sd_result = (value & 0xffffff00);
     }
  } else if(attribute == pSiSUSB->xv_SGA) {
     if(pSiSUSB->xv_sisdirectunlocked) {
#ifdef SIS_ENABLEXV
        Bool backup = pSiSUSB->XvGamma;
#endif
        pSiSUSB->CRT1gamma = (value & 0x01) ? TRUE : FALSE;
	pSiSUSB->XvGamma = (value & 0x04) ? TRUE : FALSE;
#ifdef SIS_ENABLEXV
        if(backup != pSiSUSB->XvGamma) {
	   SiSUSBUpdateXvGamma(pSiSUSB, pPriv);
	}
#endif
     }
  } else if(attribute == pSiSUSB->xv_TXS) {
     if((value < -16) || (value > 16))
        return BadValue;
  } else if(attribute == pSiSUSB->xv_TYS) {
     if((value < -4) || (value > 3))
        return BadValue;
  } else if(attribute == pSiSUSB->xv_BRR) {
     if((value < 100) || (value > 10000))
        return BadValue;
     if(pSiSUSB->xv_sisdirectunlocked) {
        pSiSUSB->GammaBriR = value;
     }
  } else if(attribute == pSiSUSB->xv_BRG) {
     if((value < 100) || (value > 10000))
        return BadValue;
     if(pSiSUSB->xv_sisdirectunlocked) {
        pSiSUSB->GammaBriG = value;
     }
  } else if(attribute == pSiSUSB->xv_BRB) {
     if((value < 100) || (value > 10000))
        return BadValue;
     if(pSiSUSB->xv_sisdirectunlocked) {
        pSiSUSB->GammaBriB = value;
     }
  } else if(attribute == pSiSUSB->xv_PBR) {
     if((value < 100) || (value > 10000))
        return BadValue;
     if(pSiSUSB->xv_sisdirectunlocked) {
        pSiSUSB->GammaPBriR = value;
     }
  } else if(attribute == pSiSUSB->xv_PBG) {
     if((value < 100) || (value > 10000))
        return BadValue;
     if(pSiSUSB->xv_sisdirectunlocked) {
        pSiSUSB->GammaPBriG = value;
     }
  } else if(attribute == pSiSUSB->xv_PBB) {
     if((value < 100) || (value > 10000))
        return BadValue;
     if(pSiSUSB->xv_sisdirectunlocked) {
        pSiSUSB->GammaPBriB = value;
     }
  } else if(attribute == pSiSUSB->xv_BRR2) {

  } else if(attribute == pSiSUSB->xv_BRG2) {

  } else if(attribute == pSiSUSB->xv_BRB2) {

  } else if(attribute == pSiSUSB->xv_PBR2) {

  } else if(attribute == pSiSUSB->xv_PBG2) {

  } else if(attribute == pSiSUSB->xv_PBB2) {

  } else if(attribute == pSiSUSB->xv_GARC2) {

  } else if(attribute == pSiSUSB->xv_GAGC2) {

  } else if(attribute == pSiSUSB->xv_GABC2) {

  } else if(attribute == pSiSUSB->xv_BRRC2) {

  } else if(attribute == pSiSUSB->xv_BRGC2) {

  } else if(attribute == pSiSUSB->xv_BRBC2) {

  } else if(attribute == pSiSUSB->xv_PBRC2) {

  } else if(attribute == pSiSUSB->xv_PBGC2) {

  } else if(attribute == pSiSUSB->xv_PBBC2) {

  } else if(attribute == pSiSUSB->xv_SHC) {
     if(pSiSUSB->xv_sisdirectunlocked) {
        Bool VisibleBackup = pSiSUSB->HWCursorIsVisible;
        pSiSUSB->HideHWCursor = value ? TRUE : FALSE;
	if(pSiSUSB->CursorInfoPtr) {
	   if(VisibleBackup) {
	      if(value) {
	         (pSiSUSB->CursorInfoPtr->HideCursor)(pScrn);
	      } else {
	         (pSiSUSB->CursorInfoPtr->ShowCursor)(pScrn);
	      }
	   }
	   pSiSUSB->HWCursorIsVisible = VisibleBackup;
	}
     }
  } else if(attribute == pSiSUSB->xv_PMD) {

#ifdef TWDEBUG
  } else if(attribute == pSiSUSB->xv_STR) {
     ULong port;
     switch((value & 0xff000000) >> 24) {
     case 0x00: port = SISSR;    break;
     case 0x01: port = SISPART1; break;
     case 0x02: port = SISPART2; break;
     case 0x03: port = SISPART3; break;
     case 0x04: port = SISPART4; break;
     case 0x05: port = SISCR;    break;
     case 0x06: port = SISVID;   break;
     default:   return BadValue;
     }
     outSISIDXREG(pSiSUSB, port,((value & 0x00ff0000) >> 16), ((value & 0x0000ff00) >> 8));
     return Success;
#endif
  } else if(pSiSUSB->VGAEngine == SIS_315_VGA) {
     if(attribute == pSiSUSB->xvSwitchCRT) {
        if(pPriv->AllowSwitchCRT) {
           if((value < 0) || (value > 1))
              return BadValue;
	   pPriv->crtnum = value;
        }
     } else if(attribute == pSiSUSB->xvHue) {
       if((value < -8) || (value > 7))
          return BadValue;
       pPriv->hue = value;
     } else if(attribute == pSiSUSB->xvSaturation) {
       if((value < -7) || (value > 7))
          return BadValue;
       pPriv->saturation = value;
     } else if(attribute == pSiSUSB->xvGammaRed) {
       if((value < 100) || (value > 10000))
          return BadValue;
       pSiSUSB->XvGammaRed = value;
#ifdef SIS_ENABLEXV
       SiSUSBUpdateXvGamma(pSiSUSB, pPriv);
#endif
     } else if(attribute == pSiSUSB->xvGammaGreen) {
       if((value < 100) || (value > 10000))
          return BadValue;
       pSiSUSB->XvGammaGreen = value;
#ifdef SIS_ENABLEXV
       SiSUSBUpdateXvGamma(pSiSUSB, pPriv);
#endif
     } else if(attribute == pSiSUSB->xvGammaBlue) {
       if((value < 100) || (value > 10000))
          return BadValue;
       pSiSUSB->XvGammaBlue = value;
#ifdef SIS_ENABLEXV
       SiSUSBUpdateXvGamma(pSiSUSB, pPriv);
#endif
     } else return BadMatch;
  } else return BadMatch;
  return Success;
}

static int
SISUSBGetPortAttribute(
  ScrnInfoPtr pScrn,
  Atom attribute,
  INT32 *value,
  pointer data
){
  SISUSBPortPrivPtr pPriv = (SISUSBPortPrivPtr)data;
  SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);

  if(attribute == pSiSUSB->xvBrightness) {
     *value = pPriv->brightness;
  } else if(attribute == pSiSUSB->xvContrast) {
     *value = pPriv->contrast;
  } else if(attribute == pSiSUSB->xvColorKey) {
     *value = pPriv->colorKey;
  } else if(attribute == pSiSUSB->xvAutopaintColorKey) {
     *value = (pPriv->autopaintColorKey) ? 1 : 0;
  } else if(attribute == pSiSUSB->xvDisableGfx) {
     *value = (pPriv->disablegfx) ? 1 : 0;
  } else if(attribute == pSiSUSB->xvDisableGfxLR) {
     *value = (pPriv->disablegfxlr) ? 1 : 0;
  } else if(attribute == pSiSUSB->xvTVXPosition) {
     *value = 0;
  } else if(attribute == pSiSUSB->xvTVYPosition) {
     *value = 0;
  } else if(attribute == pSiSUSB->xvDisableColorkey) {
     *value = (pSiSUSB->disablecolorkeycurrent) ? 1 : 0;
  } else if(attribute == pSiSUSB->xvUseChromakey) {
     *value = (pPriv->usechromakey) ? 1 : 0;
  } else if(attribute == pSiSUSB->xvInsideChromakey) {
     *value = (pPriv->insidechromakey) ? 1 : 0;
  } else if(attribute == pSiSUSB->xvYUVChromakey) {
     *value = (pPriv->yuvchromakey) ? 1 : 0;
  } else if(attribute == pSiSUSB->xvChromaMin) {
     *value = pPriv->chromamin;
  } else if(attribute == pSiSUSB->xvChromaMax) {
     *value = pPriv->chromamax;
  } else if(attribute == pSiSUSB->xv_QVF) {
     *value = pSiSUSB->VBFlags;
  } else if(attribute == pSiSUSB->xv_GDV) {
     *value = SISUSBDRIVERIVERSION;
  } else if(attribute == pSiSUSB->xv_GHI) {
     *value = (pSiSUSB->ChipFlags & 0xffff) | (pSiSUSB->sishw_ext.jChipType << 16) | (pSiSUSB->ChipRev << 24);
  } else if(attribute == pSiSUSB->xv_GBI) {
     *value = 0x55aa0000 | ((pSiSUSB->USBBus & 0xff) << 8) | (pSiSUSB->USBDev & 0xff);
  } else if(attribute == pSiSUSB->xv_QVV) {
     *value = SIS_VBFlagsVersion;
  } else if(attribute == pSiSUSB->xv_QDD) {
     *value = 0;
  } else if(attribute == pSiSUSB->xv_CT1) {
     *value = 1;
  } else if(attribute == pSiSUSB->xv_GSF) {
     *value = pSiSUSB->SiS_SD_Flags;
  } else if(attribute == pSiSUSB->xv_GSF2) {
     *value = pSiSUSB->SiS_SD2_Flags;
  } else if(attribute == pSiSUSB->xv_USD) {
     *value = pSiSUSB->xv_sisdirectunlocked;
  } else if(attribute == pSiSUSB->xv_TAF) {
     *value = 0;
  } else if(attribute == pSiSUSB->xv_TSA) {
     *value = 0;
  } else if(attribute == pSiSUSB->xv_TEE) {
     *value = 0;
  } else if(attribute == pSiSUSB->xv_CFI) {
     *value = 0;
  } else if(attribute == pSiSUSB->xv_YFI) {
     *value = 0;
  } else if(attribute == pSiSUSB->xv_COC) {
     *value = 0;
  } else if(attribute == pSiSUSB->xv_COF) {
     *value = 0;
  } else if(attribute == pSiSUSB->xv_TCO) {
     *value = 0;
  } else if(attribute == pSiSUSB->xv_TTE) {
     *value = 0;
  } else if(attribute == pSiSUSB->xv_TCF) {
     *value = 0;
  } else if(attribute == pSiSUSB->xv_TLF) {
     *value = 0;
  } else if(attribute == pSiSUSB->xv_TCC) {
     *value = 0;
  } else if(attribute == pSiSUSB->xv_CMDR) {
     *value = pSiSUSB->xv_sd_result;
  } else if(attribute == pSiSUSB->xv_OVR) {
     /* Changing of CRT2 settings not supported in DHM! */
     *value = 0;
  } else if(attribute == pSiSUSB->xv_SGA) {
     *value = 0;
     if(pSiSUSB->CRT1gamma) *value |= 0x01;
     if(pSiSUSB->XvGamma)   *value |= 0x04;
  } else if(attribute == pSiSUSB->xv_TXS) {
     *value = 0;
  } else if(attribute == pSiSUSB->xv_TYS) {
     *value = 0;
  } else if(attribute == pSiSUSB->xv_GSS) {
     *value = (pScrn->virtualX << 16) | pScrn->virtualY;
  } else if(attribute == pSiSUSB->xv_BRR) {
     *value = pSiSUSB->GammaBriR;
  } else if(attribute == pSiSUSB->xv_BRG) {
     *value = pSiSUSB->GammaBriG;
  } else if(attribute == pSiSUSB->xv_BRB) {
     *value = pSiSUSB->GammaBriB;
  } else if(attribute == pSiSUSB->xv_PBR) {
     *value = pSiSUSB->GammaPBriR;
  } else if(attribute == pSiSUSB->xv_PBG) {
     *value = pSiSUSB->GammaPBriG;
  } else if(attribute == pSiSUSB->xv_PBB) {
     *value = pSiSUSB->GammaPBriB;
  } else if(attribute == pSiSUSB->xv_BRR2) {
     *value = pSiSUSB->GammaBriR;
  } else if(attribute == pSiSUSB->xv_BRG2) {
     *value = pSiSUSB->GammaBriG;
  } else if(attribute == pSiSUSB->xv_BRB2) {
     *value = pSiSUSB->GammaBriB;
  } else if(attribute == pSiSUSB->xv_PBR2) {
    *value = pSiSUSB->GammaPBriR;
  } else if(attribute == pSiSUSB->xv_PBG2) {
    *value = pSiSUSB->GammaPBriG;
  } else if(attribute == pSiSUSB->xv_PBB2) {
    *value = pSiSUSB->GammaPBriB;
  } else if(attribute == pSiSUSB->xv_GARC2) {
     *value = 1000;
  } else if(attribute == pSiSUSB->xv_GAGC2) {
     *value = 1000;
  } else if(attribute == pSiSUSB->xv_GABC2) {
     *value = 1000;
  } else if(attribute == pSiSUSB->xv_BRRC2) {
     *value = 1000;
  } else if(attribute == pSiSUSB->xv_BRGC2) {
     *value = 1000;
  } else if(attribute == pSiSUSB->xv_BRBC2) {
     *value = 1000;
  } else if(attribute == pSiSUSB->xv_PBRC2) {
     *value = 1000;
  } else if(attribute == pSiSUSB->xv_PBGC2) {
     *value = 1000;
  } else if(attribute == pSiSUSB->xv_PBBC2) {
     *value = 1000;
  } else if(attribute == pSiSUSB->xv_SHC) {
     *value = pSiSUSB->HideHWCursor ? 1 : 0;
  } else if(attribute == pSiSUSB->xv_PMD) {
     *value = 0;
  } else if(pSiSUSB->VGAEngine == SIS_315_VGA) {
     if(attribute == pSiSUSB->xvSwitchCRT) {
        *value = 0;
     } else if(attribute == pSiSUSB->xvHue) {
        *value = pPriv->hue;
     } else if(attribute == pSiSUSB->xvSaturation) {
        *value = pPriv->saturation;
     } else if(attribute == pSiSUSB->xvGammaRed) {
        *value = pSiSUSB->XvGammaRed;
     } else if(attribute == pSiSUSB->xvGammaGreen) {
        *value = pSiSUSB->XvGammaGreen;
     } else if(attribute == pSiSUSB->xvGammaBlue) {
        *value = pSiSUSB->XvGammaBlue;
     } else return BadMatch;
  } else return BadMatch;
  return Success;
}


static void
SISUSBQueryBestSize(
  ScrnInfoPtr pScrn,
  Bool motion,
  short vid_w, short vid_h,
  short drw_w, short drw_h,
  unsigned int *p_w, unsigned int *p_h,
  pointer data
){
  *p_w = drw_w;
  *p_h = drw_h;
}

#ifdef SIS_ENABLEXV
static void
calc_scale_factor(SISUSBOverlayPtr pOverlay, ScrnInfoPtr pScrn,
                 SISUSBPortPrivPtr pPriv, int index, int iscrt2)
{
  SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);
  CARD32 I=0,mult=0;
  int flag=0;

  int dstW = pOverlay->dstBox.x2 - pOverlay->dstBox.x1;
  int dstH = pOverlay->dstBox.y2 - pOverlay->dstBox.y1;
  int srcW = pOverlay->srcW;
  int srcH = pOverlay->srcH;
  int srcPitch = pOverlay->origPitch;
  int origdstH = dstH;
  int modeflags = pOverlay->currentmode->Flags;

  /* For double scan modes, we need to double the height
   * On 315 and 550 (?), we need to double the width as well.
   * Interlace mode vice versa.
   */
  if(modeflags & V_DBLSCAN) {
     dstH = origdstH << 1;
     flag = 0;
     if((pSiSUSB->sishw_ext.jChipType >= SIS_315H) &&
	(pSiSUSB->sishw_ext.jChipType <= SIS_550)) {
	dstW <<= 1;
     }
  }
  if(modeflags & V_INTERLACE) {
     dstH = origdstH >> 1;
     flag = 0;
  }

  if(dstW < OVERLAY_MIN_WIDTH) dstW = OVERLAY_MIN_WIDTH;
  if(dstW == srcW) {
     pOverlay->HUSF   = 0x00;
     pOverlay->IntBit = 0x05;
     pOverlay->wHPre  = 0;
  } else if(dstW > srcW) {
     dstW += 2;
     pOverlay->HUSF   = (srcW << 16) / dstW;
     pOverlay->IntBit = 0x04;
     pOverlay->wHPre  = 0;
  } else {
     int tmpW = dstW;

     /* It seems, the hardware can't scale below factor .125 (=1/8) if the
        pitch isn't a multiple of 256.
	TODO: Test this on the 315 series!
      */
     if((srcPitch % 256) || (srcPitch < 256)) {
        if(((dstW * 1000) / srcW) < 125) dstW = tmpW = ((srcW * 125) / 1000) + 1;
     }

     I = 0;
     pOverlay->IntBit = 0x01;
     while(srcW >= tmpW) {
        tmpW <<= 1;
        I++;
     }
     pOverlay->wHPre = (CARD8)(I - 1);
     dstW <<= (I - 1);
     if((srcW % dstW))
        pOverlay->HUSF = ((srcW - dstW) << 16) / dstW;
     else
        pOverlay->HUSF = 0x00;
  }

  if(dstH < OVERLAY_MIN_HEIGHT) dstH = OVERLAY_MIN_HEIGHT;
  if(dstH == srcH) {
     pOverlay->VUSF   = 0x00;
     pOverlay->IntBit |= 0x0A;
  } else if(dstH > srcH) {
     dstH += 0x02;
     pOverlay->VUSF = (srcH << 16) / dstH;
     pOverlay->IntBit |= 0x08;
  } else {

     I = srcH / dstH;
     pOverlay->IntBit |= 0x02;

     if(I < 2) {
        pOverlay->VUSF = ((srcH - dstH) << 16) / dstH;
	/* Needed for LCD-scaling modes */
	if((flag) && (mult = (srcH / origdstH)) >= 2) {
	   pOverlay->pitch /= mult;
	}
     } else {
#if 0
        if(((pOverlay->bobEnable & 0x08) == 0x00) &&
           (((srcPitch * I) >> 2) > 0xFFF)){
           pOverlay->bobEnable |= 0x08;
           srcPitch >>= 1;
        }
#endif
        if(((srcPitch * I) >> 2) > 0xFFF) {
           I = (0xFFF * 2 / srcPitch);
           pOverlay->VUSF = 0xFFFF;
        } else {
           dstH = I * dstH;
           if(srcH % dstH)
              pOverlay->VUSF = ((srcH - dstH) << 16) / dstH;
           else
              pOverlay->VUSF = 0x00;
        }
        /* set video frame buffer offset */
        pOverlay->pitch = (CARD16)(srcPitch * I);
     }
  }
}

static CARD16
calc_line_buf_size(CARD32 srcW, CARD8 wHPre, CARD8 planar, SISUSBPortPrivPtr pPriv)
{
    CARD32 I;

    if(planar) {

        switch(wHPre & 0x07) {
            case 3:
	        I = (srcW >> 8);
		if(srcW & 0xff) I++;
		I <<= 5;
		break;
            case 4:
	        I = (srcW >> 9);
		if(srcW & 0x1ff) I++;
		I <<= 6;
		break;
            case 5:
	        I = (srcW >> 10);
		if(srcW & 0x3ff) I++;
		I <<= 7;
		break;
            case 6:
	        if(pPriv->is340) {
	           I = (srcW >> 11);
		   if(srcW & 0x7ff) I++;
		   I <<= 8;
		   break;
		} else {
                   return((CARD16)(255));
		}
            default:
	        I = (srcW >> 7);
		if(srcW & 0x7f) I++;
		I <<= 4;
		break;
        }

    } else { /* packed */

        I = (srcW >> 3);
	if(srcW & 0x07) I++;

    }

    if(I <= 3) I = 4;

    return((CARD16)(I - 1));
}

static __inline void
calc_line_buf_size_1(SISUSBOverlayPtr pOverlay, SISUSBPortPrivPtr pPriv)
{
    pOverlay->lineBufSize =
     	calc_line_buf_size(pOverlay->srcW, pOverlay->wHPre, pOverlay->planar, pPriv);
}

static void
merge_line_buf(SISUSBPtr pSiSUSB, SISUSBPortPrivPtr pPriv, Bool enable, short width, short limit)
{
  UChar misc1, misc2, mask = pPriv->linebufmask;

  if(enable) { 		/* ----- enable linebuffer merge */

    misc2 = 0x00;
    misc1 = 0x04;
    setvideoregmask(pSiSUSB, Index_VI_Control_Misc2, misc2, mask);
    setvideoregmask(pSiSUSB, Index_VI_Control_Misc1, misc1, 0x04);

  } else {  		/* ----- disable linebuffer merge */

    setvideoregmask(pSiSUSB, Index_VI_Control_Misc2, 0x00, mask);
    setvideoregmask(pSiSUSB, Index_VI_Control_Misc1, 0x00, 0x04);

  }
}

static __inline void
set_format(SISUSBPtr pSiSUSB, SISUSBOverlayPtr pOverlay)
{
    CARD8 fmt;

    switch (pOverlay->pixelFormat){
    case PIXEL_FMT_YV12:
    case PIXEL_FMT_I420:
        fmt = 0x0c;
        break;
    case PIXEL_FMT_YUY2:
        fmt = 0x28;
        break;
    case PIXEL_FMT_UYVY:
        fmt = 0x08;
        break;
    case PIXEL_FMT_YVYU:
        fmt = 0x38;
	break;
    case PIXEL_FMT_NV12:
        fmt = 0x4c;
	break;
    case PIXEL_FMT_NV21:
        fmt = 0x5c;
	break;
    case PIXEL_FMT_RGB5:   /* D[5:4] : 00 RGB555, 01 RGB 565 */
        fmt = 0x00;
	break;
    case PIXEL_FMT_RGB6:
        fmt = 0x10;
	break;
    default:
        fmt = 0x00;
        break;
    }
    setvideoregmask(pSiSUSB, Index_VI_Control_Misc0, fmt, 0xfc);
}

static __inline void
set_colorkey(SISUSBPtr pSiSUSB, CARD32 colorkey)
{
    CARD8 r, g, b;

    b = (CARD8)(colorkey & 0xFF);
    g = (CARD8)((colorkey>>8) & 0xFF);
    r = (CARD8)((colorkey>>16) & 0xFF);

    setvideoreg(pSiSUSB, Index_VI_Overlay_ColorKey_Blue_Min  ,(CARD8)b);
    setvideoreg(pSiSUSB, Index_VI_Overlay_ColorKey_Green_Min ,(CARD8)g);
    setvideoreg(pSiSUSB, Index_VI_Overlay_ColorKey_Red_Min   ,(CARD8)r);

    setvideoreg(pSiSUSB, Index_VI_Overlay_ColorKey_Blue_Max  ,(CARD8)b);
    setvideoreg(pSiSUSB, Index_VI_Overlay_ColorKey_Green_Max ,(CARD8)g);
    setvideoreg(pSiSUSB, Index_VI_Overlay_ColorKey_Red_Max   ,(CARD8)r);
}

static __inline void
set_chromakey(SISUSBPtr pSiSUSB, CARD32 chromamin, CARD32 chromamax)
{
    CARD8 r1, g1, b1;
    CARD8 r2, g2, b2;

    b1 = (CARD8)(chromamin & 0xFF);
    g1 = (CARD8)((chromamin>>8) & 0xFF);
    r1 = (CARD8)((chromamin>>16) & 0xFF);
    b2 = (CARD8)(chromamax & 0xFF);
    g2 = (CARD8)((chromamax>>8) & 0xFF);
    r2 = (CARD8)((chromamax>>16) & 0xFF);

    setvideoreg(pSiSUSB, Index_VI_Overlay_ChromaKey_Blue_V_Min  ,(CARD8)b1);
    setvideoreg(pSiSUSB, Index_VI_Overlay_ChromaKey_Green_U_Min ,(CARD8)g1);
    setvideoreg(pSiSUSB, Index_VI_Overlay_ChromaKey_Red_Y_Min   ,(CARD8)r1);

    setvideoreg(pSiSUSB, Index_VI_Overlay_ChromaKey_Blue_V_Max  ,(CARD8)b2);
    setvideoreg(pSiSUSB, Index_VI_Overlay_ChromaKey_Green_U_Max ,(CARD8)g2);
    setvideoreg(pSiSUSB, Index_VI_Overlay_ChromaKey_Red_Y_Max   ,(CARD8)r2);
}

static __inline void
set_brightness(SISUSBPtr pSiSUSB, CARD8 brightness)
{
    setvideoreg(pSiSUSB, Index_VI_Brightness, brightness);
}

static __inline void
set_contrast(SISUSBPtr pSiSUSB, CARD8 contrast)
{
    setvideoregmask(pSiSUSB, Index_VI_Contrast_Enh_Ctrl, contrast, 0x07);
}

/* 315 series and later only */
static __inline void
set_saturation(SISUSBPtr pSiSUSB, short saturation)
{
    CARD8 temp = 0;

    if(saturation < 0) {
    	temp |= 0x88;
	saturation = -saturation;
    }
    temp |= (saturation & 0x07);
    temp |= ((saturation & 0x07) << 4);

    setvideoreg(pSiSUSB, Index_VI_Saturation, temp);
}

/* 315 series and later only */
static __inline void
set_hue(SISUSBPtr pSiSUSB, CARD8 hue)
{
    setvideoregmask(pSiSUSB, Index_VI_Hue, (hue & 0x08) ? (hue ^ 0x07) : hue, 0x0F);
}

static __inline void
set_disablegfx(SISUSBPtr pSiSUSB, Bool mybool, SISUSBOverlayPtr pOverlay)
{
    /* This is not supported on M65x, 65x (x>0) or later */
    /* For CRT1 ONLY!!! */
    setvideoregmask(pSiSUSB, Index_VI_Control_Misc2, mybool ? 0x04 : 0x00, 0x04);
    if(mybool) pOverlay->keyOP = VI_ROP_Always;
}

static __inline void
set_disablegfxlr(SISUSBPtr pSiSUSB, Bool mybool, SISUSBOverlayPtr pOverlay)
{
    setvideoregmask(pSiSUSB, Index_VI_Control_Misc1, mybool ? 0x01 : 0x00, 0x01);
    if(mybool) pOverlay->keyOP = VI_ROP_Always;
}

static void
set_overlay(SISUSBPtr pSiSUSB, SISUSBOverlayPtr pOverlay, SISUSBPortPrivPtr pPriv, int index, int iscrt2)
{
    CARD8  h_over=0, v_over=0;
    CARD16 top, bottom, left, right, pitch=0;
    CARD16 screenX, screenY;
    CARD32 PSY;
    int    modeflags;

    screenX = pOverlay->currentmode->HDisplay;
    screenY = pOverlay->currentmode->VDisplay;
    modeflags = pOverlay->currentmode->Flags;
    top = pOverlay->dstBox.y1;
    bottom = pOverlay->dstBox.y2;
    left = pOverlay->dstBox.x1;
    right = pOverlay->dstBox.x2;
    pitch = pOverlay->pitch >> pPriv->shiftValue;

    if(bottom > screenY) {
        bottom = screenY;
    }
    if(right > screenX) {
        right = screenX;
    }

    /* DoubleScan modes require Y coordinates * 2 */
    if(modeflags & V_DBLSCAN) {
    	 top <<= 1;
	 bottom <<= 1;
    }
    /* Interlace modes require Y coordinates / 2 */
    if(modeflags & V_INTERLACE) {
    	 top >>= 1;
	 bottom >>= 1;
    }

    h_over = (((left>>8) & 0x0f) | ((right>>4) & 0xf0));
    v_over = (((top>>8) & 0x0f) | ((bottom>>4) & 0xf0));

    /* set line buffer size */

    setvideoreg(pSiSUSB, Index_VI_Line_Buffer_Size, (CARD8)pOverlay->lineBufSize);

    /* set color key mode */
    setvideoregmask(pSiSUSB, Index_VI_Key_Overlay_OP, pOverlay->keyOP, 0x0f);

    /* Unlock address registers */
    setvideoregmask(pSiSUSB, Index_VI_Control_Misc1, 0x20, 0x20);

    /* set destination window position */
    setvideoreg(pSiSUSB, Index_VI_Win_Hor_Disp_Start_Low, (CARD8)left);
    setvideoreg(pSiSUSB, Index_VI_Win_Hor_Disp_End_Low,   (CARD8)right);
    setvideoreg(pSiSUSB, Index_VI_Win_Hor_Over,           (CARD8)h_over);

    setvideoreg(pSiSUSB, Index_VI_Win_Ver_Disp_Start_Low, (CARD8)top);
    setvideoreg(pSiSUSB, Index_VI_Win_Ver_Disp_End_Low,   (CARD8)bottom);
    setvideoreg(pSiSUSB, Index_VI_Win_Ver_Over,           (CARD8)v_over);

    /* Set Y buf pitch */
    setvideoreg(pSiSUSB, Index_VI_Disp_Y_Buf_Pitch_Low, (CARD8)(pitch));
    setvideoregmask(pSiSUSB, Index_VI_Disp_Y_UV_Buf_Pitch_Middle, (CARD8)(pitch >> 8), 0x0f);

    /* Set Y start address */
    PSY = pOverlay->PSY;

    setvideoreg(pSiSUSB, Index_VI_Disp_Y_Buf_Start_Low,    (CARD8)(PSY));
    setvideoreg(pSiSUSB, Index_VI_Disp_Y_Buf_Start_Middle, (CARD8)(PSY >> 8));
    setvideoreg(pSiSUSB, Index_VI_Disp_Y_Buf_Start_High,   (CARD8)(PSY >> 16));

    /* overflow bits for Y plane */
    setvideoreg(pSiSUSB, Index_VI_Disp_Y_Buf_Pitch_High, (CARD8)(pitch >> 12));
    setvideoreg(pSiSUSB, Index_VI_Y_Buf_Start_Over, ((CARD8)(PSY >> 24) & 0x03));

    /* Set U/V data if using planar formats */
    if(pOverlay->planar) {

        CARD32  PSU = pOverlay->PSU;
	CARD32  PSV = pOverlay->PSV;

        if(pOverlay->planar_shiftpitch) pitch >>= 1;

	/* Set U/V pitch */
	setvideoreg(pSiSUSB, Index_VI_Disp_UV_Buf_Pitch_Low, (CARD8)pitch);
        setvideoregmask(pSiSUSB, Index_VI_Disp_Y_UV_Buf_Pitch_Middle, (CARD8)(pitch >> 4), 0xf0);

        /* set U/V start address */
        setvideoreg(pSiSUSB, Index_VI_U_Buf_Start_Low,   (CARD8)PSU);
        setvideoreg(pSiSUSB, Index_VI_U_Buf_Start_Middle,(CARD8)(PSU >> 8));
        setvideoreg(pSiSUSB, Index_VI_U_Buf_Start_High,  (CARD8)(PSU >> 16));

        setvideoreg(pSiSUSB, Index_VI_V_Buf_Start_Low,   (CARD8)PSV);
        setvideoreg(pSiSUSB, Index_VI_V_Buf_Start_Middle,(CARD8)(PSV >> 8));
        setvideoreg(pSiSUSB, Index_VI_V_Buf_Start_High,  (CARD8)(PSV >> 16));

	/* overflow bits */
	setvideoreg(pSiSUSB, Index_VI_Disp_UV_Buf_Pitch_High, (CARD8)(pitch >> 12));
	setvideoreg(pSiSUSB, Index_VI_U_Buf_Start_Over, ((CARD8)(PSU >> 24) & 0x03));
	setvideoreg(pSiSUSB, Index_VI_V_Buf_Start_Over, ((CARD8)(PSV >> 24) & 0x03));


    }

    setvideoregmask(pSiSUSB, Index_VI_Control_Misc1, pOverlay->bobEnable, 0x1a);

    /* Lock the address registers */
    setvideoregmask(pSiSUSB, Index_VI_Control_Misc1, 0x00, 0x20);

    /* set scale factor */
    setvideoreg(pSiSUSB, Index_VI_Hor_Post_Up_Scale_Low, (CARD8)(pOverlay->HUSF));
    setvideoreg(pSiSUSB, Index_VI_Hor_Post_Up_Scale_High,(CARD8)((pOverlay->HUSF) >> 8));
    setvideoreg(pSiSUSB, Index_VI_Ver_Up_Scale_Low,      (CARD8)(pOverlay->VUSF));
    setvideoreg(pSiSUSB, Index_VI_Ver_Up_Scale_High,     (CARD8)((pOverlay->VUSF) >> 8));

    setvideoregmask(pSiSUSB, Index_VI_Scale_Control,     (pOverlay->IntBit << 3) |
                                                      (pOverlay->wHPre), 0x7f);

}
#endif /* SIS_ENABLEXV */

/* Overlay MUST NOT be switched off while beam is over it */
#ifdef SIS_ENABLEXV
static void
close_overlay(SISUSBPtr pSiSUSB, SISUSBPortPrivPtr pPriv)
{
#if 0
  int watchdog;
#endif

  if(!(pPriv->overlayStatus)) return;
  pPriv->overlayStatus = FALSE;

  setvideoregmask(pSiSUSB, Index_VI_Control_Misc2, 0x00, 0x05);
  setvideoregmask(pSiSUSB, Index_VI_Control_Misc1, 0x00, 0x01);

#if 0
  watchdog = WATCHDOG_DELAY;
  while((!vblank_active_CRT1(pSiSUSB, pPriv)) && --watchdog);
  watchdog = WATCHDOG_DELAY;
  while(vblank_active_CRT1(pSiSUSB, pPriv) && --watchdog);
#endif
  setvideoregmask(pSiSUSB, Index_VI_Control_Misc0, 0x00, 0x02);
#if 0
  watchdog = WATCHDOG_DELAY;
  while((!vblank_active_CRT1(pSiSUSB, pPriv)) && --watchdog);
  watchdog = WATCHDOG_DELAY;
  while(vblank_active_CRT1(pSiSUSB, pPriv) && --watchdog);
#endif

}
#endif

#ifdef SIS_ENABLEXV
static void
SISUSBDisplayVideo(ScrnInfoPtr pScrn, SISUSBPortPrivPtr pPriv)
{
   SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);
   short  srcPitch = pPriv->srcPitch;
   short  height = pPriv->height;
   UShort screenwidth;
   SISUSBOverlayRec overlay;
   int    srcOffsetX = 0, srcOffsetY = 0;
   int    sx = 0, sy = 0;
   int    index = 0, iscrt2 = 0;

   pPriv->NoOverlay = FALSE;

   /* setup dispmode (MIRROR, SINGLEx) */
   set_dispmode(pScrn, pPriv);

   /* Check if overlay is supported with current mode */
   if(!(pSiSUSB->MiscFlags & MISC_CRT1OVERLAY)) {
      if(pPriv->overlayStatus) {
	 close_overlay(pSiSUSB, pPriv);
      }
      pPriv->NoOverlay = TRUE;
      return;
   }

   memset(&overlay, 0, sizeof(overlay));

   overlay.pixelFormat = pPriv->id;
   overlay.pitch = overlay.origPitch = srcPitch;
   if(pPriv->usechromakey) {
      overlay.keyOP = (pPriv->insidechromakey) ? VI_ROP_ChromaKey : VI_ROP_NotChromaKey;
   } else {
      overlay.keyOP = VI_ROP_DestKey;
   }

   overlay.bobEnable = 0x00;    /* Disable BOB de-interlacer */

   overlay.currentmode = pSiSUSB->CurrentLayout.mode;
   overlay.SCREENheight = overlay.currentmode->VDisplay;
   screenwidth = overlay.currentmode->HDisplay;
   overlay.dstBox.x1 = pPriv->drw_x - pScrn->frameX0;
   overlay.dstBox.x2 = pPriv->drw_x + pPriv->drw_w - pScrn->frameX0;
   overlay.dstBox.y1 = pPriv->drw_y - pScrn->frameY0;
   overlay.dstBox.y2 = pPriv->drw_y + pPriv->drw_h - pScrn->frameY0;

   /* Note: x2/y2 is actually real coordinate + 1 */

   if((overlay.dstBox.x1 >= overlay.dstBox.x2) ||
      (overlay.dstBox.y1 >= overlay.dstBox.y2)) {
           return;
   }

   if((overlay.dstBox.x2 <= 0) || (overlay.dstBox.y2 <= 0)) {
           return;
   }

   if((overlay.dstBox.x1 >= screenwidth) || (overlay.dstBox.y1 >= overlay.SCREENheight)) {
           return;
   }

   if(overlay.dstBox.x1 < 0) {
      srcOffsetX = pPriv->src_w * (-overlay.dstBox.x1) / pPriv->drw_w;
      overlay.dstBox.x1 = 0;
   }
   if(overlay.dstBox.y1 < 0) {
      srcOffsetY = pPriv->src_h * (-overlay.dstBox.y1) / pPriv->drw_h;
      overlay.dstBox.y1 = 0;
   }

   if((overlay.dstBox.x1 >= overlay.dstBox.x2 - 2) ||
      (overlay.dstBox.x1 >= screenwidth - 2)       ||
      (overlay.dstBox.y1 >= overlay.dstBox.y2)) {
           return;
   }

   switch(pPriv->id) {

     case PIXEL_FMT_YV12:
       overlay.planar = 1;
       overlay.planar_shiftpitch = 1;
       sx = (pPriv->src_x + srcOffsetX) & ~7;
       sy = (pPriv->src_y + srcOffsetY) & ~1;
       overlay.PSY = pPriv->bufAddr[pPriv->currentBuf] + sx + sy*srcPitch;
       overlay.PSV = pPriv->bufAddr[pPriv->currentBuf] + height*srcPitch + ((sx + sy*srcPitch/2) >> 1);
       overlay.PSU = pPriv->bufAddr[pPriv->currentBuf] + height*srcPitch*5/4 + ((sx + sy*srcPitch/2) >> 1);
       overlay.PSY >>= pPriv->shiftValue;
       overlay.PSV >>= pPriv->shiftValue;
       overlay.PSU >>= pPriv->shiftValue;
       break;

     case PIXEL_FMT_I420:
       overlay.planar = 1;
       overlay.planar_shiftpitch = 1;
       sx = (pPriv->src_x + srcOffsetX) & ~7;
       sy = (pPriv->src_y + srcOffsetY) & ~1;
       overlay.PSY = pPriv->bufAddr[pPriv->currentBuf] + sx + sy*srcPitch;
       overlay.PSV = pPriv->bufAddr[pPriv->currentBuf] + height*srcPitch*5/4 + ((sx + sy*srcPitch/2) >> 1);
       overlay.PSU = pPriv->bufAddr[pPriv->currentBuf] + height*srcPitch + ((sx + sy*srcPitch/2) >> 1);
       overlay.PSY >>= pPriv->shiftValue;
       overlay.PSV >>= pPriv->shiftValue;
       overlay.PSU >>= pPriv->shiftValue;
       break;

     case PIXEL_FMT_NV12:
     case PIXEL_FMT_NV21:
       overlay.planar = 1;
       overlay.planar_shiftpitch = 0;
       sx = (pPriv->src_x + srcOffsetX) & ~7;
       sy = (pPriv->src_y + srcOffsetY) & ~1;
       overlay.PSY = pPriv->bufAddr[pPriv->currentBuf] + sx + sy*srcPitch;
       overlay.PSV =	pPriv->bufAddr[pPriv->currentBuf] + height*srcPitch + ((sx + sy*srcPitch/2) >> 1);
       overlay.PSY >>= pPriv->shiftValue;
       overlay.PSV >>= pPriv->shiftValue;
       overlay.PSU = overlay.PSV;
       break;

     case PIXEL_FMT_YUY2:
     case PIXEL_FMT_UYVY:
     case PIXEL_FMT_YVYU:
     case PIXEL_FMT_RGB6:
     case PIXEL_FMT_RGB5:
     default:
       overlay.planar = 0;
       sx = (pPriv->src_x + srcOffsetX) & ~1;
       sy = (pPriv->src_y + srcOffsetY);
       overlay.PSY = (pPriv->bufAddr[pPriv->currentBuf] + sx*2 + sy*srcPitch);
       overlay.PSY >>= pPriv->shiftValue;
       break;
   }

   /* Some clipping checks */
   overlay.srcW = pPriv->src_w - (sx - pPriv->src_x);
   overlay.srcH = pPriv->src_h - (sy - pPriv->src_y);
   if( (pPriv->oldx1 != overlay.dstBox.x1) ||
       (pPriv->oldx2 != overlay.dstBox.x2) ||
       (pPriv->oldy1 != overlay.dstBox.y1) ||
       (pPriv->oldy2 != overlay.dstBox.y2) ) {
      pPriv->mustwait = 1;
      pPriv->oldx1 = overlay.dstBox.x1; pPriv->oldx2 = overlay.dstBox.x2;
      pPriv->oldy1 = overlay.dstBox.y1; pPriv->oldy2 = overlay.dstBox.y2;
   }

   /* Loop head */
   /* Note: index can only be 1 for CRT2, ie overlay 1
    * is only used for CRT2.
    */

   index = 0; iscrt2 = 0;
   overlay.VBlankActiveFunc = vblank_active_CRT1;

   /* set display mode SR06,32 (CRT1, CRT2 or mirror) */
   set_disptype_regs(pScrn, pPriv);

   /* set (not only calc) merge line buffer */
   merge_line_buf(pSiSUSB, pPriv, (overlay.srcW > pPriv->linebufMergeLimit), overlay.srcW,
      		     pPriv->linebufMergeLimit);

   /* calculate (not set!) line buffer length */
   calc_line_buf_size_1(&overlay, pPriv);

   setvideoregmask(pSiSUSB, Index_VI_Control_Misc3, 0x03, 0x03);

   /* calculate scale factor */
   calc_scale_factor(&overlay, pScrn, pPriv, index, iscrt2);

   /* Select overlay 0 (used for CRT1/or CRT2) or overlay 1 (used for CRT2 only) */
   setvideoregmask(pSiSUSB, Index_VI_Control_Misc2, index, 0x01);

   /* set format (before color and chroma keys) */
   set_format(pSiSUSB, &overlay);

   /* set color key */
   set_colorkey(pSiSUSB, pPriv->colorKey);

   if(pPriv->usechromakey) {
      set_chromakey(pSiSUSB, pPriv->chromamin, pPriv->chromamax);
   }

   /* set brightness, contrast, hue, saturation */
   set_brightness(pSiSUSB, pPriv->brightness);
   set_contrast(pSiSUSB, pPriv->contrast);
   set_hue(pSiSUSB, pPriv->hue);
   set_saturation(pSiSUSB, pPriv->saturation);

   /* enable/disable graphics display around overlay
    * (Since disabled overlays don't get treated in this
    * loop, we omit respective checks here)
    */
   if(!iscrt2) set_disablegfx(pSiSUSB, pPriv->disablegfx, &overlay);
   else if(!pPriv->hasTwoOverlays) {
     set_disablegfx(pSiSUSB, FALSE, &overlay);
   }
   set_disablegfxlr(pSiSUSB, pPriv->disablegfxlr, &overlay);

   /* set remaining overlay parameters */
   set_overlay(pSiSUSB, &overlay, pPriv, index, iscrt2);

   /* enable overlay */
   setvideoregmask (pSiSUSB, Index_VI_Control_Misc0, 0x02, 0x02);

   /* Trigger register copy */
   setvideoregmask(pSiSUSB, Index_VI_Control_Misc3, 0x03, 0x03);

   pPriv->mustwait = 0;
   pPriv->overlayStatus = TRUE;
}
#endif

#ifdef SIS_ENABLEXV
FBLinearPtr
SISUSBAllocateOverlayMemory(
  ScrnInfoPtr pScrn,
  FBLinearPtr linear,
  int size
){
   ScreenPtr pScreen;
   FBLinearPtr new_linear;

   if(linear) {
      if(linear->size >= size) return linear;

      if(xf86ResizeOffscreenLinear(linear, size)) return linear;

      xf86FreeOffscreenLinear(linear);
   }

   pScreen = screenInfo.screens[pScrn->scrnIndex];

   new_linear = xf86AllocateOffscreenLinear(pScreen, size, 8,
                                            NULL, NULL, NULL);

   if(!new_linear) {
      int max_size;

      xf86QueryLargestOffscreenLinear(pScreen, &max_size, 8,
				       PRIORITY_EXTREME);

      if(max_size < size) return NULL;

      xf86PurgeUnlockedOffscreenAreas(pScreen);
      new_linear = xf86AllocateOffscreenLinear(pScreen, size, 8,
                                                 NULL, NULL, NULL);
   }
   if(!new_linear)
      xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	           "Xv: Failed to allocate %d pixels of linear video memory\n", size);
#ifdef TWDEBUG
   else
      xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	           "Xv: Allocated %d pixels of linear video memory\n", size);
#endif

   return new_linear;
}

static void
SISUSBFreeOverlayMemory(ScrnInfoPtr pScrn)
{
    SISUSBPortPrivPtr pPriv = GET_PORT_PRIVATE(pScrn);

    if(pPriv->linear) {
       xf86FreeOffscreenLinear(pPriv->linear);
       pPriv->linear = NULL;
    }
}
#endif

static void
SISUSBStopVideo(ScrnInfoPtr pScrn, pointer data, Bool shutdown)
{
#ifdef SIS_ENABLEXV
  SISUSBPortPrivPtr pPriv = (SISUSBPortPrivPtr)data;
  SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);

  if(pPriv->grabbedByV4L) return;

  REGION_EMPTY(pScrn->pScreen, &pPriv->clip);

  if(shutdown) {
     if(pPriv->videoStatus & CLIENT_VIDEO_ON) {
        close_overlay(pSiSUSB, pPriv);
        pPriv->mustwait = 1;
     }
     SISUSBFreeOverlayMemory(pScrn);
     pPriv->videoStatus = 0;
  } else {
     if(pPriv->videoStatus & CLIENT_VIDEO_ON) {
        UpdateCurrentTime();
        pPriv->offTime = currentTime.milliseconds + OFF_DELAY;
        pPriv->videoStatus = OFF_TIMER | CLIENT_VIDEO_ON;
        pSiSUSB->VideoTimerCallback = SISUSBVideoTimerCallback;
     }
  }
#endif
}

#ifdef SIS_ENABLEXV
static int
SISUSBPutImage(
  ScrnInfoPtr pScrn,
  short src_x, short src_y,
  short drw_x, short drw_y,
  short src_w, short src_h,
  short drw_w, short drw_h,
  int id, UChar *buf,
  short width, short height,
  Bool sync,
  RegionPtr clipBoxes, pointer data
){
   SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);
   SISUSBPortPrivPtr pPriv = (SISUSBPortPrivPtr)data;
   int totalSize = 0, depth = pSiSUSB->CurrentLayout.bitsPerPixel >> 3;

   if(pPriv->grabbedByV4L) return Success;

   pPriv->drw_x = drw_x;
   pPriv->drw_y = drw_y;
   pPriv->drw_w = drw_w;
   pPriv->drw_h = drw_h;
   pPriv->src_x = src_x;
   pPriv->src_y = src_y;
   pPriv->src_w = src_w;
   pPriv->src_h = src_h;
   pPriv->id = id;
   pPriv->height = height;

   /* Pixel formats:
      1. YU12:  3 planes:       H    V
               Y sample period  1    1   (8 bit per pixel)
	       V sample period  2    2	 (8 bit per pixel, subsampled)
	       U sample period  2    2   (8 bit per pixel, subsampled)

 	 Y plane is fully sampled (width*height), U and V planes
	 are sampled in 2x2 blocks, hence a group of 4 pixels requires
	 4 + 1 + 1 = 6 bytes. The data is planar, ie in single planes
	 for Y, U and V.
      2. UYVY: 3 planes:        H    V
               Y sample period  1    1   (8 bit per pixel)
	       V sample period  2    1	 (8 bit per pixel, subsampled)
	       U sample period  2    1   (8 bit per pixel, subsampled)
	 Y plane is fully sampled (width*height), U and V planes
	 are sampled in 2x1 blocks, hence a group of 4 pixels requires
	 4 + 2 + 2 = 8 bytes. The data is bit packed, there are no separate
	 Y, U or V planes.
	 Bit order:  U0 Y0 V0 Y1  U2 Y2 V2 Y3 ...
      3. I420: Like YU12, but planes U and V are in reverse order.
      4. YUY2: Like UYVY, but order is
                     Y0 U0 Y1 V0  Y2 U2 Y3 V2 ...
      5. YVYU: Like YUY2, but order is
      		     Y0 V0 Y1 U0  Y2 V2 Y3 U2 ...
      6. NV12, NV21: 2 planes   H    V
               Y sample period  1    1   (8 bit per pixel)
	       V sample period  2    1	 (8 bit per pixel, subsampled)
	       U sample period  2    1   (8 bit per pixel, subsampled)
	 Y plane is fully samples (width*height), U and V planes are
	 interleaved in memory (one byte U, one byte V for NV12, NV21
	 other way round) and sampled in 2x1 blocks. Otherwise such
	 as all other planar formats.
   */

   switch(id){
     case PIXEL_FMT_YV12:
     case PIXEL_FMT_I420:
     case PIXEL_FMT_NV12:
     case PIXEL_FMT_NV21:
       pPriv->srcPitch = (width + 7) & ~7;
       /* Size = width * height * 3 / 2 */
       totalSize = (pPriv->srcPitch * height * 3) >> 1; /* Verified */
       break;
     case PIXEL_FMT_YUY2:
     case PIXEL_FMT_UYVY:
     case PIXEL_FMT_YVYU:
     case PIXEL_FMT_RGB6:
     case PIXEL_FMT_RGB5:
     default:
       pPriv->srcPitch = ((width << 1) + 3) & ~3;	/* Verified */
       /* Size = width * 2 * height */
       totalSize = pPriv->srcPitch * height;
   }

   /* make it a multiple of 16 to simplify to copy loop */
   totalSize += 15;
   totalSize &= ~15; /* in bytes */

   /* allocate memory (we do doublebuffering) - size is in pixels! */
   if(!(pPriv->linear = SISUSBAllocateOverlayMemory(pScrn, pPriv->linear,
					((totalSize + depth - 1) / depth) << 1)))
      return BadAlloc;

   /* fixup pointers */
   pPriv->bufAddr[0] = (pPriv->linear->offset * depth);

   pPriv->bufAddr[1] = pPriv->bufAddr[0] + totalSize;

   /* copy data */
   SiSUSBMemCopyToVideoRam(pSiSUSB, pSiSUSB->FbBase + pPriv->bufAddr[pPriv->currentBuf], buf, totalSize);

   SISUSBDisplayVideo(pScrn, pPriv);

   /* update cliplist */
   if(pPriv->autopaintColorKey &&
      (pPriv->grabbedByV4L ||
#if XF86_VERSION_CURRENT < XF86_VERSION_NUMERIC(4,3,99,3,0)
       (!RegionsEqual(&pPriv->clip, clipBoxes)) ||
#else
       (!REGION_EQUAL(pScrn->pScreen, &pPriv->clip, clipBoxes)) ||
#endif
       (pPriv->PrevOverlay != pPriv->NoOverlay))) {
      /* We always paint the colorkey for V4L */
      if(!pPriv->grabbedByV4L) {
      	 REGION_COPY(pScrn->pScreen, &pPriv->clip, clipBoxes);
      }
      /* draw these */
      pPriv->PrevOverlay = pPriv->NoOverlay;

      if(!pSiSUSB->disablecolorkeycurrent) {
	 xf86XVFillKeyHelper(pScrn->pScreen, pPriv->colorKey, clipBoxes);
      }

   }

   pPriv->currentBuf ^= 1;

   pPriv->videoStatus = CLIENT_VIDEO_ON;

   pSiSUSB->VideoTimerCallback = SISUSBVideoTimerCallback;

   return Success;
}
#endif

static int
SISUSBQueryImageAttributes(
  ScrnInfoPtr pScrn,
  int id,
  UShort *w, UShort *h,
  int *pitches, int *offsets
){
    int    pitchY, pitchUV;
    int    size, sizeY, sizeUV;

    if(*w < IMAGE_MIN_WIDTH) *w = IMAGE_MIN_WIDTH;
    if(*h < IMAGE_MIN_HEIGHT) *h = IMAGE_MIN_HEIGHT;

    if(*w > DummyEncoding.width) *w = DummyEncoding.width;
    if(*h > DummyEncoding.height) *h = DummyEncoding.height;

    switch(id) {
    case PIXEL_FMT_YV12:
    case PIXEL_FMT_I420:
        *w = (*w + 7) & ~7;
        *h = (*h + 1) & ~1;
        pitchY = *w;
    	pitchUV = *w >> 1;
    	if(pitches) {
      	    pitches[0] = pitchY;
            pitches[1] = pitches[2] = pitchUV;
        }
    	sizeY = pitchY * (*h);
    	sizeUV = pitchUV * ((*h) >> 1);
    	if(offsets) {
          offsets[0] = 0;
          offsets[1] = sizeY;
          offsets[2] = sizeY + sizeUV;
        }
        size = sizeY + (sizeUV << 1);
    	break;
    case PIXEL_FMT_NV12:
    case PIXEL_FMT_NV21:
        *w = (*w + 7) & ~7;
        *h = (*h + 1) & ~1;
	pitchY = *w;
    	pitchUV = *w;
    	if(pitches) {
      	    pitches[0] = pitchY;
            pitches[1] = pitchUV;
        }
    	sizeY = pitchY * (*h);
    	sizeUV = pitchUV * ((*h) >> 1);
    	if(offsets) {
          offsets[0] = 0;
          offsets[1] = sizeY;
        }
        size = sizeY + (sizeUV << 1);
        break;
    case PIXEL_FMT_YUY2:
    case PIXEL_FMT_UYVY:
    case PIXEL_FMT_YVYU:
    case PIXEL_FMT_RGB6:
    case PIXEL_FMT_RGB5:
    default:
        *w = (*w + 1) & ~1;
        pitchY = *w << 1;
    	if(pitches) pitches[0] = pitchY;
    	if(offsets) offsets[0] = 0;
    	size = pitchY * (*h);
    	break;
    }

    return size;
}

/*****************************************************************/
/*                     OFFSCREEN SURFACES                        */
/*****************************************************************/

#ifdef SIS_ENABLEXV
static int
SISUSBAllocSurface(
    ScrnInfoPtr pScrn,
    int id,
    UShort w,
    UShort h,
    XF86SurfacePtr surface
)
{
    SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);
    SISUSBPortPrivPtr pPriv = GET_PORT_PRIVATE(pScrn);
    int size, depth;

#ifdef TWDEBUG
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Xv: SISUSBAllocSurface called\n");
#endif

    if((w < IMAGE_MIN_WIDTH) || (h < IMAGE_MIN_HEIGHT))
          return BadValue;
    if((w > DummyEncoding.width) || (h > DummyEncoding.height))
    	  return BadValue;

    if(pPriv->grabbedByV4L)
    	return BadAlloc;

    depth = pSiSUSB->CurrentLayout.bitsPerPixel >> 3;
    w = (w + 1) & ~1;
    pPriv->pitch = ((w << 1) + 63) & ~63; /* Only packed pixel modes supported */
    size = h * pPriv->pitch;
    pPriv->linear = SISUSBAllocateOverlayMemory(pScrn, pPriv->linear, ((size + depth - 1) / depth));

    if(!pPriv->linear)
    	return BadAlloc;

    pPriv->offset    = pPriv->linear->offset * depth;

    surface->width   = w;
    surface->height  = h;
    surface->pScrn   = pScrn;
    surface->id      = id;
    surface->pitches = &pPriv->pitch;
    surface->offsets = &pPriv->offset;
    surface->devPrivate.ptr = (pointer)pPriv;

    close_overlay(pSiSUSB, pPriv);
    pPriv->videoStatus = 0;
    REGION_EMPTY(pScrn->pScreen, &pPriv->clip);
    pSiSUSB->VideoTimerCallback = NULL;
    pPriv->grabbedByV4L = TRUE;
    return Success;
}

static int
SISUSBStopSurface(XF86SurfacePtr surface)
{
    SISUSBPortPrivPtr pPriv = (SISUSBPortPrivPtr)(surface->devPrivate.ptr);
    SISUSBPtr pSiSUSB = SISUSBPTR(surface->pScrn);

    if(pPriv->grabbedByV4L && pPriv->videoStatus) {
       close_overlay(pSiSUSB, pPriv);
       pPriv->mustwait = 1;
       pPriv->videoStatus = 0;
    }
    return Success;
}

static int
SISUSBFreeSurface(XF86SurfacePtr surface)
{
    SISUSBPortPrivPtr pPriv = (SISUSBPortPrivPtr)(surface->devPrivate.ptr);

    if(pPriv->grabbedByV4L) {
       SISUSBStopSurface(surface);
       SISUSBFreeOverlayMemory(surface->pScrn);
       pPriv->grabbedByV4L = FALSE;
    }
    return Success;
}

static int
SISUSBGetSurfaceAttribute(
    ScrnInfoPtr pScrn,
    Atom attribute,
    INT32 *value
)
{
    SISUSBPortPrivPtr pPriv = GET_PORT_PRIVATE(pScrn);

    return SISUSBGetPortAttribute(pScrn, attribute, value, (pointer)pPriv);
}

static int
SISUSBSetSurfaceAttribute(
    ScrnInfoPtr pScrn,
    Atom attribute,
    INT32 value
)
{
    SISUSBPortPrivPtr pPriv = GET_PORT_PRIVATE(pScrn);;

    return SISUSBSetPortAttribute(pScrn, attribute, value, (pointer)pPriv);
}

static int
SISUSBDisplaySurface(
    XF86SurfacePtr surface,
    short src_x, short src_y,
    short drw_x, short drw_y,
    short src_w, short src_h,
    short drw_w, short drw_h,
    RegionPtr clipBoxes
)
{
   ScrnInfoPtr pScrn = surface->pScrn;
   SISUSBPortPrivPtr pPriv = (SISUSBPortPrivPtr)(surface->devPrivate.ptr);

#ifdef TWDEBUG
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Xv: DisplaySurface called\n");
#endif

   if(!pPriv->grabbedByV4L) return Success;

   pPriv->drw_x = drw_x;
   pPriv->drw_y = drw_y;
   pPriv->drw_w = drw_w;
   pPriv->drw_h = drw_h;
   pPriv->src_x = src_x;
   pPriv->src_y = src_y;
   pPriv->src_w = src_w;
   pPriv->src_h = src_h;
   pPriv->id = surface->id;
   pPriv->height = surface->height;
   pPriv->bufAddr[0] = surface->offsets[0];
   pPriv->currentBuf = 0;
   pPriv->srcPitch = surface->pitches[0];

   SISUSBDisplayVideo(pScrn, pPriv);

   if(pPriv->autopaintColorKey) {
      xf86XVFillKeyHelper(pScrn->pScreen, pPriv->colorKey, clipBoxes);
   }

   pPriv->videoStatus = CLIENT_VIDEO_ON;

   return Success;
}

#define NUMOFFSCRIMAGES_300 4
#define NUMOFFSCRIMAGES_315 5

static XF86OffscreenImageRec SISUSBOffscreenImages[NUMOFFSCRIMAGES_315] =
{
 {
   &SISUSBImages[0],  	/* YUV2 */
   VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT,
   SISUSBAllocSurface,
   SISUSBFreeSurface,
   SISUSBDisplaySurface,
   SISUSBStopSurface,
   SISUSBGetSurfaceAttribute,
   SISUSBSetSurfaceAttribute,
   0, 0,  			/* Rest will be filled in */
   0,
   NULL
 },
 {
   &SISUSBImages[2],	/* UYVY */
   VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT,
   SISUSBAllocSurface,
   SISUSBFreeSurface,
   SISUSBDisplaySurface,
   SISUSBStopSurface,
   SISUSBGetSurfaceAttribute,
   SISUSBSetSurfaceAttribute,
   0, 0,  			/* Rest will be filled in */
   0,
   NULL
 }
 ,
 {
   &SISUSBImages[4],	/* RV15 */
   VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT,
   SISUSBAllocSurface,
   SISUSBFreeSurface,
   SISUSBDisplaySurface,
   SISUSBStopSurface,
   SISUSBGetSurfaceAttribute,
   SISUSBSetSurfaceAttribute,
   0, 0,  			/* Rest will be filled in */
   0,
   NULL
 },
 {
   &SISUSBImages[5],	/* RV16 */
   VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT,
   SISUSBAllocSurface,
   SISUSBFreeSurface,
   SISUSBDisplaySurface,
   SISUSBStopSurface,
   SISUSBGetSurfaceAttribute,
   SISUSBSetSurfaceAttribute,
   0, 0,  			/* Rest will be filled in */
   0,
   NULL
 },
 {
   &SISUSBImages[6],	/* YVYU */
   VIDEO_OVERLAID_IMAGES | VIDEO_CLIP_TO_VIEWPORT,
   SISUSBAllocSurface,
   SISUSBFreeSurface,
   SISUSBDisplaySurface,
   SISUSBStopSurface,
   SISUSBGetSurfaceAttribute,
   SISUSBSetSurfaceAttribute,
   0, 0,  			/* Rest will be filled in */
   0,
   NULL
 }
};

static void
SISUSBInitOffscreenImages(ScreenPtr pScreen)
{
    int i, num;

    num = NUMOFFSCRIMAGES_315;

    for(i = 0; i < num; i++) {
       SISUSBOffscreenImages[i].max_width  = DummyEncoding.width;
       SISUSBOffscreenImages[i].max_height = DummyEncoding.height;
       SISUSBOffscreenImages[i].num_attributes = NUM_ATTRIBUTES_315;
       SISUSBOffscreenImages[i].attributes = &SISUSBAttributes_315[0];
    }
    xf86XVRegisterOffscreenImages(pScreen, SISUSBOffscreenImages, num);
}
#endif

/*****************************************/
/*            TIMER CALLBACK             */
/*****************************************/

#ifdef SIS_ENABLEXV
static void
SISUSBVideoTimerCallback(ScrnInfoPtr pScrn, Time now)
{
    SISUSBPtr          pSiSUSB = SISUSBPTR(pScrn);
    SISUSBPortPrivPtr  pPriv = NULL;
    UChar              sridx, cridx;
    Bool	       setcallback = FALSE;

    if(!pScrn->vtSema) return;

    if(pSiSUSB->adaptor) {
       pPriv = GET_PORT_PRIVATE(pScrn);
       if(!pPriv->videoStatus) pPriv = NULL;
    }

    if(pPriv) {
       if(pPriv->videoStatus & TIMER_MASK) {
          if(pPriv->videoStatus & OFF_TIMER) {
	     setcallback = TRUE;
	     if(pPriv->offTime < now) {
                /* Turn off the overlay */
	        sridx = inSISREG(pSiSUSB, SISSR); cridx = inSISREG(pSiSUSB, SISCR);
                close_overlay(pSiSUSB, pPriv);
	        outSISREG(pSiSUSB, SISSR, sridx); outSISREG(pSiSUSB, SISCR, cridx);
	        pPriv->mustwait = 1;
                pPriv->videoStatus = FREE_TIMER;
                pPriv->freeTime = now + FREE_DELAY;
	     }
          } else if(pPriv->videoStatus & FREE_TIMER) {
	     if(pPriv->freeTime < now) {
                SISUSBFreeOverlayMemory(pScrn);
	        pPriv->mustwait = 1;
                pPriv->videoStatus = 0;
             } else {
	        setcallback = TRUE;
	     }
          }
       }
    }

    pSiSUSB->VideoTimerCallback = (setcallback) ? SISUSBVideoTimerCallback : NULL;
}
#endif
