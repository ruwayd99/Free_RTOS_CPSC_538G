/*
 * CPSC 538G  SRP dynamic admission-control test.
 *
 * Demonstrates multi-unit resources, nested / re-entrant access, sequential
 * multi-resource access, runtime dynamic admission (accept + reject).
 *
 * Pin map (8 channels):
 *   10 T1-NESTED   11 T2-HEAVY   12 T3-DUAL   21 T4-GUARD
 *   20 T5-RT_OK    19 T6-OVERLOAD (rejected – stays LOW)
 *   13 IDLE         18 TIMER
 */

#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "pico/stdlib.h"

#define PIN_T1   10
#define PIN_T2   11
#define PIN_T3   12
#define PIN_T4   13
#define PIN_T5   18
#define PIN_T6   19   /* OVERLOAD – stays LOW (admission rejects it) */
#define PIN_IDLE 20
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

/* ---- Resources ------------------------------------------------------------ */
static SRPResourceHandle_t xR1 = NULL;   /* 4 units */
static SRPResourceHandle_t xR2 = NULL;   /* 3 units */
static SRPResourceHandle_t xR3 = NULL;   /* 2 units */

static UBaseType_t prvLevel( TickType_t xD )
{
    return ( UBaseType_t ) ( portMAX_DELAY - xD );
}

static void prvBusyWorkTicks( TickType_t xDuration )
{
    TickType_t xStart = xTaskGetTickCount();
    while( ( xTaskGetTickCount() - xStart ) < xDuration ) { }
}

/* ---- Task bodies ---------------------------------------------------------- */

static void vTaskNested( void * pv )
{
    ( void ) pv;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        while( xSRPResourceTake( xR1, 2U ) != pdPASS ) { taskYIELD(); }
        prvBusyWorkTicks( pdMS_TO_TICKS( 100 ) );
        while( xSRPResourceTake( xR2, 1U ) != pdPASS ) { taskYIELD(); }
        prvBusyWorkTicks( pdMS_TO_TICKS( 100 ) );
        while( xSRPResourceTake( xR1, 1U ) != pdPASS ) { taskYIELD(); }
        prvBusyWorkTicks( pdMS_TO_TICKS( 200 ) );
        vSRPResourceGive( xR1, 1U );
        vSRPResourceGive( xR2, 1U );
        vSRPResourceGive( xR1, 2U );
        prvBusyWorkTicks( pdMS_TO_TICKS( 100 ) );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

static void vTaskHeavy( void * pv )
{
    ( void ) pv;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        while( xSRPResourceTake( xR1, 2U ) != pdPASS ) { taskYIELD(); }
        prvBusyWorkTicks( pdMS_TO_TICKS( 500 ) );
        vSRPResourceGive( xR1, 2U );
        prvBusyWorkTicks( pdMS_TO_TICKS( 200 ) );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

static void vTaskDual( void * pv )
{
    ( void ) pv;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        while( xSRPResourceTake( xR2, 2U ) != pdPASS ) { taskYIELD(); }
        prvBusyWorkTicks( pdMS_TO_TICKS( 300 ) );
        vSRPResourceGive( xR2, 2U );
        while( xSRPResourceTake( xR3, 1U ) != pdPASS ) { taskYIELD(); }
        prvBusyWorkTicks( pdMS_TO_TICKS( 250 ) );
        vSRPResourceGive( xR3, 1U );
        prvBusyWorkTicks( pdMS_TO_TICKS( 100 ) );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

static void vTaskGuard( void * pv )
{
    ( void ) pv;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        while( xSRPResourceTake( xR3, 1U ) != pdPASS ) { taskYIELD(); }
        prvBusyWorkTicks( pdMS_TO_TICKS( 400 ) );
        vSRPResourceGive( xR3, 1U );
        prvBusyWorkTicks( pdMS_TO_TICKS( 50 ) );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

static void vTaskRuntimeOK( void * pv )
{
    ( void ) pv;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        while( xSRPResourceTake( xR3, 1U ) != pdPASS ) { taskYIELD(); }
        prvBusyWorkTicks( pdMS_TO_TICKS( 300 ) );
        vSRPResourceGive( xR3, 1U );
        prvBusyWorkTicks( pdMS_TO_TICKS( 50 ) );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* Body for the OVERLOAD task – never invoked because admission rejects it;
 * PIN_T6 stays LOW on the logic analyzer as proof. */
static void vTaskOverload( void * pv )
{
    ( void ) pv;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        while( xSRPResourceTake( xR2, 1U ) != pdPASS ) { taskYIELD(); }
        prvBusyWorkTicks( pdMS_TO_TICKS( 1000 ) );
        vSRPResourceGive( xR2, 1U );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* ---- Orchestrator -------------------------------------------------------- */
static void vOrchestratorTask( void * pv )
{
    TaskHandle_t xHandle;
    BaseType_t xResult;

    ( void ) pv;

    printf( "[SRP-DYN][orch] warmup  admitted=%lu  sysceil=%lu\r\n",
            ( unsigned long ) uxTaskGetEDFAdmittedCount(),
            ( unsigned long ) uxSRPGetSystemCeiling() );

    vTaskDelay( pdMS_TO_TICKS( 10000 ) );

    /* Phase 2: RT_OK (T5) – expected ACCEPT. */
    printf( "[SRP-DYN][orch] adding RT_OK  T=12000 D=12000 C=400\r\n" );

    vSRPResourceRegisterUser( xR3, prvLevel( pdMS_TO_TICKS( 12000 ) ),
                              1U, pdMS_TO_TICKS( 300 ) );

    xResult = xTaskCreateEDF( vTaskRuntimeOK, "RT_OK", 256, NULL,
                              pdMS_TO_TICKS( 12000 ), pdMS_TO_TICKS( 12000 ),
                              pdMS_TO_TICKS( 400 ), &xHandle );

    if( xResult == pdPASS )
    {
        taskENTER_CRITICAL();
        prvTraceRegister( xHandle, PIN_T5 );
        taskEXIT_CRITICAL();
    }

    printf( "[SRP-DYN][orch] RT_OK -> %s  admitted=%lu rejected=%lu\r\n",
            ( xResult == pdPASS ) ? "ACCEPT" : "REJECT",
            ( unsigned long ) uxTaskGetEDFAdmittedCount(),
            ( unsigned long ) uxTaskGetEDFRejectedCount() );

    vTaskDelay( pdMS_TO_TICKS( 6000 ) );

    /* Phase 3: OVERLOAD (T6) – expected REJECT; PIN_T6 stays LOW. */
    printf( "[SRP-DYN][orch] adding OVERLOAD  T=1500 D=1500 C=1200\r\n" );

    vSRPResourceRegisterUser( xR2, prvLevel( pdMS_TO_TICKS( 1500 ) ),
                              1U, pdMS_TO_TICKS( 1000 ) );

    xResult = xTaskCreateEDF( vTaskOverload, "OVERLOAD", 256, NULL,
                              pdMS_TO_TICKS( 1500 ), pdMS_TO_TICKS( 1500 ),
                              pdMS_TO_TICKS( 1200 ), &xHandle );
    if( xResult == pdPASS )
    {
        taskENTER_CRITICAL();
        prvTraceRegister( xHandle, PIN_T6 );
        taskEXIT_CRITICAL();
    }

    printf( "[SRP-DYN][orch] OVERLOAD -> %s  admitted=%lu rejected=%lu\r\n",
            ( xResult == pdPASS ) ? "ACCEPT" : "REJECT",
            ( unsigned long ) uxTaskGetEDFAdmittedCount(),
            ( unsigned long ) uxTaskGetEDFRejectedCount() );

    for( ;; )
    {
        vTaskDelay( pdMS_TO_TICKS( 10000 ) );
    }
}

/* ---- Entry point --------------------------------------------------------- */
void main_edf_test( void )
{
    const int piPins[] = { PIN_T1, PIN_T2, PIN_T3, PIN_T4, PIN_T5, PIN_T6,
                           PIN_IDLE, PIN_TIMER };

    for( int i = 0; i < ( int ) ( sizeof( piPins ) / sizeof( piPins[ 0 ] ) ); i++ )
    {
        gpio_init( ( uint ) piPins[ i ] );
        gpio_set_dir( ( uint ) piPins[ i ], GPIO_OUT );
        gpio_put( ( uint ) piPins[ i ], 0 );
    }

    xR1 = xSRPResourceCreate( 4U );
    xR2 = xSRPResourceCreate( 3U );
    xR3 = xSRPResourceCreate( 2U );
    configASSERT( xR1 != NULL );
    configASSERT( xR2 != NULL );
    configASSERT( xR3 != NULL );

    /* Register resource users before creating tasks. */
    vSRPResourceRegisterUser( xR1, prvLevel( pdMS_TO_TICKS( 3000 ) ), 2U, pdMS_TO_TICKS( 700 ) );
    vSRPResourceRegisterUser( xR2, prvLevel( pdMS_TO_TICKS( 3000 ) ), 1U, pdMS_TO_TICKS( 300 ) );
    vSRPResourceRegisterUser( xR1, prvLevel( pdMS_TO_TICKS( 6000 ) ), 2U, pdMS_TO_TICKS( 500 ) );
    vSRPResourceRegisterUser( xR2, prvLevel( pdMS_TO_TICKS( 5000 ) ), 2U, pdMS_TO_TICKS( 300 ) );
    vSRPResourceRegisterUser( xR3, prvLevel( pdMS_TO_TICKS( 5000 ) ), 1U, pdMS_TO_TICKS( 250 ) );
    vSRPResourceRegisterUser( xR3, prvLevel( pdMS_TO_TICKS( 10000 ) ), 1U, pdMS_TO_TICKS( 400 ) );

    printf( "[SRP-DYN] resources: R1(4u) R2(3u) R3(2u)\r\n" );
    printf( "[SRP-DYN] creating 4 startup tasks...\r\n" );
    printf( "[SRP-DYN] Pin map: T1=%d T2=%d T3=%d T4=%d T5=%d T6=%d(rejected) IDLE=%d TIMER=%d\r\n",
            PIN_T1, PIN_T2, PIN_T3, PIN_T4, PIN_T5, PIN_T6, PIN_IDLE, PIN_TIMER );

    {
        TaskHandle_t xHandle;

        configASSERT( xTaskCreateEDF( vTaskNested, "NESTED", 256, NULL,
                                      pdMS_TO_TICKS( 5000 ), pdMS_TO_TICKS( 3000 ),
                                      pdMS_TO_TICKS( 1000 ), &xHandle ) == pdPASS );
        prvTraceRegister( xHandle, PIN_T1 );

        configASSERT( xTaskCreateEDF( vTaskHeavy, "HEAVY", 256, NULL,
                                      pdMS_TO_TICKS( 6000 ), pdMS_TO_TICKS( 6000 ),
                                      pdMS_TO_TICKS( 800 ), &xHandle ) == pdPASS );
        prvTraceRegister( xHandle, PIN_T2 );

        configASSERT( xTaskCreateEDF( vTaskDual, "DUAL", 256, NULL,
                                      pdMS_TO_TICKS( 8000 ), pdMS_TO_TICKS( 5000 ),
                                      pdMS_TO_TICKS( 700 ), &xHandle ) == pdPASS );
        prvTraceRegister( xHandle, PIN_T3 );

        configASSERT( xTaskCreateEDF( vTaskGuard, "GUARD", 256, NULL,
                                      pdMS_TO_TICKS( 10000 ), pdMS_TO_TICKS( 10000 ),
                                      pdMS_TO_TICKS( 500 ), &xHandle ) == pdPASS );
        prvTraceRegister( xHandle, PIN_T4 );
    }

    configASSERT( xTaskCreate( vOrchestratorTask, "ORCH", 384, NULL,
                               tskIDLE_PRIORITY + 1U, NULL ) == pdPASS );

    printf( "[SRP-DYN][startup] admitted=%lu  sysceil=%lu\r\n",
            ( unsigned long ) uxTaskGetEDFAdmittedCount(),
            ( unsigned long ) uxSRPGetSystemCeiling() );

    vTaskStartScheduler();

    for( ;; ) { }
}
