#include <rtems.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bsp.h>
#include <inttypes.h>
#include <stdarg.h>

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER
#define CONFIGURE_UNLIMITED_ALLOCATION_SIZE
#define CONFIGURE_MAXIMUM_TASKS        5
#define CONFIGURE_MAXIMUM_DRIVERS      10
#define CONFIGURE_MAXIMUM_SEMAPHORES   4
#define CONFIGURE_MAXIMUM_MESSAGE_QUEUES 2
#define CONFIGURE_TICKS_PER_SECOND 1000
#define CONFIGURE_EXECUTIVE_RAM_SIZE (580 * 1024) 
#define CONFIGURE_UNIFIED_WORK_AREAS
#define CONFIGURE_INIT

#include <rtems/confdefs.h>

#define GPIO_BASE 0x3F200000
#define REG32(addr) (*(volatile uint32_t *)(addr))
#define GPIO_REG(offset) REG32(GPIO_BASE + offset)

#define GPFSEL_OFFSET   0x00
#define GPSET_OFFSET    0x1C
#define GPCLR_OFFSET    0x28
#define GPLEV_OFFSET    0x34
#define GPPUD_OFFSET    0x94
#define GPPUDCLK_OFFSET 0x98
#define GPREN_OFFSET    0x4C
#define GPFEN_OFFSET    0x58
#define GPEDS_OFFSET    0x40

#define GPIO4 4
#define GPIO_IRQ_VECTOR 49
#define MAX_MANCHESTER_BITS 580
#define MAX_EDGES 580
#define PULSE_JITTER 0.25

bool dev = false; 

static char manchester_sequence[MAX_MANCHESTER_BITS];
static int manchester_index = 0;
static char binary_sequence[MAX_MANCHESTER_BITS / 2];  
static int binary_sequence_index = 0;
bool manchester_overflow = false;

rtems_id print_sem;
rtems_id edge_event_queue_id;

static uint64_t min_pulse = 0;

typedef struct {
    uint64_t timestamp;
    bool level;
} Edge;

void gpio_set_mode(int gpio, int mode) {
    int reg = gpio / 10;
    int shift = (gpio % 10) * 3;
    uint32_t fsel = GPIO_REG(GPFSEL_OFFSET + reg * 4);
    fsel &= ~(0x7 << shift);
    fsel |= ((mode ? 1 : 0) << shift);
    GPIO_REG(GPFSEL_OFFSET + reg * 4) = fsel;
}

int gpio_read(int gpio) {
    return (GPIO_REG(GPLEV_OFFSET + (gpio / 32) * 4) & (1 << (gpio % 32))) != 0;
}

void gpio_set_edge_detect(int gpio, int rising, int falling) {
    if (rising)
        GPIO_REG(GPREN_OFFSET + (gpio / 32) * 4) |= (1 << (gpio % 32));
    if (falling)
        GPIO_REG(GPFEN_OFFSET + (gpio / 32) * 4) |= (1 << (gpio % 32));
}

void gpio_set_pull(int gpio, int pull) {
    GPIO_REG(GPPUD_OFFSET) = pull;
    for (volatile int i = 0; i < 150; i++);
    GPIO_REG(GPPUDCLK_OFFSET) = (1 << gpio);
    for (volatile int i = 0; i < 150; i++);
    GPIO_REG(GPPUD_OFFSET) = 0;
    GPIO_REG(GPPUDCLK_OFFSET) = 0;
}

void safe_print(const char *format, ...) {
    va_list args;
    va_start(args, format);
    rtems_semaphore_obtain(print_sem, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
    vprintf(format, args);
    rtems_semaphore_release(print_sem);
    va_end(args);
}

rtems_isr gpio_isr(void *arg) {
    Edge evt = {
        .timestamp = rtems_clock_get_uptime_nanoseconds(),
        .level = gpio_read(GPIO4)
    };
    rtems_message_queue_send(edge_event_queue_id, &evt, sizeof(evt));
    GPIO_REG(GPEDS_OFFSET + (GPIO4 / 32) * 4) |= (1 << (GPIO4 % 32));
}

rtems_task edge_decoder_task(rtems_task_argument arg) {
    Edge event, last_event = {0};
    size_t size;

    const rtems_interval timeout_ticks = 20 * rtems_clock_get_ticks_per_second();

    while (1) {
        rtems_status_code sc = rtems_message_queue_receive(edge_event_queue_id, &event, &size, RTEMS_WAIT, timeout_ticks);

        if (sc == RTEMS_SUCCESSFUL) {
            if (last_event.timestamp == 0) {
                last_event.timestamp = event.timestamp;
                last_event.level = event.level;
                continue;
            }

            if (manchester_index < MAX_MANCHESTER_BITS - 2) {
                
                uint64_t delta_ns = event.timestamp - last_event.timestamp;

                if (manchester_index == 0) {
                    manchester_sequence[manchester_index++] = last_event.level ? '1' : '0';
                }

                
                if (min_pulse == 0 && delta_ns > 0) {
                    min_pulse = delta_ns;
                    safe_print("Pulse width determined: Minimum pulse set to ~%" PRIu64 " ns.\n", min_pulse);
                }
                
                if (min_pulse > 0) {
                    uint64_t threshold = min_pulse + (uint64_t)(min_pulse * PULSE_JITTER);

                        if (delta_ns > threshold) {
                            manchester_sequence[manchester_index++] = last_event.level ? '1' : '0';
                            manchester_sequence[manchester_index++] = event.level ? '1' : '0';
                        } else {
                            manchester_sequence[manchester_index++] = event.level ? '1' : '0';
                        }
                } 
            }
            else if (!manchester_overflow) {
                safe_print("Manchester buffer overflow. Ignoring further edges.\n");
                manchester_overflow = true;
            }
        } 

        last_event.timestamp = event.timestamp;
        last_event.level = event.level;
        
        if (sc == RTEMS_TIMEOUT) {
            if (manchester_index > 0) {
                manchester_sequence[manchester_index] = '\0';
                binary_sequence_index = 0;

                const char *test_signal = "011001010110010101101001010101100110101001011010010110010101010101100110010101010110100101101001011010010110011001101010010110010110100101100101010110010101010101101001100101100110101001011010011010100101101001101010011001010101100101010101011010011001101001101001011001100110100110010110011010011010100101101001011001100110100110101001010110010101010101100101011010100110101001100110011010100101100101101001100110100110100101100110011010011010100101101010010110100110100101010110011010011010010101101001010101100110101001100101";  // your full test pattern

                if (dev) {
                    strncpy(manchester_sequence, test_signal, MAX_MANCHESTER_BITS - 1);
                    manchester_sequence[MAX_MANCHESTER_BITS - 1] = '\0';
                    manchester_index = strlen(manchester_sequence);
                }
                for (int i = 0; i + 1 < manchester_index; i += 2) {
                    if (manchester_sequence[i] == '1' && manchester_sequence[i + 1] == '0')
                        binary_sequence[binary_sequence_index++] = '1';
                    else if (manchester_sequence[i] == '0' && manchester_sequence[i + 1] == '1')
                        binary_sequence[binary_sequence_index++] = '0';
                }
                binary_sequence[binary_sequence_index] = '\0';

                safe_print("Manchester bits (%d): %s\n", manchester_index, manchester_sequence);
                safe_print("Decoded bits (%d): %s\n", binary_sequence_index, binary_sequence);

                if (binary_sequence_index >= 8) {
                    safe_print("Decoded ASCII: ");
                    for (int i = 0; i + 7 < binary_sequence_index; i += 8) {
                        uint8_t ch = 0;
                        for (int j = 0; j < 8; ++j) {
                            ch <<= 1;
                            ch |= (binary_sequence[i + j] == '1') ? 1 : 0;
                        }
                        safe_print((ch >= 32 && ch <= 126) ? "%c" : ".", ch);
                    }
                    safe_print("\n");
                }

                manchester_index = 0;
                binary_sequence_index = 0;
                memset(binary_sequence, 0, sizeof(binary_sequence));
                memset(manchester_sequence, 0, sizeof(manchester_sequence));
                manchester_overflow = false;
            }
        }
    }
}



rtems_task Init(rtems_task_argument ignored) {
    printf("RTEMS Manchester Decoder\n");
    gpio_set_mode(GPIO4, 0);
    gpio_set_edge_detect(GPIO4, 1, 1);
    gpio_set_pull(GPIO4, 1);

    rtems_status_code sc = rtems_semaphore_create(
        rtems_build_name('P','R','N','T'), 1,
        RTEMS_BINARY_SEMAPHORE | RTEMS_PRIORITY, 0, &print_sem);
    if (sc != RTEMS_SUCCESSFUL)
        rtems_fatal_error_occurred(sc);

    sc = rtems_message_queue_create(
        rtems_build_name('E','V','T','Q'), MAX_EDGES,
        sizeof(Edge), RTEMS_DEFAULT_ATTRIBUTES, &edge_event_queue_id);
    if (sc != RTEMS_SUCCESSFUL)
        rtems_fatal_error_occurred(sc);

    sc = rtems_interrupt_handler_install(
        GPIO_IRQ_VECTOR, "GPIO4 ISR", RTEMS_INTERRUPT_UNIQUE,
        gpio_isr, NULL);
    if (sc != RTEMS_SUCCESSFUL)
        rtems_fatal_error_occurred(sc);

    rtems_id decoder_task_id;
    sc = rtems_task_create(
        rtems_build_name('D','E','C','O'), 50,
        RTEMS_MINIMUM_STACK_SIZE, RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES, &decoder_task_id);
    if (sc != RTEMS_SUCCESSFUL)
        rtems_fatal_error_occurred(sc);

    sc = rtems_task_start(decoder_task_id, edge_decoder_task, 0);
    if (sc != RTEMS_SUCCESSFUL)
        rtems_fatal_error_occurred(sc);

    rtems_task_delete(RTEMS_SELF);
}
