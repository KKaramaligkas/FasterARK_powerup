#ifndef NOANIM_H
#define NOANIM_H

#include "anim.h"

class NoAnim : public Anim {
    
    public:
        NoAnim();
        ~NoAnim();
        int getId();
        void draw();
};

#endif
