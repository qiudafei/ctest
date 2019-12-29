/*------------------------------------------------------------------
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

Example:
Call egi_gif_slurpFile() to load a GIF file to an EGI_GIF struct and
then call egi_gif_displayFrame() to display it.

Midas Zhou
------------------------------------------------------------------*/

#include <stdio.h>
#include "egi_common.h"
#include "egi_FTsymbol.h"
#include "egi_gif.h"

int main(int argc, char **argv)
{
        /* <<<<<  EGI general init  >>>>>> */
#if 0
        printf("tm_start_egitick()...\n");
        tm_start_egitick();                     /* start sys tick */
#endif
#if 0
        printf("egi_init_log()...\n");
        if(egi_init_log("/mmc/log_test") != 0) {        /* start logger */
                printf("Fail to init logger,quit.\n");
                return -1;
        }

        printf("symbol_load_allpages()...\n");
        if(symbol_load_allpages() !=0 ) {       /* load sys fonts */
                printf("Fail to load sym pages,quit.\n");
                return -2;
        }
        if(FTsymbol_load_appfonts() !=0 ) {     /* load FT fonts LIBS */
                printf("Fail to load FT appfonts, quit.\n");
                return -2;
        }
#endif

        printf("init_fbdev()...\n");
        if( init_fbdev(&gv_fb_dev) )            /* init sys FB */
                return -1;

#if 0
        printf("start touchread thread...\n");
        egi_start_touchread();                  /* start touch read thread */
#endif

        /* <<<<------------------  End EGI Init  ----------------->>>> */

    	int xres;
    	int yres;
	EGI_GIF *egif=NULL;
	bool ImgTransp_ON=false;
	bool DirectFB_ON=false; /* For transparency_off GIF,to speed up,tear line possible. */
	int xp,yp;
	int xw,yw;


	/* help */
	if(argc < 2) {
		printf("Usage:  %s  fpath [ImgTransp_ON]  [[DirectFB_ON]] \n", argv[0] );
		printf("	default: ImgTransp_ON=0(false)   DirectFB_ON=0(false)\n");
		exit(-1);
	}

	/* get input ImgTransp_ON */
	if( argc > 2 )
		atoi(argv[2])==0 ? (ImgTransp_ON=false) : (ImgTransp_ON=true);
	if( argc > 3 )
		atoi(argv[3])==0 ? (DirectFB_ON=false) : (DirectFB_ON=true);


        /* refresh working buffer */
        //clear_screen(&gv_fb_dev, WEGI_COLOR_GRAY);

	/* Set FB mode as LANDSCAPE  */
        fb_position_rotate(&gv_fb_dev, 3);
    	xres=gv_fb_dev.pos_xres;
    	yres=gv_fb_dev.pos_yres;

        /* set FB mode */
    	if(DirectFB_ON) {
            gv_fb_dev.map_bk=gv_fb_dev.map_fb; /* Direct map_bk to map_fb */

    	} else {
            /* init FB back ground buffer page */
            memcpy(gv_fb_dev.map_buff+gv_fb_dev.screensize, gv_fb_dev.map_fb, gv_fb_dev.screensize);

	    /*  init FB working buffer page */
            //fbclear_bkBuff(&gv_fb_dev, WEGI_COLOR_BLUE);
            memcpy(gv_fb_dev.map_bk, gv_fb_dev.map_fb, gv_fb_dev.screensize);
    	}

	/* read in GIF data to EGI_GIF */
	egif= egi_gif_slurpFile( argv[1], ImgTransp_ON); /* fpath, bool ImgTransp_ON */
	if(egif==NULL) {
		printf("Fail to read in gif file!\n");
		exit(-1);
	}
        printf("Finishing read GIF.\n");

	/* Cal xp,yp xw,yw */
	xp=egif->SWidth>xres ? (egif->SWidth-xres)/2:0;
	yp=egif->SHeight>yres ? (egif->SHeight-yres)/2:0;
	xw=egif->SWidth>xres ? 0:(xres-egif->SWidth)/2;
	yw=egif->SHeight>yres ? 0:(yres-egif->SHeight)/2;


	/* Loop displaying */
        while(1) {

	    /* Display one frame/block each time, then refresh FB page.  */
            egi_gif_displayFrame( &gv_fb_dev, egif, 0,	/* *fbdev, EGI_GIF *egif, nloop */
				 /* DirectFB_ON, User_DispMode, User_TransColor, User_BkgColor */
				  DirectFB_ON, -1, -1, -1,
	 			 /* to put center of IMGBUF to the center of LCD */
				 xp,yp,xw,yw,			/* xp,yp, xw, yw */
				egif->SWidth>xres ? xres:egif->SWidth,		/* winw */
				egif->SHeight>yres ? yres:egif->SHeight		/* winh */
			);
	}
    	egi_gif_free(&egif);


        /* <<<<<-----------------  EGI general release  ----------------->>>>> */
        printf("FTsymbol_release_allfonts()...\n");
        FTsymbol_release_allfonts();
        printf("symbol_release_allpages()...\n");
        symbol_release_allpages();
        printf("release_fbdev()...\n");
        fb_filo_flush(&gv_fb_dev); /* Flush FB filo if necessary */
        release_fbdev(&gv_fb_dev);
        printf("egi_end_touchread()...\n");
        egi_end_touchread();
        printf("egi_quit_log()...\n");

#if 0
        egi_quit_log();
        printf("<-------  END  ------>\n");
#endif

	return 0;
}