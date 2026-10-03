#ifndef ISO_H
#define ISO_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <map>
#include <ciso.h>
#include "entry.h"


typedef struct{
    u32 offset;
    u32 size;
} FileData;

class Iso : public Entry
{
    public:

        Iso(string path);
        ~Iso();
    
        void loadIcon();
        void loadPics();
        void loadAVMedia();
        SfoInfo getSfoInfo();
        char* getType();
        char* getSubtype();
        int checkAudioVideo();
        
        static bool isISO(const char* filepath);
        
        /* Much faster function for extracting files in PSP_GAME/ */
        void* fastExtract(const char* file, unsigned* size=NULL, void* buf=NULL);

        static void executeISO(const char* path, char* eboot_path);
        static void executeVideoISO(const char* path);

    protected:

        // keep track to the offset and size of loaded files (icon, pmf, etc) for faster extraction
        map<string, FileData> file_cache;

        // reader information        
        int is_compressed;
        CisoFile g_ciso_file;
        
        void doExecute();
        bool isPatched();
        bool hasPlainBoot();

        int read_iso_data(SceUID fd, void* ptr, int size, int offset);
        int has_installed_file(const char* installed_file, char* out_path);
};

#endif
