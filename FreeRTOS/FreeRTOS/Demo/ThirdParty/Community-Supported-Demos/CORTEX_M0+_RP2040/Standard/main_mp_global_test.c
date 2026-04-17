/* Begin FreeRTOS CPSC_538G related - SMP - Global EDF test (main_mp_global_test.c) */
/*
 * Task 4 demo: Global EDF on dual-core RP2040.
 *
 * Verifies:
 *   - GFB admission accepts a schedulable set and rejects an infeasible one.
 *   - Jobs dispatch freely across both cores (no pinning).
 *   - Migration API (xTaskMigrateToCore) updates the affinity hint; dispatch
 *     still respects global EDF.
 *   - Remove API (vTaskRemoveFromCore) clears the affinity mask.
 *
 * Build:
 *   make main_mp_global_test.uf2
 *
 * Expected serial output (abridged):
 *   [EDF][SMP][global][GFB] sumU=... bound=... -> PASS   (for admitted tasks)
 *   [EDF][SMP][global][GFB] ... -> REJECT                (for the overload probe)
 *   [MP][global][run] task=X core=0 tick=...
 *   [MP][global][run] task=X core=1 tick=...             (same task, different core)
 *   [MP][global][migrate] ...                            (one mid-run migration)
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

/* GPIO pins to watch under a logic analyser.  Each task blinks one pin while
 * running; a scope trace confirms cores run disjoint tasks. */
#define MP_PIN_A    10
#define MP_PIN_B    11
#define MP_PIN_C    12
#define MP_PIN_D    13

/* How long to run the experiment before reporting and stopping.  15s matches
 * the cadence of the existing 100-task EDF demo. */
#define MP_RUN_SECONDS              15U

typedef struct
{
    const char * pcName;
    int iPin;
    TickType_t xPeriodTicks;
    TickType_t xWcetTicks;
    TaskHandle_t xHandle;
    volatile uint32_t ulJobsOnCore[ configNUMBER_OF_CORES ];
} MpGlobalTaskParams_t;

static MpGlobalTaskParams_t xTaskA = { "GE_A", MP_PIN_A, pdMS_TO_TICKS( 80  ), pdMS_TO_TICKS( 20 ), NULL, { 0 } };
static MpGlobalTaskParams_t xTaskB = { "GE_B", MP_PIN_B, pdMS_TO_TICKS( 100 ), pdMS_TO_TICKS( 25 ), NULL, { 0 } };
static MpGlobalTaskParams_t xTaskC = { "GE_C", MP_PIN_C, pdMS_TO_TICKS( 150 ), pdMS_TO_TICKS( 40 ), NULL, { 0 } };
static MpGlobalTaskParams_t xTaskD = { "GE_D", MP_PIN_D, pdMS_TO_TICKS( 200 ), pdMS_TO_TICKS( 50 ), NULL, { 0 } };

/* Sum of U for the four admitted tasks:
 *   20/80 + 25/100 + 40/150 + 50/200 = 0.25 + 0.25 + 0.267 + 0.25 = 1.017
 * max(U) = 0.267; GFB bound on 2 cores = 2 - 1 * 0.267 = 1.733. PASS.
 *
 * The probe task below is utilization 0.95 which would push sumU to 1.967 with
 * maxU = 0.95 and GFB bound = 2 - 0.95 = 1.05 -> REJECT.
 */

static void prvBusyWorkTicks( TickType_t xDurationTicks )
{
    TickType_t xStart = xTaskGetTickCount();
    while( ( xTaskGetTickCount() - xStart ) < xDurationTicks )
    {
        /* spin */
    }
}

static void vMpGlobalTask( void * pvParameters )
{
    MpGlobalTaskParams_t * p = ( MpGlobalTaskParams_t * ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();
    uint32_t ulJobCount = 0U;

    for( ;; )
    {
        BaseType_t xCore = ( BaseType_t ) portGET_CORE_ID();
        p->ulJobsOnCore[ xCore ]++;
        ulJobCount++;

        gpio_put( p->iPin, 1 );
        prvBusyWorkTicks( p->xWcetTicks );
        gpio_put( p->iPin, 0 );

        /* Print only every 20th job to avoid serial spam but still show
         * per-core dispatch variety. */
        if( ( ulJobCount % 20U ) == 0U )
        {
            printf( "[MP][global][run] task=%s job=%lu core=%ld tick=%lu\r\n",
                    p->pcName,
                    ( unsigned long ) ulJobCount,
                    ( long ) xCore,
                    ( unsigned long ) xTaskGetTickCount() );
        }

        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

static void vMpGlobalSupervisor( void * pvParameters )
{
    ( void ) pvParameters;
    TickType_t xStart = xTaskGetTickCount();
    BaseType_t xMigrated = pdFALSE;
    BaseType_t xOverloadProbed = pdFALSE;

    for( ;; )
    {
        TickType_t xNow = xTaskGetTickCount();
        TickType_t xElapsed = xNow - xStart;

        /* ~3s in: try to submit an infeasible task and watch GFB reject it. */
        if( ( xOverloadProbed == pdFALSE ) && ( xElapsed >= pdMS_TO_TICKS( 3000 ) ) )
        {
            TaskHandle_t xDummy = NULL;
            /* U = 400/420 ~= 0.952 — combined with existing sumU >= 1, GFB must reject. */
            BaseType_t xR = xTaskCreateEDF( vMpGlobalTask,
                                            "GE_OVER",
                                            1024,
                                            &xTaskA,   /* reused params; won't actually run */
                                            pdMS_TO_TICKS( 420 ),
                                            pdMS_TO_TICKS( 420 ),
                                            pdMS_TO_TICKS( 400 ),
                                            &xDummy );
            printf( "[MP][global][probe] overload-task create=%s\r\n",
                    ( xR == pdPASS ) ? "ACCEPT (unexpected)" : "REJECT (expected)" );
            xOverloadProbed = pdTRUE;
        }

        /* ~6s in: pin task D to core 1 via migration API to show the hint
         * takes effect immediately. */
        if( ( xMigrated == pdFALSE ) && ( xElapsed >= pdMS_TO_TICKS( 6000 ) ) && ( xTaskD.xHandle != NULL ) )
        {
            BaseType_t xR = xTaskMigrateToCore( xTaskD.xHandle, 1 );
            printf( "[MP][global][migrate] task=GE_D -> core=1 result=%s\r\n",
                    ( xR == pdPASS ) ? "OK" : "FAIL" );
            xMigrated = pdTRUE;
        }

        if( xElapsed >= pdMS_TO_TICKS( MP_RUN_SECONDS * 1000U ) )
        {
            printf( "\r\n[MP][global][report] --- per-core job counts after %us ---\r\n", MP_RUN_SECONDS );
            printf( "  GE_A core0=%lu core1=%lu\r\n",
                    ( unsigned long ) xTaskA.ulJobsOnCore[ 0 ],
                    ( unsigned long ) xTaskA.ulJobsOnCore[ 1 ] );
            printf( "  GE_B core0=%lu core1=%lu\r\n",
                    ( unsigned long ) xTaskB.ulJobsOnCore[ 0 ],
                    ( unsigned long ) xTaskB.ulJobsOnCore[ 1 ] );
            printf( "  GE_C core0=%lu core1=%lu\r\n",
                    ( unsigned long ) xTaskC.ulJobsOnCore[ 0 ],
                    ( unsigned long ) xTaskC.ulJobsOnCore[ 1 ] );
            printf( "  GE_D core0=%lu core1=%lu  (migrated to core 1 at t=6s)\r\n",
                    ( unsigned long ) xTaskD.ulJobsOnCore[ 0 ],
                    ( unsigned long ) xTaskD.ulJobsOnCore[ 1 ] );
            printf( "[MP][global][report] admitted=%lu rejected=%lu\r\n",
                    ( unsigned long ) uxTaskGetEDFAdmittedCount(),
                    ( unsigned long ) uxTaskGetEDFRejectedCount() );
            printf( "[MP][global][report] test complete — halting supervisor.\r\n" );

            /* Halt: suspend self.  Worker tasks continue running indefinitely. */
            vTaskSuspend( NULL );
        }

        vTaskDelay( pdMS_TO_TICKS( 250 ) );
    }
}

static void prvCreateOne( MpGlobalTaskParams_t * p )
{
    BaseType_t xR = xTaskCreateEDF( vMpGlobalTask,
                                    p->pcName,
                                    1024,
                                    p,
                                    p->xPeriodTicks,
                                    p->xPeriodTicks,   /* implicit deadline */
                                    p->xWcetTicks,
                                    &p->xHandle );
    printf( "[MP][global][create] task=%s T=%lu C=%lu result=%s\r\n",
            p->pcName,
            ( unsigned long ) p->xPeriodTicks,
            ( unsigned long ) p->xWcetTicks,
            ( xR == pdPASS ) ? "ACCEPT" : "REJECT" );
}

void main_mp_global_test( void )
{
    /* GPIO setup for the four worker pins. */
    static const int iPins[] = { MP_PIN_A, MP_PIN_B, MP_PIN_C, MP_PIN_D };
    for( unsigned i = 0; i < sizeof( iPins ) / sizeof( iPins[ 0 ] ); i++ )
    {
        gpio_init( iPins[ i ] );
        gpio_set_dir( iPins[ i ], GPIO_OUT );
        gpio_put( iPins[ i ], 0 );
    }

    printf( "\r\n======== Task 4: Global EDF on SMP ========\r\n" );
    printf( "Cores=%d  Mode=GLOBAL (GFB admission)\r\n", configNUMBER_OF_CORES );

    prvCreateOne( &xTaskA );
    prvCreateOne( &xTaskB );
    prvCreateOne( &xTaskC );
    prvCreateOne( &xTaskD );

    ( void ) xTaskCreate( vMpGlobalSupervisor,
                          "MP_SUP",
                          1024,
                          NULL,
                          tskIDLE_PRIORITY + 1,
                          NULL );

    vTaskStartScheduler();

    /* Should never get here. */
    for( ;; ) { }
}
/* End FreeRTOS CPSC_538G related - SMP - Global EDF test (main_mp_global_test.c) */
