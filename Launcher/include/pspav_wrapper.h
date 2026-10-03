#ifndef PSPAV_WRAPPER_H
#define PSPAV_WRAPPER_H

#include <pspav/pspav.h>

#include "entry.h"

extern PSPAVCallbacks arkmenu_av_callbacks;
PSPAVEntry convertEntry(Entry* e);

#endif