#include <stdio.h>
#include "egi_common.h"
#include "egi_pcm.h"
#include "egi_FTsymbol.h"

int main(void)
{
	int ret;

        /* for pcm capture */
        snd_pcm_t *play_handle;
	snd_pcm_t *rec_handle;
        int nchanl=1;   			/* number of channles */
        int format=SND_PCM_FORMAT_S16_LE;
        int bits_per_sample=16;
        int frame_size=2;               	/*bytes per frame, for 1 channel, format S16_LE */
        int rec_srate=44100;           		/* HZ,  sample rate for capture */
	/* if rec_srate != play_srate, crack... */
	int play_srate=44100;			/* HZ,  sample rate for playback */
        bool enable_resample=true;      	/* whether to enable resample */
        int rec_latency=100000; //500000;             	/* required overall latency in us */
        int play_latency=100000; //500000;             	/* required overall latency in us */
        //snd_pcm_uframes_t period_size;  	/* max numbers of frames that HW can hanle each time */
        /* for CAPTURE, period_size=1536 frames, while for PLAYBACK: 278 frames */
        snd_pcm_uframes_t chunk_frames=1024; 	/*in frame, expected frames for readi/writei each time */
        int chunk_size;                 	/* =frame_size*chunk_frames */
	int16_t buff[1024*1];			/* chunk_frames, int16_t for S16 */


        /* <<<<<  EGI general init  >>>>>> */
#if 0
        printf("tm_start_egitick()...\n");
        tm_start_egitick();
        printf("egi_init_log()...\n");
        if(egi_init_log("/mmc/log_fb") != 0) {
                printf("Fail to init logger,quit.\n");
                return -1;
        }
#endif
        printf("symbol_load_allpages()...\n");
        if(symbol_load_allpages() !=0 ) {   /* load sys fonts */
                printf("Fail to load sym pages,quit.\n");
                return -2;
        }
        if(FTsymbol_load_appfonts() !=0 ) {  /* and FT fonts LIBS */
                printf("Fail to load FT appfonts, quit.\n");
                return -2;
        }
        printf("init_fbdev()...\n");
        if( init_fbdev(&gv_fb_dev) )	/* init sys FB */
                return -1;



        /* open pcm captrue device */
        if( snd_pcm_open(&rec_handle, "plughw:0,0", SND_PCM_STREAM_CAPTURE, 0) <0 ) {
                printf("Fail to open pcm captrue device!\n");
                return -1;
        }

        /* set params for pcm capture handle */
        if( snd_pcm_set_params( rec_handle, format, SND_PCM_ACCESS_RW_INTERLEAVED,
                                nchanl, rec_srate, enable_resample, rec_latency )  <0 ) {
                printf("Fail to set params for pcm capture handle.!\n");
                snd_pcm_close(rec_handle);
                return -2;
        }

        /* open pcm play device */
        if( snd_pcm_open(&play_handle, "default", SND_PCM_STREAM_PLAYBACK, 0) <0 ) {
                printf("Fail to open pcm play device!\n");
                return -1;
        }

        /* set params for pcm play handle */
        if( snd_pcm_set_params( play_handle, format, SND_PCM_ACCESS_RW_INTERLEAVED,
                                nchanl, play_srate, enable_resample, play_latency )  <0 ) {
                printf("Fail to set params for pcm play handle.!\n");
                snd_pcm_close(play_handle);
                return -2;
        }

        /* adjust record volume, or to call ALSA API */
        system("amixer -D hw:0 set Capture 85%");
        system("amixer -D hw:0 set 'ADC PCM' 85%");

        printf("Start recording and playing ...\n");
        while(1) /* let user to interrupt, or if(count<record_size) */
        {
		printf(" --- \n");

                /* snd_pcm_sframes_t snd_pcm_readi (snd_pcm_t pcm, void buffer, snd_pcm_uframes_t size) */
		/* return number of frames, for 1 chan, 1 frame=size of a sample (S16_Le)  */
                ret=snd_pcm_readi(rec_handle, buff, chunk_frames);
                if(ret == -EPIPE ) {
                        /* EPIPE for overrun */
                        printf("Overrun occurs during capture, try to recover...\n");
                    /* try to recover, to put the stream in PREPARED state so it can start again next time */
                        snd_pcm_prepare(rec_handle);
                        continue;
                }
                else if(ret<0) {
                        printf("snd_pcm_readi error: %s\n",snd_strerror(ret));
                        continue; /* carry on anyway */
                }
                /* CAUTION: short read may cause trouble! Let it carry on though. */
                else if(ret != chunk_frames) {
                      printf("snd_pcm_readi: read end or short read ocuurs! get only %d of %ld expected frame",
                                                                                ret, chunk_frames);
                        /* pad 0 to chunk_frames */
                        if( ret<chunk_frames ) {  /* >chunk_frames NOT possible? */
                                memset( buff+ret*frame_size, 0, (chunk_frames-ret)*frame_size);
                        }
                }





#if 0
                /* write back to pcm dev(interleaved) */
		ret=snd_pcm_writei(play_handle, (void**)&buff, chunk_frames);
	        if (ret == -EPIPE) {
            		/* EPIPE means underrun */
            		fprintf(stderr,"snd_pcm_writei() underrun occurred\n");
            		snd_pcm_prepare(play_handle);
        	}
	        else if(ret<0) {
          	        fprintf(stderr,"snd_pcm_writei():%s\n",snd_strerror(ret));
	        }
	        else if (ret!=chunk_frames) {
                	fprintf(stderr,"snd_pcm_writei(): short write, write %d of total %ld frames\n",
										ret, chunk_frames);
	        }
#endif

	}


        snd_pcm_close(rec_handle);
        snd_pcm_close(play_handle);



        /* <<<<<  EGI general release >>>>> */
        printf("FTsymbol_release_allfonts()...\n");
        FTsymbol_release_allfonts();

        printf("symbol_release_allpages()...\n");
        symbol_release_allpages();

	printf("release_fbdev()...\n");
        release_fbdev(&gv_fb_dev);

        printf("egi_quit_log()...\n");
        egi_quit_log();
        printf("---------- END -----------\n");

return 0;
}
