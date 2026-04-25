/*
 * CPSC 538G  -  SRP stack-sharing quantitative demo.
 *
 * 100 EDF+SRP tasks in 5 preemption-level groups demonstrate stack-sharing
 * savings.  One task per group drives a GPIO pin for logic-analyzer visibility;
 * two additional pins show the idle task and the timer daemon.
 *
 * Pin map (7 channels):
 *   10 Group-A   11 Group-B   12 Group-C   20 Group-D   19 Group-E
 *   13 IDLE       18 TIMER
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "pico/stdlib.h"

/* ---- Configuration ---- */
#define SRP100_GROUPS            5
#define SRP100_PER_GROUP_OFF     20
#define SRP100_NUM_TASKS_OFF     ( SRP100_GROUPS * SRP100_PER_GROUP_OFF )
#define SRP100_STACK_WORDS       192
#define SRP100_REPORT_DELAY_MS   15000
#define SRP100_NUM_MONITORED     5

/* GPIO pins, one per group. */
static const int piGroupMonitorPins[ SRP100_GROUPS ] =
{
    10, 11, 12, 13, 18
};

/* System-task pins. */
#define PIN_IDLE   20
#define PIN_TIMER  21

/* ---- Shared stack buffers (stack-sharing mode) -------------------------- */
#if ( configSRP_STACK_SHARING == 1 )
    static StackType_t xSharedStacks[ SRP100_GROUPS ][ SRP100_STACK_WORDS ];
#endif

/* ---- Resources --------------------------------------------------------- */
static SRPResourceHandle_t xR1 = NULL;
static SRPResourceHandle_t xR2 = NULL;
static SRPResourceHandle_t xR3 = NULL;
static SRPResourceHandle_t xR4 = NULL;

/* ---- Per-task parameters ----------------------------------------------- */
typedef struct
{
    int                 iIndex;
    int                 iGroup;
    int                 iPin;
    TickType_t          xWcetTicks;
    SRPResourceHandle_t xResource;
} SRP100Params_t;

static SRP100Params_t xTaskParams [ SRP100_NUM_TASKS_OFF ];
static TaskHandle_t   xTaskHandles[ SRP100_NUM_TASKS_OFF ];

static const TickType_t xGroupDeadlineMs[ SRP100_GROUPS ] =
{
    8000, 7000, 6000, 5000, 4000
};

/* Heap snapshots. */
static size_t xHeapBeforeCreate = 0;
static size_t xHeapAfterCreate  = 0;
static int    iTasksCreated     = 0;

/* ---- Trace macro ---- */
#if ( configEDF_TRACE_ENABLE == 1 )
    #define srp100_TRACE( ... )    printf( __VA_ARGS__ )
#else
    #define srp100_TRACE( ... )
#endif

/* ---- GPIO Kernel-Hook Trace Infrastructure -------------------------------- */
#define TRACE_MAX_TASKS  ( SRP100_GROUPS + 4 )

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

/* ---- Helpers ---- */
static UBaseType_t prvLevel( TickType_t xD )
{
    return ( UBaseType_t ) ( portMAX_DELAY - xD );
}

static void prvBusyWorkTicks( TickType_t xDuration )
{
    TickType_t xStart = xTaskGetTickCount();
    while( ( xTaskGetTickCount() - xStart ) < xDuration ) { }
}

/* ---- Periodic worker ---- */
static void vPeriodicWorkerSRP( void * pvParameters )
{
    SRP100Params_t * p = ( SRP100Params_t * ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        if( p->xResource != NULL )
        {
            while( xSRPResourceTake( p->xResource, 1U ) != pdPASS )
            {
                taskYIELD();
            }
            prvBusyWorkTicks( p->xWcetTicks );
            vSRPResourceGive( p->xResource, 1U );
        }
        else
        {
            prvBusyWorkTicks( p->xWcetTicks );
        }

        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* ---- Monitor task ---- */
static void vMonitorTask( void * pvParameters )
{
    ( void ) pvParameters;

    vTaskDelay( pdMS_TO_TICKS( SRP100_REPORT_DELAY_MS ) );

    UBaseType_t uxGroupPeakUsed[ SRP100_GROUPS ];
    UBaseType_t uxGroupMinHWM  [ SRP100_GROUPS ];

    for( int g = 0; g < SRP100_GROUPS; g++ )
    {
        uxGroupPeakUsed[ g ] = 0;
        uxGroupMinHWM  [ g ] = SRP100_STACK_WORDS;
    }

    for( int i = 0; i < iTasksCreated; i++ )
    {
        if( xTaskHandles[ i ] == NULL ) { continue; }

        UBaseType_t uxHWM  = uxTaskGetStackHighWaterMark( xTaskHandles[ i ] );
        UBaseType_t uxUsed = SRP100_STACK_WORDS - uxHWM;
        int g = xTaskParams[ i ].iGroup;

        if( uxUsed > uxGroupPeakUsed[ g ] ) { uxGroupPeakUsed[ g ] = uxUsed; }
        if( uxHWM  < uxGroupMinHWM  [ g ] ) { uxGroupMinHWM  [ g ] = uxHWM;  }
    }

    size_t xHeapConsumed = ( xHeapBeforeCreate > xHeapAfterCreate )
                         ? ( xHeapBeforeCreate - xHeapAfterCreate ) : 0U;
    size_t xPerTaskHeap  = ( iTasksCreated > 0 )
                         ? ( xHeapConsumed / ( size_t ) iTasksCreated ) : 0U;

    printf( "\r\n" );
    printf( "=============================================================\r\n" );
    printf( " SRP STACK-SHARING QUANTITATIVE REPORT\r\n" );
    printf( " configSRP_STACK_SHARING = %d  (%s)\r\n",
            configSRP_STACK_SHARING,
            ( configSRP_STACK_SHARING == 1 ) ? "ON  - shared static stacks"
                                             : "OFF - per-task heap stacks" );
    printf( "=============================================================\r\n" );

    printf( " Tasks created:              %d\r\n", iTasksCreated );
    printf( " Preemption-level groups:    %d\r\n", SRP100_GROUPS );
    printf( " Stack size per task/buffer: %d words (%d bytes)\r\n",
            SRP100_STACK_WORDS, SRP100_STACK_WORDS * 4 );
    printf( "\r\n" );

    printf( " --- MEASURED HEAP USAGE ---\r\n" );
    printf( "   Heap before task creation : %u bytes\r\n", ( unsigned ) xHeapBeforeCreate );
    printf( "   Heap after  task creation : %u bytes\r\n", ( unsigned ) xHeapAfterCreate  );
    printf( "   Heap consumed by tasks    : %u bytes  (~%u bytes/task)\r\n",
            ( unsigned ) xHeapConsumed, ( unsigned ) xPerTaskHeap );

#if ( configSRP_STACK_SHARING == 1 )
    printf( "   Static .bss for stacks    : %u bytes  (NOT on heap)\r\n",
            ( unsigned ) ( SRP100_GROUPS * SRP100_STACK_WORDS * 4 ) );
#endif

    printf( "\r\n" );
    printf( " --- PER-GROUP STACK HIGH-WATER MARK (after %d ms) ---\r\n",
            SRP100_REPORT_DELAY_MS );

    for( int g = 0; g < SRP100_GROUPS; g++ )
    {
        printf( "   Group %c  D=%lu ms : peak_used=%lu words,  min_HWM=%lu words\r\n",
                'A' + g,
                ( unsigned long ) xGroupDeadlineMs[ g ],
                ( unsigned long ) uxGroupPeakUsed[ g ],
                ( unsigned long ) uxGroupMinHWM  [ g ] );
    }

    printf( "\r\n" );
    printf( " --- PROJECTED SAVINGS AT 100 TASKS (20 per group) ---\r\n" );
    {
        unsigned long uxBssBytes = ( unsigned long ) ( SRP100_GROUPS * SRP100_STACK_WORDS * 4 );

#if ( configSRP_STACK_SHARING == 0 )
        unsigned long uxNoShareActual  = ( unsigned long ) xHeapConsumed;
        unsigned long uxShareProjected = uxBssBytes;
        unsigned long uxSaved          = ( uxNoShareActual > uxShareProjected )
                                         ? ( uxNoShareActual - uxShareProjected ) : 0UL;
        unsigned long uxPct            = ( uxNoShareActual > 0UL )
                                         ? ( uxSaved * 100UL / uxNoShareActual ) : 0UL;

        printf( "   Without sharing (measured, %d tasks)  : %lu bytes on heap"
                "  (~%lu bytes/task)\r\n",
                iTasksCreated, uxNoShareActual,
                ( unsigned long ) xPerTaskHeap );
        printf( "   With sharing    (projected, %d buffers): %lu bytes .bss stack"
                "  (%d x %d bytes)\r\n",
                SRP100_GROUPS, uxShareProjected,
                SRP100_GROUPS, SRP100_STACK_WORDS * 4 );
        printf( "   Memory saved by sharing               : %lu bytes  (%lu%%)\r\n",
                uxSaved, uxPct );
#else
        unsigned long uxShareActual    = ( unsigned long ) xHeapConsumed + uxBssBytes;
        unsigned long uxNoShareProject = ( unsigned long ) ( SRP100_NUM_TASKS_OFF * SRP100_STACK_WORDS * 4 );
        unsigned long uxSaved          = ( uxNoShareProject > uxShareActual )
                                         ? ( uxNoShareProject - uxShareActual ) : 0UL;
        unsigned long uxPct            = ( uxNoShareProject > 0UL )
                                         ? ( uxSaved * 100UL / uxNoShareProject ) : 0UL;

        printf( "   Without sharing (projected, 100 tasks): %lu bytes of heap stack"
                "  (100 x %d bytes)\r\n",
                uxNoShareProject, SRP100_STACK_WORDS * 4 );
        printf( "   With sharing    (measured,  %d tasks)  : %lu bytes heap"
                " + %lu bytes .bss = %lu bytes total\r\n",
                iTasksCreated, ( unsigned long ) xHeapConsumed,
                uxBssBytes, uxShareActual );
        printf( "   Memory saved by sharing               : %lu bytes  (%lu%%)\r\n",
                uxSaved, uxPct );
#endif
    }

    printf( "=============================================================\r\n\r\n" );

    TickType_t xLastWake = xTaskGetTickCount();
    for( ;; )
    {
        srp100_TRACE( "[SRP100][monitor][tick=%lu] admitted=%lu rejected=%lu\r\n",
                      ( unsigned long ) xTaskGetTickCount(),
                      ( unsigned long ) uxTaskGetEDFAdmittedCount(),
                      ( unsigned long ) uxTaskGetEDFRejectedCount() );
        vTaskDelayUntil( &xLastWake, pdMS_TO_TICKS( 5000 ) );
    }
}

/* ---- Task-set builders ---- */

#if ( configSRP_STACK_SHARING == 0 )
static void prvBuildTasksModeOff( void )
{
    int i, uxAccepted = 0, uxRejected = 0;

    printf( "[SRP100] Mode OFF : creating %d tasks (each with its own heap stack)\r\n",
            SRP100_NUM_TASKS_OFF );

    for( i = 0; i < SRP100_NUM_TASKS_OFF; i++ )
    {
        int iGroup = i / SRP100_PER_GROUP_OFF;
        int iSlot  = i % SRP100_PER_GROUP_OFF;
        TickType_t xT = pdMS_TO_TICKS( 8000 );
        TickType_t xD = pdMS_TO_TICKS( xGroupDeadlineMs[ iGroup ] );
        TickType_t xC = pdMS_TO_TICKS( 4 );
        char pcName[ 12 ];
        BaseType_t xResult;

        xTaskParams[ i ].iIndex     = i;
        xTaskParams[ i ].iGroup     = iGroup;
        xTaskParams[ i ].iPin       = ( iSlot == 0 ) ? piGroupMonitorPins[ iGroup ] : -1;
        xTaskParams[ i ].xWcetTicks = xC;
        xTaskParams[ i ].xResource  = NULL;

        if( iSlot < 3 )
        {
            SRPResourceHandle_t xResources[] = { xR1, xR2, xR3, xR4, xR1 };
            xTaskParams[ i ].xResource = xResources[ iGroup ];
        }

        snprintf( pcName, sizeof( pcName ), "S%c%02d", 'A' + iGroup, iSlot );

        xResult = xTaskCreateEDF( vPeriodicWorkerSRP, pcName,
                                  SRP100_STACK_WORDS, &xTaskParams[ i ],
                                  xT, xD, xC, &xTaskHandles[ i ] );

        if( xResult == pdPASS )
        {
            uxAccepted++;
            /* Only slot-0 of each group is traced on GPIO. */
            if( xTaskParams[ i ].iPin >= 0 )
            {
                prvTraceRegister( xTaskHandles[ i ], ( uint ) xTaskParams[ i ].iPin );
            }
        }
        else
        {
            uxRejected++;
            xTaskHandles[ i ] = NULL;
            printf( "[SRP100][ERROR] idx=%d REJECTED\r\n", i );
        }
    }

    iTasksCreated = uxAccepted;
    printf( "[SRP100] Mode OFF : accepted=%d rejected=%d\r\n", uxAccepted, uxRejected );
}
#endif /* configSRP_STACK_SHARING == 0 */

#if ( configSRP_STACK_SHARING == 1 )
static void prvBuildTasksModeOn( void )
{
    int i, uxAccepted = 0, uxRejected = 0;

    printf( "[SRP100] Mode ON  : creating %d tasks (%d per group x %d groups)\r\n",
            SRP100_NUM_TASKS_OFF, SRP100_PER_GROUP_OFF, SRP100_GROUPS );

    for( i = 0; i < SRP100_NUM_TASKS_OFF; i++ )
    {
        int        iGroup = i / SRP100_PER_GROUP_OFF;
        int        iSlot  = i % SRP100_PER_GROUP_OFF;
        TickType_t xT     = pdMS_TO_TICKS( 8000 );
        TickType_t xD     = pdMS_TO_TICKS( xGroupDeadlineMs[ iGroup ] );
        TickType_t xC     = pdMS_TO_TICKS( 4 );
        char       pcName[ 12 ];
        BaseType_t xResult;

        xTaskParams[ i ].iIndex     = i;
        xTaskParams[ i ].iGroup     = iGroup;
        xTaskParams[ i ].iPin       = ( iSlot == 0 ) ? piGroupMonitorPins[ iGroup ] : -1;
        xTaskParams[ i ].xWcetTicks = xC;
        {
            SRPResourceHandle_t xResources[] = { xR1, xR2, xR3, xR4, xR1 };
            xTaskParams[ i ].xResource = ( iSlot < 3 ) ? xResources[ iGroup ] : NULL;
        }

        snprintf( pcName, sizeof( pcName ), "S%c%02d", 'A' + iGroup, iSlot );

        xResult = xTaskCreateEDFWithStack( vPeriodicWorkerSRP, pcName,
                                           SRP100_STACK_WORDS, &xTaskParams[ i ],
                                           xT, xD, xC,
                                           xSharedStacks[ iGroup ],
                                           &xTaskHandles[ i ] );

        if( xResult == pdPASS )
        {
            uxAccepted++;
            if( xTaskParams[ i ].iPin >= 0 )
            {
                prvTraceRegister( xTaskHandles[ i ], ( uint ) xTaskParams[ i ].iPin );
            }
        }
        else
        {
            uxRejected++;
            xTaskHandles[ i ] = NULL;
            printf( "[SRP100][ERROR] idx=%d group=%d slot=%d REJECTED\r\n",
                    i, iGroup, iSlot );
        }
    }

    iTasksCreated = uxAccepted;
    printf( "[SRP100] Mode ON  : accepted=%d rejected=%d\r\n", uxAccepted, uxRejected );
}
#endif /* configSRP_STACK_SHARING == 1 */

/* ---- Entry point ---- */
void main_edf_test( void )
{
    int i;

    sleep_ms( 2000 );

    /* Always initialise the full 8-channel logic-analyzer pin set. */
    {
        const uint auLogicAnalyzerPins[] = { 10, 11, 12, 13, 18, 19, 20, 21 };
        for( i = 0; i < ( int ) ( sizeof( auLogicAnalyzerPins ) / sizeof( auLogicAnalyzerPins[ 0 ] ) ); i++ )
        {
            gpio_init( auLogicAnalyzerPins[ i ] );
            gpio_set_dir( auLogicAnalyzerPins[ i ], GPIO_OUT );
            gpio_put( auLogicAnalyzerPins[ i ], 0 );
        }
    }

    /* Create SRP resources. */
    xR1 = xSRPResourceCreate( 2U );
    xR2 = xSRPResourceCreate( 2U );
    xR3 = xSRPResourceCreate( 2U );
    xR4 = xSRPResourceCreate( 2U );
    configASSERT( xR1 != NULL );
    configASSERT( xR2 != NULL );
    configASSERT( xR3 != NULL );
    configASSERT( xR4 != NULL );

    /* Register SRP resource users. */
    for( i = 0; i < 3; i++ )
        vSRPResourceRegisterUser( xR1, prvLevel( pdMS_TO_TICKS( 8000 ) ), 1U, pdMS_TO_TICKS( 2 ) );
    for( i = 0; i < 3; i++ )
        vSRPResourceRegisterUser( xR2, prvLevel( pdMS_TO_TICKS( 7000 ) ), 1U, pdMS_TO_TICKS( 2 ) );
    for( i = 0; i < 3; i++ )
        vSRPResourceRegisterUser( xR3, prvLevel( pdMS_TO_TICKS( 6000 ) ), 1U, pdMS_TO_TICKS( 2 ) );
    for( i = 0; i < 3; i++ )
        vSRPResourceRegisterUser( xR4, prvLevel( pdMS_TO_TICKS( 5000 ) ), 1U, pdMS_TO_TICKS( 2 ) );
    for( i = 0; i < 3; i++ )
        vSRPResourceRegisterUser( xR1, prvLevel( pdMS_TO_TICKS( 4000 ) ), 1U, pdMS_TO_TICKS( 2 ) );

    printf( "\r\n" );
    printf( "[SRP100] =================================================\r\n" );
    printf( "[SRP100]  SRP Stack-Sharing Demo\r\n" );
    printf( "[SRP100]  configSRP_STACK_SHARING = %d\r\n", configSRP_STACK_SHARING );
    printf( "[SRP100]  Pin map: Groups A..E -> GPIO 10,11,12,20,19  IDLE -> 13  TIMER -> 18\r\n" );
    printf( "[SRP100] =================================================\r\n" );

    xHeapBeforeCreate = xPortGetFreeHeapSize();
    printf( "[SRP100] Heap free before task creation: %u bytes\r\n",
            ( unsigned ) xHeapBeforeCreate );

#if ( configSRP_STACK_SHARING == 1 )
    prvBuildTasksModeOn();
#else
    prvBuildTasksModeOff();
#endif

    xHeapAfterCreate = xPortGetFreeHeapSize();
    {
        size_t xConsumed = ( xHeapBeforeCreate > xHeapAfterCreate )
                         ? ( xHeapBeforeCreate - xHeapAfterCreate ) : 0U;
        printf( "[SRP100] Heap free  after task creation: %u bytes\r\n",
                ( unsigned ) xHeapAfterCreate );
        printf( "[SRP100] Heap consumed by %d tasks   : %u bytes  (~%u bytes/task)\r\n",
                iTasksCreated, ( unsigned ) xConsumed,
                ( unsigned ) ( ( iTasksCreated > 0 ) ? ( xConsumed / ( size_t ) iTasksCreated ) : 0U ) );
#if ( configSRP_STACK_SHARING == 1 )
        printf( "[SRP100] Static .bss for shared stacks : %u bytes  (NOT on heap)\r\n",
                ( unsigned ) ( SRP100_GROUPS * SRP100_STACK_WORDS * 4 ) );
#endif
    }

    ( void ) xTaskCreate( vMonitorTask, "MON", 512, NULL,
                          tskIDLE_PRIORITY + 1U, NULL );

    vTaskStartScheduler();

    for( ;; ) { }
}
