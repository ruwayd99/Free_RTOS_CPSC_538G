/* Begin FreeRTOS CPSC_538G related - SMP - Task 4 global EDF admission test */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

typedef struct
{
    const char * pcName;
    int iPin;
    TickType_t xWorkTicks;
} SMPGlobalAdmissionParams_t;

static SMPGlobalAdmissionParams_t xTaskAParams = { "G_A", 10, pdMS_TO_TICKS( 80 ) };
static SMPGlobalAdmissionParams_t xTaskBParams = { "G_B", 11, pdMS_TO_TICKS( 100 ) };
static SMPGlobalAdmissionParams_t xTaskCParams = { "G_C", 12, pdMS_TO_TICKS( 120 ) };
static SMPGlobalAdmissionParams_t xTaskRejectParams = { "G_REJECT", 13, pdMS_TO_TICKS( 100 ) };

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
        if( pxParams->iPin >= 0 )
        {
            gpio_put( pxParams->iPin, 1 );
        }

        prvBusyWorkTicks( pxParams->xWorkTicks );

        if( pxParams->iPin >= 0 )
        {
            gpio_put( pxParams->iPin, 0 );
        }

        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

void main_edf_test( void )
{
    BaseType_t xCreateA;
    BaseType_t xCreateB;
    BaseType_t xCreateC;
    BaseType_t xCreateReject;
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
        if( pxTaskParams[ xIndex ]->iPin >= 0 )
        {
            gpio_init( ( uint ) pxTaskParams[ xIndex ]->iPin );
            gpio_set_dir( ( uint ) pxTaskParams[ xIndex ]->iPin, GPIO_OUT );
        }
    }

    printf( "[SMP][GLOBAL][admission] creating 3 admitted + 1 rejected implicit-deadline tasks\r\n" );

    xCreateA = xTaskCreateEDF( vPeriodicWorker,
                               xTaskAParams.pcName,
                               256,
                               &xTaskAParams,
                               pdMS_TO_TICKS( 1000 ),
                               pdMS_TO_TICKS( 1000 ),
                               pdMS_TO_TICKS( 200 ),
                               NULL );

    xCreateB = xTaskCreateEDF( vPeriodicWorker,
                               xTaskBParams.pcName,
                               256,
                               &xTaskBParams,
                               pdMS_TO_TICKS( 1000 ),
                               pdMS_TO_TICKS( 1000 ),
                               pdMS_TO_TICKS( 250 ),
                               NULL );

    xCreateC = xTaskCreateEDF( vPeriodicWorker,
                               xTaskCParams.pcName,
                               256,
                               &xTaskCParams,
                               pdMS_TO_TICKS( 1000 ),
                               pdMS_TO_TICKS( 1000 ),
                               pdMS_TO_TICKS( 300 ),
                               NULL );

    xCreateReject = xTaskCreateEDF( vPeriodicWorker,
                                    xTaskRejectParams.pcName,
                                    256,
                                    &xTaskRejectParams,
                                    pdMS_TO_TICKS( 1000 ),
                                    pdMS_TO_TICKS( 1000 ),
                                    pdMS_TO_TICKS( 900 ),
                                    NULL );

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
