#include "debug.h"
#include <pspthreadman.h>
#include <pspiofilemgr.h>

static SceUID debugFileSema = -1;

void debugFile(const char* text){

    if (debugFileSema < 0)
        debugFileSema = sceKernelCreateSema("debug_file_sema",  0, 1, 1, NULL);
    
    sceKernelWaitSema(debugFileSema, 1, NULL);
    SceUID fp = sceIoOpen("DEBUG.TXT", PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND, 0777);
    sceIoWrite(fp, text, strlen(text));
    sceIoClose(fp);
    sceKernelSignalSema(debugFileSema, 1);
}
