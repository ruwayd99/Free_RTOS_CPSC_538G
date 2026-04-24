#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "pico/stdlib.h"

/* GPIO pins for the four workload tasks. */
#define PIN_TASK_IMPLICIT   10
#define PIN_TASK_CONSTR     11
#define PIN_TASK_RT_OK      12
#define PIN_TASK_RT_REJECT  13   /* stays LOW – admission rejects this task */

/* System-task pins shown at the bottom of the logic-analyzer display. */
#define PIN_IDLE            20
#define PIN_TIMER           21

/* ---- GPIO Kernel-Hook Trace Infrastructure --------------------------------
 * Pins are driven by traceTASK_SWITCHED_IN / traceTASK_SWITCHED_OUT hooks
 * (defined in FreeRTOSConfig.h to call vTraceOnTaskSwitchedIn/Out below).
 * Task handles are registered with prvTraceRegister() before the scheduler
 * starts.  System tasks (idle, timer daemon) are registered lazily on the
 * first context switch, when their handles are guaranteed to be valid.
 * -------------------------------------------------------------------------- */
#define TRACE_MAX_TASKS     12

static TaskHandle_t        xTraceHandles[ TRACE_MAX_TASKS ];
static uint                uiTracePins  [ TRACE_MAX_TASKS ];
static volatile int        iTraceCount  = 0;
static volatile BaseType_t bSysTasksReg = pdFALSE;

static void prvTraceRegister( TaskHandle_t xHandle, uint uiPin )
{
    if( ( xHandle != NULL ) && ( iTraceCount < TRACE_MAX_TASKS ) )
    {
        xTraceHandles[ iTraceCount ] = xHandle;
        uiTracePins  [ iTraceCount ] = uiPin;
        iTraceCount++;
    }
}

/* Called from traceTASK_SWITCHED_IN() inside the PendSV handler
 * (interrupts disabled → single-core access is race-free). */
void vTraceOnTaskSwitchedIn( void )
{
    int i;
    TaskHandle_t xHandle;

    if( bSysTasksReg == pdFALSE )
    {
        bSysTasksReg = pdTRUE;
        { TaskHandle_t h = xTaskGetIdleTaskHandle();
          if( h != NULL && iTraceCount < TRACE_MAX_TASKS )
          { xTraceHandles[ iTraceCount ] = h; uiTracePins[ iTraceCount++ ] = PIN_IDLE; } }
        { TaskHandle_t h = xTimerGetTimerDaemonTaskHandle();
          if( h != NULL && iTraceCount < TRACE_MAX_TASKS )
          { xTraceHandles[ iTraceCount ] = h; uiTracePins[ iTraceCount++ ] = PIN_TIMER; } }
    }

    xHandle = xTaskGetCurrentTaskHandle();
    for( i = 0; i < iTraceCount; i++ )
    {
        if( xTraceHandles[ i ] == xHandle )
        {
            gpio_put( uiTracePins[ i ], 1 );
            break;
        }
    }
}

void vTraceOnTaskSwitchedOut( void )
{
    int i;
    TaskHandle_t xHandle = xTaskGetCurrentTaskHandle();
    for( i = 0; i < iTraceCount; i++ )
    {
        if( xTraceHandles[ i ] == xHandle )
        {
            gpio_put( uiTracePins[ i ], 0 );
            break;
        }
    }
}

/* ---- Task bodies -------------------------------------------------------
 * GPIO is driven exclusively by the kernel hooks above; no gpio_put() calls
 * appear here.
 * ---------------------------------------------------------------------- */

static void vImplicitTask( void * pvParameters )
{
    ( void ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 1000 ) ) { }
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

static void vConstrainedTask( void * pvParameters )
{
    ( void ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 1000 ) ) { }
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

static void vRuntimeImplicitTask( void * pvParameters )
{
    ( void ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 500 ) ) { }
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

static void vRuntimeConstrainedTask( void * pvParameters )
{
    ( void ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 500 ) ) { }
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* Runtime creator – adds two EDF tasks while the scheduler is live.
 * The first is accepted; the second is intentionally heavy and rejected.
 * Accepted handles are registered with the trace table. */
static void vRuntimeCreatorTask( void * pvParameters )
{
    TaskHandle_t xHandle;
    BaseType_t xResult;
    ( void ) pvParameters;

    vTaskDelay( pdMS_TO_TICKS( 5000 ) );

    /* Should pass if the base set has utilisation slack. */
    xResult = xTaskCreateEDF( vRuntimeImplicitTask,
                              "RT_ADD_OK",
                              256,
                              NULL,
                              pdMS_TO_TICKS( 2500 ),
                              pdMS_TO_TICKS( 2000 ),
                              pdMS_TO_TICKS( 500 ),
                              &xHandle );
    if( xResult == pdPASS )
    {
        taskENTER_CRITICAL();
        prvTraceRegister( xHandle, PIN_TASK_RT_OK );
        taskEXIT_CRITICAL();
    }

    /* Intentionally heavy – should be rejected; PIN_TASK_RT_REJECT stays LOW. */
    xResult = xTaskCreateEDF( vRuntimeConstrainedTask,
                              "RT_ADD_REJECT",
                              256,
                              NULL,
                              pdMS_TO_TICKS( 2500 ),
                              pdMS_TO_TICKS( 1500 ),
                              pdMS_TO_TICKS( 500 ),
                              &xHandle );
    if( xResult == pdPASS )
    {
        taskENTER_CRITICAL();
        prvTraceRegister( xHandle, PIN_TASK_RT_REJECT );
        taskEXIT_CRITICAL();
    }

    for( ;; )
    {
        vTaskDelay( pdMS_TO_TICKS( 1000 ) );
    }
}

void main_edf_test_previous( void )
{
    TaskHandle_t xHandle;

    /* Initialise GPIO for all traced pins (task + system). */
    const uint auPins[] = { PIN_TASK_IMPLICIT, PIN_TASK_CONSTR,
                            PIN_TASK_RT_OK,    PIN_TASK_RT_REJECT,
                            PIN_IDLE,          PIN_TIMER };
    for( size_t i = 0; i < sizeof( auPins ) / sizeof( auPins[ 0 ] ); i++ )
    {
        gpio_init( auPins[ i ] );
        gpio_set_dir( auPins[ i ], GPIO_OUT );
        gpio_put( auPins[ i ], 0 );
    }

    printf( "[EDF][startup] Creating initial task set...\r\n" );

    /* Implicit case: D == T → utilisation admission path. */
    xTaskCreateEDF( vImplicitTask,
                    "IMPLICIT_A",
                    256,
                    NULL,
                    pdMS_TO_TICKS( 5000 ),
                    pdMS_TO_TICKS( 4000 ),
                    pdMS_TO_TICKS( 1000 ),
                    &xHandle );
    prvTraceRegister( xHandle, PIN_TASK_IMPLICIT );

    /* Constrained case: D < T → exact DBF admission path. */
    xTaskCreateEDF( vConstrainedTask,
                    "CONSTR_B",
                    256,
                    NULL,
                    pdMS_TO_TICKS( 10000 ),
                    pdMS_TO_TICKS( 8000 ),
                    pdMS_TO_TICKS( 1000 ),
                    &xHandle );
    prvTraceRegister( xHandle, PIN_TASK_CONSTR );

    /* Non-EDF orchestrator that adds runtime tasks.  Kept outside EDF
     * admission accounting. */
    xTaskCreate( vRuntimeCreatorTask,
                 "RUNTIME_CREATOR",
                 256,
                 NULL,
                 tskIDLE_PRIORITY + 1U,
                 NULL );

    vTaskStartScheduler();

    for( ;; ) { }
}

void main_edf_test( void )
{
    main_edf_test_previous();
}
