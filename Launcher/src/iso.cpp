#include "iso.h"
#include "eboot.h"
#include <umd.h>

#include <ciso.h>
#include <systemctrl.h>

using namespace std;

static u8 temp_block[ISO_SECTOR_SIZE*2];

// for libcisoread
extern "C"{
int zlib_inflate(void* dst, int dst_len, void* src){
    return sctrlDeflateDecompress(dst, src, dst_len);
}

int read_raw_data(void* arg, void* addr, u32 size, u32 offset){
    SceUID fp = (SceUID)arg;
    sceIoLseek(fp, offset, PSP_SEEK_SET);
    return sceIoRead(fp, addr, size);
}
}

Iso :: Iso(string path)
{
    this->path = path;
    size_t lastSlash = path.rfind("/", string::npos);
    this->name = path.substr(lastSlash+1, string::npos);
    this->icon0 = common::getImage(IMAGE_WAITICON);
    // Ciso File Handler
    memset(&g_ciso_file, 0, sizeof(CisoFile));
    g_ciso_file.read_data = &read_raw_data;
    g_ciso_file.memalign = &memalign;
    g_ciso_file.free = &free;
};

Iso :: ~Iso()
{
    if (this->icon0 != common::getImage(IMAGE_NOICON) && this->icon0 != common::getImage(IMAGE_WAITICON))
        delete this->icon0;
};

void Iso::loadIcon(){
    Image* icon = NULL;
    unsigned size;
    void* buffer = Iso::fastExtract("ICON0.PNG", &size);
    if (buffer != NULL){
        icon = new Image(buffer, YA2D_PLACE_RAM);
        free(buffer);
    }
    icon = (icon == NULL)? common::getImage(IMAGE_NOICON) : icon;
    icon->swizzle();
    this->icon0 = icon;
}

void Iso::loadPics(){
    this->pic0 = NULL;
    this->pic1 = NULL;

    void* buffer = NULL;
    unsigned size;

    // grab pic0.png
    buffer = Iso::fastExtract("PIC0.PNG", &size);
    if (buffer != NULL){
        this->pic0 = new Image(buffer, YA2D_PLACE_RAM);
        free(buffer);
        buffer = NULL;
    }

    // grab pic1.png
    buffer = Iso::fastExtract("PIC1.PNG", &size);
    if (buffer != NULL){
        this->pic1 = new Image(buffer, YA2D_PLACE_RAM);
        free(buffer);
        buffer = NULL;
    }
}

void Iso::loadAVMedia(){
    this->icon1 = NULL;
    this->snd0 = NULL;
    this->at3_size = 0;
    this->icon1_size = 0;

    void* buffer = NULL;
    unsigned size;
    
    // grab snd0.at3
    buffer = Iso::fastExtract("SND0.AT3", &size);
    if (buffer != NULL){
        this->snd0 = buffer;
        this->at3_size = size;
        buffer = NULL;
        size = 0;
    }
    
    // grab icon1.pmf
    buffer = Iso::fastExtract("ICON1.PMF", &size);
    if (buffer != NULL){
        this->icon1 = buffer;
        this->icon1_size = size;
        buffer = NULL;
        size = 0;
    }
}

void Iso::doExecute(){
    char dlc_path[256];
    char* eboot_path;
    if (has_installed_file("PBOOT.PBP", dlc_path)){
        eboot_path = dlc_path;
    }
    else{
        if (isPatched())
            eboot_path = UMD_EBOOT_OLD;
        else if (hasPlainBoot())
            eboot_path = UMD_BOOT_BIN;
        else eboot_path = UMD_EBOOT_BIN;
    }
    Iso::executeISO(this->path.c_str(), eboot_path);
}

void Iso::executeISO(const char* path, char* eboot_path){
    struct SceKernelLoadExecVSHParam param;
    
    memset(&param, 0, sizeof(param));

    static int apitypes[2][2] = {
        {ISO_RUNLEVEL, ISO_PBOOT_RUNLEVEL},
        {ISO_RUNLEVEL_GO, ISO_PBOOT_RUNLEVEL_GO}
    };

    int sel1 = (strncmp(path, "ms", 2) == 0)? 0:1;
    int sel2 = (strstr(eboot_path, "/PBOOT.PBP") == NULL)? 0:1;
    int apitype = apitypes[sel1][sel2];

    param.size = sizeof(param);
    param.key = "umdemu";
    param.argp = eboot_path;
    param.args = strlen(eboot_path)+1;
    sctrlSESetDiscType(PSP_UMD_TYPE_GAME);
    sctrlSESetBootConfFileIndex(ISO_DRIVER);
    sctrlSESetUmdFile((char*)path);
    sctrlKernelLoadExecVSHWithApitype(apitype, path, &param);
}

int Iso::checkAudioVideo(){
    int type = 0;

    SceUID fd = sceIoOpen(path.c_str(), PSP_O_RDONLY, 0777);
    g_ciso_file.reader_arg = (void*)fd;
    this->is_compressed = ciso_open(&g_ciso_file);
    this->read_iso_data(fd, temp_block, ISO_SECTOR_SIZE*2, 32926);
    ciso_close(&g_ciso_file);
    sceIoClose(fd);

    for (int i=0; i<ISO_SECTOR_SIZE*2; i++){
        if (strcmp((char*)&temp_block[i], "UMD_VIDEO") == 0){
            type |= PSP_UMD_TYPE_VIDEO;
        }
        else if (strcmp((char*)&temp_block[i], "UMD_AUDIO") == 0){
            type |= PSP_UMD_TYPE_AUDIO;
        }
    }
    return type;
}

void Iso::executeVideoISO(const char* path)
{
    
    Iso iso = Iso(path);

    int type = iso.checkAudioVideo();

    if (type == 0) return;

    if(iso.fastExtract("EBOOT.BIN", NULL)) {
        type |= PSP_UMD_TYPE_GAME;
    }

    sctrlSESetUmdFile((char*)path);
    sctrlSESetDiscType(type);
    
    ARKConfig* ark_config = &common::ark_config;
    if ((IS_VITA(ark_config) && !IS_VITA_ADR(ark_config)) || ark_config->launcher[0]){
        sctrlSESetBootConfFileIndex(MODE_INFERNO);
        sctrlSESetDiscType(type|PSP_UMD_TYPE_GAME);
        Eboot::executeEboot(common::argv[0]);
    }
    else{
        sctrlSESetBootConfFileIndex(MODE_VSHUMD);
        sctrlKernelExitVSH(NULL);
    }
}

int Iso::has_installed_file(const char* installed_file, char* out_path){
    // game ID is always at offset 0x8373 within the ISO
    int lba = 16;
    int pos = 883;

    char game_id[10];

    SceUID fd = sceIoOpen(path.c_str(), PSP_O_RDONLY, 0777);
    g_ciso_file.reader_arg = (void*)fd;
    this->is_compressed = ciso_open(&g_ciso_file);
    this->read_iso_data(fd, (u8*)game_id, 10, 0x8373);
    ciso_close(&g_ciso_file);
    sceIoClose(fd);


    // remove the dash in the middle: ULUS-01234 -> ULUS01234
    game_id[4] = game_id[5];
    game_id[5] = game_id[6];
    game_id[6] = game_id[7];
    game_id[7] = game_id[8];
    game_id[8] = game_id[9];
    game_id[9] = 0;

    // try to find the update file
    char path[256];
    char* devs[] = {"ms0:", "ef0:"};

    for (int i=0; i<2; i++){
        sprintf(path, "%s/PSP/GAME/%s/%s", devs[i], game_id, installed_file);
        fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
        if (fd >= 0){
            // found
            sceIoClose(fd);
            if (out_path) strcpy(out_path, path);
            return 1;
        }
    }
    // not found
    return 0;
}

char* Iso::getType(){
    return "ISO";
}

char* Iso::getSubtype(){
    return getType();
}

bool Iso::isPatched(){
    return (this->fastExtract("EBOOT.OLD") != NULL);
}

bool Iso::hasPlainBoot(){
    u32 magic = 0;
    unsigned size = sizeof(magic);
    this->fastExtract("BOOT.BIN", &size, &magic);
    return (magic != 0);
}

SfoInfo Iso::getSfoInfo(){
    SfoInfo info = this->Entry::getSfoInfo();
    unsigned int size = 0;
    unsigned char* sfo_buffer = (unsigned char*)fastExtract("PARAM.SFO", &size);
    if (sfo_buffer){
        int title_size = sizeof(info.title);
        Entry::getSfoParam(sfo_buffer, size, "TITLE", (unsigned char*)(info.title), &title_size);
        
        int id_size = sizeof(info.gameid);
        Entry::getSfoParam(sfo_buffer, size, "DISC_ID", (unsigned char*)(info.gameid), &id_size);

        free(sfo_buffer);
    }
    return info;
}

bool Iso::isISO(const char* filename){
    string ext = common::getExtension(filename);
    return (
        ext == "iso" ||
        ext == "img" ||
        ext == "cso" ||
        ext == "zso" ||
        ext == "jso" ||
        ext == "dax"
    );
}

int Iso::read_iso_data(SceUID fd, void* ptr, int size, int offset){
    if (this->is_compressed){
        g_ciso_file.reader_arg = (void*)fd;
        return ciso_read(&g_ciso_file, (u8*)ptr, size, offset);
    }
    return read_raw_data((void*)fd, ptr, size, offset);
}

void* Iso::fastExtract(const char* file, unsigned* size, void* buf){
    
    void* buffer = buf;
    if (size != NULL)
        *size = 0;

    SceUID fd = sceIoOpen(path.c_str(), PSP_O_RDONLY, 0777);
    g_ciso_file.reader_arg = (void*)fd;
    this->is_compressed = ciso_open(&g_ciso_file);
    
    if (file_cache.find(file) != file_cache.end()){
        if (size == NULL) return (void*)-1;
        FileData file_data = file_cache[file];
        if (buffer == NULL) {
            buffer = malloc(file_data.size);
            *size = file_data.size;
        }
        this->read_iso_data(fd, (u8*)buffer, *size, file_data.offset);
        sceIoClose(fd);
        ciso_close(&g_ciso_file);
        return buffer;
    }
    
    this->read_iso_data(fd, temp_block, 12, 32926);
    
    unsigned dir_lba = ((unsigned*)temp_block)[0];
    unsigned block_size = ((unsigned*)temp_block)[2];
    unsigned dir_start = dir_lba*block_size + block_size;
    
    this->read_iso_data(fd, temp_block, sizeof(temp_block), dir_start);
    
    for (int i=0; i<sizeof(temp_block); i++){
        if (strcasecmp((const char*)&temp_block[i], file) == 0){
            if (size == NULL){
                return (void*)-1;
            }
            u8* sfo = (u8*)&temp_block[i-31];
            FileData file_data;
            file_data.offset = (sfo[0] + (sfo[1]<<8) + (sfo[2]<<16) + (sfo[3]<<24))*block_size;
            file_data.size = (sfo[8] + (sfo[9]<<8) + (sfo[10]<<16) + (sfo[11]<<24));
            
            if (file_data.size == 0){
                sceIoClose(fd);
                ciso_close(&g_ciso_file);
                return NULL;
            }
            
            if (buffer == NULL) {
                buffer = malloc(file_data.size);
                *size = file_data.size;
            }

            this->read_iso_data(fd, (u8*)buffer, *size, file_data.offset);
            file_cache[file] = file_data;
            sceIoClose(fd);
            ciso_close(&g_ciso_file);
            return buffer;
        }
    }
    sceIoClose(fd);
    ciso_close(&g_ciso_file);
    return NULL;
}
