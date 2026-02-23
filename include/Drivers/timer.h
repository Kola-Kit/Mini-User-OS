#ifndef TIMER_H
#define TIMER_H

#include "Tables/idt.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_FREQUENCY 1193182

void timer_init(int frequency);
void timer_handler(interrupt_frame_t* frame);

#endif