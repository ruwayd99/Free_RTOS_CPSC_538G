/*
 * CPSC 538G EDF admission control large-scale test.
 *
 * 100 EDF tasks (50 implicit + 50 constrained) exercise exact processor-demand
 * analysis on the RP2040.
 *
 * Logic-analyzer pin map (8 channels):
 *   10 11 12 13 21 20  ← 6 sampled workload tasks (indices 7,19,31,42,58,71)
 *   19                 ← idle task
 *   18                 ← timer daemon
 *
 * The original 8-monitored-task layout used pins 10–13, 21, 20, 19, 18.
 * The last two (19 and 18) are now reserved for the system tasks so the
 * logic analyzer can show when the CPU is idle or handling timer callbacks.
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "pico/stdlib.h"

#define TEST100_NUM_TASKS           100
#define TEST100_NUM_IMPLICIT        50
#define TEST100_NUM_CONSTRAINED     50
#define TEST100_NUM_MONITORED       6    /* reduced from 8 – last 2 pins go to idle/timer */
#define TEST100_MONITOR_PERIOD_MS   2000

#define PIN_IDLE   20
#define PIN_TIMER  21

/* GPIO pins for the 6 sampled workload tasks. */
static const int piMonitorPins[ TEST100_NUM_MONITORED ] =
{
    10, 11, 12, 13, 18, 19
};

/* Six task indices spread across implicit (0..49) and constrained (50..99). */
static const int piMonitoredIndices[ TEST100_NUM_MONITORED ] =
{
    7, 19, 31, 42, 58, 71
};

typedef struct
{
    int        iIndex;
    int        iPin;
    TickType_t xWcetTicks;
} Test100Params_t;

static Test100Params_t xTaskParams[ TEST100_NUM_TASKS ];

#if ( configEDF_TRACE_ENABLE == 1 )
    #define test100_TRACE( ... )    printf( __VA_ARGS__ )
#else
    #define test100_TRACE( ... )
#endif

/* ---- GPIO Kernel-Hook Trace Infrastructure -------------------------------- */
#define TRACE_MAX_TASKS     ( TEST100_NUM_MONITORED + 4 )

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

/* ---- Helpers ------------------------------------------------------------ */
static void prvBusyWorkTicks( TickType_t xDurationTicks )
{
    TickType_t xStart = xTaskGetTickCount();
    while( ( xTaskGetTickCount() - xStart ) < xDurationTicks ) { }
}

/* Periodic worker. Monitored tasks were already registered before the
 * scheduler started; the kernel hook raises/lowers their pin automatically. */
static void vPeriodicWorker100( void * pvParameters )
{
    Test100Params_t * pxParams = ( Test100Params_t * ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        prvBusyWorkTicks( pxParams->xWcetTicks );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

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

/* ---- Entry point -------------------------------------------------------- */
void main_edf_test( void )
{
    int i;
    UBaseType_t uxAccepted = 0U;
    UBaseType_t uxRejected = 0U;

    /* Initialise the 6 workload GPIO pins. */
    for( i = 0; i < TEST100_NUM_MONITORED; i++ )
    {
        gpio_init( piMonitorPins[ i ] );
        gpio_set_dir( piMonitorPins[ i ], GPIO_OUT );
        gpio_put( piMonitorPins[ i ], 0 );
    }

    /* Initialise system-task pins. */
    gpio_init( PIN_IDLE );  gpio_set_dir( PIN_IDLE,  GPIO_OUT ); gpio_put( PIN_IDLE,  0 );
    gpio_init( PIN_TIMER ); gpio_set_dir( PIN_TIMER, GPIO_OUT ); gpio_put( PIN_TIMER, 0 );

    printf( "[100T] starting: creating %d EDF tasks (50 implicit + 50 constrained)\r\n",
            TEST100_NUM_TASKS );
    printf( "[100T] running exact processor-demand analysis at each admission...\r\n" );

    for( i = 0; i < TEST100_NUM_TASKS; i++ )
    {
        TickType_t xT, xD, xC;
        BaseType_t xResult;
        TaskHandle_t xHandle;
        char pcName[ 12 ];

        xTaskParams[ i ].iIndex     = i;
        xTaskParams[ i ].iPin       = prvPinForIndex( i );
        xTaskParams[ i ].xWcetTicks = pdMS_TO_TICKS( 4 );

        xT = pdMS_TO_TICKS( 8000 );
        xC = pdMS_TO_TICKS( 4 );

        if( i < TEST100_NUM_IMPLICIT )
        {
            xD = xT;
            snprintf( pcName, sizeof( pcName ), "I%02d", i );
        }
        else
        {
            int iSlot = i - TEST100_NUM_IMPLICIT;
            xD = pdMS_TO_TICKS( 7000 );
            snprintf( pcName, sizeof( pcName ), "C%02d", iSlot );
        }

        xResult = xTaskCreateEDF( vPeriodicWorker100, pcName, 192,
                                  &xTaskParams[ i ], xT, xD, xC, &xHandle );

        if( xResult == pdPASS )
        {
            uxAccepted++;
            /* Register only the monitored subset for GPIO tracing. */
            if( xTaskParams[ i ].iPin >= 0 )
            {
                prvTraceRegister( xHandle, ( uint ) xTaskParams[ i ].iPin );
            }
        }
        else
        {
            uxRejected++;
            printf( "[100T][ERROR] task idx=%d REJECTED T=%lu D=%lu C=%lu\r\n",
                    i, ( unsigned long ) xT, ( unsigned long ) xD, ( unsigned long ) xC );
        }
    }

    printf( "[100T] admission complete: accepted=%lu rejected=%lu admitted_now=%lu\r\n",
            ( unsigned long ) uxAccepted,
            ( unsigned long ) uxRejected,
            ( unsigned long ) uxTaskGetEDFAdmittedCount() );

    ( void ) xTaskCreate( vMonitorTask, "MON", 256, NULL,
                          tskIDLE_PRIORITY + 1U, NULL );

    vTaskStartScheduler();

    for( ;; ) { }
}
