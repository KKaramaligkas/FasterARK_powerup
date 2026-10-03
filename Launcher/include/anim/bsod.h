#ifndef BSOD_H
#define BSOD_H

#include "anim.h"

#define BSOD_MAX_CHARS 40
#define BSOD_MAX_ROWS 7

class BSoD : public Anim {

    private:
        char caRow[BSOD_MAX_ROWS][BSOD_MAX_CHARS+1];
        int cur_row, cur_col, r;
    
    public:
        BSoD();
        ~BSoD();

        int getId();
        
        void draw();
        
        bool canDrawBackground();
};

#endif