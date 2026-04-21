#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

/* Test pins to visualize key tasks on a logic analyzer. */
#define PIN_TASK_IMPLICIT   10
#define PIN_TASK_CONSTR     11
#define PIN_TASK_RT_OK      12
#define PIN_TASK_RT_REJECT  13

/* Forward declarations for task bodies. */
static void vImplicitTask( void * pvParameters );
static void vConstrainedTask( void * pvParameters );
static void vRuntimeImplicitTask( void * pvParameters );
static void vRuntimeConstrainedTask( void * pvParameters );
static void vRuntimeCreatorTask( void * pvParameters );

/* Simple periodic task with D=T case for implicit admission path. */
static void vImplicitTask( void * pvParameters )
{
    ( void ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( PIN_TASK_IMPLICIT, 1 );

        /* Simulate bounded execution below WCET. */
        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 1000 ) )
        {
            /* Busy compute for deterministic demo behavior. */
        }

        gpio_put( PIN_TASK_IMPLICIT, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* Constrained task with D<T to force exact DBF admission path. */
static void vConstrainedTask( void * pvParameters )
{
    ( void ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( PIN_TASK_CONSTR, 1 );

        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 1000 ) )
        {
            /* Busy compute for constrained workload. */
        }

        gpio_put( PIN_TASK_CONSTR, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* Runtime-added implicit task on a unique pin. */
static void vRuntimeImplicitTask( void * pvParameters )
{
    ( void ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( PIN_TASK_RT_OK, 1 );

        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 500 ) )
        {
            /* Busy compute for deterministic demo behavior. */
        }

        gpio_put( PIN_TASK_RT_OK, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* Runtime-added constrained task on a unique pin. */
static void vRuntimeConstrainedTask( void * pvParameters )
{
    ( void ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( PIN_TASK_RT_REJECT, 1 );

        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 500 ) )
        {
            /* Busy compute for constrained workload. */
        }

        gpio_put( PIN_TASK_RT_REJECT, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* Runtime creator validates "add task while running" and admission rejection path. */
static void vRuntimeCreatorTask( void * pvParameters )
{
    ( void ) pvParameters;
    /* Wait so scheduler is clearly running before runtime add. */
    vTaskDelay( pdMS_TO_TICKS( 5000 ) );

    /* This one should typically pass if your base set has slack. */
    ( void ) xTaskCreateEDF( vRuntimeImplicitTask,
                             "RT_ADD_OK",
                             256,
                             NULL,
                             pdMS_TO_TICKS( 2500 ),
                             pdMS_TO_TICKS( 2000 ),
                             pdMS_TO_TICKS( 500 ),
                             NULL );

    /* This one is intentionally heavy to trigger admission reject. */
    ( void ) xTaskCreateEDF( vRuntimeConstrainedTask,
                             "RT_ADD_REJECT",
                             256,
                             NULL,
                             pdMS_TO_TICKS( 2500 ),
                             pdMS_TO_TICKS( 1500 ),
                             pdMS_TO_TICKS( 500 ),
                             NULL );

    /* Optionally force overload/miss by spinning too long in this task or others,
     * then confirm kernel emits [drop] trace and skips late job immediately. */
    for( ;; )
    {
        vTaskDelay( pdMS_TO_TICKS( 1000 ) );
    }
}

void main_edf_test_previous( void )
{
    gpio_init( PIN_TASK_IMPLICIT );
    gpio_set_dir( PIN_TASK_IMPLICIT, GPIO_OUT );
    gpio_init( PIN_TASK_CONSTR );
    gpio_set_dir( PIN_TASK_CONSTR, GPIO_OUT );
    gpio_init( PIN_TASK_RT_OK );
    gpio_set_dir( PIN_TASK_RT_OK, GPIO_OUT );
    gpio_init( PIN_TASK_RT_REJECT );
    gpio_set_dir( PIN_TASK_RT_REJECT, GPIO_OUT );

    /* Print startup task table so demo clearly shows all (C,T,D) values. */
    printf( "[EDF][startup] Creating initial task set...\r\n" );

    /* Implicit case: D = T -> utilization admission path. */
    ( void ) xTaskCreateEDF( vImplicitTask,
                             "IMPLICIT_A",
                             256,
                             NULL,
                             pdMS_TO_TICKS( 5000 ),
                             pdMS_TO_TICKS( 4000 ),
                             pdMS_TO_TICKS( 1000 ),
                             NULL );

    /* Constrained case: D < T -> exact DBF path. */
    ( void ) xTaskCreateEDF( vConstrainedTask,
                             "CONSTR_B",
                             256,
                             NULL,
                             pdMS_TO_TICKS( 10000 ),
                             pdMS_TO_TICKS( 8000 ),
                             pdMS_TO_TICKS( 1000 ),
                             NULL );

    /* Runtime creator task itself can be normal FreeRTOS or EDF.
     * Using xTaskCreate keeps this helper outside EDF admission accounting. */
    ( void ) xTaskCreate( vRuntimeCreatorTask,
                          "RUNTIME_CREATOR",
                          256,
                          NULL,
                          tskIDLE_PRIORITY + 1U,
                          NULL );

    vTaskStartScheduler();

    for( ;; )
    {
        /* Should never execute unless scheduler fails to start. */
    }
}

void main_edf_test( void )
{
    main_edf_test_previous();
}