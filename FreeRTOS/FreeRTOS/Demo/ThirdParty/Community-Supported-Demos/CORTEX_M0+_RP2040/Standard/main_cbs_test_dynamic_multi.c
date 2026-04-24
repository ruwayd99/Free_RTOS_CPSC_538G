
// Begin FreeRTOS CPSC_538G related - CBS - CBS dynamic multi-task test
/*
 * Dynamic multi-task CBS test.  All 8 logic-analyzer channels are used:
 *
 *   10 PERIODIC_10  11 PERIODIC_11  12 PERIODIC_12  13 PERIODIC_13
 *      (subsequent periodic tasks added at runtime)
 *   18 CBS_0        19 CBS_1
 *      (initial CBS tasks; CBS_2 and CBS_3 added later have no GPIO pin)
 *   20 IDLE         21 TIMER
 *
 * The last two pins (20, 21) are reserved for the system tasks so that
 * the logic analyzer shows idle and timer-daemon activity.
 */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "pico/stdlib.h"

#define PIN_PERIODIC_SUB_BASE  10   /* subsequent periodic tasks: 10..13 */
#define PIN_CBS_BASE           18   /* CBS_0=18, CBS_1=19 (CBS_2,3 no pin) */
#define PIN_IDLE               20
#define PIN_TIMER              21

#define NUM_PERIODIC_TASKS_INITIAL    10
#define NUM_PERIODIC_TASKS_SUBSEQUENT  4
#define PERIODIC_SUBSEQUENT_DISPATCH_DELAY_MS  2000

#define NUM_CBS_TASKS_INITIAL    2
#define NUM_CBS_TASKS_SUBSEQUENT 2

#define PERIODIC_PERIOD_MS   3000
#define PERIODIC_WCET_MS     120

#define CBS_PERIOD_MS        1000
#define CBS_BUDGET_MS        200

#define APPROX_CBS_TRIGGER_PERIOD_MS    30
#define CBS_WCET_MS                     20

/* ---- GPIO Kernel-Hook Trace Infrastructure -------------------------------- */
#define TRACE_MAX_TASKS  20

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
static TaskHandle_t xCBSTriggerHandle = NULL;
static TaskHandle_t xPeriodicTaskDispatchHandle = NULL;

#define NUM_CBS_TASKS_TOTAL      ( NUM_CBS_TASKS_INITIAL + NUM_CBS_TASKS_SUBSEQUENT )
static TaskHandle_t xCBSTaskHandles[ NUM_CBS_TASKS_TOTAL ] = { NULL };

#define NUM_PERIODIC_TASKS_TOTAL ( NUM_PERIODIC_TASKS_INITIAL + NUM_PERIODIC_TASKS_SUBSEQUENT )
static TaskHandle_t xPeriodicTaskHandles[ NUM_PERIODIC_TASKS_TOTAL ] = { NULL };

static volatile uint32_t   ulPeriodicRunCount[ NUM_PERIODIC_TASKS_TOTAL ] = { 0U };
static volatile uint32_t   ulCBSRunCount     [ NUM_CBS_TASKS_TOTAL ]      = { 0U };
static volatile TickType_t xLastCBSTriggerTick[ NUM_CBS_TASKS_TOTAL ]     = { 0U };
static volatile TickType_t xLastCBSStartTick  [ NUM_CBS_TASKS_TOTAL ]     = { 0U };
static volatile TickType_t xLastCBSEndTick    [ NUM_CBS_TASKS_TOTAL ]     = { 0U };
static volatile uint32_t   ulNextCBSToTrigger = 0U;

/* ---- Helpers ------------------------------------------------------------- */
/* Returns the GPIO pin for a subsequent periodic task, or -1 for initial ones.
 * Only subsequent tasks (indices NUM_PERIODIC_TASKS_INITIAL .. total-1) get pins. */
static BaseType_t prvGetPeriodicPinFromIndex( int idx, uint * pinOut )
{
    if( ( idx >= ( int ) NUM_PERIODIC_TASKS_INITIAL ) &&
        ( idx < ( int ) NUM_PERIODIC_TASKS_TOTAL ) )
    {
        *pinOut = PIN_PERIODIC_SUB_BASE + ( uint ) ( idx - ( int ) NUM_PERIODIC_TASKS_INITIAL );
        return pdTRUE;
    }
    return pdFALSE;
}

/* Only CBS_0 and CBS_1 get GPIO pins; CBS_2 and CBS_3 have no pin. */
static BaseType_t prvGetCBSPinFromIndex( int idx, uint * pinOut )
{
    if( ( idx >= 0 ) && ( idx < NUM_CBS_TASKS_INITIAL ) )
    {
        *pinOut = PIN_CBS_BASE + ( uint ) idx;
        return pdTRUE;
    }
    return pdFALSE;
}

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
    int idx = ( int ) ( uintptr_t ) pvParameters;

    for( ;; )
    {
        ulTaskNotifyTake( pdTRUE, portMAX_DELAY );

        xLastCBSStartTick[ idx ] = xTaskGetTickCount();
        prvBusyWait( pdMS_TO_TICKS( CBS_WCET_MS ) );
        xLastCBSEndTick[ idx ] = xTaskGetTickCount();
        ulCBSRunCount[ idx ]++;

        printf( "[CBS TEST] [CBS_%d] job=%lu START=%lu END=%lu trigger=%lu\n",
                idx,
                ( unsigned long ) ulCBSRunCount[ idx ],
                ( unsigned long ) xLastCBSStartTick[ idx ],
                ( unsigned long ) xLastCBSEndTick[ idx ],
                ( unsigned long ) xLastCBSTriggerTick[ idx ] );
    }
}

static void vCBSTriggerTask( void * pvParameters )
{
    ( void ) pvParameters;
    for( ;; )
    {
        vTaskDelay( pdMS_TO_TICKS( APPROX_CBS_TRIGGER_PERIOD_MS ) );

        {
            TaskHandle_t xTargetHandle = NULL;
            uint32_t ulTargetIndex;

            taskENTER_CRITICAL();
            {
                ulTargetIndex  = ulNextCBSToTrigger % NUM_CBS_TASKS_TOTAL;
                xTargetHandle  = xCBSTaskHandles[ ulTargetIndex ];
                ulNextCBSToTrigger = ( ulNextCBSToTrigger + 1U ) % NUM_CBS_TASKS_TOTAL;
            }
            taskEXIT_CRITICAL();

            if( xTargetHandle != NULL )
            {
                xLastCBSTriggerTick[ ulTargetIndex ] = xTaskGetTickCount();
                xTaskNotifyGive( xTargetHandle );
            }
        }
    }
}

static void vPeriodicTriggerTask( void * pvParameters )
{
    ( void ) pvParameters;

    printf( "[CBS TEST] [PERIODIC_DISPATCH] tick %lu creating %d subsequent tasks\n",
            ( unsigned long ) xTaskGetTickCount(),
            NUM_PERIODIC_TASKS_SUBSEQUENT );

    for( int i = 0; i < NUM_PERIODIC_TASKS_SUBSEQUENT; i++ )
    {
        vTaskDelay( pdMS_TO_TICKS( PERIODIC_SUBSEQUENT_DISPATCH_DELAY_MS ) );

        int idx = ( int ) NUM_PERIODIC_TASKS_INITIAL + i;
        char name[ 20 ];
        snprintf( name, sizeof( name ), "PERIODIC_%d", idx );

        configASSERT( xTaskCreateEDF( vPeriodicTask, name, 1024,
                                      ( void * ) ( uintptr_t ) idx,
                                      pdMS_TO_TICKS( PERIODIC_PERIOD_MS ),
                                      pdMS_TO_TICKS( PERIODIC_PERIOD_MS ),
                                      pdMS_TO_TICKS( PERIODIC_WCET_MS ),
                                      &xPeriodicTaskHandles[ idx ] ) == pdPASS );

        /* Register subsequent periodic task pin. */
        {
            uint pin;
            if( prvGetPeriodicPinFromIndex( idx, &pin ) != pdFALSE )
            {
                taskENTER_CRITICAL();
                prvTraceRegister( xPeriodicTaskHandles[ idx ], pin );
                taskEXIT_CRITICAL();
            }
        }

        /* Add subsequent CBS tasks (CBS_2 and CBS_3 have no GPIO pin). */
        if( i < NUM_CBS_TASKS_SUBSEQUENT )
        {
            int cbsIdx = NUM_CBS_TASKS_INITIAL + i;
            char cbsName[ 16 ];
            snprintf( cbsName, sizeof( cbsName ), "CBS_%d", cbsIdx );

            configASSERT( xTaskCreateCBS( vCBSAperiodicTask, cbsName, 1280,
                                          ( void * ) ( uintptr_t ) cbsIdx,
                                          pdMS_TO_TICKS( CBS_BUDGET_MS ),
                                          pdMS_TO_TICKS( CBS_PERIOD_MS ),
                                          &xCBSTaskHandles[ cbsIdx ] ) == pdPASS );
            /* No GPIO pin for CBS_2 / CBS_3 – those pin numbers are now idle/timer. */
        }
    }

    printf( "[CBS TEST] [PERIODIC_DISPATCH] completed, deleting self at tick %lu\n",
            ( unsigned long ) xTaskGetTickCount() );
    vTaskDelete( NULL );
}

/* ---- Entry point --------------------------------------------------------- */
void main_cbs_test( void )
{
    stdio_init_all();

    /* Initialise all 8 GPIO pins up-front. */
    for( int i = 0; i < NUM_PERIODIC_TASKS_SUBSEQUENT; i++ )
    {
        gpio_init( PIN_PERIODIC_SUB_BASE + ( uint ) i );
        gpio_set_dir( PIN_PERIODIC_SUB_BASE + ( uint ) i, GPIO_OUT );
        gpio_put( PIN_PERIODIC_SUB_BASE + ( uint ) i, 0 );
    }
    for( int i = 0; i < NUM_CBS_TASKS_INITIAL; i++ )
    {
        gpio_init( PIN_CBS_BASE + ( uint ) i );
        gpio_set_dir( PIN_CBS_BASE + ( uint ) i, GPIO_OUT );
        gpio_put( PIN_CBS_BASE + ( uint ) i, 0 );
    }
    gpio_init( PIN_IDLE );  gpio_set_dir( PIN_IDLE,  GPIO_OUT ); gpio_put( PIN_IDLE,  0 );
    gpio_init( PIN_TIMER ); gpio_set_dir( PIN_TIMER, GPIO_OUT ); gpio_put( PIN_TIMER, 0 );

    printf( "[CBS TEST] \n" );
    printf( "[CBS TEST] Starting with %d periodic tasks.\n", NUM_PERIODIC_TASKS_INITIAL );
    printf( "[CBS TEST] Starting with %d CBS tasks.\n", NUM_CBS_TASKS_INITIAL );
    printf( "[CBS TEST] Version 2.\n" );
    printf( "[CBS TEST] Pin map: subsequent PERIODIC_%d..%d -> GPIO %d..%d, "
            "CBS_0..1 -> GPIO %d..%d, IDLE -> GPIO %d, TIMER -> GPIO %d\n",
            NUM_PERIODIC_TASKS_INITIAL, NUM_PERIODIC_TASKS_TOTAL - 1,
            PIN_PERIODIC_SUB_BASE, PIN_PERIODIC_SUB_BASE + NUM_PERIODIC_TASKS_SUBSEQUENT - 1,
            PIN_CBS_BASE, PIN_CBS_BASE + NUM_CBS_TASKS_INITIAL - 1,
            PIN_IDLE, PIN_TIMER );

    /* Initial periodic tasks (no GPIO – too many to show). */
    for( int i = 0; i < NUM_PERIODIC_TASKS_INITIAL; ++i )
    {
        char name[ 16 ];
        snprintf( name, sizeof( name ), "PERIODIC_%d", i );
        configASSERT( xTaskCreateEDF( vPeriodicTask, name, 1024,
                                      ( void * ) ( uintptr_t ) i,
                                      pdMS_TO_TICKS( PERIODIC_PERIOD_MS ),
                                      pdMS_TO_TICKS( PERIODIC_PERIOD_MS ),
                                      pdMS_TO_TICKS( PERIODIC_WCET_MS ),
                                      &xPeriodicTaskHandles[ i ] ) == pdPASS );
    }

    /* Initial CBS tasks (CBS_0=pin18, CBS_1=pin19). */
    for( int i = 0; i < NUM_CBS_TASKS_INITIAL; ++i )
    {
        char name[ 16 ];
        snprintf( name, sizeof( name ), "CBS_%d", i );

        configASSERT( xTaskCreateCBS( vCBSAperiodicTask, name, 1280,
                                      ( void * ) ( uintptr_t ) i,
                                      pdMS_TO_TICKS( CBS_BUDGET_MS ),
                                      pdMS_TO_TICKS( CBS_PERIOD_MS ),
                                      &xCBSTaskHandles[ i ] ) == pdPASS );

        uint pin;
        if( prvGetCBSPinFromIndex( i, &pin ) != pdFALSE )
        {
            prvTraceRegister( xCBSTaskHandles[ i ], pin );
        }
    }

    /* CBS trigger and periodic dispatcher. */
    configASSERT( xTaskCreate( vCBSTriggerTask, "CBS_TRIGGER", 1024, NULL,
                               tskIDLE_PRIORITY + 1, &xCBSTriggerHandle ) == pdPASS );

    configASSERT( xTaskCreate( vPeriodicTriggerTask, "PERIODIC_DISPATCH", 1024, NULL,
                               tskIDLE_PRIORITY + 1, &xPeriodicTaskDispatchHandle ) == pdPASS );

    vTaskStartScheduler();
    for( ;; ) {}
}
// End FreeRTOS CPSC_538G related - CBS - CBS dynamic multi-task test
