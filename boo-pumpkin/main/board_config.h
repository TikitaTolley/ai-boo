#pragma once
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// Disable flash LED PWM so it cannot claim the camera clock channel.
#ifdef LED_GPIO_NUM
#undef LED_GPIO_NUM
#endif
