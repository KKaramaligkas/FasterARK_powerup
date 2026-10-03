#ifndef MATRIX_H
#define MATRIX_H

#include "anim.h"

#define MATRIX_MAX_CHARS 15
#define MATRIX_MAX_COLS 12

class Matrix : public Anim {

    private:

        char caRow[MATRIX_MAX_COLS][MATRIX_MAX_CHARS+1];
        int r;
        int cur_col, cur_row;
        
        void drawColumn(int xoffset, int i);
    
    public:
        Matrix();
        ~Matrix();

        int getId();
        
        void draw();
        
        bool canDrawBackground();
};

#endif