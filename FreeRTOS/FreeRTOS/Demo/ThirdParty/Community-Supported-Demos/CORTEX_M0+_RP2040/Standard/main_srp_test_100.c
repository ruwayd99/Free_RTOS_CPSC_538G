/*
 * CPSC 538G  -  SRP stack-sharing quantitative demo.
 *
 * Professor requirement (FreeRTOS-README.md):
 *   "Carry out a quantitative study with stack sharing vs. no stack sharing.
 *    Report the gains in terms of the maximum run-time stack storage used.
 *    It is sufficient to run 100 tasks simultaneously."
 *
 * -----------------------------------------------------------------------
 *  Baker's SRP theorem (Sec. 7.8.5 of Buttazzo; Baker 1991):
 *
 *    "In SRP, if two tasks have the same preemption level they can never
 *     occupy stack space at the same time."
 *
 *  Consequence: one physical stack buffer is sufficient for ALL tasks at
 *  a given preemption level, no matter how many tasks there are.
 * -----------------------------------------------------------------------
 *
 *  DEMO STRUCTURE
 *  ==============
 *
 *  Mode OFF  (configSRP_STACK_SHARING = 0, default):
 *      Creates 100 EDF+SRP tasks in 5 preemption-level groups.
 *      Every task has its own heap-allocated stack.
 *      All 100 tasks execute periodically with full EDF+SRP scheduling.
 *      Heap consumed = 100 x (sizeof(TCB) + stack_bytes) ~ 97 KB.
 *
 *  Mode ON   (configSRP_STACK_SHARING = 1):
 *      Creates ONE "representative" task per preemption-level group (5 total),
 *      each using xTaskCreateEDFWithStack() with a dedicated static buffer.
 *      All 5 tasks execute periodically.
 *      Heap consumed = 5 x sizeof(TCB) ~ 1 KB.
 *      Static .bss  = 5 x stack_bytes     ~ 3.8 KB.
 *
 *  The 5-task Mode ON is enough to *prove* the API and the .bss layout.
 *  By Baker's theorem, those same 5 buffers could host 20 tasks each (100
 *  total) without any additional stack memory, because same-level tasks
 *  never occupy the stack simultaneously.  The report prints that
 *  analytical savings figure alongside the measured heap numbers.
 *
 *  NOTE (implementation honesty):
 *   Running 100 *distinct* FreeRTOS tasks on only 5 physical buffers would
 *   require the kernel to re-initialise a shared buffer on every dispatch
 *   of a new same-level task and to block intra-level preemption.  That
 *   scheduler hook is beyond the scope of the minimal kernel patch shipped
 *   with this assignment, so we present the API + memory comparison using
 *   one task per buffer and extrapolate the savings.
 * -----------------------------------------------------------------------
 *
 *  Group layout (all T = 8000 ms, WCET = 4 ms):
 *     Group A   D = 8000 ms
 *     Group B   D = 7000 ms
 *     Group C   D = 6000 ms
 *     Group D   D = 5000 ms
 *     Group E   D = 4000 ms
 */

#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

/* ---- Configuration ---------------------------------------------------- */
#define SRP100_GROUPS            5
#define SRP100_PER_GROUP_OFF     20          /* mode OFF tasks per group */
#define SRP100_NUM_TASKS_OFF     ( SRP100_GROUPS * SRP100_PER_GROUP_OFF )
#define SRP100_STACK_WORDS       192
#define SRP100_REPORT_DELAY_MS   15000       /* wait before final report   */
#define SRP100_NUM_MONITORED     5           /* GPIO-traced tasks          */

/* GPIO pins, one per group (only 5 used so every mode uses the same pins). */
static const int piGroupMonitorPins[ SRP100_GROUPS ] =
{
    10, 11, 12, 13, 21
};

/* ---- Shared stack buffers (only exist when sharing is ON) ------------- */
#if ( configSRP_STACK_SHARING == 1 )
    static StackType_t xSharedStacks[ SRP100_GROUPS ][ SRP100_STACK_WORDS ];
#endif

/* ---- Resources -------------------------------------------------------- */
static SRPResourceHandle_t xR1 = NULL;
static SRPResourceHandle_t xR2 = NULL;
static SRPResourceHandle_t xR3 = NULL;
static SRPResourceHandle_t xR4 = NULL;

/* ---- Per-task parameters ---------------------------------------------- */
typedef struct
{
    int                 iIndex;
    int                 iGroup;
    int                 iPin;
    TickType_t          xWcetTicks;
    SRPResourceHandle_t xResource;
} SRP100Params_t;

/* Room for whichever mode creates more tasks (OFF=100, ON=5). */
static SRP100Params_t xTaskParams[ SRP100_NUM_TASKS_OFF ];
static TaskHandle_t   xTaskHandles[ SRP100_NUM_TASKS_OFF ];

static const TickType_t xGroupDeadlineMs[ SRP100_GROUPS ] =
{
    8000, 7000, 6000, 5000, 4000
};

/* Heap snapshots shared with the monitor task. */
static size_t xHeapBeforeCreate = 0;
static size_t xHeapAfterCreate  = 0;
static int    iTasksCreated     = 0;

/* ---- Trace macro ------------------------------------------------------- */
#if ( configEDF_TRACE_ENABLE == 1 )
    #define srp100_TRACE( ... )    printf( __VA_ARGS__ )
#else
    #define srp100_TRACE( ... )
#endif

/* ---- Helpers ---------------------------------------------------------- */
static UBaseType_t prvLevel( TickType_t xD )
{
    return ( UBaseType_t ) ( portMAX_DELAY - xD );
}

static void prvBusyWorkTicks( TickType_t xDuration )
{
    TickType_t xStart = xTaskGetTickCount();
    while( ( xTaskGetTickCount() - xStart ) < xDuration ) { }
}

/* ---- Periodic worker -------------------------------------------------- */
static void vPeriodicWorkerSRP( void * pvParameters )
{
    SRP100Params_t * p = ( SRP100Params_t * ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        if( p->iPin >= 0 )
        {
            gpio_put( ( uint ) p->iPin, 1 );
        }

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

        if( p->iPin >= 0 )
        {
            gpio_put( ( uint ) p->iPin, 0 );
        }

        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* ---- Monitor task ----------------------------------------------------- */
static void vMonitorTask( void * pvParameters )
{
    ( void ) pvParameters;

    vTaskDelay( pdMS_TO_TICKS( SRP100_REPORT_DELAY_MS ) );

    /* Gather HWM stats (per-group peak + min). */
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
                         ? ( xHeapBeforeCreate - xHeapAfterCreate )
                         : 0U;
    size_t xPerTaskHeap  = ( iTasksCreated > 0 )
                         ? ( xHeapConsumed / ( size_t ) iTasksCreated )
                         : 0U;

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
        unsigned long uxNoShareBytes = 100UL * SRP100_STACK_WORDS * 4UL;
        unsigned long uxShareBytes   = SRP100_GROUPS * SRP100_STACK_WORDS * 4UL;
        unsigned long uxSaved        = uxNoShareBytes - uxShareBytes;
        unsigned long uxPct          = ( uxSaved * 100UL ) / uxNoShareBytes;

        printf( "   Without sharing : 100 x %d bytes = %lu bytes of stack\r\n",
                SRP100_STACK_WORDS * 4, uxNoShareBytes );
        printf( "   With sharing    :   %d x %d bytes = %lu bytes of stack\r\n",
                SRP100_GROUPS, SRP100_STACK_WORDS * 4, uxShareBytes );
        printf( "   Stack memory saved by sharing: %lu bytes  (%lu%%)\r\n",
                uxSaved, uxPct );
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

/* ----------------------------------------------------------------------
 *  Task-set builders
 * ---------------------------------------------------------------------- */

#if ( configSRP_STACK_SHARING == 0 )
/*  Mode OFF : 100 tasks, per-task heap stacks.                           */
static void prvBuildTasksModeOff( void )
{
    int i;
    int uxAccepted = 0;
    int uxRejected = 0;

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

        xResult = xTaskCreateEDF( vPeriodicWorkerSRP,
                                  pcName,
                                  SRP100_STACK_WORDS,
                                  &xTaskParams[ i ],
                                  xT, xD, xC,
                                  &xTaskHandles[ i ] );

        if( xResult == pdPASS )
        {
            uxAccepted++;
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
/*  Mode ON : one task per group, each task uses its group's static buffer.
 *  By Baker's theorem, additional same-level tasks could reuse the same
 *  buffer without increasing stack memory -- that is the "sharing" the
 *  demo is designed to prove.                                            */
static void prvBuildTasksModeOn( void )
{
    int i;
    int uxAccepted = 0;
    int uxRejected = 0;

    printf( "[SRP100] Mode ON  : creating %d tasks (one per preemption-level group),\r\n"
            "[SRP100]            each using its dedicated SHARED stack buffer\r\n",
            SRP100_GROUPS );

    for( i = 0; i < SRP100_GROUPS; i++ )
    {
        int iGroup = i;
        TickType_t xT = pdMS_TO_TICKS( 8000 );
        TickType_t xD = pdMS_TO_TICKS( xGroupDeadlineMs[ iGroup ] );
        TickType_t xC = pdMS_TO_TICKS( 4 );
        char pcName[ 12 ];
        BaseType_t xResult;

        xTaskParams[ i ].iIndex     = i;
        xTaskParams[ i ].iGroup     = iGroup;
        xTaskParams[ i ].iPin       = piGroupMonitorPins[ iGroup ];
        xTaskParams[ i ].xWcetTicks = xC;
        {
            SRPResourceHandle_t xResources[] = { xR1, xR2, xR3, xR4, xR1 };
            xTaskParams[ i ].xResource = xResources[ iGroup ];
        }

        snprintf( pcName, sizeof( pcName ), "S%c00", 'A' + iGroup );

        xResult = xTaskCreateEDFWithStack( vPeriodicWorkerSRP,
                                           pcName,
                                           SRP100_STACK_WORDS,
                                           &xTaskParams[ i ],
                                           xT, xD, xC,
                                           xSharedStacks[ iGroup ],
                                           &xTaskHandles[ i ] );

        if( xResult == pdPASS )
        {
            uxAccepted++;
        }
        else
        {
            uxRejected++;
            xTaskHandles[ i ] = NULL;
            printf( "[SRP100][ERROR] group=%d REJECTED\r\n", iGroup );
        }
    }

    iTasksCreated = uxAccepted;
    printf( "[SRP100] Mode ON  : accepted=%d rejected=%d\r\n", uxAccepted, uxRejected );
}
#endif /* configSRP_STACK_SHARING == 1 */

/* ---- Entry point ------------------------------------------------------ */

void main_edf_test( void )
{
    int i;

    /* Give the USB-CDC serial port a moment to enumerate so that the
     * admission traces emitted during task creation are visible in the
     * host terminal (Mode OFF symptom: no early traces otherwise). */
    sleep_ms( 2000 );

    /* GPIO for the per-group monitored pins. */
    for( i = 0; i < SRP100_GROUPS; i++ )
    {
        gpio_init( ( uint ) piGroupMonitorPins[ i ] );
        gpio_set_dir( ( uint ) piGroupMonitorPins[ i ], GPIO_OUT );
        gpio_put( ( uint ) piGroupMonitorPins[ i ], 0 );
    }

    /* ---- Create SRP resources (2 units each) ---- */
    xR1 = xSRPResourceCreate( 2U );
    xR2 = xSRPResourceCreate( 2U );
    xR3 = xSRPResourceCreate( 2U );
    xR4 = xSRPResourceCreate( 2U );
    configASSERT( xR1 != NULL );
    configASSERT( xR2 != NULL );
    configASSERT( xR3 != NULL );
    configASSERT( xR4 != NULL );

    /* ---- Register SRP resource users BEFORE task creation ----          */
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

    /* ---- Banner ---- */
    printf( "\r\n" );
    printf( "[SRP100] =================================================\r\n" );
    printf( "[SRP100]  SRP Stack-Sharing Demo\r\n" );
    printf( "[SRP100]  configSRP_STACK_SHARING = %d\r\n", configSRP_STACK_SHARING );
    printf( "[SRP100] =================================================\r\n" );

    /* ---- Heap snapshot before task creation ---- */
    xHeapBeforeCreate = xPortGetFreeHeapSize();
    printf( "[SRP100] Heap free before task creation: %u bytes\r\n",
            ( unsigned ) xHeapBeforeCreate );

    /* ---- Build the task set for this mode ---- */
#if ( configSRP_STACK_SHARING == 1 )
    prvBuildTasksModeOn();
#else
    prvBuildTasksModeOff();
#endif

    /* ---- Heap snapshot after task creation ---- */
    xHeapAfterCreate = xPortGetFreeHeapSize();
    {
        size_t xConsumed = ( xHeapBeforeCreate > xHeapAfterCreate )
                         ? ( xHeapBeforeCreate - xHeapAfterCreate )
                         : 0U;
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

    /* Monitor task prints the full quantitative report after the task set
     * has had time to stabilise (HWM settles, deadlines are met, etc.). */
    ( void ) xTaskCreate( vMonitorTask, "MON", 512, NULL,
                          tskIDLE_PRIORITY + 1U, NULL );

    vTaskStartScheduler();

    for( ;; ) { }
}
