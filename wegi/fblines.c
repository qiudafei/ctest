 /*------------------------------------------------------------------------------
Referring to: http://blog.chinaunix.net/uid-22666248-id-285417.html

 本文的copyright归yuweixian4230@163.com 所有，使用GPL发布，可以自由拷贝，转载。
但转载请保持文档的完整性，注明原作者及原链接，严禁用于任何商业用途。

作者：yuweixian4230@163.com
博客：yuweixian4230.blog.chinaunix.net


modified by Midas-Zhou
1. add 
-----------------------------------------------------------------------------*/
#include "fblines.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

/* global variale, Frame buffer device */
FBDEV   gv_fb_dev;

#ifndef _TYPE_FBDEV_
#define _TYPE_FBDEV_
    typedef struct fbdev{
        int fdfd; //open "dev/fb0"
        struct fb_var_screeninfo vinfo;
        struct fb_fix_screeninfo finfo;
        long int screensize;
        char *map_fb;
    }FBDEV;
#endif

 /* default color set */
 static uint16_t fb_color=(30<<11)|(10<<5)|10;  //r(5)g(6)b(5)

 void init_dev(FBDEV *dev)
 {
        FBDEV *fr_dev=dev;

        fr_dev->fdfd=open("/dev/fb0",O_RDWR);
//        printf("the framebuffer device was opended successfully.\n");
        ioctl(fr_dev->fdfd,FBIOGET_FSCREENINFO,&(fr_dev->finfo)); //获取 固定参数
        ioctl(fr_dev->fdfd,FBIOGET_VSCREENINFO,&(fr_dev->vinfo)); //获取可变参数
        fr_dev->screensize=fr_dev->vinfo.xres*fr_dev->vinfo.yres*fr_dev->vinfo.bits_per_pixel/8;
        fr_dev->map_fb=(char *)mmap(NULL,fr_dev->screensize,PROT_READ|PROT_WRITE,MAP_SHARED,fr_dev->fdfd,0);
//        printf("init_dev successfully. fr_dev->map_fb=%p\n",fr_dev->map_fb);
  }

    void release_dev(FBDEV *dev)
    {
	if(!dev->map_fb)return;

	munmap(dev->map_fb,dev->screensize);
	close(dev->fdfd);
    }


  /*------------------------------------
  check if (px,py) in box(x1,y1,x2,y2)
  return true or false
  -------------------------------------*/
  bool point_inbox(int px,int py, int x1, int y1,int x2, int y2)
  {
      int xl,xh,yl,yh;

	if(x1>=x2){
		xh=x1;xl=x2;
	}
	else {
		xl=x1;xh=x2;
	}

	if(y1>=y2){
		yh=y1;yl=y2;
	}
	else {
		yh=y2;yl=y1;
	}

	if( (px>=xl && px<=xh) && (py>=yl && py<=yh))
		return true;
	else
		return false;
  }


    /*  set color for every dot */
    void fbset_color(uint16_t color)
    {
	fb_color=color;

    }

    /* clear screen with given color */
    void clear_screen(FBDEV *dev, uint16_t color)
    {
	FBDEV *fr_dev=dev;
	long int location=0;

	for(location=0; location < (fr_dev->screensize); location++)
	        *((unsigned short int *)(fr_dev->map_fb+location))=color;
    }


    void draw_dot(FBDEV *dev,int x,int y) //(x.y) 是坐标
    {
        FBDEV *fr_dev=dev;
        int *xx=&x;
        int *yy=&y;
        long int location=0;

        location=(*xx+fr_dev->vinfo.xoffset)*(fr_dev->vinfo.bits_per_pixel/8)+
                     (*yy+fr_dev->vinfo.yoffset)*fr_dev->finfo.line_length;

        *((unsigned short int *)(fr_dev->map_fb+location))=fb_color;
    }




    void draw_line(FBDEV *dev,int x1,int y1,int x2,int y2) 
    {
        FBDEV *fr_dev=dev;
        int *xx1=&x1;
        int *yy1=&y1;
        int *xx2=&x2;
        int *yy2=&y2;

        int i=0;
        int j=0;
        int tekxx=*xx2-*xx1;
        int tekyy=*yy2-*yy1;

// draw_line(&fr_dev,240,318,240,246);

        //if((*xx2>=*xx1)&&(*yy2>=*yy1))
        if(*xx2>*xx1)
        {
            for(i=*xx1;i<=*xx2;i++)
            {
                j=(i-*xx1)*tekyy/tekxx+*yy1;
                draw_dot(fr_dev,i,j);
//		draw_dot(fr_dev,i+1,j);
//		draw_dot(fr_dev,i,j+1);
//		draw_dot(fr_dev,i+1,j+1);
            }
        }
	else if(*xx2 == *xx1)
	{
	   if(*yy2>=*yy1)
	   {
		for(i=*yy1;i<=*yy2;i++)
		   draw_dot(fr_dev,*xx1,i);
	    }
	    else //yy2<yy1
	   {
		for(i=*yy2;i<=*yy1;i++)
			draw_dot(fr_dev,*xx1,i);
	   }
	}
        else
        {
            //if(*xx2<*xx1)
            for(i=*xx2;i<=*xx1;i++)
            {
                j=(i-*xx2)*tekyy/tekxx+*yy2;
                draw_dot(fr_dev,i,j);
            }
        }


    }


    /*------------------------------------------------
	           draw an oval
    -------------------------------------------------*/
    void draw_oval(FBDEV *dev,int x,int y) //(x.y) 是坐标
    {
        FBDEV *fr_dev=dev;
        int *xx=&x;
        int *yy=&y;

	draw_line(fr_dev,*xx,*yy-3,*xx,*yy+3);
	draw_line(fr_dev,*xx-1,*yy-2,*xx-1,*yy+2);
	draw_line(fr_dev,*xx-2,*yy-1,*xx-2,*yy+1);
	draw_line(fr_dev,*xx+1,*yy-2,*xx+1,*yy+2);
	draw_line(fr_dev,*xx+2,*yy-2,*xx+2,*yy+2);

    }


    void draw_rect(FBDEV *dev,int x1,int y1,int x2,int y2)
    {
        FBDEV *fr_dev=dev;

	draw_line(fr_dev,x1,y1,x1,y2);
	draw_line(fr_dev,x1,y2,x2,y2);
	draw_line(fr_dev,x2,y2,x2,y1);
	draw_line(fr_dev,x2,y1,x1,y1);
    }


    void draw_filled_rect(FBDEV *dev,int x1,int y1,int x2,int y2)
    {
        FBDEV *fr_dev=dev;
        int *xx1=&x1;
        int *yy1=&y1;
        int *xx2=&x2;
        int *yy2=&y2;
        int i=0,j=0;

        for(j=*yy1;j<*yy2;j++) //注意 这里要 xx1 < xx2
            for(i=*xx1;i<*xx2;i++)
            {

                draw_dot(fr_dev,i,j);
            }
    }


