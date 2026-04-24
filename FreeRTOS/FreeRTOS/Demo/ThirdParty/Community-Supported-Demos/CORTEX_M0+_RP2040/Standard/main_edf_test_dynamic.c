/*
 * CPSC 538G EDF admission control dynamic test.
 *
 * Workload overview:
 *   - 4 tasks created before the scheduler starts (2 implicit + 2 constrained).
 *   - Orchestrator adds 2 more at runtime:
 *       IMPL4_OVERLOAD  T=1000 C=900  → must be REJECTED  (pin stays LOW)
 *       IMPL5_LATE_OK   T=10000 C=150 → must be ACCEPTED
 *
 * Reduced from the original 6+2 to 4+2 so that 2 logic-analyzer channels
 * are free for the idle task (pin 18) and timer daemon (pin 19).
 *
 * Pin map (8 channels total):
 *   10 IMPL1    11 IMPL2    12 CONS1    13 CONS2
 *   20 IMPL4_OVERLOAD (rejected – stays LOW)
 *   21 IMPL5_LATE_OK  (runtime accept)
 *   18 IDLE     19 TIMER
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "pico/stdlib.h"

#define PIN_IMPL1           10
#define PIN_IMPL2           11
#define PIN_CONS1           12
#define PIN_CONS2           13
#define PIN_IMPL4_OVERLOAD  18   /* rejected – stays LOW */
#define PIN_IMPL5_LATE_OK   19
#define PIN_IDLE            20
#define PIN_TIMER           21

/* ---- GPIO Kernel-Hook Trace Infrastructure -------------------------------- */
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

/* ---- Parameter bundle --------------------------------------------------- */
typedef struct
{
    int        iPin;   /* kept for reference; gpio_put is done by the hook */
    TickType_t xWcetTicks;
} DynWorkParams_t;

static DynWorkParams_t xImpl1Params;
static DynWorkParams_t xImpl2Params;
static DynWorkParams_t xCons1Params;
static DynWorkParams_t xCons2Params;
static DynWorkParams_t xImpl4Params;
static DynWorkParams_t xImpl5Params;

/* ---- Busy spin ---------------------------------------------------------- */
static void prvBusyWorkTicks( TickType_t xDurationTicks )
{
    TickType_t xStart = xTaskGetTickCount();
    while( ( xTaskGetTickCount() - xStart ) < xDurationTicks ) { }
}

/* ---- Generic periodic worker -------------------------------------------- */
static void vPeriodicWorker( void * pvParameters )
{
    DynWorkParams_t * pxParams = ( DynWorkParams_t * ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        prvBusyWorkTicks( pxParams->xWcetTicks );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* Hidden worker for the runtime LATE_OK task (no parameter needed). */
static void vHiddenWorker( void * pvParameters )
{
    ( void ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        prvBusyWorkTicks( pdMS_TO_TICKS( 150 ) );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* Log helper. */
static void prvLogCounters( const char * pcLabel, BaseType_t xResult )
{
    printf( "[DYN][result] %s -> %s (admitted=%lu rejected=%lu)\r\n",
            pcLabel,
            ( xResult == pdPASS ) ? "ACCEPT" : "REJECT",
            ( unsigned long ) uxTaskGetEDFAdmittedCount(),
            ( unsigned long ) uxTaskGetEDFRejectedCount() );
}

/* ---- Orchestrator ------------------------------------------------------- */
static void vOrchestratorTask( void * pvParameters )
{
    TaskHandle_t xHandle;
    BaseType_t xResult;

    ( void ) pvParameters;

    printf( "[DYN][orch] warmup (admitted=%lu)\r\n",
            ( unsigned long ) uxTaskGetEDFAdmittedCount() );
    vTaskDelay( pdMS_TO_TICKS( 8000 ) );

    printf( "[DYN][orch] phase 2: runtime tasks\r\n" );

    /* OVERLOAD – must be rejected; its pin stays permanently LOW. */
    xImpl4Params.iPin       = PIN_IMPL4_OVERLOAD;
    xImpl4Params.xWcetTicks = pdMS_TO_TICKS( 900 );
    xResult = xTaskCreateEDF( vPeriodicWorker,
                              "IMPL4_OVERLOAD",
                              256,
                              &xImpl4Params,
                              pdMS_TO_TICKS( 1000 ),
                              pdMS_TO_TICKS( 1000 ),
                              pdMS_TO_TICKS( 900 ),
                              &xHandle );
    if( xResult == pdPASS )
    {
        taskENTER_CRITICAL();
        prvTraceRegister( xHandle, PIN_IMPL4_OVERLOAD );
        taskEXIT_CRITICAL();
    }
    prvLogCounters( "runtime IMPL4 OVERLOAD", xResult );

    /* LATE_OK – must be accepted. */
    xImpl5Params.iPin       = PIN_IMPL5_LATE_OK;
    xImpl5Params.xWcetTicks = pdMS_TO_TICKS( 150 );
    xResult = xTaskCreateEDF( vHiddenWorker,
                              "IMPL5_LATE_OK",
                              256,
                              NULL,
                              pdMS_TO_TICKS( 10000 ),
                              pdMS_TO_TICKS( 10000 ),
                              pdMS_TO_TICKS( 150 ),
                              &xHandle );
    if( xResult == pdPASS )
    {
        taskENTER_CRITICAL();
        prvTraceRegister( xHandle, PIN_IMPL5_LATE_OK );
        taskEXIT_CRITICAL();
    }
    prvLogCounters( "runtime IMPL5 Late OK", xResult );

    for( ;; )
    {
        vTaskDelay( pdMS_TO_TICKS( 10000 ) );
    }
}

/* ---- Entry point -------------------------------------------------------- */
void main_edf_test( void )
{
    TaskHandle_t xHandle;
    BaseType_t xImpl1Create, xImpl2Create, xCons1Create, xCons2Create, xOrchCreate;

    /* Initialise all GPIO pins up-front so the hooks can call gpio_put
     * safely from the very first context switch. */
    const uint auPins[] = { PIN_IMPL1, PIN_IMPL2, PIN_CONS1, PIN_CONS2,
                            PIN_IMPL4_OVERLOAD, PIN_IMPL5_LATE_OK,
                            PIN_IDLE, PIN_TIMER };
    for( size_t i = 0; i < sizeof( auPins ) / sizeof( auPins[ 0 ] ); i++ )
    {
        gpio_init( auPins[ i ] );
        gpio_set_dir( auPins[ i ], GPIO_OUT );
        gpio_put( auPins[ i ], 0 );
    }

    /* ---- Phase 1: 4 startup EDF tasks (2 implicit + 2 constrained). ---- */
    xImpl1Params.iPin       = PIN_IMPL1;
    xImpl1Params.xWcetTicks = pdMS_TO_TICKS( 100 );
    xImpl1Create = xTaskCreateEDF( vPeriodicWorker, "IMPL1", 256, &xImpl1Params,
                                   pdMS_TO_TICKS( 2000 ), pdMS_TO_TICKS( 2000 ),
                                   pdMS_TO_TICKS( 100 ), &xHandle );
    prvTraceRegister( xHandle, PIN_IMPL1 );

    xImpl2Params.iPin       = PIN_IMPL2;
    xImpl2Params.xWcetTicks = pdMS_TO_TICKS( 150 );
    xImpl2Create = xTaskCreateEDF( vPeriodicWorker, "IMPL2", 256, &xImpl2Params,
                                   pdMS_TO_TICKS( 3000 ), pdMS_TO_TICKS( 3000 ),
                                   pdMS_TO_TICKS( 150 ), &xHandle );
    prvTraceRegister( xHandle, PIN_IMPL2 );

    xCons1Params.iPin       = PIN_CONS1;
    xCons1Params.xWcetTicks = pdMS_TO_TICKS( 100 );
    xCons1Create = xTaskCreateEDF( vPeriodicWorker, "CONS1", 256, &xCons1Params,
                                   pdMS_TO_TICKS( 3000 ), pdMS_TO_TICKS( 2000 ),
                                   pdMS_TO_TICKS( 100 ), &xHandle );
    prvTraceRegister( xHandle, PIN_CONS1 );

    xCons2Params.iPin       = PIN_CONS2;
    xCons2Params.xWcetTicks = pdMS_TO_TICKS( 150 );
    xCons2Create = xTaskCreateEDF( vPeriodicWorker, "CONS2", 256, &xCons2Params,
                                   pdMS_TO_TICKS( 4500 ), pdMS_TO_TICKS( 3000 ),
                                   pdMS_TO_TICKS( 150 ), &xHandle );
    prvTraceRegister( xHandle, PIN_CONS2 );

    /* Non-EDF orchestrator drives Phase 2 after startup. */
    xOrchCreate = xTaskCreate( vOrchestratorTask, "ORCH", 384, NULL,
                               tskIDLE_PRIORITY + 1U, NULL );

    printf( "[DYN][startup] impl1=%ld impl2=%ld cons1=%ld cons2=%ld orch=%ld admitted=%lu\r\n",
            ( long ) xImpl1Create,
            ( long ) xImpl2Create,
            ( long ) xCons1Create,
            ( long ) xCons2Create,
            ( long ) xOrchCreate,
            ( unsigned long ) uxTaskGetEDFAdmittedCount() );

    vTaskStartScheduler();

    for( ;; ) { }
}
