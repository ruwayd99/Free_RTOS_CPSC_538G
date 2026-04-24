/* Begin FreeRTOS CPSC_538G related - SMP - Task 4 partitioned EDF migration/remove API test */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "pico/stdlib.h"
#include "pico/platform.h"

#define PIN_P_A   10
#define PIN_P_B   11
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
} SMPPartitionMigrationParams_t;

static TaskHandle_t xTaskAHandle = NULL;
static TaskHandle_t xTaskBHandle = NULL;

static SMPPartitionMigrationParams_t xTaskAParams = { "P_A_U70", PIN_P_A, pdMS_TO_TICKS( 120 ) };
static SMPPartitionMigrationParams_t xTaskBParams = { "P_B_U40", PIN_P_B, pdMS_TO_TICKS( 120 ) };

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
    SMPPartitionMigrationParams_t * pxParams = ( SMPPartitionMigrationParams_t * ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();
    uint32_t ulLastCore = UINT32_MAX;

    for( ; ; )
    {
        uint32_t ulCore = get_core_num();

        if( ulCore != ulLastCore )
        {
            printf( "[SMP][PARTITION][worker] task=%s core=%lu tick=%lu\r\n",
                    pxParams->pcName,
                    ( unsigned long ) ulCore,
                    ( unsigned long ) xTaskGetTickCount() );
            ulLastCore = ulCore;
        }

        prvBusyWorkTicks( pxParams->xWorkTicks );

        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

static void vPartitionController( void * pvParameters )
{
    BaseType_t xMigrateFail;
    BaseType_t xMigratePass;

    ( void ) pvParameters;

    vTaskDelay( pdMS_TO_TICKS( 3000 ) );

    xMigrateFail = xTaskMigrateToCore( xTaskBHandle, 0 );
    printf( "[SMP][PARTITION][controller] migrate B->core0 expect fail result=%ld\r\n",
            ( long ) xMigrateFail );

    vTaskRemoveFromCore( xTaskAHandle );
    printf( "[SMP][PARTITION][controller] removed A from core assignment\r\n" );

    vTaskDelay( pdMS_TO_TICKS( 1000 ) );

    xMigratePass = xTaskMigrateToCore( xTaskBHandle, 0 );
    printf( "[SMP][PARTITION][controller] migrate B->core0 after remove expect pass result=%ld\r\n",
            ( long ) xMigratePass );

    for( ; ; )
    {
        vTaskDelay( pdMS_TO_TICKS( 10000 ) );
    }
}

void main_edf_test( void )
{
    BaseType_t xCreateA;
    BaseType_t xCreateB;
    SMPPartitionMigrationParams_t * pxTaskParams[] = { &xTaskAParams, &xTaskBParams };
    size_t xIndex;

    for( xIndex = 0; xIndex < ( sizeof( pxTaskParams ) / sizeof( pxTaskParams[ 0 ] ) ); xIndex++ )
    {
        if( pxTaskParams[ xIndex ]->iPin >= 0 )
        {
            gpio_init( ( uint ) pxTaskParams[ xIndex ]->iPin );
            gpio_set_dir( ( uint ) pxTaskParams[ xIndex ]->iPin, GPIO_OUT );
            gpio_put( ( uint ) pxTaskParams[ xIndex ]->iPin, 0 );
        }
    }
    gpio_init( PIN_IDLE0 ); gpio_set_dir( PIN_IDLE0, GPIO_OUT ); gpio_put( PIN_IDLE0, 0 );
    gpio_init( PIN_IDLE1 ); gpio_set_dir( PIN_IDLE1, GPIO_OUT ); gpio_put( PIN_IDLE1, 0 );
    gpio_init( PIN_TIMER ); gpio_set_dir( PIN_TIMER, GPIO_OUT ); gpio_put( PIN_TIMER, 0 );

    xCreateA = xTaskCreateEDFOnCore( vPartitionWorker,
                                     xTaskAParams.pcName,
                                     256,
                                     &xTaskAParams,
                                     pdMS_TO_TICKS( 1000 ),
                                     pdMS_TO_TICKS( 1000 ),
                                     pdMS_TO_TICKS( 700 ),
                                     0,
                                     &xTaskAHandle );
    if( xCreateA == pdPASS )
    {
        prvTraceRegister( xTaskAHandle, PIN_P_A );
    }

    xCreateB = xTaskCreateEDFOnCore( vPartitionWorker,
                                     xTaskBParams.pcName,
                                     256,
                                     &xTaskBParams,
                                     pdMS_TO_TICKS( 1000 ),
                                     pdMS_TO_TICKS( 1000 ),
                                     pdMS_TO_TICKS( 400 ),
                                     1,
                                     &xTaskBHandle );
    if( xCreateB == pdPASS )
    {
        prvTraceRegister( xTaskBHandle, PIN_P_B );
    }

    ( void ) xTaskCreate( vPartitionController,
                          "P_CTRL",
                          256,
                          NULL,
                          tskIDLE_PRIORITY + 1U,
                          NULL );

    printf( "[SMP][PARTITION][startup] createA=%ld createB=%ld admitted=%lu rejected=%lu\r\n",
            ( long ) xCreateA,
            ( long ) xCreateB,
            ( unsigned long ) uxTaskGetEDFAdmittedCount(),
            ( unsigned long ) uxTaskGetEDFRejectedCount() );
        printf( "[SMP][PARTITION][startup] pin map: P_A=%d P_B=%d IDLE0=%d IDLE1=%d TIMER=%d\r\n",
            PIN_P_A, PIN_P_B, PIN_IDLE0, PIN_IDLE1, PIN_TIMER );

    vTaskStartScheduler();

    for( ; ; )
    {
    }
}
/* End FreeRTOS CPSC_538G related - SMP - Task 4 partitioned EDF migration/remove API test */
