/*-----------------------  touch_home.c ------------------------------
1. Only first byte read from XPT is meaningful !!!??

2. Keep enough time-gap for XPT to prepare data in each read-session,
usleep 3000us seems OK, or the data will be too scattered.

3. Hold LCD-Touch module carefully, it may influence touch-read data.

4. TOUCH Y edge doesn't have good homogeneous property along X,
   try to draw a horizontal line and check visually, it will bend.

5. if sx0=sy0=0, it means that last TOUCH coordinate is invalid or pen-up.

6. Linear piont to piont mapping from TOUCH to LCD. There are
   unmapped points on LCD.

7. point to an oval area mapping form TOUCH to LCD.


TODO:
1. Screen sleep
2. LCD_SIZE_X,Y to be cancelled.
3. home page refresh button... 3s pressing test.
4. apply mutli-process for separative jobs: reading-touch-pad, CLOCK,texting,... etc.
5. systme()... sh -c ...
6. btn-press and btn-release signal

Midas Zhou
----------------------------------------------------------------*/
#include <stdio.h>
#include <signal.h>
#include "spi.h"
#include "fblines.h"
#include "egi.h"
#include "xpt2046.h"
#include "bmpjpg.h"
#include "dict.h"
#include "egi_timer.h"
#include "symbol.h"




/*  ---------------------  MAIN  ---------------------  */
int main(void)
{
	int i,j;
	int index;
	int ret;
	uint16_t sx,sy;  //current TOUCH point coordinate, it's a LCD screen coordinates derived from TOUCH coordinate.

	/* Frame buffer device */
//        FBDEV     fb_dev;

	/* ---- buttons array param ------*/
	int nrow=2; /* number of buttons at Y directions */
	int ncolumn=3; /* number of buttons at X directions */
	struct egi_element_box ebox[nrow*ncolumn]; /* square boxes for buttons */
	struct egi_element_box ebox_clock;
//	struct egi_element_box imgbox;
	int startX=0;  /* start X of eboxes array */
	int startY=120; /* start Y of eboxes array */
	int sbox=70; /* side length of the square box */
	int sgap=(LCD_SIZE_X - ncolumn*sbox)/(ncolumn+1); /* gaps between boxes(bottons) */

	//char str_syscmd[100];
	char strf[100];

	/* --- open spi dev --- */
	SPI_Open();

	/* --- prepare fb device --- */
        gv_fb_dev.fdfd=-1;
        init_dev(&gv_fb_dev);


	/* --- clear screen with BLACK --- */
/* do NOT clear, to avoid flashing */
	//clear_screen(&fb_dev,(0<<11|0<<5|0));

	/* --- load screen paper --- */
	show_jpg("home.jpg",&gv_fb_dev,0,0,0); /*black on*/

	/* --- load symbol dict --- */
	//dict_display_img(&fb_dev,"dict.img");
	if(dict_load_h20w15("/home/dict.img")==NULL)
	{
		printf("Fail to load home page!\n");
		exit(-1);
	}
	/* --- print and display symbols --- */
#if 0
	dict_print_symb20x15(dict_h20w15);
	for(i=0;i<10;i++)
		dict_writeFB_symb20x15(&gv_fb_dev,1,(30<<11|45<<5|10),i,30+i*15,320-40);
#endif


	/* --- load testfont ---- */
	if(symbol_load_page(&sympg_testfont)==NULL)
		exit(-2);

	/* print all symbols in the page */
#if 0
	for(i=32;i<127;i++)
	{
		symbol_print_symbol(&sympg_testfont,i,0xffff);
		//getchar();
	}
#endif
	//symbol_writeFB(&gv_fb_dev, &sympg_testfont,0xffff,0,0,'M');
	//symbol_string_writeFB(&gv_fb_dev, &sympg_testfont,0xffff,50,50,"Hello Widora!");

//	exit(-1);


	/* ----- image box test ----- */
/*
	imgbox.height=100;
	imgbox.width=240;
	imgbox.x0=0;
	imgbox.y0=0;
	draw_rect(&gv_fb_dev,imgbox.x0,imgbox.y0,\
		imgbox.x0+imgbox.width-1,imgbox.y0+imgbox.height-1);
*/

	/* ----- generate ebox parameters ----- */
	for(i=0;i<nrow;i++) /* row */
	{
		for(j=0;j<ncolumn;j++) /* column */
		{
			/* generate boxes */
			ebox[ncolumn*i+j].type=type_button;
			ebox[ncolumn*i+j].height=sbox;
			ebox[ncolumn*i+j].width=sbox;
			ebox[ncolumn*i+j].x0=startX+(j+1)*sgap+j*sbox;
			ebox[ncolumn*i+j].y0=startY+i*(sgap+sbox);
		}
	}

	/* ------- CLOCK ebox ------- */
	ebox_clock.type = type_txt;
	ebox_clock.height = 30;
	ebox_clock.width = 150;
	ebox_clock.color = -1; /*transparent*/
	ebox_clock.x0= 60;
	ebox_clock.y0= 320-38;

	egi_init_ebox(&ebox_clock);

	/* print box position for debug */
	// for(i=0;i<nrow*ncolumn;i++)
	//	printf("ebox[%d]: x0=%d, y0=%d\n",i,ebox[i].x0,ebox[i].y0);

	/* ----- draw the eboxes ----- */
	for(i=0;i<nrow*ncolumn;i++)
	{
		/* color adjust for button */
		fbset_color( (30-i*5)<<11 | (50-i*8)<<5 | (i+1)*10 );/* R5-G6-B5 */
//		fbset_color( (35-i*5)<<11 | (55-i*5)<<5 | (i+1)*10 );/* R5-G6-B5 */
//		fbset_color( (15+i*5)<<11 | (55-i*5)<<5 | (i+1)*5 );/* R5-G6-B5 */

		draw_filled_rect(&gv_fb_dev,ebox[i].x0,ebox[i].y0,\
			ebox[i].x0+ebox[i].width-1,ebox[i].y0+ebox[i].height-1);
	}


	/* ---- set timer for time display ---- */
	tm_settimer(500000);/* set timer interval interval */
	signal(SIGALRM, tm_sigroutine);


	/* ----- set default color ----- */
        fbset_color((30<<11)|(10<<5)|10);/* R5-G6-B5 */


	/* ===============----------(((  MAIN LOOP  )))----------================= */
	while(1)
	{
		/*------ relate with number of touch-read samples -----*/
		usleep(3000); //3000

		/*--------- read XPT to get avg tft-LCD coordinate --------*/
		ret=xpt_getavg_xy(&sx,&sy); /* if fail to get touched tft-LCD xy */
		if(ret == XPT_READ_STATUS_GOING )
		{
			continue; /* continue to loop to finish reading touch data */
		}

		/* -------  put PEN-UP status events here !!!! ------- */
		else if(ret == XPT_READ_STATUS_PENUP )
		{
#if 1
			/* get time and display */
			tm_get_strtime(tm_strbuf);
			wirteFB_str20x15(&gv_fb_dev, 0, (30<<11|45<<5|10), tm_strbuf, 60, 320-38);
			tm_get_strday(tm_strbuf);
			symbol_string_writeFB(&gv_fb_dev, &sympg_testfont,0xffff,45,2,tm_strbuf);
			//symbol_string_writeFB(&gv_fb_dev, &sympg_testfont,0xffff,32,90,tm_strbuf);
#endif
			continue; /* continue to loop to read touch data */
		}
		else if(ret == XPT_READ_STATUS_COMPLETE)
		{
			printf("--- XPT_READ_STATUS_COMPLETE ---\n");

			/* going on then to check and activate pressed button */
		}


		////////// -----------  Touch Event Handling  ----------- //////////

		/*---  get index of pressed ebox and activate the button ----*/
	    	index=egi_get_boxindex(sx,sy,ebox,nrow*ncolumn);

		printf("get box index=%d\n",index);
		if(index>=0) /* if get meaningful index */
		{
			if(index==0)
			{
				printf("refresh fb now.\n");
				system("/tmp/tmp_app");
				exit(1);
			}
			else
			{
				printf("button[%d] pressed!\n",index);
				sprintf(strf,"m_%d.jpg",index+1);
				show_jpg(strf, &gv_fb_dev, 1, 0, 0);/*black off*/

			}
			//usleep(200000); //this will make touch points scattered.
		}

	} /* end of while() loop */


	/* release symbol mem page */
	symbol_release_page(&sympg_testfont);

	/* release dict mem */
	dict_release_h20w15();

	/* close fb dev */
        munmap(gv_fb_dev.map_fb,gv_fb_dev.screensize);
        close(gv_fb_dev.fdfd);

	/* close spi dev */
	SPI_Close();
	return 0;
}