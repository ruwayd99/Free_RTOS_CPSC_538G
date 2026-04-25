
// Begin FreeRTOS CPSC_538G related - CBS - CBS basic test
/*
 * Pin map (6 channels):
 *   10 PERIODIC_0   11 PERIODIC_1   12 PERIODIC_2
 *   21 CBS_aperiodic
 *   18 IDLE          19 TIMER
 */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "pico/stdlib.h"

#define PIN_PERIODIC_BASE   10
#define NUM_PERIODIC_TASKS  3
#define PIN_CBS_TASK        13

#define PERIODIC_PERIOD_MS   1000
#define PERIODIC_WCET_MS     150

#define CBS_PERIOD_MS        1000
#define CBS_BUDGET_MS        150

#define APPROX_CBS_TRIGGER_PERIOD_MS  700
#define CBS_WCET_MS                   90

/* System-task pins. */
#define PIN_IDLE   20
#define PIN_TIMER  21

/* ---- GPIO Kernel-Hook Trace Infrastructure -------------------------------- */
#define TRACE_MAX_TASKS  10

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

/* ---- State --------------------------------------------------------------- */
static TaskHandle_t xCBSTaskHandle    = NULL;
static TaskHandle_t xCBSTriggerHandle = NULL;
static TaskHandle_t xPeriodicTaskHandles[ NUM_PERIODIC_TASKS ] = { NULL };

static volatile uint32_t  ulPeriodicRunCount[ NUM_PERIODIC_TASKS ] = { 0U };
static volatile uint32_t  ulCBSRunCount = 0U;
static volatile TickType_t xLastCBSTriggerTick = 0U;
static volatile TickType_t xLastCBSStartTick   = 0U;
static volatile TickType_t xLastCBSEndTick     = 0U;

/* ---- Helpers ------------------------------------------------------------- */
static void prvBusyWait( TickType_t xTicks )
{
    TickType_t xStart = xTaskGetTickCount();
    while( ( xTaskGetTickCount() - xStart ) < xTicks ) {}
}

/* ---- Task bodies --------------------------------------------------------- */
static void vPeriodicTask( void * pvParameters )
{
    int idx = ( int ) ( uintptr_t ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        ulPeriodicRunCount[ idx ]++;
        prvBusyWait( pdMS_TO_TICKS( PERIODIC_WCET_MS ) );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

static void vCBSAperiodicTask( void * pvParameters )
{
    ( void ) pvParameters;

    for( ;; )
    {
        ulTaskNotifyTake( pdTRUE, portMAX_DELAY );

        xLastCBSStartTick = xTaskGetTickCount();
        prvBusyWait( pdMS_TO_TICKS( CBS_WCET_MS ) );
        xLastCBSEndTick = xTaskGetTickCount();
        ulCBSRunCount++;

        printf( "[CBS TEST] [CBS_aperiodic] job=%lu START=%lu END=%lu trigger=%lu\n",
                ( unsigned long ) ulCBSRunCount,
                ( unsigned long ) xLastCBSStartTick,
                ( unsigned long ) xLastCBSEndTick,
                ( unsigned long ) xLastCBSTriggerTick );
    }
}

static void vCBSTriggerTask( void * pvParameters )
{
    ( void ) pvParameters;
    for( ;; )
    {
        vTaskDelay( pdMS_TO_TICKS( APPROX_CBS_TRIGGER_PERIOD_MS ) );
        if( xCBSTaskHandle )
        {
            xLastCBSTriggerTick = xTaskGetTickCount();
            xTaskNotifyGive( xCBSTaskHandle );
        }
    }
}

/* ---- Entry point --------------------------------------------------------- */
void main_cbs_test( void )
{
    int i;

    stdio_init_all();

    /* Always initialise the full 8-channel logic-analyzer pin set. */
    const uint auLogicAnalyzerPins[] = { 10, 11, 12, 13, 18, 19, 20, 21 };
    for( i = 0; i < ( int ) ( sizeof( auLogicAnalyzerPins ) / sizeof( auLogicAnalyzerPins[ 0 ] ) ); i++ )
    {
        gpio_init( auLogicAnalyzerPins[ i ] );
        gpio_set_dir( auLogicAnalyzerPins[ i ], GPIO_OUT );
        gpio_put( auLogicAnalyzerPins[ i ], 0 );
    }

    printf( "[CBS TEST] \n" );
    printf( "[CBS TEST] Starting with %d periodic tasks.\n", NUM_PERIODIC_TASKS );
    printf( "[CBS TEST] Version 2.\n" );
    printf( "[CBS TEST] Pin map: PERIODIC_%d..%d -> GPIO %d..%d, CBS -> GPIO %d, IDLE -> GPIO %d, TIMER -> GPIO %d\n",
            0, NUM_PERIODIC_TASKS - 1,
            PIN_PERIODIC_BASE, PIN_PERIODIC_BASE + NUM_PERIODIC_TASKS - 1,
            PIN_CBS_TASK, PIN_IDLE, PIN_TIMER );

    for( i = 0; i < NUM_PERIODIC_TASKS; i++ )
    {
        char name[ 16 ];
        snprintf( name, sizeof( name ), "PERIODIC_%d", i );
        configASSERT( xTaskCreateEDF( vPeriodicTask, name, 1024,
                                      ( void * ) ( uintptr_t ) i,
                                      pdMS_TO_TICKS( PERIODIC_PERIOD_MS ),
                                      pdMS_TO_TICKS( PERIODIC_PERIOD_MS ),
                                      pdMS_TO_TICKS( PERIODIC_WCET_MS ),
                                      &xPeriodicTaskHandles[ i ] ) == pdPASS );
        prvTraceRegister( xPeriodicTaskHandles[ i ], ( uint ) ( PIN_PERIODIC_BASE + i ) );
    }

    configASSERT( xTaskCreateCBS( vCBSAperiodicTask, "CBS", 1280, NULL,
                                  pdMS_TO_TICKS( CBS_BUDGET_MS ),
                                  pdMS_TO_TICKS( CBS_PERIOD_MS ),
                                  &xCBSTaskHandle ) == pdPASS );
    prvTraceRegister( xCBSTaskHandle, PIN_CBS_TASK );

    configASSERT( xTaskCreate( vCBSTriggerTask, "CBS_TRIGGER", 1024, NULL,
                               tskIDLE_PRIORITY + 1, &xCBSTriggerHandle ) == pdPASS );

    vTaskStartScheduler();
    for( ;; ) {}
}
// End FreeRTOS CPSC_538G related - CBS - CBS basic test
