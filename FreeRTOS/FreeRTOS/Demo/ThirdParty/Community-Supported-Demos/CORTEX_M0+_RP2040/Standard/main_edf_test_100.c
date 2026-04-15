/*
 * CPSC 538G EDF admission control large-scale test.
 *
 * This test demonstrates the admission controller scaling to the workload
 * size called out in the professor's README: "perform admission control on
 * roughly 100 periodic tasks running simultaneously".
 *
 * Workload:
 *   - 100 periodic EDF tasks are created before the scheduler starts.
 *   - 50 are implicit-deadline (D == T), 50 are constrained-deadline (D < T).
 *   - All tasks use period T = 8000 ms so the hyperperiod (LCM = 8000) is
 *     tight, and the constrained tasks use D = 7000 ms so the DBF test
 *     range [min(D), horizon] is small (~1000 ticks).  This keeps the
 *     exact processor-demand analysis cheap enough that 100 admissions
 *     finish in a few seconds at boot.
 *   - Parameters are chosen so total utilization is well under 1 and every
 *     task is accepted.  Any rejection is a bug.
 *
 * Logic analyzer support:
 *   - RP2040 has plenty of GPIO but the target analyzer has only 8 channels.
 *     Eight fixed task indices (piMonitoredIndices) are sampled from the 100
 *     and wired to GP10..GP17.  All other tasks execute their work silently.
 *
 * Trace:
 *   - A MONITOR helper task prints the current admitted-task count every
 *     two seconds.  The print is gated by configEDF_TRACE_ENABLE so it can
 *     be disabled without editing code.
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

#define TEST100_NUM_TASKS                100
#define TEST100_NUM_IMPLICIT             50
#define TEST100_NUM_CONSTRAINED          50
#define TEST100_NUM_MONITORED            8
#define TEST100_MONITOR_PERIOD_MS        2000

/* GPIO pins used by the 8 sampled tasks. */
static const int piMonitorPins[ TEST100_NUM_MONITORED ] =
{
    10, 11, 12, 13, 21, 20, 19, 18
};

/* Eight task indices (out of 100) chosen ahead of time to drive GPIO. The
 * values spread across both the implicit (0..49) and constrained (50..99)
 * halves so that both scheduling regimes are visible on the analyzer. */
static const int piMonitoredIndices[ TEST100_NUM_MONITORED ] =
{
    7, 19, 31, 42, 58, 71, 83, 95
};

/* Parameter block handed to each worker task.  Allocated as a static array
 * so its lifetime outlives task creation.  iPin is -1 if the task is not
 * being monitored on a logic-analyzer pin. */
typedef struct
{
    int        iIndex;
    int        iPin;
    TickType_t xWcetTicks;
} Test100Params_t;

static Test100Params_t xTaskParams[ TEST100_NUM_TASKS ];

/* Trace macro - gated by configEDF_TRACE_ENABLE so the monitor output can
 * be silenced by flipping that one config switch. */
#if ( configEDF_TRACE_ENABLE == 1 )
    #define test100_TRACE( ... )    printf( __VA_ARGS__ )
#else
    #define test100_TRACE( ... )
#endif

/* Simple deterministic busy work. */
static void prvBusyWorkTicks( TickType_t xDurationTicks )
{
    TickType_t xStart = xTaskGetTickCount();

    while( ( xTaskGetTickCount() - xStart ) < xDurationTicks )
    {
        /* Intentional spin. */
    }
}

/* Generic periodic body shared by all 100 tasks. Monitored tasks drive a
 * GPIO pin, unmonitored tasks just do busy work silently. */
static void vPeriodicWorker100( void * pvParameters )
{
    Test100Params_t * pxParams = ( Test100Params_t * ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        if( pxParams->iPin >= 0 )
        {
            gpio_put( pxParams->iPin, 1 );
        }

        prvBusyWorkTicks( pxParams->xWcetTicks );

        if( pxParams->iPin >= 0 )
        {
            gpio_put( pxParams->iPin, 0 );
        }

        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* Monitor task - not an EDF task.  Runs at idle+1 priority so it never
 * interferes with the EDF ready list, and prints how many tasks are
 * currently admitted.  This fulfills the "periodic trace print, gated by
 * EDF trace config" requirement. */
static void vMonitorTask( void * pvParameters )
{
    TickType_t xLastWake = xTaskGetTickCount();

    ( void ) pvParameters;

    for( ;; )
    {
        test100_TRACE( "[100T][monitor][tick=%lu] admitted=%lu rejected=%lu\r\n",
                       ( unsigned long ) xTaskGetTickCount(),
                       ( unsigned long ) uxTaskGetEDFAdmittedCount(),
                       ( unsigned long ) uxTaskGetEDFRejectedCount() );
        vTaskDelayUntil( &xLastWake, pdMS_TO_TICKS( TEST100_MONITOR_PERIOD_MS ) );
    }
}

/* Returns the logic-analyzer pin for a given task index, or -1 if the task
 * is not in the monitored subset. */
static int prvPinForIndex( int iIndex )
{
    int iSlot;

    for( iSlot = 0; iSlot < TEST100_NUM_MONITORED; iSlot++ )
    {
        if( piMonitoredIndices[ iSlot ] == iIndex )
        {
            return piMonitorPins[ iSlot ];
        }
    }

    return -1;
}

void main_edf_test( void )
{
    int i;
    UBaseType_t uxAccepted = 0U;
    UBaseType_t uxRejected = 0U;

    /* ---- Initialize the 8 GPIO pins that will be driven by the sampled
     * subset of the 100 tasks. ---- */
    for( i = 0; i < TEST100_NUM_MONITORED; i++ )
    {
        gpio_init( piMonitorPins[ i ] );
        gpio_set_dir( piMonitorPins[ i ], GPIO_OUT );
        gpio_put( piMonitorPins[ i ], 0 );
    }

    printf( "[100T] starting: creating %d EDF tasks (50 implicit + 50 constrained)\r\n",
            TEST100_NUM_TASKS );
    printf( "[100T] running exact processor-demand analysis at each admission...\r\n" );

    /* ---- Create all 100 tasks.  The first 50 are implicit-deadline
     * (D == T = 8000 ms) and the last 50 are constrained-deadline
     * (D = 7000 ms, T = 8000 ms).  Keeping min(D) close to the horizon
     * shrinks the DBF integer sweep so that admitting all 100 tasks
     * completes in a few seconds on the RP2040. ---- */
    for( i = 0; i < TEST100_NUM_TASKS; i++ )
    {
        TickType_t xT;
        TickType_t xD;
        TickType_t xC;
        BaseType_t xResult;
        char pcName[ 12 ];

        xTaskParams[ i ].iIndex     = i;
        xTaskParams[ i ].iPin       = prvPinForIndex( i );
        xTaskParams[ i ].xWcetTicks = pdMS_TO_TICKS( 4 );

        xT = pdMS_TO_TICKS( 8000 );
        xC = pdMS_TO_TICKS( 4 );

        if( i < TEST100_NUM_IMPLICIT )
        {
            /* Implicit: D == T. */
            xD = xT;
            snprintf( pcName, sizeof( pcName ), "I%02d", i );
        }
        else
        {
            /* Constrained: D = 7000 ms (< T = 8000 ms). */
            int iSlot = i - TEST100_NUM_IMPLICIT;
            xD = pdMS_TO_TICKS( 7000 );
            snprintf( pcName, sizeof( pcName ), "C%02d", iSlot );
        }

        xResult = xTaskCreateEDF( vPeriodicWorker100,
                                  pcName,
                                  192,
                                  &xTaskParams[ i ],
                                  xT,
                                  xD,
                                  xC,
                                  NULL );

        if( xResult == pdPASS )
        {
            uxAccepted++;
        }
        else
        {
            uxRejected++;
            printf( "[100T][ERROR] task idx=%d REJECTED T=%lu D=%lu C=%lu\r\n",
                    i,
                    ( unsigned long ) xT,
                    ( unsigned long ) xD,
                    ( unsigned long ) xC );
        }
    }

    printf( "[100T] admission complete: accepted=%lu rejected=%lu admitted_now=%lu\r\n",
            ( unsigned long ) uxAccepted,
            ( unsigned long ) uxRejected,
            ( unsigned long ) uxTaskGetEDFAdmittedCount() );

    /* ---- Create the non-EDF monitor task.  It emits a periodic trace
     * line (gated by configEDF_TRACE_ENABLE) with the current admitted
     * count so the user can visually confirm that all 100 tasks are
     * accepted and still alive. ---- */
    ( void ) xTaskCreate( vMonitorTask,
                          "MON",
                          256,
                          NULL,
                          tskIDLE_PRIORITY + 1U,
                          NULL );

    vTaskStartScheduler();

    for( ;; )
    {
    }
}
