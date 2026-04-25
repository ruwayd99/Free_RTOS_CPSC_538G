#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

#define PIN_TASK_LOW        10
#define PIN_TASK_HIGH       11
#define PIN_TASK_BG         12
#define PIN_TASK_RUNTIME    13

static SRPResourceHandle_t xSharedResource = NULL;

static UBaseType_t prvLevelFromDeadline( TickType_t xDeadline )
{
    return ( UBaseType_t ) ( portMAX_DELAY - xDeadline );
}

static void prvBusyWorkTicks( TickType_t xDurationTicks )
{
    TickType_t xStart = xTaskGetTickCount();

    while( ( xTaskGetTickCount() - xStart ) < xDurationTicks )
    {
        /* Busy work to generate deterministic runtime on analyzer traces. */
    }
}

static void vLowTask( void * pvParameters )
{
    ( void ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( PIN_TASK_LOW, 1 );

        while( xSRPResourceTake( xSharedResource, 1U ) != pdPASS )
        {
            taskYIELD();
        }

        prvBusyWorkTicks( pdMS_TO_TICKS( 1200 ) );
        vSRPResourceGive( xSharedResource, 1U );

        prvBusyWorkTicks( pdMS_TO_TICKS( 200 ) );

        gpio_put( PIN_TASK_LOW, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

static void vHighTask( void * pvParameters )
{
    ( void ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( PIN_TASK_HIGH, 1 );

        while( xSRPResourceTake( xSharedResource, 1U ) != pdPASS )
        {
            taskYIELD();
        }

        prvBusyWorkTicks( pdMS_TO_TICKS( 350 ) );
        vSRPResourceGive( xSharedResource, 1U );

        gpio_put( PIN_TASK_HIGH, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

static void vBackgroundTask( void * pvParameters )
{
    ( void ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( PIN_TASK_BG, 1 );
        prvBusyWorkTicks( pdMS_TO_TICKS( 250 ) );
        gpio_put( PIN_TASK_BG, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

static void vRuntimeAcceptedTask( void * pvParameters )
{
    ( void ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( PIN_TASK_RUNTIME, 1 );
        prvBusyWorkTicks( pdMS_TO_TICKS( 250 ) );
        gpio_put( PIN_TASK_RUNTIME, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

static void vRuntimeCreatorTask( void * pvParameters )
{
    BaseType_t xAccepted;
    BaseType_t xRejected;

    ( void ) pvParameters;

    vTaskDelay( pdMS_TO_TICKS( 10000 ) );

    xAccepted = xTaskCreateEDF( vRuntimeAcceptedTask,
                                "RT_OK",
                                256,
                                NULL,
                                pdMS_TO_TICKS( 4000 ),
                                pdMS_TO_TICKS( 3000 ),
                                pdMS_TO_TICKS( 350 ),
                                NULL );

    xRejected = xTaskCreateEDF( vRuntimeAcceptedTask,
                                "RT_REJECT",
                                256,
                                NULL,
                                pdMS_TO_TICKS( 2000 ),
                                pdMS_TO_TICKS( 1500 ),
                                pdMS_TO_TICKS( 1400 ),
                                NULL );

    printf( "[TEST][runtime] add_ok=%ld add_reject=%ld\r\n",
            ( long ) xAccepted,
            ( long ) xRejected );

    for( ;; )
    {
        vTaskDelay( pdMS_TO_TICKS( 1000 ) );
    }
}

void main_edf_test( void )
{
    const TickType_t xLowPeriod = pdMS_TO_TICKS( 7000 );
    const TickType_t xLowDeadline = pdMS_TO_TICKS( 5500 );
    const TickType_t xLowWCET = pdMS_TO_TICKS( 1600 );

    const TickType_t xHighPeriod = pdMS_TO_TICKS( 5000 );
    const TickType_t xHighDeadline = pdMS_TO_TICKS( 2500 );
    const TickType_t xHighWCET = pdMS_TO_TICKS( 700 );

    const TickType_t xBgPeriod = pdMS_TO_TICKS( 6000 );
    const TickType_t xBgDeadline = pdMS_TO_TICKS( 6000 );
    const TickType_t xBgWCET = pdMS_TO_TICKS( 500 );

    const uint auLogicAnalyzerPins[] = { 10, 11, 12, 13, 18, 19, 20, 21 };
    for( int i = 0; i < ( int ) ( sizeof( auLogicAnalyzerPins ) / sizeof( auLogicAnalyzerPins[ 0 ] ) ); i++ )
    {
        gpio_init( auLogicAnalyzerPins[ i ] );
        gpio_set_dir( auLogicAnalyzerPins[ i ], GPIO_OUT );
        gpio_put( auLogicAnalyzerPins[ i ], 0 );
    }

    xSharedResource = xSRPResourceCreate( 1U );
    configASSERT( xSharedResource != NULL );

    vSRPResourceRegisterUser( xSharedResource,
                              prvLevelFromDeadline( xLowDeadline ),
                              1U,
                              pdMS_TO_TICKS( 1200 ) );

    vSRPResourceRegisterUser( xSharedResource,
                              prvLevelFromDeadline( xHighDeadline ),
                              1U,
                              pdMS_TO_TICKS( 350 ) );

    printf( "[TEST][startup] low(T=%lu D=%lu C=%lu) high(T=%lu D=%lu C=%lu) bg(T=%lu D=%lu C=%lu)\r\n",
            ( unsigned long ) xLowPeriod,
            ( unsigned long ) xLowDeadline,
            ( unsigned long ) xLowWCET,
            ( unsigned long ) xHighPeriod,
            ( unsigned long ) xHighDeadline,
            ( unsigned long ) xHighWCET,
            ( unsigned long ) xBgPeriod,
            ( unsigned long ) xBgDeadline,
            ( unsigned long ) xBgWCET );

    configASSERT( xTaskCreateEDF( vLowTask,
                                  "SRP_LOW",
                                  256,
                                  NULL,
                                  xLowPeriod,
                                  xLowDeadline,
                                  xLowWCET,
                                  NULL ) == pdPASS );

    configASSERT( xTaskCreateEDF( vHighTask,
                                  "SRP_HIGH",
                                  256,
                                  NULL,
                                  xHighPeriod,
                                  xHighDeadline,
                                  xHighWCET,
                                  NULL ) == pdPASS );

    configASSERT( xTaskCreateEDF( vBackgroundTask,
                                  "SRP_BG",
                                  256,
                                  NULL,
                                  xBgPeriod,
                                  xBgDeadline,
                                  xBgWCET,
                                  NULL ) == pdPASS );

    configASSERT( xTaskCreate( vRuntimeCreatorTask,
                               "RUNTIME_CREATE",
                               256,
                               NULL,
                               tskIDLE_PRIORITY + 1U,
                               NULL ) == pdPASS );

    vTaskStartScheduler();

    for( ;; )
    {
    }
}
