/* Begin FreeRTOS CPSC_538G related - SMP - Task 4 global EDF admission test */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "pico/stdlib.h"

#define PIN_TASK_A 10
#define PIN_TASK_B 11
#define PIN_TASK_C 12
#define PIN_TASK_REJECT 13
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
} SMPGlobalAdmissionParams_t;

static SMPGlobalAdmissionParams_t xTaskAParams = { "G_A", PIN_TASK_A, pdMS_TO_TICKS( 80 ) };
static SMPGlobalAdmissionParams_t xTaskBParams = { "G_B", PIN_TASK_B, pdMS_TO_TICKS( 100 ) };
static SMPGlobalAdmissionParams_t xTaskCParams = { "G_C", PIN_TASK_C, pdMS_TO_TICKS( 120 ) };
static SMPGlobalAdmissionParams_t xTaskRejectParams = { "G_REJECT", PIN_TASK_REJECT, pdMS_TO_TICKS( 100 ) };

static void prvBusyWorkTicks( TickType_t xDurationTicks )
{
    TickType_t xStart = xTaskGetTickCount();

    while( ( xTaskGetTickCount() - xStart ) < xDurationTicks )
    {
        /* Intentional spin to create deterministic compute demand. */
    }
}

static void vPeriodicWorker( void * pvParameters )
{
    SMPGlobalAdmissionParams_t * pxParams = ( SMPGlobalAdmissionParams_t * ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ; ; )
    {
        prvBusyWorkTicks( pxParams->xWorkTicks );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

void main_edf_test( void )
{
    BaseType_t xCreateA;
    BaseType_t xCreateB;
    BaseType_t xCreateC;
    BaseType_t xCreateReject;
    TaskHandle_t xHandleA = NULL;
    TaskHandle_t xHandleB = NULL;
    TaskHandle_t xHandleC = NULL;
    TaskHandle_t xHandleReject = NULL;
    SMPGlobalAdmissionParams_t * pxTaskParams[] =
    {
        &xTaskAParams,
        &xTaskBParams,
        &xTaskCParams,
        &xTaskRejectParams
    };
    size_t xIndex;

    for( xIndex = 0; xIndex < ( sizeof( pxTaskParams ) / sizeof( pxTaskParams[ 0 ] ) ); xIndex++ )
    {
        if( ( pxTaskParams[ xIndex ] != NULL ) && ( pxTaskParams[ xIndex ]->iPin >= 0 ) )
        {
            gpio_init( ( uint ) pxTaskParams[ xIndex ]->iPin );
            gpio_set_dir( ( uint ) pxTaskParams[ xIndex ]->iPin, GPIO_OUT );
            gpio_put( ( uint ) pxTaskParams[ xIndex ]->iPin, 0 );
        }
    }
    gpio_init( PIN_IDLE0 ); gpio_set_dir( PIN_IDLE0, GPIO_OUT ); gpio_put( PIN_IDLE0, 0 );
    gpio_init( PIN_IDLE1 ); gpio_set_dir( PIN_IDLE1, GPIO_OUT ); gpio_put( PIN_IDLE1, 0 );
    gpio_init( PIN_TIMER ); gpio_set_dir( PIN_TIMER, GPIO_OUT ); gpio_put( PIN_TIMER, 0 );

    printf( "[SMP][GLOBAL][admission] creating 3 admitted + 1 rejected implicit-deadline tasks\r\n" );
    printf( "[SMP][GLOBAL][admission] pin map: A=%d B=%d C=%d REJECT=%d IDLE0=%d IDLE1=%d TIMER=%d\r\n",
            PIN_TASK_A, PIN_TASK_B, PIN_TASK_C, PIN_TASK_REJECT, PIN_IDLE0, PIN_IDLE1, PIN_TIMER );

    xCreateA = xTaskCreateEDF( vPeriodicWorker,
                               xTaskAParams.pcName,
                               256,
                               &xTaskAParams,
                               pdMS_TO_TICKS( 1000 ),
                               pdMS_TO_TICKS( 1000 ),
                               pdMS_TO_TICKS( 200 ),
                               &xHandleA );
    if( xCreateA == pdPASS )
    {
        prvTraceRegister( xHandleA, PIN_TASK_A );
    }

    xCreateB = xTaskCreateEDF( vPeriodicWorker,
                               xTaskBParams.pcName,
                               256,
                               &xTaskBParams,
                               pdMS_TO_TICKS( 1000 ),
                               pdMS_TO_TICKS( 1000 ),
                               pdMS_TO_TICKS( 250 ),
                               &xHandleB );
    if( xCreateB == pdPASS )
    {
        prvTraceRegister( xHandleB, PIN_TASK_B );
    }

    xCreateC = xTaskCreateEDF( vPeriodicWorker,
                               xTaskCParams.pcName,
                               256,
                               &xTaskCParams,
                               pdMS_TO_TICKS( 1000 ),
                               pdMS_TO_TICKS( 1000 ),
                               pdMS_TO_TICKS( 300 ),
                               &xHandleC );
    if( xCreateC == pdPASS )
    {
        prvTraceRegister( xHandleC, PIN_TASK_C );
    }

    xCreateReject = xTaskCreateEDF( vPeriodicWorker,
                                    xTaskRejectParams.pcName,
                                    256,
                                    &xTaskRejectParams,
                                    pdMS_TO_TICKS( 1000 ),
                                    pdMS_TO_TICKS( 1000 ),
                                    pdMS_TO_TICKS( 900 ),
                                    &xHandleReject );
    if( xCreateReject == pdPASS )
    {
        prvTraceRegister( xHandleReject, PIN_TASK_REJECT );
    }

    printf( "[SMP][GLOBAL][admission] create A=%ld B=%ld C=%ld reject=%ld admitted=%lu rejected=%lu\r\n",
            ( long ) xCreateA,
            ( long ) xCreateB,
            ( long ) xCreateC,
            ( long ) xCreateReject,
            ( unsigned long ) uxTaskGetEDFAdmittedCount(),
            ( unsigned long ) uxTaskGetEDFRejectedCount() );

    vTaskStartScheduler();

    for( ; ; )
    {
    }
}
/* End FreeRTOS CPSC_538G related - SMP - Task 4 global EDF admission test */
