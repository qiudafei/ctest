/*------------------------------------------------------------------
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This module is a wrapper of GIFLIB routines and functions.

The GIFLIB distribution is Copyright (c) 1997  Eric S. Raymond
                SPDX-License-Identifier: MIT


Midas-Zhou
------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "egi_gif.h"
#include "egi_common.h"

#define GIF_MAX_CHECKSIZE	32  /* in Mbytes, 1024*1024*GIF_MAX_CHECKSIZE bytes,
				     * For checking GIF data size */

static void PrintGifError(int ErrorCode);
static void egi_gif_FreeSavedImages(SavedImage **psimg, int ImageCount);

/*****************************************************************************
 Same as fprintf to stderr but with optional print.
******************************************************************************/
static void GifQprintf(char *Format, ...)
{
bool GifNoisyPrint = true;

    va_list ArgPtr;

    va_start(ArgPtr, Format);

    if (GifNoisyPrint) {
        char Line[128];
        (void)vsnprintf(Line, sizeof(Line), Format, ArgPtr);
        (void)fputs(Line, stderr);
    }

    va_end(ArgPtr);
}


static void PrintGifError(int ErrorCode)
{
    const char *Err = GifErrorString(ErrorCode);

    if (Err != NULL)
        printf("GIF-LIB error: %s.\n", Err);
    else
        printf("GIF-LIB undefined error %d.\n", ErrorCode);
}


/*---------------------------------------------------------------------------------------
Read a GIF file and display images one by one, Meanwhile, the corresponding ImageCount
is passed to the caller. If Silent_Mode is TRUE, then only ImageCount is passed out,
Finally, ImageCount gets to the total number of images in the GIF file.

Note:
1. This function is for big size GIF file, it reads in and display frame by frame.
   For small size GIF file, just call egi_gif_slurpFile() instead.
2. The passed ImageCount starts from 1, and ends with the total number of images.

@fpath: File path
@Silent_Mode:	 TRUE: Do NOT display and delay, just read frame by frame and pass
		       image count to the caller.
		 FALE: Do display and delay.

@ImgTransp_ON:    (User define)

		 Usually to be set as TRUE.
		 Suggest to turn OFF only when no transparency setting in the GIF file,
		 to show SBackGroundColor.

@ImageCount:	 Counting number of images that have been displayed/parsed.
		 If NULL, ignore.

Return:
	0	OK
	<0	Fails
        >0	Final DGifCloseFile() fails
-----------------------------------------------------------------------------------------*/
int  egi_gif_readFile(const char *fpath, bool Silent_Mode, bool ImgTransp_ON, int *ImageCount)
{
    int Error=0;
    int	i, j, Size;
    int Width=0, Height=0;  /* image block size */
    int Row=0, Col=0;	    /* init. as image block offset relative to screen/canvvas */
    int ExtCode, Count;
    int DelayMs;	    /* delay time in ms */
    int DelayFact;	    /* adjust delay time according to block image size */

    GifFileType *GifFile=NULL;
    GifRecordType RecordType;
    GifByteType *Extension=NULL;
    GifRowType *ScreenBuffer=NULL;  /* typedef unsigned char *GifRowType */
    int
	InterlacedOffset[] = { 0, 4, 2, 1 }, /* The way Interlaced image should. */
	InterlacedJumps[] = { 8, 8, 4, 2 };    /* be read - offsets and jumps... */
    int ImageNum = 0;
    ColorMapObject *ColorMap=NULL;
    GraphicsControlBlock gcb;

    int  SWidth=0, SHeight=0;   /* screen(gif canvas) width and height, defined in gif file */
    int  offx=0, offy=0;	/* gif block image width and height, defined in gif file */


    bool Is_ImgColorMap=false;   /* TRUE If color map is image color map, NOT global screen color map
				    Defined in gif file. */
    int  Disposal_Mode=0;	   /* Defined in gif file:
					0. No disposal specified
				 	1. Leave image in place
					2. Set area to background color(image)
					3. Restore to previous content
				    */
    int  trans_color=-1;        /* Palette index for transparency, -1 if none, or NO_TRANSPARENT_COLOR
				 * Defined in gif file. */
    int  user_trans_color=-1;   /* -1, User defined palette index for the transparency */
    bool Bkg_Transp=false;      /* If true, make backgroud transparent. User define. */
    int  bkg_color=0;	  	/* Back ground color index, defined in gif file. */


    /* Open gif file */
    if((GifFile = DGifOpenFileName(fpath, &Error)) == NULL) {
	    PrintGifError(Error);
	    return -1;
    }
    if(GifFile->SHeight == 0 || GifFile->SWidth == 0) {
	sprintf("%s: Image width or height is 0\n",__func__);
	Error=-2;
	goto END_FUNC;
    }

    /* Get GIF version*/
    printf("GIF Verion: %s\n", DGifGetGifVersion(GifFile));

    /* Get global color map */
#if 1
    if(GifFile->Image.ColorMap) {
	Is_ImgColorMap=true;
	ColorMap=GifFile->Image.ColorMap;
		printf("GIF Image Colorcount=%d\n", GifFile->Image.ColorMap->ColorCount);
    }
    else {
	Is_ImgColorMap=false;
	ColorMap=GifFile->SColorMap;
	printf("GIF Global Colorcount=%d\n", GifFile->SColorMap->ColorCount);
    }
    printf("GIF ColorMap.BitsPerPixel=%d\n", ColorMap->BitsPerPixel);

    /* check that the background color isn't garbage (SF bug #87) */
    if (GifFile->SBackGroundColor < 0 || GifFile->SBackGroundColor >= ColorMap->ColorCount) {
        sprintf("%s: Background color out of range for colorMap.\n",__func__);
	Error=-3;
	goto END_FUNC;
    }
#endif

    /* Get back ground color */
    bkg_color=GifFile->SBackGroundColor;

    /* get SWidth and SHeight */
    SWidth=GifFile->SWidth;
    SHeight=GifFile->SHeight;

    printf("GIF Background color index=%d\n", bkg_color);
    printf("GIF SColorResolution = %d\n", (int)GifFile->SColorResolution);
    printf("GIF SWidthxSHeight=%dx%d ---\n", SWidth, SHeight);
    //printf("GIF ImageCount=%d\n", GifFile->ImageCount); /* 0 */

    /***   ---   Allocate screen as vector of column and rows   ---
     * Note this screen is device independent - it's the screen defined by the
     * GIF file parameters.
     */
    if ((ScreenBuffer = (GifRowType *)malloc(GifFile->SHeight * sizeof(GifRowType))) == NULL)
    {
	printf("%s: Failed to allocate memory required, aborted.",__func__);
	return -4;
    }

    Size = GifFile->SWidth * sizeof(GifPixelType); /* Size in bytes one row.*/
    if ((ScreenBuffer[0] = (GifRowType) malloc(Size)) == NULL) { /* First row. */
	printf("%s: Failed to allocate memory required, aborted.",__func__);
	Error=-5;
	goto END_FUNC;
    }

    for (i = 0; i < GifFile->SWidth; i++)  /* Set its color to BackGround. */
	ScreenBuffer[0][i] = GifFile->SBackGroundColor;
    for (i = 1; i < GifFile->SHeight; i++) {
	/* Allocate the other rows, and set their color to background too: */
	if ((ScreenBuffer[i] = (GifRowType) malloc(Size)) == NULL) {
	      	printf("%s: Failed to allocate memory required, aborted.",__func__);
		Error=-6;
		goto END_FUNC;
	}
	memcpy(ScreenBuffer[i], ScreenBuffer[0], Size);
    }

    ImageNum = 0;
    if(ImageCount != NULL)
	*ImageCount=0;
    /* Scan the content of the GIF file and load the image(s) in: */
    do {

	if (DGifGetRecordType(GifFile, &RecordType) == GIF_ERROR) {
	    PrintGifError(GifFile->Error);
	    Error=-6;
	    goto END_FUNC;
	}

	/*---------- GIFLIB DEFINED RECORD TYPE ---------
	    UNDEFINED_RECORD_TYPE,
	    SCREEN_DESC_RECORD_TYPE,
	    IMAGE_DESC_RECORD_TYPE,    Begin with ','
	    EXTENSION_RECORD_TYPE,     Begin with '!'
	    TERMINATE_RECORD_TYPE      Begin with ';'
 	------------------------------------------------*/
	switch (RecordType) {
	    case IMAGE_DESC_RECORD_TYPE:
		if (DGifGetImageDesc(GifFile) == GIF_ERROR) {
		    PrintGifError(GifFile->Error);
		    Error=-7;
		    goto END_FUNC;
		}
		Row = GifFile->Image.Top; /* Image Position relative to Screen. */
		Col = GifFile->Image.Left;

		/* Get offx, offy, Original Row AND Col will increment later!!!  */
		offx = Col;
		offy = Row;
		Width = GifFile->Image.Width;
		Height = GifFile->Image.Height;

		/* check block image size and position */
    		printf("\nGIF ImageCount=%d\n", GifFile->ImageCount);

		GifQprintf("GIF Image %d at (%d, %d) [%dx%d]:     ",
		    	     			++ImageNum, Col, Row, Width, Height);

		if (GifFile->Image.Left + GifFile->Image.Width > GifFile->SWidth ||
		   GifFile->Image.Top + GifFile->Image.Height > GifFile->SHeight) {
		    printf("%s: Image %d is not confined to screen dimension, aborted.\n",__func__, ImageNum);
		    Error=-8;
		    goto END_FUNC;
		}

		/* Get color (index) data */
		if (GifFile->Image.Interlace) {
//		    printf(" Interlaced image data ...\n");
		    /* Need to perform 4 passes on the images: */
		    for (Count = i = 0; i < 4; i++)
			for (j = Row + InterlacedOffset[i]; j < Row + Height;
						 j += InterlacedJumps[i]  )
			{
			    GifQprintf("\b\b\b\b%-4d", Count++);
			    if ( DGifGetLine(GifFile, &ScreenBuffer[j][Col],
				 Width) == GIF_ERROR )
			    {
				 PrintGifError(GifFile->Error);
				 Error=-9;
				 goto END_FUNC;
			    }
			}
		}
		else {
//		    printf(" Noninterlaced image data ...\n");
		    for (i = 0; i < Height; i++) {
			GifQprintf("\b\b\b\b%-4d", i);
			if (DGifGetLine(GifFile, &ScreenBuffer[Row++][Col],
				Width) == GIF_ERROR) {
			    PrintGifError(GifFile->Error);
			    Error=-10;
			    goto END_FUNC;
			}
		    }
		}
		printf("\n");

	       /* Get color map, colormap may be updated/changed here! */
	       if(GifFile->Image.ColorMap) {
			Is_ImgColorMap=true;
			ColorMap=GifFile->Image.ColorMap;
//			printf("GIF Image ColorCount=%d\n", GifFile->Image.ColorMap->ColorCount);
    	       }
	       else {
			Is_ImgColorMap=false;
			ColorMap=GifFile->SColorMap;
//			printf("GIF Global Colorcount=%d\n", GifFile->SColorMap->ColorCount);
	       }
//	       printf("GIF ColorMap.BitsPerPixel=%d\n", ColorMap->BitsPerPixel);

		/* --- Display --- */
		if(!Silent_Mode) {
	 //  		display_gif(Width, Height, offx, offy, ColorMap, ScreenBuffer);
			/* hold on ... see displaying delay in case EXTENSION_RECORD_TYPE */
			//usleep(55000);
		}

		break;

	    case EXTENSION_RECORD_TYPE:
		/*  extension blocks in file: */
		if (DGifGetExtension(GifFile, &ExtCode, &Extension) == GIF_ERROR) {
		    PrintGifError(GifFile->Error);
		    Error=-11;
		    goto END_FUNC;
		}

		/*--------------------------------------------------------------
		 *             ( FUNC CODE defined in GIFLIB )
		 *   CONTINUE_EXT_FUNC_CODE    	0x00  continuation subblock
		 *   COMMENT_EXT_FUNC_CODE     	0xfe  comment
		 *   GRAPHICS_EXT_FUNC_CODE    	0xf9  graphics control (GIF89)
		 *   PLAINTEXT_EXT_FUNC_CODE   	0x01  plaintext
		 *   APPLICATION_EXT_FUNC_CODE 	0xff  application block (GIF89)
		 --------------------------------------------------------------*/

		/*   --- parse extension code ---  */
                if (ExtCode == GRAPHICS_EXT_FUNC_CODE) {
                     if (Extension == NULL) {
                            printf("Invalid extension block\n");
                            GifFile->Error = D_GIF_ERR_IMAGE_DEFECT;
                            PrintGifError(GifFile->Error);
			    Error=-12;
			    goto END_FUNC;
                     }
                     if (DGifExtensionToGCB(Extension[0], Extension+1, &gcb) == GIF_ERROR) {
                            PrintGifError(GifFile->Error);
			    Error=-13;
			    goto END_FUNC;
                      }
		      /* ----- for test ---- */
		      #if 0
                      printf("Transparency on: %s\n",
                      			gcb.TransparentColor != -1 ? "yes" : "no");
                      printf("Transparent Index: %d\n", gcb.TransparentColor);
                      printf("DelayTime: %d\n", gcb.DelayTime);
		      printf("DisposalMode: %d\n", gcb.DisposalMode);
		      #endif

		      /* Get disposal mode */
		      Disposal_Mode=gcb.DisposalMode;
      		      /* Get trans_color */
		      trans_color=gcb.TransparentColor;

		      /* >200x200 no delay, 0 full delay */
		      if(Width*Height > 40000)
			DelayFact=0;
		      else
		      	DelayFact= (200*200-Width*Height);

		      /* Get delay time in ms, and delay */
		      if(!Silent_Mode)
		      {
		      	  #if 1
		          DelayMs=gcb.DelayTime*10*DelayFact/40000;
		          sleep(DelayMs/1000);
		          usleep((DelayMs%1000)*1000);
		          #else
			  usleep(50000);
		          #endif
		      }

		 } /* if END:  GRAPHICS_EXT_FUNC_CODE */

		/* Read out next extension and discard, TODO: parse it.. */
		while (Extension!=NULL) {
                    if (DGifGetExtensionNext(GifFile, &Extension) == GIF_ERROR) {
                        PrintGifError(GifFile->Error);
			Error=-14;
			goto END_FUNC;
                    }
                    if (Extension == NULL) {
			//printf("Extension is NULL\n");
                        break;
		    }
                }

		break;

	    case TERMINATE_RECORD_TYPE:
		break;

	    /* TODO: other record type */
	    default:		    /* Should be trapped by DGifGetRecordType. */
		break;
	}

	/* Pass ImageCount */
	if(ImageCount != NULL)
		*ImageCount=ImageNum;

    } while (RecordType != TERMINATE_RECORD_TYPE);


END_FUNC:
    /* Free scree buffers */
    if(ScreenBuffer != NULL) {
	for (i = 0; i < SHeight; i++)
		free(ScreenBuffer[i]);
    	free(ScreenBuffer);
    }

    /* Close file */
    if (DGifCloseFile(GifFile, &Error) == GIF_ERROR) {
	PrintGifError(Error);
	return Error;
    }

    return Error;
}


/*------------------------------------------------------------------------------
Slurp a GIF file and load all image data to an EGI_GIF struct.

@fpath: File path

@ImgTransp_ON:    (User define)
		 Usually set as TRUE, even for a nontransparent GIF.

		 To make the GIF gcb.TransparentColor transparent to its
		 background image by resetting its alpha value of EGI_GIF.Simgbuf to 0!

		 NOTE: GIF Transparency_on dosen't mean the GIF image itself is transparent!
		   it refers to gcb.TransparentColor for pixles in each sequence block images!
		   the purpose of gcb.TransparentColor is to keep previous pixel color/alpha unchaged,
		   and it's NOT necessary to make backgroup of the whole GIF image!
		   Only if you turn on ImgTransp_ON and all or some of its images' Disposal_Mode is 2!

		   A transparent GIF is applied only if it is so designed, and its SBackGroundColor
		   is usually also transparent(BUT not necessary!), so the background of the GIF
		   image is visible there.

 		 TRUE:  Alpha value of transparent pixels in each sequence block images
			will be set to 0, and draw_dot() will NOT be called for them,
			so it will speed up displaying!
			Only when all or some of its images' Disposal_Mode is 2!

		 FALSE: SBackGroundColor will take effect.
                        draw_dot() will be called to draw every dot in each block image.

Return:
	A pointer to EGI_GIF	OK
	NULL			Fail
--------------------------------------------------------------------------------*/
EGI_GIF*  egi_gif_slurpFile(const char *fpath, bool ImgTransp_ON)
{
    EGI_GIF* egif=NULL;
    GifFileType *GifFile;
    GifRecordType RecordType;
    int Error;
    int check_size;
    GifByteType *ExtData;
    int ExtFunction;
    int ImageTotal=0;

    GifColorType *ColorMapEntry;
    EGI_16BIT_COLOR img_bkcolor;

    /* Try to get total number of images, for size check */
    if( egi_gif_readFile(fpath, true, true, &ImageTotal) !=0 ) /* fpath, Silent, ImgTransp_ON, *ImageCount */
    {
	printf("%s: Fail to egi_gif_readFile() '%s'\n",__func__,fpath);
	return NULL;
    }

    /* Open gif file */
    if ((GifFile = DGifOpenFileName(fpath, &Error)) == NULL) {
	    PrintGifError(Error);
	    return NULL;
    }

    /* Big Size WARNING! */
    printf("%s: GIF SHeight*SWidth*ImageCount=%d*%d*%d \n",__func__,
						GifFile->SHeight,GifFile->SWidth,ImageTotal );
    check_size=GifFile->SHeight*GifFile->SWidth*ImageTotal;
    if( check_size > 1024*1024*GIF_MAX_CHECKSIZE )
    {
	printf("%s: GIF check_size > 1024*1024*%d, stop slurping! \n", __func__, GIF_MAX_CHECKSIZE );
    	if (DGifCloseFile(GifFile, &Error) == GIF_ERROR)
        	PrintGifError(Error);
	return NULL;
    }

    /* calloc EGI_GIF */
    egif=calloc(1, sizeof(EGI_GIF));
    if(egif==NULL) {
	    printf("%s: Fail to calloc EGI_GIF.\n", __func__);
	    return NULL;
    }


    /* Slurp reads an entire GIF into core, hanging all its state info off
     *  the GifFileType pointer, it may take a short of time...
     */
    printf("%s: Slurping GIF file and put images to memory...\n", __func__);
    if (DGifSlurp(GifFile) == GIF_ERROR) {
	PrintGifError(GifFile->Error);
    	if (DGifCloseFile(GifFile, &Error) == GIF_ERROR)
        	PrintGifError(Error);
	free(egif);
	return NULL;
    }

    /* check sanity */
    if( GifFile->ImageCount < 1 ) {
    	if (DGifCloseFile(GifFile, &Error) == GIF_ERROR)
        	PrintGifError(Error);
        free(egif);
	return NULL;
    }

    /* Get GIF version*/
    if( strcmp( "98",EGifGetGifVersion(GifFile) ) >= 0 )
	egif->VerGIF89=true;  /* GIF89 */
    else
	egif->VerGIF89=false; /* GIF87 */

    /* xxx */
    egif->ImgTransp_ON=ImgTransp_ON;

    /* Assign EGI_GIF members. Ownership to be transfered later...*/
    egif->SWidth =       GifFile->SWidth;
    egif->SHeight =      GifFile->SHeight;
    egif->SColorResolution =     GifFile->SColorResolution;
    egif->SBackGroundColor =     GifFile->SBackGroundColor;
    egif->AspectByte =   GifFile->AspectByte;
    egif->SColorMap  =   GifFile->SColorMap;
    egif->ImageCount =   0;
    egif->ImageTotal =  GifFile->ImageCount; 	/* after slurp, GifFile->ImageCount is total number */
    egif->SavedImages=   GifFile->SavedImages;
    egif->ExtensionBlockCount = GifFile->ExtensionBlockCount;

    #if 1
    printf("%s --- GIF Params---\n",__func__);
    printf("		Version: %s", egif->VerGIF89 ? "GIF89":"GIF87");
    printf("		SWidth x SHeight: 	%dx%d\n", egif->SWidth, egif->SHeight);
    printf("		SColorMap:		%s\n", egif->SColorMap==NULL ? "No":"Yes" );
    printf("   		SColorResolution: 	%d\n", egif->SColorResolution);
    printf("   		SBackGroundColor: 	%d\n", egif->SBackGroundColor);
    printf("   		AspectByte:		%d\n", egif->AspectByte);
    printf("   		ImageTotal:       	%d\n", egif->ImageTotal);
    #endif

    /* --- initiate Simgbuf --- */

    /* get bkg color, ---- NOT necessary??? ---- */
    ColorMapEntry = &(egif->SColorMap->Colors[egif->SBackGroundColor]);
    img_bkcolor=COLOR_RGB_TO16BITS( ColorMapEntry->Red,
                                    ColorMapEntry->Green,
                                    ColorMapEntry->Blue );

    /* create imgbuf, with bkg alpha == 0!!!*/
    egif->Simgbuf=egi_imgbuf_create(egif->SHeight, egif->SWidth,
					egif->ImgTransp_ON==true ? 0:255, img_bkcolor); //height, width, alpha, color
    if(egif->Simgbuf==NULL) {
	printf("%s: Fail to create Simgbuf!\n", __func__);
	/* close GIF file */
	if (DGifCloseFile(GifFile, &Error) == GIF_ERROR) {
       		PrintGifError(Error);
  	}

        free(egif);
        return NULL;
    }

    /* Decouple gif file handle, with respect to DGifCloseFile().
     * Ownership transfered from GifFile to EGI_GIF!
     */
    GifFile->SColorMap      =NULL;
    GifFile->SavedImages    =NULL;
    /* Note: Ownership of GifFile->ExtensionBlocks and GifFile->Image NOT transfered!
     * it will be freed by DGifCloseFile().
     */

    /* Now we can close GIF file, and let EGI_GIF to hold the data. */
    if (DGifCloseFile(GifFile, &Error) == GIF_ERROR) {
        PrintGifError(Error);
	// ...carry on ....
    }

    return egif;

}


/*-----------------------------------------------------------
Free array of struct SavedImage.

@sp:	Poiter to first SavedImage of the array.
@ImageCount: Total umber of  SavedImages
-----------------------------------------------------------*/
static void  egi_gif_FreeSavedImages(SavedImage **psimg, int ImageTotal)
{
    int i;
    SavedImage* sp;

    if(psimg==NULL || *psimg==NULL || ImageTotal < 1 )
	return;

    sp=*psimg;

    /* free array of SavedImage */
    for ( i=0; i<ImageTotal; i++ ) {
        if (sp->ImageDesc.ColorMap != NULL) {
            GifFreeMapObject(sp->ImageDesc.ColorMap);
            sp->ImageDesc.ColorMap = NULL;
        }

        if (sp->RasterBits != NULL)
            free((char *)sp->RasterBits);

        GifFreeExtensions(&sp->ExtensionBlockCount, &sp->ExtensionBlocks);

	sp++; /* move to next SavedImage */
    }

    free(*psimg); /* free itself! */
    *psimg=NULL;
}


/*-----------------------------
Free An EGI_GIF struct.
------------------------------*/
void egi_gif_free(EGI_GIF **egif)
{
   if(egif==NULL || *egif==NULL)
	return;

    /* Free EGI_GIF, Note: Same procedure as per DGifCloseFile()  */
    if ((*egif)->SColorMap) {
        GifFreeMapObject((*egif)->SColorMap);
        (*egif)->SColorMap = NULL;
    }
    egi_gif_FreeSavedImages(&(*egif)->SavedImages, (*egif)->ImageTotal);

    /* free imgbuf */
    if ((*egif)->Simgbuf != NULL ) {
	egi_imgbuf_free((*egif)->Simgbuf);
	(*egif)->Simgbuf=NULL;
    }

   /* free itself */
   free(*egif);
   *egif=NULL;
}



/*---------------------------------------------------------------------------------------
Update Simgbuf with raster data of a sequence GIF frame and its colormap. If fbdev is
not NULL, then display the Simgbuf.

NOTE:
1. The sequence image raster data will first write to Simgbuf, which functions as GIF canvas,
   then write to FB/(FB buffer) to display the whole canvas content.

2. For Disposal_Mode 2 && DirectFB_OFF:
   !!! XXX it will uses FB FILO to restore the original screen image before displaying
   each GIF sequence image. So if Disposal_Mode changes it will mess up then  XXX !!!
   So use FB buff memcpy to restore image!


@fbdev:		FBDEV.
		If NULL, update Simgbuf, but do NOT write to FB.
@Simgbuf:	An EGI_IMGBUF as for GIF screen/canvas.
		Size SWidth x SHeight.
		Hooked to an EGI_GIF struct.

@Disposal_Mode:  How to dispose current block image ( Defined in GIF extension control blocks )
                                     0. No disposal specified
                                     1. Leave image in place
                                     2. Set area to background color/(OR background image)
                                     3. Restore to previous content (Rarely applied)

@xp,yp:		The origin of displaying image block relative to Simgbuf(virtual GIF canvas).
@xw,yw:         Displaying window origin, relate to the LCD coord system.
@winw,winh:     width and height(row/column for fb) of the displaying window.
                !!! Note: You'd better set winw,winh not exceeds acutual LCD size, or it will
                waste time calling draw_dot() for pixels outsie FB zone. --- OK

@BWidth, BHeight:  Width and Heigh for current GIF frame/block image size.  <= SWidth,SHeigh.
@offx, offy:	 offset of current image block relative to GIF virtual canvas left top point.
@ColorMap:	 Color map for current image block. (Global or Local)
@buffer:         Gif block image rasterbits, as palette index.

@trans_color:    Transparent color index ( Defined in GIF extension control blocks )
		 <0, disable. or NO_TRANSPARENT_COLOR(=-1)

@User_TransColor:   (User define)  --- FOR TEST ONLY ---
		 Effective only ImgTransp_ON is true!
                 Palette index for transparency, defined by the user.
		 <0, disable.
                 When ENABLE, you shall use FB FILO or FB buff page copy! or moving trails
                 will overlapped! BUT, BUT!!! If image Disposal_Mode != 2,then the image
                 will mess up!!!
                 To check imageframe[0] for wanted user_trans_color index!, for following imageframe[]
                 usually contains trans_color only !!!

@bkg_color:	 Back ground color index, ( Defined in GIF file header )
		 <0, disable. (For comparision)
		 This byte is only meaningful if the global color table flag is 1, and if there is no
		 global color table, this byte should be 0.

@DirectFB_ON:    (User define)  	-- FOR TEST ONLY --
		 Normally to be FALSE, or for nontransparent(transparency off) GIF.
		 This will NOT affect Simgbuf.
                 If TRUE: write data to FB directly, without FB working buffer.
                       --- NOTE ---
                 1. DirectFB_ON mode MUST NOT use FB_FILO !!! for GIF is a kind of
                    overlapping image operation, FB_FILO is just agains it!
                 2  !!! Turning ON will make a transparent GIF nontransparent! !!!
                 3. Turn ON for samll size (maybe)and nontransparent GIF images
                    to speed up writing!!
                 4. For big size image, it may result in tear lines and flash(by FILO OP)
                    on the screen.
                 5. For images with transparent area, it MAY flash with bkgcolor!(by FILO OP)

xxxxx @BkgTransp_ON:     (User define) Usually it's FALSE!  trans_color will cause the same effect?
                 If TRUE: make backgroud color transparent.
                 WARN!: The GIF should has separated background color from image content,
                        otherwise the same color index in image will be transparent also!

-------------------------------------------------------------------------------------------*/
inline static void egi_gif_rasterWriteFB( FBDEV *fbdev, EGI_IMGBUF *Simgbuf,int Disposal_Mode,
				   int xp, int yp, int xw, int yw, int winw, int winh,
				   int BWidth, int BHeight, int offx, int offy,
		  		   ColorMapObject *ColorMap, GifByteType *buffer,
				   int trans_color, int User_TransColor, int bkg_color,
				   bool ImgTransp_ON ) 			// bool BkgTransp_ON )
{
    /* check params */
    if( Simgbuf==NULL || ColorMap==NULL || buffer==NULL )
		return;

    int i,j;
    int pos=0;
    int spos=0;
    GifColorType *ColorMapEntry;
    int SWidth=Simgbuf->width;		/* Screen/canvas size */
    int SHeight=Simgbuf->height;
    EGI_16BIT_COLOR bkcolor;		/* 16bit color, NOT color index */

   /* Limit Screen width x heigh, necessary? */
   if(BWidth==SWidth)offx=0;
   if(BHeight==SHeight)offy=0;
   //printf("%s: input Height=%d, Width=%d offx=%d offy=%d\n", __func__, Height, Width, offx, offy);

    /* get mutex lock -------------- >>>>>  */
    if(pthread_mutex_lock(&Simgbuf->img_mutex) !=0){
                printf("%s: Fail to lock imgbuf mutex!\n",__func__);
                return;
    }

    /* update Simgbuf */
    for(i=0; i<BHeight; i++)
    {
	 /* update block of Simgbuf */
	 for(j=0; j<BWidth; j++ ) {
	      pos=i*BWidth+j;
              spos=(offy+i)*SWidth+(offx+j);
	      /* Nontransparent color: set color and set alpha to 255 */
	      if( trans_color < 0 || trans_color != buffer[pos]  )
              {
	          ColorMapEntry = &ColorMap->Colors[buffer[pos]];
                  Simgbuf->imgbuf[spos]=COLOR_RGB_TO16BITS( ColorMapEntry->Red,
							    ColorMapEntry->Green,
							    ColorMapEntry->Blue);
		  /* set alpha  */
	  	  Simgbuf->alpha[spos]=255;
	      }

        /* Just after above...!
	 * Instead of leaving previous color/alpha unchanged, ImgTransp_ON will reset alpha of trans_color
         * to 0 while Disposal_Mode==2, so it will be transparent to the back ground image!
         * BUT for Disposal_Mode==1, it only keep previous color/alpha unchanged.
	      */
	      if(ImgTransp_ON && Disposal_Mode==2) {
	          if( buffer[pos] == trans_color || buffer[pos] == User_TransColor) {   /* ???? if trans_color == bkg_color */
		      Simgbuf->alpha[spos]=0;
	          }
	      }
	      /* Make background color transparent xxxx -- NOT APPLICABLE ! */
	      /* bkg_color meaningful ONLY when global color table exists */
	      //if( BkgTransp_ON && ( buffer[pos] == bkg_color) ) {
	      //	      Simgbuf->alpha[spos]=0;
	      // }
	      /*  ELSE : Keep old color and alpha value in Simgbuf->imgbuf */
          }
    }

    /*  <<<--------- put mutex lock */
    pthread_mutex_unlock(&Simgbuf->img_mutex);

    /* --- Return if FBDEV is NULL --- */
    if(fbdev == NULL) {
	return;
    }

    /* display the whole Gif screen/canvas */
    egi_imgbuf_windisplay( Simgbuf, fbdev, -1,              /* img, fb, subcolor */
			   xp, yp,				/* xp, yp */
			   xw, yw,				/* xw, yw */
                     	   winw, winh   /* winw, winh */
			);

}


/*---------------------------------------------------------------------------------------------------
Display current frame of GIF, then increase count of EGI_GIF, If dev==NULL, then update eigf->Simgbuf
and return!
Note: If dev is NOT NULL, it will affect whole FB data with memcpy and page fresh operation!


@dev:	     	 FB Device.
		 If NULL, update egif->Simgbuf, but will DO NOT write to FB.
@egif:	     	 The subjective EGI_GIF.
@nloop:		 loop times for each function call:
                 <0 : loop displaying GIF frames forever
		 0  : display one frame only, and then ImageCount++.
		 >0 : nloop times

@DirectFB_ON:    (User define) !!!-- FOR TEST ONLY --
                 If TRUE: write data to FB directly, without FB buffer.
		 See NOTE of egi_gif_rasterWriteFB() for more details

@User_DisposalMode:	(User define) !!!-- FOR TEST ONLY --
		 Disposal mode imposed by user, it will prevail GIF file disposal mode.
		 =0,1,2,3.
		 <0, ignore.

@User_TransColor:   (User define) !!! --- FOR TEST ONLY ---
		 Effective only egif->ImgTransp_ON is true!
                 Palette index for the transparency
		 <0, ignore.

@User_BkgColor:   (User define)  !!! --- FOR TEST ONLY ---
		 GIF canvans backgroup color
		 <0, ignore.

@xp,yp:         The origin of GIF canvas relative to FB/LCD coordinate system.
@xw,yw:         Displaying window origin, relate to the LCD coord system.
@winw,winh:     width and height(row/column for fb) of the displaying window.
                !!! Note: You'd better set winw,winh not exceeds acutual LCD size, or it will
                waste time calling draw_dot() for pixels outsie FB zone. --- OK

----------------------------------------------------------------------------------------------------*/
void egi_gif_displayFrame( FBDEV *fbdev, EGI_GIF *egif, int nloop, bool DirectFB_ON,
					   	int User_DisposalMode, int User_TransColor, int User_BkgColor,
	   			   		int xp, int yp, int xw, int yw, int winw, int winh )
{
    int i,j,k;
    int BWidth=0, BHeight=0;  	/* image block size */
    int offx, offy;		/* image block offset relative to gif canvas */
    int DelayMs=0;            	/* delay time in ms */

    SavedImage *ImageData;      /*  savedimages EGI_GIF */
    ExtensionBlock  *ExtBlock;  /*  extension block in savedimage */
    ColorMapObject *ColorMap;   /*  Color map */
    GraphicsControlBlock gcb;   /* extension control block */

    bool Is_LocalColorMap;	/* Local colormap or global colormap */
    int  Disposal_Mode=0;       /*** Defined in GIF extension control block:
				   Actions after displaying:
                                   0. No disposal specified
                                   1. Leave image in place
                                   2. Set area to background color(image)
                                   3. Restore to previous content
				*/
//  GifByteType *buffer; 	/* For METHOD_2 CODE BLOCK */
    GifColorType *ColorMapEntry;

    int bkg_color=-1;		/* color index */
    EGI_16BIT_COLOR bkcolor=0;	/* 16bit color, NOT color index. default is BLACK
				 * Applicable only if global colorMap exists!
				 */
    int trans_color=NO_TRANSPARENT_COLOR;  /* color index */
    int spos;

    /* sanity check */
    if( egif==NULL || egif->SavedImages==NULL ) {
	printf("%s: input EGI_GIF(data) is NULL.\n", __func__);
	return;
    }

    /* check ImageCount */
    if( egif->ImageCount > egif->ImageTotal-1)
		egif->ImageCount=0;

     /* Impose User_BkgColor, !!! If Local colorMap applied, then this is NOT applicable !!!  */
     if( User_BkgColor >=0 )
		bkg_color=User_BkgColor;
     else
		bkg_color=egif->SBackGroundColor;

     /* Get bkcolor only if global colorMap exists, or use default bkcolor=0 */
     if(egif->SColorMap) {
	   	ColorMapEntry = &egif->SColorMap->Colors[bkg_color];   /* bkg_color: index */
     	   	bkcolor=COLOR_RGB_TO16BITS( ColorMapEntry->Red,
              		                    ColorMapEntry->Green,
      	        		            ColorMapEntry->Blue );
     }

 /* Do nloop times, or just one frame if nloop==0 */
 k=0;
 do {

     /* Get saved image data sequence */
     ImageData=&egif->SavedImages[egif->ImageCount];
//   buffer=ImageData->RasterBits;  /* For METHOD_2 CODE BLOCK */

     /* Get image colorMap */
     if(ImageData->ImageDesc.ColorMap) {
	Is_LocalColorMap=true;
        ColorMap=ImageData->ImageDesc.ColorMap;
	printf("  --->  Local colormap: Colorcount=%d   <--\n", ColorMap->ColorCount);
     }
     else {  /* Apply global colormap */
	Is_LocalColorMap=false;
	ColorMap=egif->SColorMap;
     }

     /* Reset Simgbuf and working FB buffer, for the first block image */
     if( egif->ImageCount==0 && fbdev != NULL ) {
	  ColorMapEntry = &ColorMap->Colors[bkg_color];
	  bkcolor=COLOR_RGB_TO16BITS( ColorMapEntry->Red,
                                      ColorMapEntry->Green,
               	                      ColorMapEntry->Blue );

	  egi_imgbuf_resetColorAlpha(egif->Simgbuf, bkcolor, egif->ImgTransp_ON ? 0:255 );

     	  memcpy(fbdev->map_bk, fbdev->map_buff+fbdev->screensize, fbdev->screensize);
     }


     #if 1	/* For TEST */
     printf("\n Get ImageData %d of [0-%d] ... \n", 	egif->ImageCount, egif->ImageTotal-1);
     printf("ExtensionBlockCount=%d\n", 	ImageData->ExtensionBlockCount);
     printf("ColorMap.SortFlag:	%s\n", 		ColorMap->SortFlag?"YES":"NO");
     printf("ColorMap.BitsPerPixel:	%d\n", 	ColorMap->BitsPerPixel);
     #endif

     /* get Block image offset and size */
     offx=ImageData->ImageDesc.Left;
     offy=ImageData->ImageDesc.Top;
     BWidth=ImageData->ImageDesc.Width;
     BHeight=ImageData->ImageDesc.Height;
     egif->offx=offx;
     egif->offy=offy;
     egif->BWidth=BWidth;
     egif->BHeight=BHeight;

     printf("Block: offset(%d,%d) size(%dx%d) \n", offx, offy, BWidth, BHeight);

     /* Get extension block */
     if( ImageData->ExtensionBlockCount > 0 )
     {
	/* Search for control block */
	for( j=0; j< ImageData->ExtensionBlockCount; j++ )
	{
	    ExtBlock=&ImageData->ExtensionBlocks[j];

	    switch(ExtBlock->Function)
	    {

	   /***   	       ---  Extension Block Classification ---
	    *
	    * 	0x00 - 0x7F(0-127):     the Graphic Rendering Blocks, excluding the Trailer(0x3B,59)
	    *	0x80 - 0xF9(128-249): 	the Control Blocks
	    *   0xFA - 0xFF:		the Special Purpose Blocks
	    *
	    *		      ( FUNC CODE defined in GIFLIB )
	    *	CONTINUE_EXT_FUNC_CODE    	0x00  continuation subblock
	    *   COMMENT_EXT_FUNC_CODE     	0xfe  comment
	    *   GRAPHICS_EXT_FUNC_CODE    	0xf9  graphics control (GIF89)
	    *	PLAINTEXT_EXT_FUNC_CODE   	0x01  plaintext
	    *	APPLICATION_EXT_FUNC_CODE 	0xff  application block (GIF89)
	    */

		case GRAPHICS_EXT_FUNC_CODE:
			printf(" --- GRAPHICS_EXT_FUNC_CODE ---\n");
	       		if (DGifExtensionToGCB(ExtBlock->ByteCount, ExtBlock->Bytes, &gcb) == GIF_ERROR)
			{
        	                printf("%s: DGifExtensionToGCB() Fails!\n",__func__);
	        	   	continue;
			 }
        	        /* ----- for test ---- */
	                #if 1
        	        printf("Transparency on: %s\n",
           				gcb.TransparentColor != -1 ? "yes" : "no");
	                printf("Transparent Index: %d\n", gcb.TransparentColor);
        	        printf("DelayTime: %d\n", gcb.DelayTime);
              		printf("DisposalMode: %d\n", gcb.DisposalMode);
	                #endif

        	        /* Get disposal mode */
                	Disposal_Mode=gcb.DisposalMode;

	                /* Get trans_color */
        	      	trans_color=gcb.TransparentColor;


	               	/* Get delay time in ms, and delay */
        	        DelayMs=gcb.DelayTime*10;
			if(DelayMs==0)		/* For some GIF it's 0! */
				DelayMs=50;

			break;
		case CONTINUE_EXT_FUNC_CODE:
			printf(" --- CONTINUE_EXT_FUNC_CODE ---\n");
			break;
		case COMMENT_EXT_FUNC_CODE:
			printf(" --- COMMENT_EXT_FUNC_CODE ---\n");
			break;
		case PLAINTEXT_EXT_FUNC_CODE:
			printf(" --- PLAINTEXT_EXT_FUNC_CODE ---\n");
			break;
		case APPLICATION_EXT_FUNC_CODE:
			printf(" --- APPLICATION_EXT_FUNC_CODE ---\n");
			break;

		default:
			break;

	    } /* END of switch(), EXT_FUNC_CODE */

	} /* END for(j):  read and parse extension block*/

     }
     /* ELSE: NO extension blocks */
     else {
		ExtBlock=NULL;
     }

     /* impose User_DisposalMode */
     if(User_DisposalMode >=0 )
		Disposal_Mode=User_DisposalMode;

     /* ---- Write GIF block image to FB  ---- */
      egi_gif_rasterWriteFB( fbdev, egif->Simgbuf,Disposal_Mode,     /* FBDEV, Simgbuf, Disposal_Mode */
			     xp,yp,xw,yw,winw,winh,		    /* xp,yp,xw,yw,winw,winh */
			     BWidth, BHeight, offx, offy,           /* BWidth, BHeight, offx, offy */
		  	     ColorMap, ImageData->RasterBits,  	    /*  ColorMap, buffer, */
                      trans_color, User_TransColor, bkg_color,      /* trans_color, user_trans_color, bkg_color */
			     egif->ImgTransp_ON ); 	/* DirectFB_ON, Img_Transp_ON, BkgTransp_ON */

    /* Refresh FB page if NOT DirectFB_ON */
    if(!DirectFB_ON && fbdev != NULL )
    	fb_page_refresh(fbdev);

    /* Delay */
    if(fbdev != NULL)
        egi_sleep(0,0,DelayMs);

    /* ----- Parse Disposal_Mode: Actions after GIF displaying  ----- */
    switch(Disposal_Mode)
    {
     	case 0: /* Disposal_Mode 0: No disposal specified */
		break;
	case 1: /* Disposal_Mode 1: Leave image in place */
		break;
     	case 2: /* Disposal_Mode 2: Set block area to background color/image */
    		if( !DirectFB_ON )
     		{
	    	    /* update Simgbuf Block*, make current block area transparent! */
	    	    for(i=0; i<BHeight; i++) {
        	 	 /* update block of Simgbuf */
		         for(j=0; j<BWidth; j++ ) {
	        	        spos=(offy+i)*(egif->SWidth)+(offx+j);
		      	 	if(egif->ImgTransp_ON )	/* Make curretn block area transparent! */
		      	   		egif->Simgbuf->alpha[spos]=0;

		      	 	else {   /* bkcolor is NOT applicable for local colorMap*/
			  		egif->Simgbuf->imgbuf[spos]=bkcolor; /* bkcolor: WEGI_16BIT_COLOR */
			      	   	egif->Simgbuf->alpha[spos]=255;
		     	 	}
			}
	    	    }
	        }
    	        /* Set area to FB background color/image */
		if(fbdev != NULL)
	        	memcpy(fbdev->map_bk, fbdev->map_buff+fbdev->screensize, fbdev->screensize);

		break;

       /* TODO: Disposal_Mode 3: Restore to previous image
        * "The mode Restore to Previous is intended to be used in small sections of the graphic
        *	 ... this mode should be used sparingly" --- < Graphics Interchange Format Version 89a >
        */
	case 3:
		printf("%s: !!! Skip Disposal_Mode 3 !!!\n",__func__);
		break;

	default:
		break;

    } /* --- End switch() Disposal_Mode --- */


#if 0 //////////////  vars for block codes method  /////////////////
    int i;
    //    int bkg_color=-1;
    /*  bkg_color: This byte is only meaningful if the global color table flag is 1, and if there is no
       global color table, this byte should be 0. Assign it to -1 just for comparision! */
    EGI_16BIT_COLOR bkcolor;		/* 16bit color, NOT color index */
    int user_trans_color=-1;  		////// User_TransColor ////////
    bool BkgTransp_ON=false;
    bool Is_ImgColorMap;
    int pos=0;
    int spos=0;
    int SWidth=egif->SWidth;		/* Screen/canvas size */
    int SHeight=egif->SHeight;
    GifColorType *ColorMapEntry;
    EGI_IMGBUF *Simgbuf= egif->Simgbuf;
#endif //////////////////////////////////////////////////////////////

     /* ImageCount incremental */
     egif->ImageCount++;

    if( egif->ImageCount > egif->ImageTotal-1) {
	/* End of one round loop */
	egif->ImageCount=0;
	k++;
    }

 }  while( k < nloop || nloop < 0 );  /* Do nloop times! OR loop forever if nloop<0 */

}