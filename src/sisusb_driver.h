/* $XFree86$ */
/* $XdotOrg$ */
/*
 * Global data and definitions
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
 * Author:     	Thomas Winischhofer <thomas@winischhofer.net>
 *
 */

/* For calculating refresh rate index (CR33) */
static const struct _sis_vrate {
    CARD16 idx;
    CARD16 xres;
    CARD16 yres;
    CARD16 refresh;
    Bool SiS730valid32bpp;
} sisx_vrate[] = {
	{1,  320,  200,  60,  TRUE}, {1,  320,  200,  70,  TRUE},
	{1,  320,  240,  60,  TRUE},
	{1,  400,  300,  60,  TRUE},
        {1,  512,  384,  60,  TRUE},
	{1,  640,  400,  60,  TRUE}, {1,  640,  400,  72,  TRUE},
	{1,  640,  480,  60,  TRUE}, {2,  640,  480,  72,  TRUE}, {3,  640,  480,  75,  TRUE},
	{4,  640,  480,  85,  TRUE}, {5,  640,  480, 100,  TRUE}, {6,  640,  480, 120,  TRUE},
	{7,  640,  480, 160, FALSE}, {8,  640,  480, 200, FALSE},
	{1,  720,  480,  60,  TRUE},
	{1,  720,  576,  60,  TRUE},
	{1,  768,  576,  60,  TRUE},
	{1,  800,  480,  60,  TRUE}, {2,  800,  480,  75,  TRUE}, {3,  800,  480,  85,  TRUE},
	{1,  800,  600,  56,  TRUE}, {2,  800,  600,  60,  TRUE}, {3,  800,  600,  72,  TRUE},
	{4,  800,  600,  75,  TRUE}, {5,  800,  600,  85,  TRUE}, {6,  800,  600, 105,  TRUE},
	{7,  800,  600, 120, FALSE}, {8,  800,  600, 160, FALSE},
	{1,  848,  480,  39,  TRUE}, {2,  848,  480,  60,  TRUE},
	{1,  856,  480,  39,  TRUE}, {2,  856,  480,  60,  TRUE},
	{1,  960,  540,  60,  TRUE},
	{1,  960,  600,  60,  TRUE},
	{1, 1024,  576,  60,  TRUE}, {2, 1024,  576,  75,  TRUE}, {3, 1024,  576,  85,  TRUE},
	{1, 1024,  768,  43,  TRUE}, {2, 1024,  768,  60,  TRUE}, {3, 1024,  768,  70, FALSE},
	{4, 1024,  768,  75, FALSE}, {5, 1024,  768,  85,  TRUE}, {6, 1024,  768, 100,  TRUE},
	{7, 1024,  768, 120,  TRUE},
	{1, 1280,  720,  60,  TRUE}, {2, 1280,  720,  75, FALSE}, {3, 1280,  720,  85,  TRUE},
	{1, 1280,  768,  60,  TRUE},
	{0,    0,    0,   0, FALSE}
};

/* Mandatory functions */
static void SISUSBIdentify(int flags);
static Bool SISUSBProbe(DriverPtr drv, int flags);
static Bool SISUSBPreInit(ScrnInfoPtr pScrn, int flags);
static Bool SISUSBScreenInit(int Index, ScreenPtr pScreen, int argc, char **argv);
static Bool SISUSBEnterVT(int scrnIndex, int flags);
static void SISUSBLeaveVT(int scrnIndex, int flags);
static Bool SISUSBCloseScreen(int scrnIndex, ScreenPtr pScreen);
static Bool SISUSBSaveScreen(ScreenPtr pScreen, int mode);
static Bool SISUSBSwitchMode(int scrnIndex, DisplayModePtr mode, int flags);
static void SISUSBAdjustFrame(int scrnIndex, int x, int y, int flags);

#ifdef X_XF86MiscPassMessage
static int  SISUSBHandleMessage(int scrnIndex, const char *msgtype,
		      const char *msgval, char **retmsg);
#endif

/* Optional functions */
static void       SISUSBFreeScreen(int scrnIndex, int flags);
static ModeStatus SISUSBValidMode(int scrnIndex, DisplayModePtr mode,
                               Bool verbose, int flags);

/* Internally used functions */
static Bool    SISUSBMapMem(ScrnInfoPtr pScrn);
static Bool    SISUSBUnmapMem(ScrnInfoPtr pScrn);
static void    SISUSBSave(ScrnInfoPtr pScrn);
static void    SISUSBRestore(ScrnInfoPtr pScrn);
static Bool    SISUSBModeInit(ScrnInfoPtr pScrn, DisplayModePtr mode);
static void    SISUSBModifyModeInfo(DisplayModePtr mode);
static void    SiSUSBPreSetMode(ScrnInfoPtr pScrn, DisplayModePtr mode, int viewmode);
static void    SiSUSBPostSetMode(ScrnInfoPtr pScrn, SISUSBRegPtr sisReg);
static void    SISUSBBridgeRestore(ScrnInfoPtr pScrn);
static void    SiSUSBEnableTurboQueue(ScrnInfoPtr pScrn);
static void    SiSUSBRestoreQueueMode(SISUSBPtr pSiSUSB, SISUSBRegPtr sisReg);
UChar  	       SISUSBSearchCRT1Rate(ScrnInfoPtr pScrn, DisplayModePtr mode);
static void    SISUSBWaitVBRetrace(ScrnInfoPtr pScrn);
void           SISUSBWaitRetraceCRT1(ScrnInfoPtr pScrn);
static UShort  SiSUSB_CheckModeCRT1(ScrnInfoPtr pScrn, DisplayModePtr mode,
				 ULong VBFlags, Bool hcm);

Bool           SiSUSBBridgeIsInSlaveMode(ScrnInfoPtr pScrn);
UShort	       SiSUSB_GetModeNumber(ScrnInfoPtr pScrn, DisplayModePtr mode, ULong VBFlags);
UChar  	       SiSUSB_GetSetBIOSScratch(ScrnInfoPtr pScrn, UShort offset, UChar value);
#ifdef DEBUG
static void    SiSUSBDumpModeInfo(ScrnInfoPtr pScrn, DisplayModePtr mode);
#endif
void           SISUSBSaveDetectedDevices(ScrnInfoPtr pScrn);

/* Our very own vgaHW functions (sis_vga.c) */
extern void 	SiSUSBVGASave(ScrnInfoPtr pScrn, SISUSBRegPtr save, int flags);
extern void 	SiSUSBVGARestore(ScrnInfoPtr pScrn, SISUSBRegPtr restore, int flags);
extern void 	SiSUSBVGASaveFonts(ScrnInfoPtr pScrn);
extern void 	SiSUSBVGARestoreFonts(ScrnInfoPtr pScrn);
extern void 	SISUSBVGALock(SISUSBPtr pSiSUSB);
extern void 	SiSUSBVGAUnlock(SISUSBPtr pSiSUSB);
extern void 	SiSUSBVGAProtect(ScrnInfoPtr pScrn, Bool on);
extern Bool 	SiSUSBVGAMapMem(ScrnInfoPtr pScrn);
extern void 	SiSUSBVGAUnmapMem(ScrnInfoPtr pScrn);
extern Bool 	SiSUSBVGASaveScreen(ScreenPtr pScreen, int mode);

/* shadow */
extern void 	SISUSBRefreshArea(ScrnInfoPtr pScrn, int num, BoxPtr pbox);
extern void 	SISUSBDoRefreshArea(ScrnInfoPtr pScrn);

/* vb */
extern void 	SISUSBCRT1PreInit(ScrnInfoPtr pScrn);
extern Bool 	SISUSBRedetectCRT2Type(ScrnInfoPtr pScrn);

/* init.c, init301.c ----- (use their data types!) */
extern USHORT   SiSUSB_GetModeID(int VGAEngine, ULONG VBFlags, int HDisplay, int VDisplay,
				  int Depth, BOOLEAN FSTN, int LCDwith, int LCDheight);
extern int      SiSUSBTranslateToOldMode(int modenumber);
extern BOOLEAN 	SiSUSBDetermineROMLayout661(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo);
extern BOOLEAN 	SiSUSBBIOSSetMode(SiS_Private *SiS_Pr, PSIS_HW_INFO HwDeviceExtension,
                               ScrnInfoPtr pScrn, DisplayModePtr mode, BOOLEAN IsCustom);
extern BOOLEAN	SiSUSBSetMode(SiS_Private *SiS_Pr, PSIS_HW_INFO HwDeviceExtension,
                           ScrnInfoPtr pScrn, USHORT ModeNo, BOOLEAN dosetpitch);
extern DisplayModePtr SiSUSBBuildBuiltInModeList(ScrnInfoPtr pScrn, BOOLEAN includelcdmodes,
					      BOOLEAN isfordvi, BOOLEAN fakecrt2modes);
/* End of init.c, init301.c ----- */






