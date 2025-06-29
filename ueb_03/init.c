#include <rtems.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <bsp.h> 

#define CONFIGURE_INIT
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER

#define CONFIGURE_MAXIMUM_TASKS        4
#define CONFIGURE_MAXIMUM_DRIVERS      10
#define CONFIGURE_MAXIMUM_SEMAPHORES   4
#define CONFIGURE_MAXIMUM_MESSAGE_QUEUES 2

#include <rtems/confdefs.h>

#define GPIO_BASE 0x3F200000
#define REG32(addr) (*(volatile uint32_t *)(addr))
#define GPIO_REG(offset) REG32(GPIO_BASE + offset)

#define GPFSEL_OFFSET   0x00  // GPIO Function Select (GPFSEL0–5)
#define GPSET_OFFSET    0x1C  // GPIO Pin Output Set (GPSET0–1)
#define GPCLR_OFFSET    0x28  // GPIO Pin Output Clear (GPCLR0–1)
#define GPLEV_OFFSET    0x34  // GPIO Pin Level (GPLEV0–1)
#define GPPUD_OFFSET    0x94  // GPIO Pull-up/down Register (deprecated in Pi 3+)
#define GPPUDCLK_OFFSET 0x98  // Clock pull-up/down setting
#define GPREN_OFFSET    0x4C  // Rising edge detect enable
#define GPFEN_OFFSET    0x58  // Falling edge detect enable
#define GPEDS_OFFSET    0x40  // Event detect status
#define GPIO_FALLING_EDGE_REG GPIO_REG(GPFEN_OFFSET + (gpio / 32) * 4)
#define GPIO_EVENT_STATUS_REG GPIO_REG(GPEDS_OFFSET + (gpio / 32) * 4)
#define UART0_BASE 0x3F201000
#define UART0_FR   (*(volatile uint32_t *)(UART0_BASE + 0x18))  // Flag Register
#define UART0_DR   (*(volatile uint32_t *)(UART0_BASE + 0x00))  // Data Register


#define GPIO4 4
#define GPIO18 18
#define GPIO23 23
#define GPIO_IRQ_VECTOR 49 
#define UART_BUFFER_LEN 64
#define ENABLE 1
#define DEBUG_MODE 0

volatile rtems_interval blink_interval_ticks;
volatile int gpio4_state = 0;
volatile int toggle_count = 0;
volatile int gpio23_state = 0;
rtems_id print_sem;
rtems_id toggle_sem;
rtems_id uart_sem;
rtems_id interval_sem;

void gpio_set_mode(int gpio, int mode) {
    // mode: 0 = input, 1 = output
    int reg = gpio / 10;
    int shift = (gpio % 10) * 3;
    uint32_t fsel = GPIO_REG(GPFSEL_OFFSET + reg * 4);
    fsel &= ~(0x7 << shift);        // Clear existing function
    fsel |= ((mode ? 1 : 0) << shift); // Set 001 for output, 000 for input
    GPIO_REG(GPFSEL_OFFSET + reg * 4) = fsel;
}

void gpio_write(int gpio, int value) {
    if (value)
        GPIO_REG(GPSET_OFFSET + (gpio / 32) * 4) = (1 << (gpio % 32));
    else
        GPIO_REG(GPCLR_OFFSET + (gpio / 32) * 4) = (1 << (gpio % 32));
}

int gpio_read(int gpio) {
    uint32_t val = GPIO_REG(GPLEV_OFFSET + (gpio / 32) * 4);
    return (val & (1 << (gpio % 32))) ? 1 : 0;
}

void gpio_toggle(int gpio) {
    int current = gpio_read(gpio);
    toggle_count++;
    gpio_write(gpio, !current);
}

void gpio_set_pull(int gpio, int pull) {
    GPIO_REG(GPPUD_OFFSET) = pull;
    for (volatile int i = 0; i < 150; i++);
    GPIO_REG(GPPUDCLK_OFFSET) = (1 << gpio);
    for (volatile int i = 0; i < 150; i++);
    GPIO_REG(GPPUD_OFFSET) = 0;
    GPIO_REG(GPPUDCLK_OFFSET) = 0;
}

void gpio_set_falling_edge(int gpio) {
    if (ENABLE) 
        GPIO_REG(GPFEN_OFFSET + (gpio / 32) * 4) |= (1 << (gpio % 32));
    else
        GPIO_REG(GPFEN_OFFSET + (gpio / 32) * 4) &= ~(1 << (gpio % 32));
}

void gpio23_isr(void *arg) {
    gpio23_state = !gpio23_state;
    gpio_write(GPIO18, gpio23_state);
    rtems_semaphore_obtain(toggle_sem, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
    toggle_count++;
    rtems_semaphore_release(toggle_sem);
    //printf("ISR triggered\n");

    // Clear GPIO23 interrupt event
    GPIO_REG(GPEDS_OFFSET + (GPIO23 / 32) * 4) |= (1 << (GPIO23 % 32));
}

rtems_task gpio4_task(rtems_task_argument arg) {
    while (1) {
        gpio_write(GPIO4, 1);

        rtems_semaphore_obtain(print_sem, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
        printf("GPIO4 On\n");
        rtems_semaphore_release(print_sem);
        rtems_semaphore_obtain(interval_sem, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
        rtems_interval local_ticks = blink_interval_ticks;
        rtems_semaphore_release(interval_sem);

        rtems_task_wake_after(local_ticks);
        gpio_write(GPIO4, 0);
        if (DEBUG_MODE)
            gpio_toggle(GPIO18);

        rtems_semaphore_obtain(print_sem, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
        printf("GPIO4 Off\n");
        rtems_semaphore_release(print_sem);


        rtems_task_wake_after(blink_interval_ticks);
    }
}

rtems_task logger_task(rtems_task_argument arg) {
    while (1) {
        rtems_semaphore_obtain(toggle_sem, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
        int count = toggle_count;
        rtems_semaphore_release(toggle_sem);
        
        rtems_semaphore_obtain(print_sem, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
        printf("GPIO18 Status: %s,  GPIO18 Toggles: %d\n",
            gpio_read(GPIO18) ? "On" : "Off", count);
        rtems_semaphore_release(print_sem);

        rtems_semaphore_obtain(interval_sem, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
        rtems_interval local_ticks = blink_interval_ticks;
        rtems_semaphore_release(interval_sem);

        rtems_task_wake_after(local_ticks);
    }
}

char uart0_getchar() {
    while (UART0_FR & (1 << 4)) {
        rtems_task_wake_after(RTEMS_YIELD_PROCESSOR); // let others run
    }
    return UART0_DR & 0xFF;
}

rtems_task uart_reader_task(rtems_task_argument arg) {
    char buffer[UART_BUFFER_LEN];
    int idx = 0;

    rtems_semaphore_obtain(print_sem, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
    printf("UART input ready. Type a number and press Enter to set blink interval (ms).\n");
    rtems_semaphore_release(print_sem);

    while (1) {
        // Blocking read from UART0
        while (UART0_FR & (1 << 4)) {
            rtems_task_wake_after(1); // prevent starvation
        }

        char c = UART0_DR & 0xFF;

        // Handle newline (end of command)
        if (c == '\n' || c == '\r') {
            buffer[idx] = '\0';

            // Validate: check if all chars are digits
            int valid = 1;
            for (int i = 0; i < idx; i++) {
                if (buffer[i] < '0' || buffer[i] > '9') {
                    valid = 0;
                    break;
                }
            }

            if (valid && idx > 0) {
                int val = atoi(buffer);
                if (val > 10000)
                    val = 10000;
                else if (val < 1)
                    val = 1;

                rtems_semaphore_obtain(interval_sem, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
                blink_interval_ticks = RTEMS_MILLISECONDS_TO_TICKS(val);
                rtems_semaphore_release(interval_sem);

                rtems_semaphore_obtain(print_sem, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
                printf("Blink interval updated to %d ms.\n", val);
                rtems_semaphore_release(print_sem);
            } else {

                rtems_semaphore_obtain(print_sem, RTEMS_WAIT, RTEMS_NO_TIMEOUT);    
                printf("Invalid input. Please enter only numbers.\n");
                rtems_semaphore_release(print_sem);
            }

            idx = 0; // reset buffer
        }
        // Add to buffer if printable and space left
        else if (idx < UART_BUFFER_LEN - 1 && c >= 32 && c <= 126) {
        buffer[idx++] = c;
    }
    rtems_task_wake_after(1); 
    }
}

rtems_task Init(rtems_task_argument ignored) {
    printf("RTEMS LED Blink with Interrupt Example\n");
    blink_interval_ticks = RTEMS_MILLISECONDS_TO_TICKS(1000);

    GPIO_REG(GPFEN_OFFSET + (GPIO18 / 32) * 4) |= (1 << (GPIO18 % 32));

    gpio_set_mode(GPIO4, 1);       // output
    gpio_set_mode(GPIO18, 1);      // output
    gpio_set_mode(GPIO23, 0);      // input
    gpio_set_pull(GPIO23, 1);      // pull-down
    gpio_set_falling_edge(GPIO23);
    gpio_write(GPIO4, 0); 
    gpio_write(GPIO18, 0);    

    rtems_status_code sc;

    sc = rtems_semaphore_create(
        rtems_build_name('P','R','N','T'),
        1,
        RTEMS_BINARY_SEMAPHORE | RTEMS_PRIORITY,
        0,
        &print_sem
    );
    if (sc != RTEMS_SUCCESSFUL) {
        printf("Failed to create print_sem: %s\n", rtems_status_text(sc));
        rtems_fatal_error_occurred(sc);
    }

    sc = rtems_semaphore_create(
        rtems_build_name('T','O','G','L'),
        1,
        RTEMS_BINARY_SEMAPHORE | RTEMS_PRIORITY,
        0,
        &toggle_sem
    );
    if (sc != RTEMS_SUCCESSFUL) {
        printf("Failed to create toggle_sem: %s\n", rtems_status_text(sc));
        rtems_fatal_error_occurred(sc);
    }

    sc = rtems_semaphore_create(
        rtems_build_name('I','N','T','V'),
        1,
        RTEMS_BINARY_SEMAPHORE | RTEMS_PRIORITY,
        0,
        &interval_sem
    );
    if (sc != RTEMS_SUCCESSFUL) {
        printf("Failed to create interval_sem: %s\n", rtems_status_text(sc));
        rtems_fatal_error_occurred(sc);
    }

    sc = rtems_interrupt_handler_install(
        /* vector */ GPIO_IRQ_VECTOR,
        /* name   */ "GPIO23 ISR",
        /* attr   */ RTEMS_INTERRUPT_UNIQUE,
        /* handler*/ gpio23_isr,
        /* arg    */ NULL
    );
    if (sc != RTEMS_SUCCESSFUL) {
        printf("Failed to install GPIO23 ISR: %s\n", rtems_status_text(sc));
        rtems_fatal_error_occurred(sc);
    }


    rtems_id gpio4_id;
    sc = rtems_task_create(
        rtems_build_name('G','P','4','T'),
        50, RTEMS_MINIMUM_STACK_SIZE, RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES,
        &gpio4_id
    );
    if (sc != RTEMS_SUCCESSFUL) {
        printf("Failed to create gpio4 task: %s\n", rtems_status_text(sc));
        rtems_fatal_error_occurred(sc);
    }
    sc = rtems_task_start(gpio4_id, gpio4_task, 0);
    if (sc != RTEMS_SUCCESSFUL) {
        printf("Failed to start gpio4 task: %s\n", rtems_status_text(sc));
        rtems_fatal_error_occurred(sc);
    }

    rtems_id logger_id;
    sc = rtems_task_create(
        rtems_build_name('L','O','G','T'),
        50, RTEMS_MINIMUM_STACK_SIZE, RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES,
        &logger_id
    );
    if (sc != RTEMS_SUCCESSFUL) {
        printf("Failed to create logger task: %s\n", rtems_status_text(sc));
        rtems_fatal_error_occurred(sc);
    }
    sc = rtems_task_start(logger_id, logger_task, 0);
    if (sc != RTEMS_SUCCESSFUL) {
        printf("Failed to start logger task: %s\n", rtems_status_text(sc));
        rtems_fatal_error_occurred(sc);
    }


    rtems_id uart_reader_id;
    sc = rtems_task_create(
        rtems_build_name('U','A','R','T'),
        10, RTEMS_MINIMUM_STACK_SIZE, RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES,
        &uart_reader_id
    );
    if (sc != RTEMS_SUCCESSFUL) {
        printf("Failed to create uart_reader task: %s\n", rtems_status_text(sc));
        rtems_fatal_error_occurred(sc);
    }
    sc = rtems_task_start(uart_reader_id, uart_reader_task, 0);
    if (sc != RTEMS_SUCCESSFUL) {
        printf("Failed to start uart_reader task: %s\n", rtems_status_text(sc));
        rtems_fatal_error_occurred(sc);
    }

    rtems_task_delete(RTEMS_SELF);
}
