/* $XFree86$ */
/* $XdotOrg$ */
/*
 * 2D Acceleration for SiS 315/USB - not functional!
 *
 * Copyright (C) 2001-2005 by Thomas Winischhofer, Vienna, Austria
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
 *
 * Author:  	Thomas Winischhofer <thomas@winischhofer.net>
 *
 * 2003/08/18: Rewritten for using VRAM command queue
 *
 */

#include "sisusb.h"
#include "sisusb_regs.h"
#include "xaarop.h"
#include "sisusb_accel.h"

/* Accelerator functions */
static void SiSUSBInitializeAccelerator(ScrnInfoPtr pScrn);
#if 0
void  	    SiSUSBSync(ScrnInfoPtr pScrn);
#endif
#if 0
static void SiSUSBSetupForScreenToScreenCopy(ScrnInfoPtr pScrn,
                                int xdir, int ydir, int rop,
                                unsigned int planemask, int trans_color);
static void SiSUSBSubsequentScreenToScreenCopy(ScrnInfoPtr pScrn,
                                int x1, int y1, int x2, int y2,
                                int width, int height);
#endif			
#if 0	
static void SiSUSBSetupForSolidFill(ScrnInfoPtr pScrn, int color,
                                int rop, unsigned int planemask);
static void SiSUSBSubsequentSolidFillRect(ScrnInfoPtr pScrn,
                                int x, int y, int w, int h);
#endif				
#if 0
static void SiSUSBSetupForSolidLine(ScrnInfoPtr pScrn, int color,
                                int rop, unsigned int planemask);
static void SiSUSBSubsequentSolidTwoPointLine(ScrnInfoPtr pScrn, int x1,
                                int y1, int x2, int y2, int flags);
static void SiSUSBSubsequentSolidHorzVertLine(ScrnInfoPtr pScrn,
                                int x, int y, int len, int dir);
static void SiSUSBSetupForDashedLine(ScrnInfoPtr pScrn,
                                int fg, int bg, int rop, unsigned int planemask,
                                int length, unsigned char *pattern);
static void SiSUSBSubsequentDashedTwoPointLine(ScrnInfoPtr pScrn,
                                int x1, int y1, int x2, int y2,
                                int flags, int phase);
static void SiSUSBSetupForMonoPatternFill(ScrnInfoPtr pScrn,
                                int patx, int paty, int fg, int bg,
                                int rop, unsigned int planemask);
static void SiSUSBSubsequentMonoPatternFill(ScrnInfoPtr pScrn,
                                int patx, int paty,
                                int x, int y, int w, int h);
#ifdef SISVRAMQ
static void SiSUSBSetupForColor8x8PatternFill(ScrnInfoPtr pScrn,
				int patternx, int patterny,
				int rop, unsigned int planemask, int trans_col);
static void SiSUSBSubsequentColor8x8PatternFillRect(ScrnInfoPtr pScrn,
				int patternx, int patterny, int x, int y,
				int w, int h);
#endif
#endif

extern unsigned char SiSUSBGetCopyROP(int rop);
extern unsigned char SiSUSBGetPatternROP(int rop);

CARD32 dummybuf;

/* VRAM queue acceleration specific */

#define SIS_WQINDEX(i)  sis_accel_cmd_buffer[i]
#define SIS_RQINDEX(i)  ((ULong)tt + 1)  /* Bullshit, but we don't flush anyway */

#if 0
CARD32 sis_accel_cmd_buffer[4];

static void SiSUSBWriteQueue(SISUSBPtr pSiSUSB, pointer tt)
{
   int num, retry = 3;
   do {
      lseek(pSiSUSB->sisusbdev, (int)tt, SEEK_SET);
      num = write(pSiSUSB->sisusbdev, &sis_accel_cmd_buffer[0], 16);
   } while((num != 16) && --retry);
   if(!retry) SiSLostConnection(pSiSUSB);
}
#endif

static void
SiSUSBInitializeAccelerator(ScrnInfoPtr pScrn)
{
#ifndef SISVRAMQ
	SISUSBPtr  pSiSUSB = SISUSBPTR(pScrn);

	if(pSiSUSB->ChipFlags & SiSCF_Integrated) {
	   CmdQueLen = 0;
        } else {
	   CmdQueLen = ((128 * 1024) / 4) - 64;
        }
#endif
}

Bool
SiSUSBAccelInit(ScreenPtr pScreen)
{
	ScrnInfoPtr     pScrn = xf86Screens[pScreen->myNum];
	SISUSBPtr      	pSiSUSB = SISUSBPTR(pScrn);
	int		topFB, reservedFbSize, usableFbSize;
	BoxRec          Avail;

	pSiSUSB->ColorExpandBufferNumber = 0;
	pSiSUSB->PerColorExpandBufferSize = 0;
	
	if((pScrn->bitsPerPixel != 8)  && 
	   (pScrn->bitsPerPixel != 16) &&
	   (pScrn->bitsPerPixel != 32)) {
	   pSiSUSB->NoAccel = TRUE;
	}
	
	if(!pSiSUSB->NoAccel) {

	   SiSUSBInitializeAccelerator(pScrn);

        } 
	
	/* Init framebuffer memory manager */

	topFB = pSiSUSB->maxxfbmem;

	reservedFbSize = pSiSUSB->ColorExpandBufferNumber * pSiSUSB->PerColorExpandBufferSize;

	usableFbSize = topFB - reservedFbSize;
	
	/* Layout:
	 * |--------------++++++++++++++++++++^************==========~~~~~~~~~~~~|
	 *   UsableFbSize  ColorExpandBuffers |  DRI-Heap   HWCursor  CommandQueue
	 *                                 topFB
	 */
	 
	Avail.x1 = 0;
	Avail.y1 = 0;
	Avail.x2 = pScrn->displayWidth;
	Avail.y2 = (usableFbSize / (pScrn->displayWidth * pScrn->bitsPerPixel/8)) - 1;

	if(Avail.y2 < 0) Avail.y2 = 32767;
	
	if(Avail.y2 < pScrn->currentMode->VDisplay) {
	   xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		"Not enough video RAM for accelerator. At least "
		"%dKB needed, %ldKB available\n",
		((((pScrn->displayWidth * pScrn->bitsPerPixel/8)   /* +8 for make it sure */
		     * pScrn->currentMode->VDisplay) + reservedFbSize) / 1024) + 8,
		pSiSUSB->maxxfbmem/1024);
	   pSiSUSB->NoAccel = TRUE;
	   pSiSUSB->NoXvideo = TRUE;	   
	   return FALSE;   /* Don't even init fb manager */
	}

	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
		   "Framebuffer from (%d,%d) to (%d,%d)\n",
		   Avail.x1, Avail.y1, Avail.x2 - 1, Avail.y2 - 1);
	
	xf86InitFBManager(pScreen, &Avail);
	
	return TRUE;
}

#if 0
void
SiSUSBSync(ScrnInfoPtr pScrn)
{
	SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);
	
	SiSUSBIdle
}
#endif

#if 0
static void SiSUSBSetupForScreenToScreenCopy(ScrnInfoPtr pScrn,
                                int xdir, int ydir, int rop,
                                unsigned int planemask, int trans_color)
{
	SISUSBPtr  pSiSUSB = SISUSBPTR(pScrn);

	PDEBUG(ErrorF("Setup ScreenCopy(%d, %d, 0x%x, 0x%x, 0x%x)\n",
			xdir, ydir, rop, planemask, trans_color));

#ifdef SISVRAMQ
        SiSUSBSetupDSTColorDepth(pSiSUSB->SiS310_AccelDepth);
	SiSUSBCheckQueue(16 * 2);
	SiSUSBSetupSRCPitchDSTRect(pSiSUSB->scrnOffset, pSiSUSB->scrnOffset, -1)
#else
	SiSUSBSetupDSTColorDepth(pSiSUSB->DstColor);
	SiSUSBSetupSRCPitch(pSiSUSB->scrnOffset)
	SiSUSBSetupDSTRect(pSiSUSB->scrnOffset, -1)
#endif

	if(trans_color != -1) {
	   SiSUSBSetupROP(0x0A)
	   SiSUSBSetupSRCTrans(trans_color)
	   SiSUSBSetupCMDFlag(TRANSPARENT_BITBLT)
	} else {
	   SiSUSBSetupROP(SiSUSBGetCopyROP(rop))
	   /* Set command - not needed, both 0 */
	   /* SiSUSBSetupCMDFlag(BITBLT | SRCVIDEO) */
	}

#ifndef SISVRAMQ
	SiSUSBSetupCMDFlag(pSiSUSB->SiS310_AccelDepth)
#endif

#ifdef SISVRAMQ
        SiSUSBSyncWP
#endif

	/* The chip is smart enough to know the direction */
}

static void SiSUSBSubsequentScreenToScreenCopy(ScrnInfoPtr pScrn,
                                int src_x, int src_y, int dst_x, int dst_y,
                                int width, int height)
{
	SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);
	CARD32 srcbase, dstbase;
	int    mymin, mymax;

	PDEBUG(ErrorF("Subsequent ScreenCopy(%d,%d, %d,%d, %d,%d)\n",
			  src_x, src_y, dst_x, dst_y, width, height));

	srcbase = dstbase = 0;
	mymin = min(src_y, dst_y);
	mymax = max(src_y, dst_y);

	/* Although the chip knows the direction to use
	 * if the source and destination areas overlap,
	 * that logic fails if we fiddle with the bitmap
	 * addresses. Therefore, we check if the source
	 * and destination blitting areas overlap and
	 * adapt the bitmap addresses synchronously 
	 * if the coordinates exceed the valid range.
	 * The the areas do not overlap, we do our 
	 * normal check.
	 */
	if((mymax - mymin) < height) {
	   if((src_y >= 2048) || (dst_y >= 2048)) {	      
	      srcbase = pSiSUSB->scrnOffset * mymin;
	      dstbase = pSiSUSB->scrnOffset * mymin;
	      src_y -= mymin;
	      dst_y -= mymin;
	   }
	} else {
	   if(src_y >= 2048) {
	      srcbase = pSiSUSB->scrnOffset * src_y;
	      src_y = 0;
	   }
	   if((dst_y >= pScrn->virtualY) || (dst_y >= 2048)) {
	      dstbase = pSiSUSB->scrnOffset * dst_y;
	      dst_y = 0;
	   }
	}

#ifdef SISVRAMQ
	SiSUSBCheckQueue(16 * 3);
        SiSUSBSetupSRCDSTBase(srcbase, dstbase)
	SiSUSBSetupSRCDSTXY(src_x, src_y, dst_x, dst_y)
	SiSUSBSetRectDoCMD(width,height)
#else
	SiSUSBSetupSRCBase(srcbase);
	SiSUSBSetupDSTBase(dstbase);
	SiSUSBSetupRect(width, height)
	SiSUSBSetupSRCXY(src_x, src_y)
	SiSUSBSetupDSTXY(dst_x, dst_y)
	SiSUSBDoCMD
#endif
}
#endif

#if 0
static void
SiSUSBSetupForSolidFill(ScrnInfoPtr pScrn, int color,
			int rop, unsigned int planemask)
{
	SISUSBPtr  pSiSUSB = SISUSBPTR(pScrn);

	PDEBUG(ErrorF("Setup SolidFill(0x%x, 0x%x, 0x%x)\n",
					color, rop, planemask));

	if(pSiSUSB->disablecolorkeycurrent) {
	   if((CARD32)color == pSiSUSB->colorKey) {
	      rop = 5;  /* NOOP */
	   }
	}

#ifdef SISVRAMQ
	SiSUSBSetupDSTColorDepth(pSiSUSB->SiS310_AccelDepth);
	SiSUSBCheckQueue(16 * 1);
	SiSUSBSetupPATFGDSTRect(color, pSiSUSB->scrnOffset, -1)
	SiSUSBSetupROP(SiSUSBGetPatternROP(rop))
	SiSUSBSetupCMDFlag(PATFG)
        SiSUSBSyncWP
#else
  	SiSUSBSetupPATFG(color)
	SiSUSBSetupDSTRect(pSiSUSB->scrnOffset, -1)
	SiSUSBSetupDSTColorDepth(pSiSUSB->DstColor);
	SiSUSBSetupROP(SiSUSBGetPatternROP(rop))
	SiSUSBSetupCMDFlag(PATFG | pSiSUSB->SiS310_AccelDepth)
#endif
}

static void
SiSUSBSubsequentSolidFillRect(ScrnInfoPtr pScrn,
                        int x, int y, int w, int h)
{
	SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);
	CARD32 dstbase;

	PDEBUG(ErrorF("Subsequent SolidFillRect(%d, %d, %d, %d)\n",
					x, y, w, h));

	dstbase = 0;
	if(y >= 2048) {
	   dstbase = pSiSUSB->scrnOffset * y;
	   y = 0;
	}

	pSiSUSB->CommandReg &= ~(T_XISMAJORL | T_XISMAJORR |
	                      T_L_X_INC | T_L_Y_INC |
	                      T_R_X_INC | T_R_Y_INC |
			      TRAPAZOID_FILL);

	/* SiSUSBSetupCMDFlag(BITBLT)  - BITBLT = 0 */

#ifdef SISVRAMQ
	SiSUSBCheckQueue(16 * 2)
	SiSUSBSetupDSTXYRect(x,y,w,h)
	SiSUSBSetupDSTBaseDoCMD(dstbase)
#else
	SiSUSBSetupDSTBase(dstbase)
	SiSUSBSetupDSTXY(x,y)
	SiSUSBSetupRect(w,h)
	SiSUSBDoCMD
#endif
}
#endif

#if 0
static void
SiSUSBSetupForSolidLine(ScrnInfoPtr pScrn, int color, int rop,
                                     unsigned int planemask)
{
	SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);

	PDEBUG(ErrorF("Setup SolidLine(0x%x, 0x%x, 0x%x)\n",
					color, rop, planemask));

#ifdef SISVRAMQ
	SiSUSBSetupDSTColorDepth(pSiSUSB->SiS310_AccelDepth);
	SiSUSBCheckQueue(16 * 3);
        SiSUSBSetupLineCountPeriod(1, 1)
	SiSUSBSetupPATFGDSTRect(color, pSiSUSB->scrnOffset, -1)
	SiSUSBSetupROP(SiSUSBGetPatternROP(rop))
	SiSUSBSetupCMDFlag(PATFG | LINE)
        SiSUSBSyncWP
#else
	SiSUSBSetupLineCount(1)
	SiSUSBSetupPATFG(color)
	SiSUSBSetupDSTRect(pSiSUSB->scrnOffset, -1)
	SiSUSBSetupDSTColorDepth(pSiSUSB->DstColor)
	SiSUSBSetupROP(SiSUSBGetPatternROP(rop))
	SiSUSBSetupCMDFlag(PATFG | LINE | pSiSUSB->SiS310_AccelDepth)
#endif
}

static void
SiSUSBSubsequentSolidTwoPointLine(ScrnInfoPtr pScrn,
                        int x1, int y1, int x2, int y2, int flags)
{
	SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);
	CARD32 dstbase;
	int    miny,maxy;

	PDEBUG(ErrorF("Subsequent SolidLine(%d, %d, %d, %d, 0x%x)\n",
					x1, y1, x2, y2, flags));

	dstbase = 0;
	miny = (y1 > y2) ? y2 : y1;
	maxy = (y1 > y2) ? y1 : y2;
	if(maxy >= 2048) {
	   dstbase = pSiSUSB->scrnOffset*miny;
	   y1 -= miny;
	   y2 -= miny;
	}

	if(flags & OMIT_LAST) {
	   SiSUSBSetupCMDFlag(NO_LAST_PIXEL)
	} else {
	   pSiSUSB->CommandReg &= ~(NO_LAST_PIXEL);
	}

#ifdef SISVRAMQ
	SiSUSBCheckQueue(16 * 2);
        SiSUSBSetupX0Y0X1Y1(x1,y1,x2,y2)
	SiSUSBSetupDSTBaseDoCMD(dstbase)
#else
	SiSUSBSetupDSTBase(dstbase)
	SiSUSBSetupX0Y0(x1,y1)
	SiSUSBSetupX1Y1(x2,y2)
	SiSUSBDoCMD
#endif
}

static void
SiSUSBSubsequentSolidHorzVertLine(ScrnInfoPtr pScrn,
                                int x, int y, int len, int dir)
{
	SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);
	CARD32 dstbase;

	PDEBUG(ErrorF("Subsequent SolidHorzVertLine(%d, %d, %d, %d)\n",
					x, y, len, dir));

	len--; /* starting point is included! */
	dstbase = 0;
	if((y >= 2048) || ((y + len) >= 2048)) {
	   dstbase = pSiSUSB->scrnOffset * y;
	   y = 0;
	}

#ifdef SISVRAMQ
	SiSUSBCheckQueue(16 * 2);
    	if(dir == DEGREES_0) {
	   SiSUSBSetupX0Y0X1Y1(x, y, (x + len), y)
	} else {
	   SiSUSBSetupX0Y0X1Y1(x, y, x, (y + len))
	}
	SiSUSBSetupDSTBaseDoCMD(dstbase)
#else
	SiSUSBSetupDSTBase(dstbase)
	SiSUSBSetupX0Y0(x,y)
	if(dir == DEGREES_0) {
	   SiSUSBSetupX1Y1(x + len, y);
	} else {
	   SiSUSBSetupX1Y1(x, y + len);
	}
	SiSUSBDoCMD
#endif
}

static void
SiSUSBSetupForDashedLine(ScrnInfoPtr pScrn,
                                int fg, int bg, int rop, unsigned int planemask,
                                int length, unsigned char *pattern)
{
	SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);

	PDEBUG(ErrorF("Setup DashedLine(0x%x, 0x%x, 0x%x, 0x%x, %d, 0x%x:%x)\n",
			fg, bg, rop, planemask, length, *(pattern+4), *pattern));

#ifdef SISVRAMQ
	SiSUSBSetupDSTColorDepth(pSiSUSB->SiS310_AccelDepth);
	SiSUSBCheckQueue(16 * 3);
 	SiSUSBSetupLineCountPeriod(1, length-1)
	SiSUSBSetupStyle(*pattern,*(pattern+4))
	SiSUSBSetupPATFGDSTRect(fg, pSiSUSB->scrnOffset, -1)
#else
	SiSUSBSetupLineCount(1)
	SiSUSBSetupDSTRect(pSiSUSB->scrnOffset, -1)
	SiSUSBSetupDSTColorDepth(pSiSUSB->DstColor);
	SiSUSBSetupStyleLow(*pattern)
	SiSUSBSetupStyleHigh(*(pattern+4))
	SiSUSBSetupStylePeriod(length-1);
	SiSUSBSetupPATFG(fg)
#endif

	SiSUSBSetupROP(SiSUSBGetPatternROP(rop))

	SiSUSBSetupCMDFlag(LINE | LINE_STYLE)

	if(bg != -1) {
	   SiSUSBSetupPATBG(bg)
	} else {
	   SiSUSBSetupCMDFlag(TRANSPARENT)
	}
#ifndef SISVRAMQ
	SiSUSBSetupCMDFlag(pSiSUSB->SiS310_AccelDepth)
#endif

#ifdef SISVRAMQ
        SiSUSBSyncWP
#endif
}

static void
SiSUSBSubsequentDashedTwoPointLine(ScrnInfoPtr pScrn,
                                int x1, int y1, int x2, int y2,
                                int flags, int phase)
{
	SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);
	CARD32 dstbase,miny,maxy;

	PDEBUG(ErrorF("Subsequent DashedLine(%d,%d, %d,%d, 0x%x,0x%x)\n",
			x1, y1, x2, y2, flags, phase));

	dstbase = 0;
	miny = (y1 > y2) ? y2 : y1;
	maxy = (y1 > y2) ? y1 : y2;
	if(maxy >= 2048) {
	   dstbase = pSiSUSB->scrnOffset * miny;
	   y1 -= miny;
	   y2 -= miny;
	}

	if(flags & OMIT_LAST) {
	   SiSUSBSetupCMDFlag(NO_LAST_PIXEL)
	} else {
	   pSiSUSB->CommandReg &= ~(NO_LAST_PIXEL);
	}

#ifdef SISVRAMQ
	SiSUSBCheckQueue(16 * 2);
	SiSUSBSetupX0Y0X1Y1(x1,y1,x2,y2)
	SiSUSBSetupDSTBaseDoCMD(dstbase)
#else
	SiSUSBSetupDSTBase(dstbase)
	SiSUSBSetupX0Y0(x1,y1)
	SiSUSBSetupX1Y1(x2,y2)
	SiSUSBDoCMD
#endif
}

static void
SiSUSBSetupForMonoPatternFill(ScrnInfoPtr pScrn,
                                int patx, int paty, int fg, int bg,
                                int rop, unsigned int planemask)
{
	SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);

	PDEBUG(ErrorF("Setup MonoPatFill(0x%x,0x%x, 0x%x,0x%x, 0x%x, 0x%x)\n",
					patx, paty, fg, bg, rop, planemask));

#ifdef SISVRAMQ
	SiSUSBSetupDSTColorDepth(pSiSUSB->SiS310_AccelDepth);
	SiSUSBCheckQueue(16 * 3);
	SiSUSBSetupPATFGDSTRect(fg, pSiSUSB->scrnOffset, -1)
#else
	SiSUSBSetupDSTRect(pSiSUSB->scrnOffset, -1)
	SiSUSBSetupDSTColorDepth(pSiSUSB->DstColor);
#endif

	SiSUSBSetupMONOPAT(patx,paty)

	SiSUSBSetupROP(SiSUSBGetPatternROP(rop))

#ifdef SISVRAMQ
        SiSUSBSetupCMDFlag(PATMONO)
#else
	SiSUSBSetupPATFG(fg)
	SiSUSBSetupCMDFlag(PATMONO | pSiSUSB->SiS310_AccelDepth)
#endif

	if(bg != -1) {
	   SiSUSBSetupPATBG(bg)
	} else {
	   SiSUSBSetupCMDFlag(TRANSPARENT)
	}

#ifdef SISVRAMQ
        SiSUSBSyncWP
#endif
}

static void
SiSUSBSubsequentMonoPatternFill(ScrnInfoPtr pScrn,
                                int patx, int paty,
                                int x, int y, int w, int h)
{
	SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);
	CARD32 dstbase;

	PDEBUG(ErrorF("Subsequent MonoPatFill(0x%x,0x%x, %d,%d, %d,%d)\n",
							patx, paty, x, y, w, h));
	dstbase = 0;
	if(y >= 2048) {
	   dstbase = pSiSUSB->scrnOffset * y;
	   y = 0;
	}

	/* Clear commandReg because Setup can be used for Rect and Trap */
	pSiSUSB->CommandReg &= ~(T_XISMAJORL | T_XISMAJORR |
	                      T_L_X_INC | T_L_Y_INC |
	                      T_R_X_INC | T_R_Y_INC |
	                      TRAPAZOID_FILL);

#ifdef SISVRAMQ
	SiSUSBCheckQueue(16 * 2);
	SiSUSBSetupDSTXYRect(x,y,w,h)
	SiSUSBSetupDSTBaseDoCMD(dstbase)
#else
	SiSUSBSetupDSTBase(dstbase)
	SiSUSBSetupDSTXY(x,y)
	SiSUSBSetupRect(w,h)
	SiSUSBDoCMD
#endif
}

/* Color 8x8 pattern */

#ifdef SISVRAMQ
static void
SiSUSBSetupForColor8x8PatternFill(ScrnInfoPtr pScrn, int patternx, int patterny,
			int rop, unsigned int planemask, int trans_col)
{
	SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);
	int j = pScrn->bitsPerPixel >> 3;
	CARD32 *patadr = (CARD32 *)(pSiSUSB->FbBase + (patterny * pSiSUSB->scrnOffset) +
				(patternx * j));

#ifdef ACCELDEBUG
	xf86DrvMsg(0, X_INFO, "Setup Color8x8PatFill(0x%x, 0x%x, 0x%x, 0x%x)\n",
				patternx, patterny, rop, planemask);
#endif

	SiSUSBSetupDSTColorDepth(pSiSUSB->SiS310_AccelDepth);
	SiSUSBCheckQueue(16 * 3);

	SiSUSBSetupDSTRectBurstHeader(pSiSUSB->scrnOffset, -1, PATTERN_REG, (pScrn->bitsPerPixel << 1))

	while(j--) {
	   SiSUSBSetupPatternRegBurst(patadr[0],  patadr[1],  patadr[2],  patadr[3]);
	   SiSUSBSetupPatternRegBurst(patadr[4],  patadr[5],  patadr[6],  patadr[7]);
	   SiSUSBSetupPatternRegBurst(patadr[8],  patadr[9],  patadr[10], patadr[11]);
	   SiSUSBSetupPatternRegBurst(patadr[12], patadr[13], patadr[14], patadr[15]);
	   patadr += 16;  /* = 64 due to (CARD32 *) */
	}

	SiSUSBSetupROP(SiSUSBGetPatternROP(rop))

	SiSUSBSetupCMDFlag(PATPATREG)

        SiSUSBSyncWP
}

static void
SiSUSBSubsequentColor8x8PatternFillRect(ScrnInfoPtr pScrn, int patternx,
			int patterny, int x, int y, int w, int h)
{
	SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);
	CARD32 dstbase;

#ifdef ACCELDEBUG
	xf86DrvMsg(0, X_INFO, "Subsequent Color8x8FillRect(%d, %d, %d, %d)\n",
					x, y, w, h);
#endif

	dstbase = 0;
	if(y >= 2048) {
	   dstbase = pSiSUSB->scrnOffset * y;
	   y = 0;
	}
	
	/* SiSUSBSetupCMDFlag(BITBLT)  - BITBLT = 0 */

	SiSUSBCheckQueue(16 * 2)
	SiSUSBSetupDSTXYRect(x,y,w,h)
	SiSUSBSetupDSTBaseDoCMD(dstbase)
}
#endif

#endif

#if 0

/* High level */

typedef void (*ClipAndRenderRectsFunc)(ScrnInfoPtr, GCPtr, int, BoxPtr, int, int); 

#define DO_COLOR_8x8		0x00000001
#define DO_MONO_8x8		0x00000002
#define DO_CACHE_BLT		0x00000003
#define DO_COLOR_EXPAND		0x00000004
#define DO_CACHE_EXPAND		0x00000005
#define DO_IMAGE_WRITE		0x00000006
#define DO_PIXMAP_COPY		0x00000007
#define DO_SOLID		0x00000008

Bool SiSUSBAPolyFillRect(ScrnInfoPtr pScrn, DrawablePtr pDraw, GCPtr pGC, 
					int nrectFill, xRectangle *prectInit);


static void
XAAClipAndRenderRects(
   ScrnInfoPtr pScrn,
   GCPtr pGC, 
   ClipAndRenderRectsFunc BoxFunc, 
   int nrectFill, 
   xRectangle *prect, 
   int xorg, int yorg
){
    SISUSBPtr   pSiSUSB = SISUSBPTR(pScrn);
    int 	Right, Bottom, MaxBoxes;
    BoxPtr 	pextent, pboxClipped, pboxClippedBase;
    
    MaxBoxes = pSiSUSB->PreAllocSize/sizeof(BoxRec);  
    pboxClippedBase = (BoxPtr)pSiSUSB->PreAllocMem;
    pboxClipped = pboxClippedBase;

    if (REGION_NUM_RECTS(pGC->pCompositeClip) == 1) {
	pextent = REGION_RECTS(pGC->pCompositeClip);
    	while (nrectFill--) {
	    pboxClipped->x1 = max(pextent->x1, prect->x + xorg);
	    pboxClipped->y1 = max(pextent->y1, prect->y + yorg);

	    Right = (int)prect->x + xorg + (int)prect->width;
	    pboxClipped->x2 = min(pextent->x2, Right);
    
	    Bottom = (int)prect->y + yorg + (int)prect->height;
	    pboxClipped->y2 = min(pextent->y2, Bottom);

	    prect++;
	    if ((pboxClipped->x1 < pboxClipped->x2) &&
		(pboxClipped->y1 < pboxClipped->y2)) {
		pboxClipped++;
		if(pboxClipped >= (pboxClippedBase + MaxBoxes)) {
		    (*BoxFunc)(pScrn, pGC, MaxBoxes, pboxClippedBase, xorg, yorg); 
		    pboxClipped = pboxClippedBase;
		}
	    }
    	}
    } else {
	pextent = REGION_EXTENTS(pGC->pScreen, pGC->pCompositeClip);
    	while (nrectFill--) {
	    int n;
	    BoxRec box, *pbox;
   
	    box.x1 = max(pextent->x1, prect->x + xorg);
   	    box.y1 = max(pextent->y1, prect->y + yorg);
     
	    Right = (int)prect->x + xorg + (int)prect->width;
	    box.x2 = min(pextent->x2, Right);
  
	    Bottom = (int)prect->y + yorg + (int)prect->height;
	    box.y2 = min(pextent->y2, Bottom);
    
	    prect++;
    
	    if ((box.x1 >= box.x2) || (box.y1 >= box.y2))
	    	continue;
    
	    n = REGION_NUM_RECTS (pGC->pCompositeClip);
	    pbox = REGION_RECTS(pGC->pCompositeClip);
    
	    /* clip the rectangle to each box in the clip region
	       this is logically equivalent to calling Intersect()
	    */
	    while(n--) {
		pboxClipped->x1 = max(box.x1, pbox->x1);
		pboxClipped->y1 = max(box.y1, pbox->y1);
		pboxClipped->x2 = min(box.x2, pbox->x2);
		pboxClipped->y2 = min(box.y2, pbox->y2);
		pbox++;

		/* see if clipping left anything */
		if(pboxClipped->x1 < pboxClipped->x2 && 
		   pboxClipped->y1 < pboxClipped->y2) {
		    pboxClipped++;
		    if(pboxClipped >= (pboxClippedBase + MaxBoxes)) {
			(*BoxFunc)(pScrn, pGC, MaxBoxes, pboxClippedBase, xorg, yorg); 
			pboxClipped = pboxClippedBase;
		    }
		}
	    }
    	}
    }

    if(pboxClipped != pboxClippedBase) {
	(*BoxFunc)(pScrn, pGC, pboxClipped - pboxClippedBase, pboxClippedBase, xorg, yorg); 
    }
}

/* Copy area - can't accelerate (src in system RAM) */

Bool
SiSUSBFBCopyArea()
{
    return FALSE;
}

/* Fill rects */

static void
SiSUSBFillSolidRects(
    ScrnInfoPtr pScrn,
    int	fg, int rop,
    unsigned int planemask,
    int		nBox, 		/* number of rectangles to fill */
    BoxPtr	pBox  		/* Pointer to first rectangle to fill */
){
    SISUSBPtr pSiSUSB = SISUSBPTR(pScrn);

    SiSUSBSetupForSolidFill(pScrn, fg, rop, planemask);
     while(nBox--) {
        SiSUSBSubsequentSolidFillRect(pScrn, pBox->x1, pBox->y1,
 			pBox->x2 - pBox->x1, pBox->y2 - pBox->y1);
	pBox++;
     }
     pSiSUSB->AccelNeedSync = TRUE;
}

static void
SiSUSBRenderSolidRects(
   ScrnInfoPtr pScrn,
   GCPtr pGC,
   int nboxes,
   BoxPtr pClipBoxes,
   int xorg, int yorg
){
   SiSUSBFillSolidRects(pScrn, pGC->fgPixel, pGC->alu, pGC->planemask, nboxes, pClipBoxes);
}

Bool SiSUSBAPolyFillRect(
    ScrnInfoPtr pScrn,
    DrawablePtr pDraw,
    GCPtr pGC,
    int		nrectFill, 	/* number of rectangles to fill */
    xRectangle	*prectInit   	/* Pointer to first rectangle to fill */
){
    ClipAndRenderRectsFunc function;
    int		xorg = pDraw->x;
    int		yorg = pDraw->y;
    int		type = 0;

    if((nrectFill <= 0) || !pGC->planemask)
        return FALSE;

    if(!REGION_NUM_RECTS(pGC->pCompositeClip))
	return FALSE;

    switch(pGC->fillStyle) {
    case FillSolid:
	type = DO_SOLID;
	break;
#if 0	
    case FillStippled:
	type = (*infoRec->StippledFillChooser)(pGC);
	break;
    case FillOpaqueStippled:
	if((pGC->fgPixel == pGC->bgPixel) && infoRec->FillSolidRects &&
                CHECK_PLANEMASK(pGC,infoRec->FillSolidRectsFlags) &&
                CHECK_ROP(pGC,infoRec->FillSolidRectsFlags) &&
                CHECK_ROPSRC(pGC,infoRec->FillSolidRectsFlags) &&
                CHECK_FG(pGC,infoRec->FillSolidRectsFlags))
	    type = DO_SOLID;
	else
	    type = (*infoRec->OpaqueStippledFillChooser)(pGC);
	break;
    case FillTiled:
	type = (*infoRec->TiledFillChooser)(pGC);
	break;
#endif	
    default:
        return FALSE;
    }
    
    switch(type) {
    case DO_SOLID:
	function = SiSUSBRenderSolidRects;
	break;	
#if 0	
    case DO_COLOR_8x8:
	function = XAARenderColor8x8Rects;	
	break;	
    case DO_MONO_8x8:
	function = XAARenderMono8x8Rects;	
	break;	
    case DO_CACHE_BLT:
	function = XAARenderCacheBltRects;	
	break;	
    case DO_COLOR_EXPAND:
	function = XAARenderColorExpandRects;	
	break;	
    case DO_CACHE_EXPAND:
	function = XAARenderCacheExpandRects;	
	break;	
    case DO_IMAGE_WRITE:
	function = XAARenderImageWriteRects;	
	break;	
    case DO_PIXMAP_COPY:
	function = XAARenderPixmapCopyRects;	
	break;	
#endif	
    default:
	return FALSE;
    }
    
    XAAClipAndRenderRects(pScrn, pGC, function, nrectFill, prectInit, xorg, yorg);

    return TRUE;
}
#endif





