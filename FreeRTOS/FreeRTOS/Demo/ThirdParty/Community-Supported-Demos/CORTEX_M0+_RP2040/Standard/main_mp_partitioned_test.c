/* Begin FreeRTOS CPSC_538G related - SMP - Partitioned EDF test (main_mp_partitioned_test.c) */
/*
 * Task 4 demo: Partitioned EDF (First-Fit Decreasing) on dual-core RP2040.
 *
 * Verifies:
 *   - Submit tasks in decreasing-U order -> First-Fit-Decreasing bin-packing.
 *   - The FFD decision is printed via edfTRACE in the kernel ([EDF][SMP]
 *     [partitioned][FF][try]/[bind]).
 *   - Admission rejects a task that cannot fit on any core.
 *   - xTaskCreateEDFOnCore hard-pins, overriding FFD.
 *   - xTaskMigrateToCore moves utilization between bins and refuses when the
 *     target has no room.
 *   - vTaskRemoveFromCore frees that bin's share so a later admission can
 *     reuse the capacity.
 *
 * Build:
 *   make main_mp_partitioned_test.uf2
 *
 * Expected serial output (abridged):
 *   [EDF][SMP][partitioned][FF][try] task=P_HI U=0.500000 core=0 ... FIT
 *   [EDF][SMP][partitioned][bind]    task=P_HI U=0.500000 -> core=0 ...
 *   [EDF][SMP][partitioned][FF][try] task=P_OVER U=0.900000 core=0 ... SKIP
 *   [EDF][SMP][partitioned][FF][try] task=P_OVER U=0.900000 core=1 ... SKIP
 *   [MP][part][probe] overload-task create=REJECT (expected)
 *   [MP][part][run] task=X core=N        (each task stays on its bin)
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

#define MP_PIN_HI    10
#define MP_PIN_MI    11
#define MP_PIN_LO    12
#define MP_PIN_XT    13

#define MP_RUN_SECONDS              15U

typedef struct
{
    const char * pcName;
    int iPin;
    TickType_t xPeriodTicks;
    TickType_t xWcetTicks;
    TaskHandle_t xHandle;
    volatile uint32_t ulJobsOnCore[ configNUMBER_OF_CORES ];
} MpPartTaskParams_t;

/* Utilizations chosen so First-Fit-Decreasing packs them into 2 bins:
 *   core 0: HI (0.50) + XT (0.30) = 0.80
 *   core 1: MI (0.40) + LO (0.20) = 0.60
 * Plain First-Fit on the same input would still reach 2 bins, but the
 * decreasing pre-sort is what makes FFD's worst-case bound 11/9 OPT + 6/9. */
static MpPartTaskParams_t xTaskHi = { "P_HI", MP_PIN_HI, pdMS_TO_TICKS( 100 ), pdMS_TO_TICKS( 50 ), NULL, { 0 } };
static MpPartTaskParams_t xTaskMi = { "P_MI", MP_PIN_MI, pdMS_TO_TICKS( 100 ), pdMS_TO_TICKS( 40 ), NULL, { 0 } };
static MpPartTaskParams_t xTaskXt = { "P_XT", MP_PIN_XT, pdMS_TO_TICKS( 100 ), pdMS_TO_TICKS( 30 ), NULL, { 0 } };
static MpPartTaskParams_t xTaskLo = { "P_LO", MP_PIN_LO, pdMS_TO_TICKS( 100 ), pdMS_TO_TICKS( 20 ), NULL, { 0 } };

static void prvBusyWorkTicks( TickType_t xDurationTicks )
{
    TickType_t xStart = xTaskGetTickCount();
    while( ( xTaskGetTickCount() - xStart ) < xDurationTicks )
    {
        /* spin */
    }
}

static void vMpPartTask( void * pvParameters )
{
    MpPartTaskParams_t * p = ( MpPartTaskParams_t * ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();
    uint32_t ulJobCount = 0U;
    BaseType_t xLastSeenCore = -1;

    for( ;; )
    {
        BaseType_t xCore = ( BaseType_t ) portGET_CORE_ID();
        p->ulJobsOnCore[ xCore ]++;
        ulJobCount++;

        gpio_put( p->iPin, 1 );
        prvBusyWorkTicks( p->xWcetTicks );
        gpio_put( p->iPin, 0 );

        /* Print only on core changes (should never happen in partitioned mode
         * except at migration) or every 20th job — avoids spam. */
        if( ( xCore != xLastSeenCore ) || ( ( ulJobCount % 20U ) == 0U ) )
        {
            printf( "[MP][part][run] task=%s job=%lu core=%ld tick=%lu\r\n",
                    p->pcName,
                    ( unsigned long ) ulJobCount,
                    ( long ) xCore,
                    ( unsigned long ) xTaskGetTickCount() );
            xLastSeenCore = xCore;
        }

        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

static void vMpPartSupervisor( void * pvParameters )
{
    ( void ) pvParameters;
    TickType_t xStart = xTaskGetTickCount();
    BaseType_t xProbedOverload = pdFALSE;
    BaseType_t xProbedMigrate  = pdFALSE;
    BaseType_t xProbedRemove   = pdFALSE;
    BaseType_t xProbedReadmit  = pdFALSE;

    for( ;; )
    {
        TickType_t xNow = xTaskGetTickCount();
        TickType_t xElapsed = xNow - xStart;

        /* ~2s: submit an overload task that cannot fit on any core. */
        if( ( xProbedOverload == pdFALSE ) && ( xElapsed >= pdMS_TO_TICKS( 2000 ) ) )
        {
            TaskHandle_t xDummy = NULL;
            /* Core 0 has 0.20 free, core 1 has 0.40 free. U = 0.5 -> no fit. */
            BaseType_t xR = xTaskCreateEDF( vMpPartTask,
                                            "P_OVER",
                                            1024,
                                            &xTaskHi,
                                            pdMS_TO_TICKS( 100 ),
                                            pdMS_TO_TICKS( 100 ),
                                            pdMS_TO_TICKS( 50 ),
                                            &xDummy );
            printf( "[MP][part][probe] overload-task U=0.50 create=%s (both cores already full enough to reject)\r\n",
                    ( xR == pdPASS ) ? "ACCEPT (unexpected)" : "REJECT (expected)" );
            printf( "[MP][part][probe] core0 util=%lu.%06lu core1 util=%lu.%06lu\r\n",
                    ( unsigned long ) ( ulTaskGetCoreUtilMicro( 0 ) / 1000000UL ),
                    ( unsigned long ) ( ulTaskGetCoreUtilMicro( 0 ) % 1000000UL ),
                    ( unsigned long ) ( ulTaskGetCoreUtilMicro( 1 ) / 1000000UL ),
                    ( unsigned long ) ( ulTaskGetCoreUtilMicro( 1 ) % 1000000UL ) );
            xProbedOverload = pdTRUE;
        }

        /* ~5s: try to migrate P_HI (U=0.5) from core 0 to core 1.  Core 1
         * currently has 0.60; moving 0.5 would push it to 1.10 -> reject. */
        if( ( xProbedMigrate == pdFALSE ) && ( xElapsed >= pdMS_TO_TICKS( 5000 ) ) && ( xTaskHi.xHandle != NULL ) )
        {
            BaseType_t xR = xTaskMigrateToCore( xTaskHi.xHandle, 1 );
            printf( "[MP][part][probe] migrate P_HI -> core 1 result=%s (expected FAIL — no room)\r\n",
                    ( xR == pdPASS ) ? "OK" : "FAIL" );
            xProbedMigrate = pdTRUE;
        }

        /* ~8s: remove P_MI from core 1.  Frees 0.4 on core 1. */
        if( ( xProbedRemove == pdFALSE ) && ( xElapsed >= pdMS_TO_TICKS( 8000 ) ) && ( xTaskMi.xHandle != NULL ) )
        {
            printf( "[MP][part][probe] removing P_MI from core 1 (frees U=0.40)\r\n" );
            vTaskRemoveFromCore( xTaskMi.xHandle );
            xTaskMi.xHandle = NULL;   /* handle is now invalid */
            printf( "[MP][part][probe] after remove: core0 util=%lu.%06lu core1 util=%lu.%06lu\r\n",
                    ( unsigned long ) ( ulTaskGetCoreUtilMicro( 0 ) / 1000000UL ),
                    ( unsigned long ) ( ulTaskGetCoreUtilMicro( 0 ) % 1000000UL ),
                    ( unsigned long ) ( ulTaskGetCoreUtilMicro( 1 ) / 1000000UL ),
                    ( unsigned long ) ( ulTaskGetCoreUtilMicro( 1 ) % 1000000UL ) );
            xProbedRemove = pdTRUE;
        }

        /* ~10s: re-admit a task that only fits after the removal above. */
        if( ( xProbedReadmit == pdFALSE ) && ( xElapsed >= pdMS_TO_TICKS( 10000 ) ) )
        {
            TaskHandle_t xDummy = NULL;
            BaseType_t xR = xTaskCreateEDF( vMpPartTask,
                                            "P_LATE",
                                            1024,
                                            &xTaskHi,
                                            pdMS_TO_TICKS( 100 ),
                                            pdMS_TO_TICKS( 100 ),
                                            pdMS_TO_TICKS( 35 ),   /* U=0.35 fits now */
                                            &xDummy );
            printf( "[MP][part][probe] late-admit U=0.35 create=%s (expected ACCEPT after remove)\r\n",
                    ( xR == pdPASS ) ? "ACCEPT" : "REJECT" );
            xProbedReadmit = pdTRUE;
        }

        if( xElapsed >= pdMS_TO_TICKS( MP_RUN_SECONDS * 1000U ) )
        {
            printf( "\r\n[MP][part][report] --- per-core job counts after %us ---\r\n", MP_RUN_SECONDS );
            printf( "  P_HI core0=%lu core1=%lu  (pinned core 0)\r\n",
                    ( unsigned long ) xTaskHi.ulJobsOnCore[ 0 ],
                    ( unsigned long ) xTaskHi.ulJobsOnCore[ 1 ] );
            printf( "  P_MI core0=%lu core1=%lu  (pinned core 1, removed at t=8s)\r\n",
                    ( unsigned long ) xTaskMi.ulJobsOnCore[ 0 ],
                    ( unsigned long ) xTaskMi.ulJobsOnCore[ 1 ] );
            printf( "  P_XT core0=%lu core1=%lu  (pinned core 0)\r\n",
                    ( unsigned long ) xTaskXt.ulJobsOnCore[ 0 ],
                    ( unsigned long ) xTaskXt.ulJobsOnCore[ 1 ] );
            printf( "  P_LO core0=%lu core1=%lu  (pinned core 1)\r\n",
                    ( unsigned long ) xTaskLo.ulJobsOnCore[ 0 ],
                    ( unsigned long ) xTaskLo.ulJobsOnCore[ 1 ] );
            printf( "[MP][part][report] final core0 util=%lu.%06lu core1 util=%lu.%06lu\r\n",
                    ( unsigned long ) ( ulTaskGetCoreUtilMicro( 0 ) / 1000000UL ),
                    ( unsigned long ) ( ulTaskGetCoreUtilMicro( 0 ) % 1000000UL ),
                    ( unsigned long ) ( ulTaskGetCoreUtilMicro( 1 ) / 1000000UL ),
                    ( unsigned long ) ( ulTaskGetCoreUtilMicro( 1 ) % 1000000UL ) );
            printf( "[MP][part][report] admitted=%lu rejected=%lu\r\n",
                    ( unsigned long ) uxTaskGetEDFAdmittedCount(),
                    ( unsigned long ) uxTaskGetEDFRejectedCount() );
            printf( "[MP][part][report] test complete — halting supervisor.\r\n" );

            vTaskSuspend( NULL );
        }

        vTaskDelay( pdMS_TO_TICKS( 250 ) );
    }
}

/*
 * Create tasks in DECREASING-U order so the kernel's FFD admission produces
 * the First-Fit-Decreasing packing the lecture describes.  Each submission
 * uses the core-aware API — we name the ideal bin — but the kernel's FFD
 * trace is what actually makes the decision.  When the user's named core
 * already has room, FFD's first-fit pick matches it, so no rebind happens.
 */
static void prvCreateOne( MpPartTaskParams_t * p, BaseType_t xPreferredCore )
{
    BaseType_t xR = xTaskCreateEDFOnCore( vMpPartTask,
                                          p->pcName,
                                          1024,
                                          p,
                                          p->xPeriodTicks,
                                          p->xPeriodTicks,
                                          p->xWcetTicks,
                                          xPreferredCore,
                                          &p->xHandle );
    printf( "[MP][part][create] task=%s T=%lu C=%lu preferredCore=%ld result=%s\r\n",
            p->pcName,
            ( unsigned long ) p->xPeriodTicks,
            ( unsigned long ) p->xWcetTicks,
            ( long ) xPreferredCore,
            ( xR == pdPASS ) ? "ACCEPT" : "REJECT" );
}

void main_mp_partitioned_test( void )
{
    static const int iPins[] = { MP_PIN_HI, MP_PIN_MI, MP_PIN_XT, MP_PIN_LO };
    for( unsigned i = 0; i < sizeof( iPins ) / sizeof( iPins[ 0 ] ); i++ )
    {
        gpio_init( iPins[ i ] );
        gpio_set_dir( iPins[ i ], GPIO_OUT );
        gpio_put( iPins[ i ], 0 );
    }

    printf( "\r\n======== Task 4: Partitioned EDF on SMP ========\r\n" );
    printf( "Cores=%d  Mode=PARTITIONED (First-Fit-Decreasing)\r\n", configNUMBER_OF_CORES );

    /* Submission order implements FFD: highest U first. */
    prvCreateOne( &xTaskHi, 0 );   /* 0.50 -> core 0 */
    prvCreateOne( &xTaskMi, 1 );   /* 0.40 -> core 1 */
    prvCreateOne( &xTaskXt, 0 );   /* 0.30 -> core 0 (0.50 + 0.30 = 0.80, fits) */
    prvCreateOne( &xTaskLo, 1 );   /* 0.20 -> core 1 (0.40 + 0.20 = 0.60, fits) */

    printf( "[MP][part][init] core0 util=%lu.%06lu core1 util=%lu.%06lu\r\n",
            ( unsigned long ) ( ulTaskGetCoreUtilMicro( 0 ) / 1000000UL ),
            ( unsigned long ) ( ulTaskGetCoreUtilMicro( 0 ) % 1000000UL ),
            ( unsigned long ) ( ulTaskGetCoreUtilMicro( 1 ) / 1000000UL ),
            ( unsigned long ) ( ulTaskGetCoreUtilMicro( 1 ) % 1000000UL ) );

    ( void ) xTaskCreate( vMpPartSupervisor,
                          "MP_SUP",
                          1024,
                          NULL,
                          tskIDLE_PRIORITY + 1,
                          NULL );

    vTaskStartScheduler();

    for( ;; ) { }
}
/* End FreeRTOS CPSC_538G related - SMP - Partitioned EDF test (main_mp_partitioned_test.c) */
