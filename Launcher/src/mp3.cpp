#include "mp3.h"
#include "debug.h"

static SceUID mp3Thread = -1;
static SceUID mp3_mutex = sceKernelCreateSema("mp3_mutex", 0, 1, 1, 0);


MP3::MP3(void* buffer, int size){
    this->filename = NULL;
    this->buffer_size = size;
    this->buffer = buffer;
    this->on_music_end = NULL;
}

MP3::MP3(const char* filename, bool to_buffer){
    this->on_music_end = NULL;
    if (!to_buffer){
        this->filename = filename;
        this->buffer = NULL;
        this->buffer_size = 0;
    }
    else {
        this->filename = NULL;
        SceUID fd = sceIoOpen(filename, PSP_O_RDONLY, 0777 );
        this->buffer_size = sceIoLseek32(fd, 0, SEEK_END);
        sceIoLseek(fd, 0, SEEK_SET);
        this->buffer = malloc(this->buffer_size);
        sceIoRead(fd, this->buffer, this->buffer_size);
        sceIoClose(fd);
    }
}

MP3::~MP3(){
    if (this->buffer != NULL)
        free(this->buffer);
}

void* MP3::getBuffer(){
    return this->buffer;
}

int MP3::getBufferSize(){
    return this->buffer_size;
}

void MP3::play(){
    sceKernelWaitSema(mp3_mutex, 1, 0);
    if (!pspavIsMP3Active()){
        void* self = (void*)this;
        mp3Thread = sceKernelCreateThread("", (SceKernelThreadEntry)MP3::playThread, 0x10, 0x10000, PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU, NULL);
        sceKernelStartThread(mp3Thread,  sizeof(self), &self);
    }
    sceKernelSignalSema(mp3_mutex, 1);
}

void MP3::stop(){
    pspavStopMP3Playback();
    sceKernelWaitThreadEnd(mp3Thread, 0);
}

void MP3::pauseResume(){
    pspavResumeOrPauseMP3Playback();
}

int MP3::isPlaying(){
    return pspavIsMP3Active();
}

int MP3::isPaused(){
    return pspavIsMP3Paused();
}

int MP3::playThread(SceSize _args, void** _argp)
{
    MP3* self = (MP3*)(*_argp);
    pspavPlayMP3File((char*)self->filename, self->buffer, self->buffer_size);
    if (self->on_music_end) self->on_music_end(self);
    sceKernelExitDeleteThread(0);
    return 0;
}

void MP3::fullStop(){
    pspavStopMP3Playback();
    sceKernelWaitThreadEnd(mp3Thread, NULL);
}