#include "controller.h"
#include "common.h"
#include <kubridge.h>
#include <systemctrl.h>

#define CONTROL_DELAY 10

static void *_ksceCtrlReadBufferPositive;

Controller::Controller(){
    memset(&pad, 0, sizeof(pad));
    this->nowpad = 0;
    this->newpad = 0;
    this->oldpad = 0;
    this->n = 0;
    if (_ksceCtrlReadBufferPositive == NULL)
        _ksceCtrlReadBufferPositive = (void *)sctrlHENFindFunction("sceController_Service", "sceCtrl_driver", 0x1F803938);
}

Controller::~Controller(){
}

void Controller::clCtrlReadBufferPositive(){
    if (_ksceCtrlReadBufferPositive != NULL){
        struct KernelCallArg args;
        args.arg1 = (u32)&pad;  // Pad
        args.arg2 = 1;          // Count
        args.arg3 = 0;          // ??
        args.arg4 = 0;          // Mode
        kuKernelCall(_ksceCtrlReadBufferPositive, &args); // Call from kernel mode to get access to more inputs
    }else { // Fallback to usermode
        sceCtrlReadBufferPositive(&pad, 1);
    }
}
        
void Controller::update(int ignore){
    for (int i=0; i<=ignore; i++)
        clCtrlReadBufferPositive();
    
    nowpad = pad.Buttons;
    newpad = nowpad & ~oldpad;
    
    if (oldpad == nowpad){
        n++;
        if (n >= CONTROL_DELAY){
            newpad = nowpad;
        }
    }
    else {
        n = 0;
        oldpad = nowpad;
    }
}

void Controller::flush(){
    memset(&pad, 0, sizeof(pad));
    this->nowpad = 0;
    this->newpad = 0;
    this->oldpad = 0;
    while (pad.Buttons&0xFFFF)
        clCtrlReadBufferPositive();
}

bool Controller::wait(void* busy_wait){
    bool ret;
    while (true){
        if (busy_wait != NULL){
            void (*exec)(void) = (void (*)())busy_wait;
            exec();
        }
        this->update();
        if (this->cross()){
            ret = true;
            break;
        }
        else if (this->circle()){
            ret = false;
            break;
        }
    }
    return ret;
}

bool Controller::any(){
    return ((newpad&0xFFFF)!=0);
}

bool Controller::accept(){
    return (common::config.swap_buttons)? this->circle() : this->cross();
}

bool Controller::decline(){
    return (common::config.swap_buttons)? this->cross() : this->circle();
}
        
bool Controller::up(){
    return (newpad & PSP_CTRL_UP);
}

bool Controller::down(){
    return (newpad & PSP_CTRL_DOWN);
}

bool Controller::left(){
    return (newpad & PSP_CTRL_LEFT);
}

bool Controller::right(){
    return (newpad & PSP_CTRL_RIGHT);
}

bool Controller::cross(){
    return (newpad & PSP_CTRL_CROSS);
}

bool Controller::circle(){
    return (newpad & PSP_CTRL_CIRCLE);
}

bool Controller::square(){
    return (newpad & PSP_CTRL_SQUARE);
}

bool Controller::triangle(){
    return (newpad & PSP_CTRL_TRIANGLE);
}

bool Controller::RT(){
    return (newpad & PSP_CTRL_RTRIGGER);
}

bool Controller::LT(){
    return (newpad & PSP_CTRL_LTRIGGER);
}

bool Controller::start(){
    return (newpad & PSP_CTRL_START);
}

bool Controller::select(){
    return (newpad & PSP_CTRL_SELECT);
}

bool Controller::home(){
    return (newpad & PSP_CTRL_HOME);
}

bool Controller::volume(){
    return (newpad & (PSP_CTRL_VOLUP | PSP_CTRL_VOLDOWN));
}

bool Controller::volup(){
    return (newpad & PSP_CTRL_VOLUP);
}

bool Controller::voldown(){
    return (newpad & PSP_CTRL_VOLDOWN);
}

bool Controller::mute(){
    return (newpad & PSP_CTRL_NOTE);
}

