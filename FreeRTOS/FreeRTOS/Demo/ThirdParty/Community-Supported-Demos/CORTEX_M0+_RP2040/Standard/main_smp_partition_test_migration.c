/* Begin FreeRTOS CPSC_538G related - SMP - Task 4 partitioned EDF migration/remove API test */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"
#include "pico/platform.h"

typedef struct
{
    const char * pcName;
    int iPin;
    TickType_t xWorkTicks;
} SMPPartitionMigrationParams_t;

static TaskHandle_t xTaskAHandle = NULL;
static TaskHandle_t xTaskBHandle = NULL;

static SMPPartitionMigrationParams_t xTaskAParams = { "P_A_U70", 18, pdMS_TO_TICKS( 120 ) };
static SMPPartitionMigrationParams_t xTaskBParams = { "P_B_U40", 19, pdMS_TO_TICKS( 120 ) };

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
    SMPPartitionMigrationParams_t * pxParams = ( SMPPartitionMigrationParams_t * ) pvParameters;
    TickType_t xLastWake = xTaskGetTickCount();
    uint32_t ulLastCore = UINT32_MAX;

    for( ; ; )
    {
        uint32_t ulCore = get_core_num();

        if( ulCore != ulLastCore )
        {
            printf( "[SMP][PARTITION][worker] task=%s core=%lu tick=%lu\r\n",
                    pxParams->pcName,
                    ( unsigned long ) ulCore,
                    ( unsigned long ) xTaskGetTickCount() );
            ulLastCore = ulCore;
        }

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

static void vPartitionController( void * pvParameters )
{
    BaseType_t xMigrateFail;
    BaseType_t xMigratePass;

    ( void ) pvParameters;

    vTaskDelay( pdMS_TO_TICKS( 3000 ) );

    xMigrateFail = xTaskMigrateToCore( xTaskBHandle, 0 );
    printf( "[SMP][PARTITION][controller] migrate B->core0 expect fail result=%ld\r\n",
            ( long ) xMigrateFail );

    vTaskRemoveFromCore( xTaskAHandle );
    printf( "[SMP][PARTITION][controller] removed A from core assignment\r\n" );

    vTaskDelay( pdMS_TO_TICKS( 1000 ) );

    xMigratePass = xTaskMigrateToCore( xTaskBHandle, 0 );
    printf( "[SMP][PARTITION][controller] migrate B->core0 after remove expect pass result=%ld\r\n",
            ( long ) xMigratePass );

    for( ; ; )
    {
        vTaskDelay( pdMS_TO_TICKS( 10000 ) );
    }
}

void main_edf_test( void )
{
    BaseType_t xCreateA;
    BaseType_t xCreateB;
    SMPPartitionMigrationParams_t * pxTaskParams[] = { &xTaskAParams, &xTaskBParams };
    size_t xIndex;

    for( xIndex = 0; xIndex < ( sizeof( pxTaskParams ) / sizeof( pxTaskParams[ 0 ] ) ); xIndex++ )
    {
        if( pxTaskParams[ xIndex ]->iPin >= 0 )
        {
            gpio_init( ( uint ) pxTaskParams[ xIndex ]->iPin );
            gpio_set_dir( ( uint ) pxTaskParams[ xIndex ]->iPin, GPIO_OUT );
        }
    }

    xCreateA = xTaskCreateEDFOnCore( vPartitionWorker,
                                     xTaskAParams.pcName,
                                     256,
                                     &xTaskAParams,
                                     pdMS_TO_TICKS( 1000 ),
                                     pdMS_TO_TICKS( 1000 ),
                                     pdMS_TO_TICKS( 700 ),
                                     0,
                                     &xTaskAHandle );

    xCreateB = xTaskCreateEDFOnCore( vPartitionWorker,
                                     xTaskBParams.pcName,
                                     256,
                                     &xTaskBParams,
                                     pdMS_TO_TICKS( 1000 ),
                                     pdMS_TO_TICKS( 1000 ),
                                     pdMS_TO_TICKS( 400 ),
                                     1,
                                     &xTaskBHandle );

    ( void ) xTaskCreate( vPartitionController,
                          "P_CTRL",
                          256,
                          NULL,
                          tskIDLE_PRIORITY + 1U,
                          NULL );

    printf( "[SMP][PARTITION][startup] createA=%ld createB=%ld admitted=%lu rejected=%lu\r\n",
            ( long ) xCreateA,
            ( long ) xCreateB,
            ( unsigned long ) uxTaskGetEDFAdmittedCount(),
            ( unsigned long ) uxTaskGetEDFRejectedCount() );

    vTaskStartScheduler();

    for( ; ; )
    {
    }
}
/* End FreeRTOS CPSC_538G related - SMP - Task 4 partitioned EDF migration/remove API test */
