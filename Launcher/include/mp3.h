#ifndef MP3_H
#define MP3_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <pspsdk.h>
#include <pspkernel.h>
#include <pspmp3.h>
#include <pspaudio.h>
#include <psputility.h>
#include <psputility_avmodules.h>
#include <pspav/mp3.h>

class MP3{

    private:
        const char* filename;
        void* buffer;
        int buffer_size;
        
        static int playThread(SceSize _args, void** _argp);
        
    public:

        void (*on_music_end)(MP3* self);

        MP3(void* buffer, int size);
        MP3(const char* filename, bool to_buffer=false);
        ~MP3();
        
        void* getBuffer();
        int getBufferSize();

        void play();
        void stop();
        void pauseResume();
        const char* getFilename(){ return filename; };
        static int isPlaying();
        static int isPaused();
        static void fullStop();
};

#endif
