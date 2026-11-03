#ifndef INDICATORS_H
#define INDICATORS_H

#include "led_strip.h"

class Indicators {
    private:
        led_strip_handle_t led_strip;
    public:
        void init();

        void setRed();
        void setGreen();
        void setBlue();
        void turnOff(); 
};

#endif