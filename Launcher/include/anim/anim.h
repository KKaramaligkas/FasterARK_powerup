#ifndef ANIM_H
#define ANIM_H

class Anim {

    public:
    
        Anim();
        virtual ~Anim() = 0;

        virtual int getId() = 0;

        virtual void draw() = 0;
        
        /* Returns false if the animation overwrites the menu's background */
        virtual bool canDrawBackground();
};

#endif
