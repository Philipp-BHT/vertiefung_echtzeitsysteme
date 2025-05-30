#include <rtems.h>
#include <stdio.h>
#include <stdlib.h>
#include <bsp.h> 

#define GPIO4 4
#define GPIO18 18

volatile rtems_interval blink_interval_ticks = RTEMS_MILLISECONDS_TO_TICKS(1000);
volatile int gpio4_state = 0;
volatile int toggle_count = 0;

void gpio_write(int gpio, int value) {
    // replace with actual GPIO register access or BSP function
}

void gpio_toggle(int gpio) {
    gpio4_state = !gpio4_state;
    gpio_write(gpio, gpio4_state);
}

int gpio_read(int gpio) {
    return gpio4_state; 
}

rtems_isr gpio18_isr(rtems_vector_number vector) {
    static int gpio18_state = 0;
    gpio18_state = !gpio18_state;
    gpio_write(GPIO18, gpio18_state);
    toggle_count++;
}

rtems_task gpio4_task(rtems_task_argument arg) {
    while (1) {
        gpio_write(GPIO4, 1);
        rtems_task_wake_after(blink_interval_ticks);
        gpio_write(GPIO4, 0);
        rtems_task_wake_after(blink_interval_ticks);
    }
}

rtems_task logger_task(rtems_task_argument arg) {
    while (1) {
        printf("GPIO4: %d, Toggles: %d\n", gpio_read(GPIO4), toggle_count);
        rtems_task_wake_after(RTEMS_MILLISECONDS_TO_TICKS(1000)); 
    }
}

rtems_task Init(rtems_task_argument ignored) {
    printf("RTEMS LED Blink with Interrupt Example\n");

    gpio_write(GPIO4, 0);
    gpio_write(GPIO18, 0);

    rtems_interrupt_handler_install(
        /* vector */ YOUR_GPIO18_IRQ_VECTOR,
        /* name   */ "GPIO18 ISR",
        /* attr   */ RTEMS_INTERRUPT_UNIQUE,
        /* handler*/ gpio18_isr,
        /* arg    */ NULL
    );


    rtems_id gpio4_id;
    rtems_task_create(
        rtems_build_name('G','P','4','T'),
        1, RTEMS_MINIMUM_STACK_SIZE, RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES,
        &gpio4_id
    );
    rtems_task_start(gpio4_id, gpio4_task, 0);


    rtems_id logger_id;
    rtems_task_create(
        rtems_build_name('L','O','G','T'),
        1, RTEMS_MINIMUM_STACK_SIZE, RTEMS_DEFAULT_MODES, RTEMS_DEFAULT_ATTRIBUTES,
        &logger_id
    );
    rtems_task_start(logger_id, logger_task, 0);

    rtems_task_delete(RTEMS_SELF);
}


#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER  
#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_MAXIMUM_TASKS 4