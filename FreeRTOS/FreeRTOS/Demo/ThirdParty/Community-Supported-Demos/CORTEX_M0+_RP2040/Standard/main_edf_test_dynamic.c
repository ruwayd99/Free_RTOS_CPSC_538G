/*
 * CPSC 538G EDF admission control dynamic test.
 *
 * Workload overview:
 *   - 8 periodic EDF tasks total (4 implicit-deadline, 4 constrained-deadline).
 *   - 6 tasks are created before the scheduler starts.
 *   - 2 tasks are added at runtime by an orchestrator task. Both are sized so
 *     the admission controller must ACCEPT them.
 *   - After the 8-task workload is running, the orchestrator attempts to
 *     create an OVERLOAD task that must be REJECTED by admission control
 *     (utilization pushes sum(C/T) above 1).
 *   - Finally the orchestrator attempts to create a LATE_OK task that is
 *     small enough to fit and must be ACCEPTED again.
 *
 * The 8 "main" tasks each drive one GPIO pin (GP10..GP17) so a logic analyzer
 * can observe their release/execution behaviour. The OVERLOAD candidate never
 * runs (it is rejected), and the LATE_OK candidate does not drive a GPIO pin
 * (it just prints EDF trace events).
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

/* GPIO pins for the 8 main workload tasks. */
#define PIN_IMPL1    10
#define PIN_IMPL2    11
#define PIN_IMPL3    12
#define PIN_IMPL4    13
#define PIN_CONS1    21
#define PIN_CONS2    20
#define PIN_CONS3    19
#define PIN_IMPL5    18

/* Parameter bundle passed via pvParameters to the generic periodic worker. */
typedef struct
{
    int        iPin;
    TickType_t xWcetTicks;
} DynWorkParams_t;

/* Static parameter blocks so lifetime outlives task creation. */
static DynWorkParams_t xImpl1Params;
static DynWorkParams_t xImpl2Params;
static DynWorkParams_t xImpl3Params;
static DynWorkParams_t xImpl4Params;
static DynWorkParams_t xCons1Params;
static DynWorkParams_t xCons2Params;
static DynWorkParams_t xCons3Params;
static DynWorkParams_t xImpl5Params;

/* Busy wait to burn CPU for deterministic traces on the logic analyzer. */
static void prvBusyWorkTicks( TickType_t xDurationTicks )
{
    TickType_t xStart = xTaskGetTickCount();

    while( ( xTaskGetTickCount() - xStart ) < xDurationTicks )
    {
        /* Intentional spin. */
    }
}

/* Generic periodic body: raise pin, do WCET worth of work, lower pin, sleep. */
static void vPeriodicWorker( void * pvParameters )
{
    DynWorkParams_t * pxParams = ( DynWorkParams_t * ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( pxParams->iPin, 1 );
        prvBusyWorkTicks( pxParams->xWcetTicks );
        gpio_put( pxParams->iPin, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* Body used by the runtime "LATE_OK" task. It does not drive any GPIO so
 * it is safe to pass NULL parameters. */
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

/* Logs both admitted and rejected counters so the operator can confirm that
 * the admission controller accepted or refused the candidate. */
static void prvLogCounters( const char * pcLabel, BaseType_t xResult )
{
    printf( "[DYN][result] %s -> %s (admitted=%lu rejected=%lu)\r\n",
            pcLabel,
            ( xResult == pdPASS ) ? "ACCEPT" : "REJECT",
            ( unsigned long ) uxTaskGetEDFAdmittedCount(),
            ( unsigned long ) uxTaskGetEDFRejectedCount() );
}

/* Orchestrator task - created as a normal FreeRTOS task (not EDF) so it can
 * drive the sequence of runtime admission-control experiments. */
static void vOrchestratorTask( void * pvParameters )
{
    BaseType_t xResult;

    ( void ) pvParameters;

    /* Let the startup workload run for several periods before meddling. */
    printf( "[DYN][orch] warmup (admitted=%lu)\r\n",
            ( unsigned long ) uxTaskGetEDFAdmittedCount() );
    vTaskDelay( pdMS_TO_TICKS( 8000 ) );

    /* Add two runtime EDF tasks, one is REJECTED and the other should be ACCEPTED. ---- */
    printf( "[DYN][orch] phase 2: adding IMPL4 and IMPL5 at runtime\r\n" );

        /* ---- Phase 2: attempt an OVERLOAD task that must be REJECTED. ----
     * U = 900/1000 = 0.9 which alone pushes sum(C/T) above 1. The admission
     * controller rejects it without ever invoking vHiddenWorker. */
    
    xImpl4Params.iPin = PIN_IMPL4;
    xImpl4Params.xWcetTicks = pdMS_TO_TICKS( 250 );
    xResult = xTaskCreateEDF( vPeriodicWorker,
                              "IMPL4_OVERLOAD",
                              256,
                              &xImpl4Params,
                              pdMS_TO_TICKS( 1000 ),   /* T */
                              pdMS_TO_TICKS( 1000 ),   /* D == T (implicit) */
                              pdMS_TO_TICKS( 900 ),    /* C */
                              NULL );
    prvLogCounters( "runtime IMPL4 OVERLOAD", xResult );

    xImpl5Params.iPin = PIN_IMPL5;
    xImpl5Params.xWcetTicks = pdMS_TO_TICKS( 250 );
    xResult = xTaskCreateEDF( vPeriodicWorker,
                              "IMPL5_LATE_OK",
                              256,
                              &xImpl5Params,
                              pdMS_TO_TICKS( 10000 ),   /* T */
                              pdMS_TO_TICKS( 10000 ),   /* D < T (constrained) */
                              pdMS_TO_TICKS( 150 ),    /* C */
                              NULL );
    prvLogCounters( "runtime IMPL5 Late OK", xResult );

    /* Orchestrator has nothing left to do - keep it parked. */
    for( ;; )
    {
        vTaskDelay( pdMS_TO_TICKS( 10000 ) );
    }
}

void main_edf_test( void )
{
    BaseType_t xImpl1Create = pdFAIL;
    BaseType_t xImpl2Create = pdFAIL;
    BaseType_t xImpl3Create = pdFAIL;
    BaseType_t xCons1Create = pdFAIL;
    BaseType_t xCons2Create = pdFAIL;
    BaseType_t xCons3Create = pdFAIL;
    BaseType_t xOrchCreate  = pdFAIL;

    /* Initialize GPIO for the 8 workload pins. */
    const int piMainPins[ 8 ] =
    {
        PIN_IMPL1, PIN_IMPL2, PIN_IMPL3, PIN_IMPL4,
        PIN_CONS1, PIN_CONS2, PIN_CONS3, PIN_IMPL5
    };
    for( int i = 0; i < 8; i++ )
    {
        gpio_init( piMainPins[ i ] );
        gpio_set_dir( piMainPins[ i ], GPIO_OUT );
        gpio_put( piMainPins[ i ], 0 );
    }

    /* ---- Phase 1: create 6 EDF tasks before the scheduler starts. ----
     * 3 implicit-deadline (IMPL1..3) + 3 constrained-deadline (CONS1..3). */
    xImpl1Params.iPin = PIN_IMPL1;
    xImpl1Params.xWcetTicks = pdMS_TO_TICKS( 100 );
    xImpl1Create = xTaskCreateEDF( vPeriodicWorker,
                                   "IMPL1",
                                   256,
                                   &xImpl1Params,
                                   pdMS_TO_TICKS( 2000 ),
                                   pdMS_TO_TICKS( 2000 ),
                                   pdMS_TO_TICKS( 100 ),
                                   NULL );

    xImpl2Params.iPin = PIN_IMPL2;
    xImpl2Params.xWcetTicks = pdMS_TO_TICKS( 150 );
    xImpl2Create = xTaskCreateEDF( vPeriodicWorker,
                                   "IMPL2",
                                   256,
                                   &xImpl2Params,
                                   pdMS_TO_TICKS( 3000 ),
                                   pdMS_TO_TICKS( 3000 ),
                                   pdMS_TO_TICKS( 150 ),
                                   NULL );

    xImpl3Params.iPin = PIN_IMPL3;
    xImpl3Params.xWcetTicks = pdMS_TO_TICKS( 200 );
    xImpl3Create = xTaskCreateEDF( vPeriodicWorker,
                                   "IMPL3",
                                   256,
                                   &xImpl3Params,
                                   pdMS_TO_TICKS( 4000 ),
                                   pdMS_TO_TICKS( 4000 ),
                                   pdMS_TO_TICKS( 200 ),
                                   NULL );

    xCons1Params.iPin = PIN_CONS1;
    xCons1Params.xWcetTicks = pdMS_TO_TICKS( 100 );
    xCons1Create = xTaskCreateEDF( vPeriodicWorker,
                                   "CONS1",
                                   256,
                                   &xCons1Params,
                                   pdMS_TO_TICKS( 3000 ),
                                   pdMS_TO_TICKS( 2000 ),
                                   pdMS_TO_TICKS( 100 ),
                                   NULL );

    xCons2Params.iPin = PIN_CONS2;
    xCons2Params.xWcetTicks = pdMS_TO_TICKS( 150 );
    xCons2Create = xTaskCreateEDF( vPeriodicWorker,
                                   "CONS2",
                                   256,
                                   &xCons2Params,
                                   pdMS_TO_TICKS( 4500 ),
                                   pdMS_TO_TICKS( 3000 ),
                                   pdMS_TO_TICKS( 150 ),
                                   NULL );

    xCons3Params.iPin = PIN_CONS3;
    xCons3Params.xWcetTicks = pdMS_TO_TICKS( 200 );
    xCons3Create = xTaskCreateEDF( vPeriodicWorker,
                                   "CONS3",
                                   256,
                                   &xCons3Params,
                                   pdMS_TO_TICKS( 6000 ),
                                   pdMS_TO_TICKS( 4000 ),
                                   pdMS_TO_TICKS( 200 ),
                                   NULL );

    /* Non-EDF orchestrator that drives Phase 2, 3 and 4 after startup. */
    xOrchCreate = xTaskCreate( vOrchestratorTask,
                               "ORCH",
                               384,
                               NULL,
                               tskIDLE_PRIORITY + 1U,
                               NULL );

    printf( "[DYN][startup] impl1=%ld impl2=%ld impl3=%ld cons1=%ld cons2=%ld cons3=%ld orch=%ld admitted=%lu\r\n",
            ( long ) xImpl1Create,
            ( long ) xImpl2Create,
            ( long ) xImpl3Create,
            ( long ) xCons1Create,
            ( long ) xCons2Create,
            ( long ) xCons3Create,
            ( long ) xOrchCreate,
            ( unsigned long ) uxTaskGetEDFAdmittedCount() );

    vTaskStartScheduler();

    for( ;; )
    {
    }
}
