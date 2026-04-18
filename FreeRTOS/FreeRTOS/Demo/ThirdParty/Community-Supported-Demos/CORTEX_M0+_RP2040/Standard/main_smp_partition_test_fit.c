/* Begin FreeRTOS CPSC_538G related - SMP - Task 4 partitioned EDF fit/reject test */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

typedef struct
{
    const char * pcName;
    int iPin;
    TickType_t xWorkTicks;
} SMPPartitionFitParams_t;

static SMPPartitionFitParams_t xTaskParams[] =
{
    { "P_U40", 10, pdMS_TO_TICKS( 60 ) },
    { "P_U30A", 11, pdMS_TO_TICKS( 60 ) },
    { "P_U30B", 12, pdMS_TO_TICKS( 60 ) },
    { "P_U25A", 13, pdMS_TO_TICKS( 60 ) },
    { "P_U25B", 18, pdMS_TO_TICKS( 60 ) },
    { "P_U80_REJECT", -1, pdMS_TO_TICKS( 60 ) }
};

static void prvBusyWorkTicks( TickType_t xDurationTicks )
{
    TickType_t xStart = xTaskGetTickCount();

    while( ( xTaskGetTickCount() - xStart ) < xDurationTicks )
    {
        /* Intentional spin to create deterministic compute demand. */
    }
}

static void vPartitionWorker( void * pvParameters )
{
    SMPPartitionFitParams_t * pxParams = ( SMPPartitionFitParams_t * ) pvParameters;
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
    BaseType_t xCreateResults[ 6 ];
    size_t xIndex;

    for( xIndex = 0; xIndex < ( sizeof( xTaskParams ) / sizeof( xTaskParams[ 0 ] ) ); xIndex++ )
    {
        if( xTaskParams[ xIndex ].iPin >= 0 )
        {
            gpio_init( ( uint ) xTaskParams[ xIndex ].iPin );
            gpio_set_dir( ( uint ) xTaskParams[ xIndex ].iPin, GPIO_OUT );
        }
    }

    printf( "[SMP][PARTITION][fit] creating U={0.40,0.30,0.30,0.25,0.25} then U=0.80 reject candidate\r\n" );

    xCreateResults[ 0 ] = xTaskCreateEDF( vPartitionWorker,
                                          xTaskParams[ 0 ].pcName,
                                          256,
                                          &xTaskParams[ 0 ],
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 400 ),
                                          NULL );

    xCreateResults[ 1 ] = xTaskCreateEDF( vPartitionWorker,
                                          xTaskParams[ 1 ].pcName,
                                          256,
                                          &xTaskParams[ 1 ],
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 300 ),
                                          NULL );

    xCreateResults[ 2 ] = xTaskCreateEDF( vPartitionWorker,
                                          xTaskParams[ 2 ].pcName,
                                          256,
                                          &xTaskParams[ 2 ],
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 300 ),
                                          NULL );

    xCreateResults[ 3 ] = xTaskCreateEDF( vPartitionWorker,
                                          xTaskParams[ 3 ].pcName,
                                          256,
                                          &xTaskParams[ 3 ],
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 250 ),
                                          NULL );

    xCreateResults[ 4 ] = xTaskCreateEDF( vPartitionWorker,
                                          xTaskParams[ 4 ].pcName,
                                          256,
                                          &xTaskParams[ 4 ],
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 250 ),
                                          NULL );

    xCreateResults[ 5 ] = xTaskCreateEDF( vPartitionWorker,
                                          xTaskParams[ 5 ].pcName,
                                          256,
                                          &xTaskParams[ 5 ],
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 1000 ),
                                          pdMS_TO_TICKS( 800 ),
                                          NULL );

    printf( "[SMP][PARTITION][fit] results={%ld,%ld,%ld,%ld,%ld} reject=%ld admitted=%lu rejected=%lu\r\n",
            ( long ) xCreateResults[ 0 ],
            ( long ) xCreateResults[ 1 ],
            ( long ) xCreateResults[ 2 ],
            ( long ) xCreateResults[ 3 ],
            ( long ) xCreateResults[ 4 ],
            ( long ) xCreateResults[ 5 ],
            ( unsigned long ) uxTaskGetEDFAdmittedCount(),
            ( unsigned long ) uxTaskGetEDFRejectedCount() );

    vTaskStartScheduler();

    for( ; ; )
    {
    }
}
/* End FreeRTOS CPSC_538G related - SMP - Task 4 partitioned EDF fit/reject test */
