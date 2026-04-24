/* Begin FreeRTOS CPSC_538G related - SMP - Task 4 partitioned EDF fit/reject test */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "pico/stdlib.h"

#define PIN_P_U40       10
#define PIN_P_U30A      11
#define PIN_P_U30B      12
#define PIN_P_U25A      13
#define PIN_P_U25B      18
#define PIN_IDLE0       19
#define PIN_IDLE1       20
#define PIN_TIMER       21

/* ---- GPIO Kernel-Hook Trace Infrastructure -------------------------------- */
#define TRACE_MAX_TASKS  14

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
        { TaskHandle_t h = xTaskGetIdleTaskHandleForCore( 0 );
          if( h != NULL && iTraceCount < TRACE_MAX_TASKS )
          { xTraceHandles[ iTraceCount ] = h; uiTracePins[ iTraceCount++ ] = PIN_IDLE0; } }
        { TaskHandle_t h = xTaskGetIdleTaskHandleForCore( 1 );
          if( h != NULL && iTraceCount < TRACE_MAX_TASKS )
          { xTraceHandles[ iTraceCount ] = h; uiTracePins[ iTraceCount++ ] = PIN_IDLE1; } }
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

typedef struct
{
    const char * pcName;
    int iPin;
    TickType_t xWorkTicks;
} SMPPartitionFitParams_t;

static SMPPartitionFitParams_t xTaskParams[] =
{
    { "P_U40", PIN_P_U40, pdMS_TO_TICKS( 60 ) },
    { "P_U30A", PIN_P_U30A, pdMS_TO_TICKS( 60 ) },
    { "P_U30B", PIN_P_U30B, pdMS_TO_TICKS( 60 ) },
    { "P_U25A", PIN_P_U25A, pdMS_TO_TICKS( 60 ) },
    { "P_U25B", PIN_P_U25B, pdMS_TO_TICKS( 60 ) },
    { "P_U80_REJECT", -1, pdMS_TO_TICKS( 60 ) }
};

static void prvBusyWorkTicks( TickType_t xDurationTicks )
{
    TickType_t xStart = xTaskGetTickCount();

    while( ( xTaskGetTickCount() - xStart ) < xDurationTicks )
    {
        /* Intentional spin to create deterministic compute demand. */
    }
}

static void vPartitionWorker( void * pvParameters )
{
    SMPPartitionFitParams_t * pxParams = ( SMPPartitionFitParams_t * ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    ( void ) pxParams;

    for( ; ; )
    {
        prvBusyWorkTicks( pxParams->xWorkTicks );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

void main_edf_test( void )
{
    BaseType_t xCreateResults[ 6 ];
    TaskHandle_t xHandles[ 6 ] = { NULL };
    size_t xIndex;

    for( xIndex = 0; xIndex < ( sizeof( xTaskParams ) / sizeof( xTaskParams[ 0 ] ) ); xIndex++ )
    {
        if( xTaskParams[ xIndex ].iPin >= 0 )
        {
            gpio_init( ( uint ) xTaskParams[ xIndex ].iPin );
            gpio_set_dir( ( uint ) xTaskParams[ xIndex ].iPin, GPIO_OUT );
            gpio_put( ( uint ) xTaskParams[ xIndex ].iPin, 0 );
        }
    }
    gpio_init( PIN_IDLE0 ); gpio_set_dir( PIN_IDLE0, GPIO_OUT ); gpio_put( PIN_IDLE0, 0 );
    gpio_init( PIN_IDLE1 ); gpio_set_dir( PIN_IDLE1, GPIO_OUT ); gpio_put( PIN_IDLE1, 0 );
    gpio_init( PIN_TIMER ); gpio_set_dir( PIN_TIMER, GPIO_OUT ); gpio_put( PIN_TIMER, 0 );

    printf( "[SMP][PARTITION][fit] creating U={0.40,0.30,0.30,0.25,0.25} then U=0.80 reject candidate\r\n" );

    xCreateResults[ 0 ] = xTaskCreateEDF( vPartitionWorker,
                                          xTaskParams[ 0 ].pcName,
                                          256,
                                          &xTaskParams[ 0 ],
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 400 ),
                                          &xHandles[ 0 ] );
    if( xCreateResults[ 0 ] == pdPASS )
    {
        prvTraceRegister( xHandles[ 0 ], ( uint ) xTaskParams[ 0 ].iPin );
    }

    xCreateResults[ 1 ] = xTaskCreateEDF( vPartitionWorker,
                                          xTaskParams[ 1 ].pcName,
                                          256,
                                          &xTaskParams[ 1 ],
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 300 ),
                                          &xHandles[ 1 ] );
    if( xCreateResults[ 1 ] == pdPASS )
    {
        prvTraceRegister( xHandles[ 1 ], ( uint ) xTaskParams[ 1 ].iPin );
    }

    xCreateResults[ 2 ] = xTaskCreateEDF( vPartitionWorker,
                                          xTaskParams[ 2 ].pcName,
                                          256,
                                          &xTaskParams[ 2 ],
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 300 ),
                                          &xHandles[ 2 ] );
    if( xCreateResults[ 2 ] == pdPASS )
    {
        prvTraceRegister( xHandles[ 2 ], ( uint ) xTaskParams[ 2 ].iPin );
    }

    xCreateResults[ 3 ] = xTaskCreateEDF( vPartitionWorker,
                                          xTaskParams[ 3 ].pcName,
                                          256,
                                          &xTaskParams[ 3 ],
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 250 ),
                                          &xHandles[ 3 ] );
    if( xCreateResults[ 3 ] == pdPASS )
    {
        prvTraceRegister( xHandles[ 3 ], ( uint ) xTaskParams[ 3 ].iPin );
    }

    xCreateResults[ 4 ] = xTaskCreateEDF( vPartitionWorker,
                                          xTaskParams[ 4 ].pcName,
                                          256,
                                          &xTaskParams[ 4 ],
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 250 ),
                                          &xHandles[ 4 ] );
    if( xCreateResults[ 4 ] == pdPASS )
    {
        prvTraceRegister( xHandles[ 4 ], ( uint ) xTaskParams[ 4 ].iPin );
    }

    xCreateResults[ 5 ] = xTaskCreateEDF( vPartitionWorker,
                                          xTaskParams[ 5 ].pcName,
                                          256,
                                          &xTaskParams[ 5 ],
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 800 ),
                                          &xHandles[ 5 ] );
    if( ( xCreateResults[ 5 ] == pdPASS ) && ( xTaskParams[ 5 ].iPin >= 0 ) )
    {
        prvTraceRegister( xHandles[ 5 ], ( uint ) xTaskParams[ 5 ].iPin );
    }

    printf( "[SMP][PARTITION][fit] results={%ld,%ld,%ld,%ld,%ld} reject=%ld admitted=%lu rejected=%lu\r\n",
            ( long ) xCreateResults[ 0 ],
            ( long ) xCreateResults[ 1 ],
            ( long ) xCreateResults[ 2 ],
            ( long ) xCreateResults[ 3 ],
            ( long ) xCreateResults[ 4 ],
            ( long ) xCreateResults[ 5 ],
            ( unsigned long ) uxTaskGetEDFAdmittedCount(),
            ( unsigned long ) uxTaskGetEDFRejectedCount() );
        printf( "[SMP][PARTITION][fit] pin map: U40=%d U30A=%d U30B=%d U25A=%d U25B=%d IDLE0=%d IDLE1=%d TIMER=%d\r\n",
            PIN_P_U40, PIN_P_U30A, PIN_P_U30B, PIN_P_U25A, PIN_P_U25B, PIN_IDLE0, PIN_IDLE1, PIN_TIMER );

    vTaskStartScheduler();

    for( ; ; )
    {
    }
}
/* End FreeRTOS CPSC_538G related - SMP - Task 4 partitioned EDF fit/reject test */
