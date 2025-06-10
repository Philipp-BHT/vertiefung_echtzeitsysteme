#include <rtems.h>
#include <stdio.h>
#include <stdlib.h>

/* Configuration */

#define CONFIGURE_INIT
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER

#define CONFIGURE_MAXIMUM_TASKS        3
#define CONFIGURE_MAXIMUM_DRIVERS      10
#define CONFIGURE_MAXIMUM_SEMAPHORES   4
#define CONFIGURE_MAXIMUM_MESSAGE_QUEUES 2

#include <rtems/confdefs.h>
/* Thread entry functions */
rtems_task thread_one(rtems_task_argument argument) {
    printf("Hello from Thread 1!\n");
    rtems_task_delete(RTEMS_SELF);
}

rtems_task thread_two(rtems_task_argument argument) {
    printf("Hello from Thread 2!\n");
    rtems_task_delete(RTEMS_SELF);
}

/* Init task (like main) */
rtems_task Init(rtems_task_argument argument) {
    rtems_id tid1, tid2;
    rtems_status_code status;

    /* Create Thread 1 */
    status = rtems_task_create(
        rtems_build_name('T', '1', ' ', ' '), // name
        200,                                    // priority
        RTEMS_MINIMUM_STACK_SIZE,             // stack size
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &tid1
    );
    if (status != RTEMS_SUCCESSFUL) {
        printf("Failed to create Thread 1\n");
        rtems_task_exit();
        
    }

    /* Create Thread 2 */
    status = rtems_task_create(
        rtems_build_name('T', '2', ' ', ' '),
        200,
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &tid2
    );
    if (status != RTEMS_SUCCESSFUL) {
        printf("Failed to create Thread 2\n");
        rtems_task_exit();
    }

    /* Start threads */
    rtems_task_start(tid1, thread_one, 0);
    rtems_task_start(tid2, thread_two, 0);

    /* Exit Init task (optional) */
    rtems_task_delete(RTEMS_SELF);
}


