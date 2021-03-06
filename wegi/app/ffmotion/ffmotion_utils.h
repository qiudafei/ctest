/*--------------------------------------------------------------------
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.


Note:
	Never forget why you start! ---Just For Fun!

TODO:
1. Use a new FB_DEV to display subtitle. or it rises a race condition
   for the FB dev.
2.

Midas_Zhou
----------------------------------------------------------------------*/
#ifndef  __FFMOTION_UTILS_H__
#define  __FFMOTION_UTILS_H__

#include <stdio.h>
#include "egi_bjp.h"
#include "egi.h"
#include "egi_timer.h"
#include "ffmotion.h"
#include "libavcodec/avcodec.h"

//#define LCD_MAX_WIDTH 240
//#define LCD_MAX_HEIGHT 320
//#define FFPLAY_MUSIC_PATH "/mmc/"

/* in seconds, playing time elapsed for Video */
extern int ff_sec_Velapsed;
extern int ff_sub_delays; /* delay sub display in seconds, relating to ff_sec_Velapsed */
extern enum ffmotion_cmd control_cmd;

extern FFMOTION_CONTEXT *FFmotion_Ctx;
extern FBDEV ff_fb_dev;

//#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48KHz 32bit audio
#define BUFF_NUM_EXPONENT	2
#define PIC_BUFF_NUM  		(1<<BUFF_NUM_EXPONENT)  /* total number of RGB picture data buffers. */

/* information of a decoded picture, for pthread params */
struct ffmotion_PicInfo {
	EGI_BOX 	dispBox;	/* Displaying area */
	int 		nPICbuff; 	/* slot number of buff data in pPICbuffs[] */
	uint8_t 	*data; 		/* RGB data, pointer to pPICbuffs[] page */
	int 		numBytes;  	/* total bytes for a picture RGB data, depend on pixel format and pixel numbers */
	enum AVCodecID 	vcodecID; 	/* Video codec ID */
	char 		*fname;		/* current file name */
	EGI_PAGE*  	app_page; 	/*  PAGE for the Pic to display */
//	EGI_IMGBUF 	*imgbuf;	/* destination imgbuf if applied, now for page->ebox->imgbuf */
};

extern struct ffmotion_PicInfo motPicInfo;


/* check if it's image */
#define IS_IMAGE_CODEC(vcodecID)  ( vcodecID == AV_CODEC_ID_MJPEG    || vcodecID == AV_CODEC_ID_BMP  ||	\
                                    vcodecID == AV_CODEC_ID_MJPEGB   || vcodecID == AV_CODEC_ID_PNG  ||	\
                                    vcodecID == AV_CODEC_ID_JPEG2000 || vcodecID == AV_CODEC_ID_GIF  ||	\
                                    vcodecID == AV_CODEC_ID_TIFF     || vcodecID == AV_CODEC_ID_GIF  ||	\
                                    vcodecID == AV_CODEC_ID_LJPEG    || vcodecID == AV_CODEC_ID_JPEGLS   )

/*  functions  */
int 		init_ffmotionCtx(char *fext);
void 		free_ffmotionCtx(void);
uint8_t**  	malloc_PicBuffs(int pic_size, int pixel_size );
int 	   	load_Pic2Buff(struct ffmotion_PicInfo *ppic,const uint8_t *data, int numBytes);
void* 	   	thdf_Display_motionPic(void * argv);
void* 	   	thdf_Display_Subtitle(void * argv);

#endif
