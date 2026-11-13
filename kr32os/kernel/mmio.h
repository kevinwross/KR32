#pragma once

/* Polled serial console. */
#define SERIAL_BASE 0xFFF05000U

struct serial_registers {
    volatile unsigned int data;
    volatile unsigned int status;
};

#define SERIAL_RX_READY 1U
#define SERIAL_TX_READY 2U
#define SERIAL_DATA_MASK 0xFFU

/* Programmable interval timer. */
#define TIMER_BASE 0xFFF00000U

struct timer_registers {
    volatile unsigned int control;
    volatile unsigned int reload;
    volatile unsigned int count;
    volatile unsigned int status;
    volatile unsigned int irq;
};

#define TIMER_CONTROL_ENABLE 1U
#define TIMER_CONTROL_PERIODIC 2U
#define TIMER_CONTROL_IRQ 4U
#define TIMER_STATUS_PENDING 1U

/* IRQ router: route line 0 to core 1. */
#define IRQ_ROUTER_BASE 0xFFF03000U

struct irq_router {
    volatile unsigned int route[16];
};

#define IRQ_ROUTE_TIMER 0U
#define IRQ_CORE1 1U

/* Framebuffer: 32 MiB aperture plus the control page. */
#define FRAMEBUFFER_BASE 0xFC000000U
#define FRAMEBUFFER_CONTROL_BASE 0xFFF04000U

struct framebuffer_registers {
    volatile unsigned int control;
    volatile unsigned int width;
    volatile unsigned int height;
    volatile unsigned int stride;
    volatile unsigned int format;
    volatile unsigned int front;
    volatile unsigned int back;
    volatile unsigned int status;
    volatile unsigned int flip;
    volatile unsigned int frame;
    volatile unsigned int id;
};

#define FRAMEBUFFER_STATUS_ENABLED 1U
#define FRAMEBUFFER_STATUS_MODE_OK 2U
#define FRAMEBUFFER_STATUS_FLIP_ERROR 4U
#define FRAMEBUFFER_ID_VALUE 0x46425231U

/* Keyboard: packed events, pop by reading DATA. */
#define KEYBOARD_BASE 0xFFF06000U

struct keyboard_registers {
    volatile unsigned int data;
    volatile unsigned int status;
    volatile unsigned int control;
    volatile unsigned int irq;
};

#define KEYBOARD_STATUS_AVAILABLE 1U
#define KEYBOARD_STATUS_OVERRUN 2U

/* Host terminal (page 8). The guest writes characters; the host renders. */
struct terminal_registers {
    volatile unsigned int data;
    volatile unsigned int status;
    volatile unsigned int control;
    volatile unsigned int command;
    volatile unsigned int color;
    volatile unsigned int id;
};

#define TERMINAL_BASE 0xFFF08000U
#define TERMINAL_ID_VALUE 0x5445524DU
#define TERMINAL_COMMAND_CLEAR 1U
#define TERMINAL_COMMAND_RESET 2U