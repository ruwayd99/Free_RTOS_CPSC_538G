/*
 * CPSC 538G  SRP dynamic admission-control test.
 *
 * Demonstrates:
 *   - 3 multi-unit SRP resources  (R1: 4 units, R2: 3 units, R3: 2 units)
 *   - Nested (re-entrant) multi-unit access to a single resource
 *   - Nested cross-resource access (holding R1 while taking R2)
 *   - Sequential multi-resource access within one task
 *   - Multiple tasks competing for overlapping resources
 *   - Runtime dynamic admission:  one task ACCEPTED, one REJECTED
 *
 * Workload (startup – 4 tasks):
 *
 *   T1 "NESTED"   T=5000  D=3000  C=1000  (constrained)
 *      R1: 2 units (CS=700)   R2: 1 unit (CS=300)
 *      Pattern: Take R1(2) → Take R2(1) → Take R1(1 re-entry) →
 *               work → Give R1(1) → Give R2(1) → Give R1(2)
 *
 *   T2 "HEAVY"    T=6000  D=6000  C=800   (implicit)
 *      R1: 2 units (CS=500)
 *      Pattern: Take R1(2) → work → Give R1(2)
 *
 *   T3 "DUAL"     T=8000  D=5000  C=700   (constrained)
 *      R2: 2 units (CS=300)   R3: 1 unit (CS=250)
 *      Pattern: Take R2(2) → work → Give R2(2) →
 *               Take R3(1) → work → Give R3(1)
 *
 *   T4 "GUARD"    T=10000 D=10000 C=500   (implicit)
 *      R3: 1 unit  (CS=400)
 *      Pattern: Take R3(1) → work → Give R3(1)
 *
 * Runtime additions by orchestrator:
 *
 *   T5 "RT_OK"    T=12000 D=12000 C=400   → ACCEPTED
 *      R3: 1 unit  (CS=300)
 *
 *   T6 "OVERLOAD" T=1500  D=1500  C=1200  → REJECTED
 *      R2: 1 unit  (CS=1000)
 *
 * Each of the main 4+1 accepted tasks drives a GPIO pin so a logic
 * analyzer can verify EDF+SRP scheduling behaviour.
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

/* ---- GPIO pins -------------------------------------------------------- */
#define PIN_T1   10
#define PIN_T2   11
#define PIN_T3   12
#define PIN_T4   21
#define PIN_T5   20
#define PIN_T6   19   /* OVERLOAD task pin -- stays LOW because admission rejects it */

/* ---- Resources (created in main_edf_test) ----------------------------- */
static SRPResourceHandle_t xR1 = NULL;   /* 4 units */
static SRPResourceHandle_t xR2 = NULL;   /* 3 units */
static SRPResourceHandle_t xR3 = NULL;   /* 2 units */

/* ---- Helper: preemption level from relative deadline ------------------- */
static UBaseType_t prvLevel( TickType_t xD )
{
    return ( UBaseType_t ) ( portMAX_DELAY - xD );
}

/* ---- Busy spin for a given number of ticks ----------------------------- */
static void prvBusyWorkTicks( TickType_t xDuration )
{
    TickType_t xStart = xTaskGetTickCount();

    while( ( xTaskGetTickCount() - xStart ) < xDuration )
    {
        /* Intentional spin. */
    }
}

/* ---- Task bodies ------------------------------------------------------ */

/*
 * T1 "NESTED": showcases re-entrant + cross-resource nesting.
 *
 *   Take R1(2)           -- acquire 2 of 4 R1 units
 *     Take R2(1)         -- while R1 held, acquire 1 of 3 R2 units
 *       Take R1(1)       -- re-entry: holder==self, acquires 1 more R1 unit
 *         busy work
 *       Give R1(1)       -- release the extra R1 unit
 *     Give R2(1)         -- release R2
 *   Give R1(2)           -- release original R1 hold
 *   busy work (non-CS)
 */
static void vTaskNested( void * pv )
{
    ( void ) pv;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( PIN_T1, 1 );

        /* Outer: R1(2) */
        while( xSRPResourceTake( xR1, 2U ) != pdPASS )
        {
            taskYIELD();
        }

        prvBusyWorkTicks( pdMS_TO_TICKS( 100 ) );

        /* Nested: R2(1) while R1 held */
        while( xSRPResourceTake( xR2, 1U ) != pdPASS )
        {
            taskYIELD();
        }

        prvBusyWorkTicks( pdMS_TO_TICKS( 100 ) );

        /* Re-entry: R1(1) more while R1+R2 held (holder==self) */
        while( xSRPResourceTake( xR1, 1U ) != pdPASS )
        {
            taskYIELD();
        }

        prvBusyWorkTicks( pdMS_TO_TICKS( 200 ) );

        vSRPResourceGive( xR1, 1U );   /* release extra R1 unit */
        vSRPResourceGive( xR2, 1U );   /* release R2 */
        vSRPResourceGive( xR1, 2U );   /* release original R1 hold */

        /* Non-critical-section remainder. */
        prvBusyWorkTicks( pdMS_TO_TICKS( 100 ) );

        gpio_put( PIN_T1, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* T2 "HEAVY": multi-unit lock on R1. */
static void vTaskHeavy( void * pv )
{
    ( void ) pv;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( PIN_T2, 1 );

        while( xSRPResourceTake( xR1, 2U ) != pdPASS )
        {
            taskYIELD();
        }

        prvBusyWorkTicks( pdMS_TO_TICKS( 500 ) );
        vSRPResourceGive( xR1, 2U );

        prvBusyWorkTicks( pdMS_TO_TICKS( 200 ) );

        gpio_put( PIN_T2, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* T3 "DUAL": sequential access to R2 then R3. */
static void vTaskDual( void * pv )
{
    ( void ) pv;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( PIN_T3, 1 );

        while( xSRPResourceTake( xR2, 2U ) != pdPASS )
        {
            taskYIELD();
        }

        prvBusyWorkTicks( pdMS_TO_TICKS( 300 ) );
        vSRPResourceGive( xR2, 2U );

        while( xSRPResourceTake( xR3, 1U ) != pdPASS )
        {
            taskYIELD();
        }

        prvBusyWorkTicks( pdMS_TO_TICKS( 250 ) );
        vSRPResourceGive( xR3, 1U );

        prvBusyWorkTicks( pdMS_TO_TICKS( 100 ) );

        gpio_put( PIN_T3, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* T4 "GUARD": simple single-resource access to R3. */
static void vTaskGuard( void * pv )
{
    ( void ) pv;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( PIN_T4, 1 );

        while( xSRPResourceTake( xR3, 1U ) != pdPASS )
        {
            taskYIELD();
        }

        prvBusyWorkTicks( pdMS_TO_TICKS( 400 ) );
        vSRPResourceGive( xR3, 1U );

        prvBusyWorkTicks( pdMS_TO_TICKS( 50 ) );

        gpio_put( PIN_T4, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* T5 "RT_OK": runtime-admitted task, uses R3(1).
 * Uses R3 instead of R1 to avoid concurrent holding with NESTED on R1,
 * which would break the single-holder tracking model and cause the
 * dynamic ceiling to spike to NESTED's own level (deadlock). */
static void vTaskRuntimeOK( void * pv )
{
    ( void ) pv;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( PIN_T5, 1 );

        while( xSRPResourceTake( xR3, 1U ) != pdPASS )
        {
            taskYIELD();
        }

        prvBusyWorkTicks( pdMS_TO_TICKS( 300 ) );
        vSRPResourceGive( xR3, 1U );

        prvBusyWorkTicks( pdMS_TO_TICKS( 50 ) );

        gpio_put( PIN_T5, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* T6 "OVERLOAD": task body assigned to the overload candidate.
 * Drives PIN_T6.  This body is NEVER invoked because admission control
 * rejects the task before it is created, so PIN_T6 stays permanently LOW.
 * That flat line on the logic analyzer is the proof of rejection. */
static void vTaskOverload( void * pv )
{
    ( void ) pv;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( PIN_T6, 1 );

        while( xSRPResourceTake( xR2, 1U ) != pdPASS )
        {
            taskYIELD();
        }

        prvBusyWorkTicks( pdMS_TO_TICKS( 1000 ) );
        vSRPResourceGive( xR2, 1U );

        gpio_put( PIN_T6, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* ---- Orchestrator (non-EDF) ------------------------------------------- */

static void vOrchestratorTask( void * pv )
{
    BaseType_t xResult;

    ( void ) pv;

    printf( "[SRP-DYN][orch] warmup  admitted=%lu  sysceil=%lu\r\n",
            ( unsigned long ) uxTaskGetEDFAdmittedCount(),
            ( unsigned long ) uxSRPGetSystemCeiling() );

    vTaskDelay( pdMS_TO_TICKS( 10000 ) );

    /* ---- Phase 2: add RT_OK (T5), expect ACCEPT ---- */
    printf( "[SRP-DYN][orch] adding RT_OK  T=12000 D=12000 C=400\r\n" );

    /* Register T5 as user of R3 BEFORE creating the task so admission
     * control can compute the correct blocking bound. */
    vSRPResourceRegisterUser( xR3,
                              prvLevel( pdMS_TO_TICKS( 12000 ) ),
                              1U,
                              pdMS_TO_TICKS( 300 ) );

    xResult = xTaskCreateEDF( vTaskRuntimeOK,
                              "RT_OK",
                              256,
                              NULL,
                              pdMS_TO_TICKS( 12000 ),
                              pdMS_TO_TICKS( 12000 ),
                              pdMS_TO_TICKS( 400 ),
                              NULL );

    printf( "[SRP-DYN][orch] RT_OK -> %s  admitted=%lu rejected=%lu\r\n",
            ( xResult == pdPASS ) ? "ACCEPT" : "REJECT",
            ( unsigned long ) uxTaskGetEDFAdmittedCount(),
            ( unsigned long ) uxTaskGetEDFRejectedCount() );

    vTaskDelay( pdMS_TO_TICKS( 6000 ) );

    /* ---- Phase 3: add OVERLOAD (T6), expect REJECT ---- */
    printf( "[SRP-DYN][orch] adding OVERLOAD  T=1500 D=1500 C=1200\r\n" );

    vSRPResourceRegisterUser( xR2,
                              prvLevel( pdMS_TO_TICKS( 1500 ) ),
                              1U,
                              pdMS_TO_TICKS( 1000 ) );

    xResult = xTaskCreateEDF( vTaskOverload,     /* body drives PIN_T6 -- never invoked */
                              "OVERLOAD",
                              256,
                              NULL,
                              pdMS_TO_TICKS( 1500 ),
                              pdMS_TO_TICKS( 1500 ),
                              pdMS_TO_TICKS( 1200 ),
                              NULL );

    printf( "[SRP-DYN][orch] OVERLOAD -> %s  admitted=%lu rejected=%lu\r\n",
            ( xResult == pdPASS ) ? "ACCEPT" : "REJECT",
            ( unsigned long ) uxTaskGetEDFAdmittedCount(),
            ( unsigned long ) uxTaskGetEDFRejectedCount() );

    /* Park. */
    for( ;; )
    {
        vTaskDelay( pdMS_TO_TICKS( 10000 ) );
    }
}

/* ---- Entry point (called from main.c via main_edf_test) --------------- */

void main_edf_test( void )
{
    /* Initialize GPIO pins.  PIN_T6 is the OVERLOAD task's pin and will
     * stay LOW for the entire run, proving that admission rejected it. */
    const int piPins[] = { PIN_T1, PIN_T2, PIN_T3, PIN_T4, PIN_T5, PIN_T6 };

    for( int i = 0; i < 6; i++ )
    {
        gpio_init( piPins[ i ] );
        gpio_set_dir( piPins[ i ], GPIO_OUT );
        gpio_put( piPins[ i ], 0 );
    }

    /* ---- Create 3 SRP resources ---- */
    xR1 = xSRPResourceCreate( 4U );   /* 4 units */
    xR2 = xSRPResourceCreate( 3U );   /* 3 units */
    xR3 = xSRPResourceCreate( 2U );   /* 2 units */
    configASSERT( xR1 != NULL );
    configASSERT( xR2 != NULL );
    configASSERT( xR3 != NULL );

    /* ---- Register resource users BEFORE creating tasks. ----
     * This lets the admission controller compute accurate blocking
     * bounds at task-creation time. */

    /* T1 NESTED: R1(2 units, CS=700), R2(1 unit, CS=300) */
    vSRPResourceRegisterUser( xR1, prvLevel( pdMS_TO_TICKS( 3000 ) ),
                              2U, pdMS_TO_TICKS( 700 ) );
    vSRPResourceRegisterUser( xR2, prvLevel( pdMS_TO_TICKS( 3000 ) ),
                              1U, pdMS_TO_TICKS( 300 ) );

    /* T2 HEAVY: R1(2 units, CS=500) */
    vSRPResourceRegisterUser( xR1, prvLevel( pdMS_TO_TICKS( 6000 ) ),
                              2U, pdMS_TO_TICKS( 500 ) );

    /* T3 DUAL: R2(2 units, CS=300), R3(1 unit, CS=250) */
    vSRPResourceRegisterUser( xR2, prvLevel( pdMS_TO_TICKS( 5000 ) ),
                              2U, pdMS_TO_TICKS( 300 ) );
    vSRPResourceRegisterUser( xR3, prvLevel( pdMS_TO_TICKS( 5000 ) ),
                              1U, pdMS_TO_TICKS( 250 ) );

    /* T4 GUARD: R3(1 unit, CS=400) */
    vSRPResourceRegisterUser( xR3, prvLevel( pdMS_TO_TICKS( 10000 ) ),
                              1U, pdMS_TO_TICKS( 400 ) );

    printf( "[SRP-DYN] resources: R1(4u) R2(3u) R3(2u)\r\n" );
    printf( "[SRP-DYN] creating 4 startup tasks...\r\n" );

    /* ---- Create 4 startup EDF tasks ---- */
    configASSERT( xTaskCreateEDF( vTaskNested, "NESTED", 256, NULL,
                                  pdMS_TO_TICKS( 5000 ),
                                  pdMS_TO_TICKS( 3000 ),
                                  pdMS_TO_TICKS( 1000 ),
                                  NULL ) == pdPASS );

    configASSERT( xTaskCreateEDF( vTaskHeavy, "HEAVY", 256, NULL,
                                  pdMS_TO_TICKS( 6000 ),
                                  pdMS_TO_TICKS( 6000 ),
                                  pdMS_TO_TICKS( 800 ),
                                  NULL ) == pdPASS );

    configASSERT( xTaskCreateEDF( vTaskDual, "DUAL", 256, NULL,
                                  pdMS_TO_TICKS( 8000 ),
                                  pdMS_TO_TICKS( 5000 ),
                                  pdMS_TO_TICKS( 700 ),
                                  NULL ) == pdPASS );

    configASSERT( xTaskCreateEDF( vTaskGuard, "GUARD", 256, NULL,
                                  pdMS_TO_TICKS( 10000 ),
                                  pdMS_TO_TICKS( 10000 ),
                                  pdMS_TO_TICKS( 500 ),
                                  NULL ) == pdPASS );

    /* Non-EDF orchestrator for runtime phases. */
    configASSERT( xTaskCreate( vOrchestratorTask, "ORCH", 384, NULL,
                               tskIDLE_PRIORITY + 1U,
                               NULL ) == pdPASS );

    printf( "[SRP-DYN][startup] admitted=%lu  sysceil=%lu\r\n",
            ( unsigned long ) uxTaskGetEDFAdmittedCount(),
            ( unsigned long ) uxSRPGetSystemCeiling() );

    vTaskStartScheduler();

    for( ;; )
    {
    }
}
