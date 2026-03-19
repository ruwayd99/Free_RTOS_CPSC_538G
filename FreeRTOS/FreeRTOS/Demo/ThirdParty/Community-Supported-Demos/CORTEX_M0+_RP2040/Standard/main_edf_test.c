#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"      /* Pico SDK: GPIO, stdio */

/* Each task gets its own GPIO pin. When the task is running,
 * its pin is HIGH. When it's done (sleeping), its pin is LOW.
 * Connect LEDs or a logic analyzer to these pins to see the schedule. */
#define LED_PIN_TASK_A  2
#define LED_PIN_TASK_B  3

/* Task A: runs every 500ms, does ~100ms of work each time.
 * Deadline = period = 500ms (must finish before next period). */
void vTaskA( void * pvParameters )
{
    /* Initialize the wake-time tracker to the current tick.
     * vTaskDelayUntilNextPeriod uses this to calculate exact
     * periodic wake-up times. */
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        /* Signal that Task A is running (turn on LED / set GPIO high). */
        gpio_put( LED_PIN_TASK_A, 1 );

        /* Simulate doing ~100ms of work by busy-waiting.
         * In a real application, this would be actual computation
         * (reading a sensor, processing data, etc.). */
        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 1000 ) )
        {
            /* Busy wait -- the task is "working." */
        }

        /* Signal that Task A is done with this period's work. */
        gpio_put( LED_PIN_TASK_A, 0 );

        /* Sleep until the next period. This also updates the deadline.
         * The task will wake up at xLastWakeTime + period (500ms).
         * While sleeping, other tasks can use the CPU. */
        vTaskDelayUntilNextPeriod( &xLastWakeTime );
    }
}

/* Task B: runs every 1000ms, does ~200ms of work each time.
 * Deadline = period = 1000ms. */
void vTaskB( void * pvParameters )
{
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( LED_PIN_TASK_B, 1 );

        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 2000 ) )
        {
            /* Busy wait. */
        }

        gpio_put( LED_PIN_TASK_B, 0 );

        vTaskDelayUntilNextPeriod( &xLastWakeTime );
    }
}

void main_edf_test( void )
{
    /* Initialize GPIO pins for LEDs. */
    gpio_init( LED_PIN_TASK_A );
    gpio_set_dir( LED_PIN_TASK_A, GPIO_OUT );
    gpio_init( LED_PIN_TASK_B );
    gpio_set_dir( LED_PIN_TASK_B, GPIO_OUT );

    /* Create Task A: period=5000ms, deadline=5000ms.
     * pdMS_TO_TICKS() converts milliseconds to ticks.
     * With configTICK_RATE_HZ=1000, pdMS_TO_TICKS(5000) = 5000 ticks. */
    xTaskCreateEDF( vTaskA,               /* Task function */
                    "TaskA",              /* Name (for debugging) */
                    256,                  /* Stack size in words */
                    NULL,                 /* No parameters to pass */
                    pdMS_TO_TICKS( 5000 ), /* Period: 5000ms */
                    pdMS_TO_TICKS( 5000 ), /* Deadline: 5000ms (= period) */
                    NULL );               /* Don't need the handle */

    /* Create Task B: period=1000ms, deadline=1000ms. */
    xTaskCreateEDF( vTaskB,
                    "TaskB",
                    256,
                    NULL,
                    pdMS_TO_TICKS( 10000 ),
                    pdMS_TO_TICKS( 10000 ),
                    NULL );

    /* Start the scheduler. This function never returns.
     * From this point on, FreeRTOS is in control. It will
     * create the Idle task, start the tick timer, and begin
     * running the task with the earliest deadline. */
    vTaskStartScheduler();

    /* We should never get here. If we do, it means the scheduler
     * failed to start (probably out of memory). */
    for( ;; ) { }
}