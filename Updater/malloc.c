#include <pspsdk.h>
#include <pspkernel.h>
#include <pspsysmem.h>

extern void* memset(void* buffer, int value, unsigned int size);

/*
 * The updater's own allocator: every block comes straight from the user partition.
 * psp-cfw-sdk's libpspmalloc now serves everything from a single 16 KB VPL (and its
 * memalign fails until malloc has run once), far too little for the background texture
 * and libpng, so the updater crashed on a NULL texture before drawing anything.
 */

void* memalign(unsigned int align, unsigned int size){
    if (align < sizeof(SceUID))
        align = sizeof(SceUID);
    if (align & (align-1))
        return NULL;
    // room for the block id in front of the aligned pointer, and for the alignment itself
    SceUID uid = sceKernelAllocPartitionMemory(PSP_MEMORY_PARTITION_USER, "", PSP_SMEM_High, size + sizeof(SceUID) + align, NULL);
    if (uid < 0)
        return NULL;
    u32 base = (u32)sceKernelGetBlockHeadAddr(uid);
    SceUID* ptr = (SceUID*)((base + sizeof(SceUID) + align - 1) & ~(align-1));
    ptr[-1] = uid;
    return ptr;
}

void* malloc(unsigned int size){
    return memalign(16, size);
}

void* calloc(unsigned int n, unsigned int size){
    if (size && n > 0xFFFFFFFF/size)
        return NULL;
    void* ptr = malloc(n*size);
    if (ptr)
        memset(ptr, 0, n*size);
    return ptr;
}

void free(void* ptr){
    if (ptr)
        sceKernelFreePartitionMemory(((SceUID*)ptr)[-1]);
}
