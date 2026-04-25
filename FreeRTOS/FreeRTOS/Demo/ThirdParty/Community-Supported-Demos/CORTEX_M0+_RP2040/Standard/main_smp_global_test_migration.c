/* Begin FreeRTOS CPSC_538G related - SMP - Task 4 global EDF migration/remove API test */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "pico/stdlib.h"
#include "pico/platform.h"

#define PIN_G_HINT 10
#define PIN_G_PEER 11
#define PIN_IDLE0 19
#define PIN_IDLE1 20
#define PIN_TIMER 21

/* ---- GPIO Kernel-Hook Trace Infrastructure -------------------------------- */
#define TRACE_MAX_TASKS  12

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
} SMPGlobalMigrationParams_t;

static TaskHandle_t xHintedTaskHandle = NULL;
static TaskHandle_t xPeerTaskHandle = NULL;

static SMPGlobalMigrationParams_t xHintedParams = { "G_HINT", PIN_G_HINT, pdMS_TO_TICKS( 90 ) };
static SMPGlobalMigrationParams_t xPeerParams = { "G_PEER", PIN_G_PEER, pdMS_TO_TICKS( 100 ) };

static void prvBusyWorkTicks( TickType_t xDurationTicks )
{
    TickType_t xStart = xTaskGetTickCount();

    while( ( xTaskGetTickCount() - xStart ) < xDurationTicks )
    {
        /* Intentional spin to create deterministic compute demand. */
    }
}

static void vGlobalWorker( void * pvParameters )
{
    SMPGlobalMigrationParams_t * pxParams = ( SMPGlobalMigrationParams_t * ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();
    uint32_t ulLastCore = UINT32_MAX;

    for( ; ; )
    {
        uint32_t ulCore = get_core_num();

        if( ulCore != ulLastCore )
        {
            printf( "[SMP][GLOBAL][worker] task=%s core=%lu tick=%lu\r\n",
                    pxParams->pcName,
                    ( unsigned long ) ulCore,
                    ( unsigned long ) xTaskGetTickCount() );
            ulLastCore = ulCore;
        }

        prvBusyWorkTicks( pxParams->xWorkTicks );

        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

static void vGlobalController( void * pvParameters )
{
    BaseType_t xMigrateResult;

    ( void ) pvParameters;

    vTaskDelay( pdMS_TO_TICKS( 3000 ) );

    xMigrateResult = xTaskMigrateToCore( xHintedTaskHandle, 1 );
    printf( "[SMP][GLOBAL][controller] migrate hinted->core1 result=%ld\r\n",
            ( long ) xMigrateResult );

    vTaskDelay( pdMS_TO_TICKS( 3000 ) );

    vTaskRemoveFromCore( xPeerTaskHandle );
    printf( "[SMP][GLOBAL][controller] removed peer task from scheduling set\r\n" );

    for( ; ; )
    {
        vTaskDelay( pdMS_TO_TICKS( 10000 ) );
    }
}

void main_edf_test( void )
{
    BaseType_t xHintedCreate;
    BaseType_t xPeerCreate;
    const uint auLogicAnalyzerPins[] = { 10, 11, 12, 13, 18, 19, 20, 21 };
    size_t xIndex;

    /* Always initialise the full 8-channel logic-analyzer pin set. */
    for( xIndex = 0; xIndex < ( sizeof( auLogicAnalyzerPins ) / sizeof( auLogicAnalyzerPins[ 0 ] ) ); xIndex++ )
    {
        gpio_init( auLogicAnalyzerPins[ xIndex ] );
        gpio_set_dir( auLogicAnalyzerPins[ xIndex ], GPIO_OUT );
        gpio_put( auLogicAnalyzerPins[ xIndex ], 0 );
    }

    xHintedCreate = xTaskCreateEDFOnCore( vGlobalWorker,
                                          xHintedParams.pcName,
                                          256,
                                          &xHintedParams,
                                          pdMS_TO_TICKS( 1200 ),
                                          pdMS_TO_TICKS( 1200 ),
                                          pdMS_TO_TICKS( 300 ),
                                          0,
                                          &xHintedTaskHandle );
    if( xHintedCreate == pdPASS )
    {
        prvTraceRegister( xHintedTaskHandle, PIN_G_HINT );
    }

    xPeerCreate = xTaskCreateEDF( vGlobalWorker,
                                  xPeerParams.pcName,
                                  256,
                                  &xPeerParams,
                                  pdMS_TO_TICKS( 1500 ),
                                  pdMS_TO_TICKS( 1500 ),
                                  pdMS_TO_TICKS( 350 ),
                                  &xPeerTaskHandle );
    if( xPeerCreate == pdPASS )
    {
        prvTraceRegister( xPeerTaskHandle, PIN_G_PEER );
    }

    ( void ) xTaskCreate( vGlobalController,
                          "G_CTRL",
                          256,
                          NULL,
                          tskIDLE_PRIORITY + 1U,
                          NULL );

    printf( "[SMP][GLOBAL][startup] hinted=%ld peer=%ld admitted=%lu rejected=%lu\r\n",
            ( long ) xHintedCreate,
            ( long ) xPeerCreate,
            ( unsigned long ) uxTaskGetEDFAdmittedCount(),
            ( unsigned long ) uxTaskGetEDFRejectedCount() );
        printf( "[SMP][GLOBAL][startup] pin map: G_HINT=%d G_PEER=%d IDLE0=%d IDLE1=%d TIMER=%d\r\n",
            PIN_G_HINT, PIN_G_PEER, PIN_IDLE0, PIN_IDLE1, PIN_TIMER );

    vTaskStartScheduler();

    for( ; ; )
    {
    }
}
/* End FreeRTOS CPSC_538G related - SMP - Task 4 global EDF migration/remove API test */
