#ifndef HACKER_H
#define HACKER_H

#include "anim.h"

#define HACKER_MAX_CHARS 64
#define HACKER_MAX_ROWS 42

class Hacker : public Anim {

    private:

        char caRow[HACKER_MAX_ROWS][HACKER_MAX_CHARS+1];
        int cur_row, r;
    
    public:
        Hacker();
        ~Hacker();

        int getId();
        
        void draw();
        
        bool canDrawBackground();
};

#endif
