/*-------------------------------------------------------------------
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

Test egi_xxx_base64() functions.
Print out encoded base64URL string.

Usage:	test_base64 file

Midas Zhou
-------------------------------------------------------------------*/
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include "egi_utils.h"

int main(int argc, char **argv)
{
	FILE *fp=NULL;
	int fd;
	struct stat sb;
	size_t fsize;
	unsigned char *fmap;
	char *buff;	/* For base64 data */
	char *URLbuff;  /* For base64URL data */
	int ret=0;

	if(argc<2) {
		printf("Usage: %s fpath \n", argv[0]);
		exit(1);
	}


	fd=open(argv[1],O_RDONLY);
	if(fd<0) {
		perror("open");
		return -1;
	}
	/* get size */
	if( fstat(fd,&sb)<0) {
		perror("fstat");
		return -2;
	}
	fsize=sb.st_size;


	/* allocate buff */
	buff=calloc((fsize+2)/3+1, 4); /* 3byte to 4 byte, 1 more*/
	if(buff==NULL) {
		printf("Fail to calloc buff\n");
		fclose(fp);
		return -3;
	}

        /* mmap file */
        fmap=mmap(NULL, fsize, PROT_READ, MAP_PRIVATE, fd, 0);
        if(fmap==MAP_FAILED) {
                perror("mmap");
                return -2;
        }

	/* Encode to base64 */
	ret=egi_encode_base64(fmap, fsize, buff);
//	printf("\nEncode base64: input size=%d, output size ret=%d, result buff contest:\n%s\n",fsize, ret,buff);

	/* allocate URLbuff */
	URLbuff=calloc(ret, 2); /*  !!! NOTICE HERE:  2*ret, 2 times base64 data size for URLbuff */
	if(URLbuff==NULL) {
		printf("Fail to calloc URLbuff\n");
		munmap(fmap, fsize);
		fclose(fp);
		free(buff);
		return -3;
	}

	/* Encode base64 to URL */
	ret=egi_encode_base64URL((const unsigned char *)buff, strlen(buff), URLbuff, ret*2);
//	printf("\nEncode base64 to URL: input size strlen(buff)=%d, output size ret=%d, URLbuff contest:\n%s\n",
//										strlen(buff), ret, URLbuff);
	printf("%s", URLbuff);


	munmap(fmap,fsize);
	close(fd);
	free(buff);
	free(URLbuff);

	return 0;
}
