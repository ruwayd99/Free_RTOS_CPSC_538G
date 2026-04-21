
// Begin FreeRTOS CPSC_538G related - CBS - CBS test with configurable periodic tasks
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

#define PIN_PERIODIC_SUB_BASE  10
#define PIN_CBS_BASE           18

#define NUM_PERIODIC_TASKS_INITIAL 10
#define NUM_PERIODIC_TASKS_SUBSEQUENT 4
#define PERIODIC_SUBSEQUENT_DISPATCH_DELAY_MS  2000

#define NUM_CBS_TASKS_INITIAL 2
#define NUM_CBS_TASKS_SUBSEQUENT 2

#define PERIODIC_PERIOD_MS   3000
#define PERIODIC_WCET_MS     120

#define CBS_PERIOD_MS       1000
#define CBS_BUDGET_MS       200

#define APPROX_CBS_TRIGGER_PERIOD_MS    200
#define CBS_WCET_MS                     20

static TaskHandle_t xCBSTriggerHandle = NULL;
static TaskHandle_t xPeriodicTaskDispatchHandle = NULL;

#define NUM_CBS_TASKS_TOTAL ( NUM_CBS_TASKS_INITIAL + NUM_CBS_TASKS_SUBSEQUENT )
static TaskHandle_t xCBSTaskHandles[ NUM_CBS_TASKS_TOTAL ] = { NULL };

#define NUM_PERIODIC_TASKS_TOTAL ( NUM_PERIODIC_TASKS_INITIAL + NUM_PERIODIC_TASKS_SUBSEQUENT )
static TaskHandle_t xPeriodicTaskHandles[ NUM_PERIODIC_TASKS_TOTAL ] = { NULL };

static volatile uint32_t ulPeriodicRunCount[ NUM_PERIODIC_TASKS_TOTAL ] = { 0U };
static volatile uint32_t ulCBSRunCount[ NUM_CBS_TASKS_TOTAL ] = { 0U };
static volatile TickType_t xLastCBSTriggerTick[ NUM_CBS_TASKS_TOTAL ] = { 0U };
static volatile TickType_t xLastCBSStartTick[ NUM_CBS_TASKS_TOTAL ] = { 0U };
static volatile TickType_t xLastCBSEndTick[ NUM_CBS_TASKS_TOTAL ] = { 0U };
static volatile uint32_t ulNextCBSToTrigger = 0U;

static BaseType_t prvGetPeriodicPinFromIndex( int idx, uint * pinOut )
{
    if( ( idx >= ( int ) NUM_PERIODIC_TASKS_INITIAL ) &&
        ( idx < ( int ) NUM_PERIODIC_TASKS_TOTAL ) )
    {
        *pinOut = PIN_PERIODIC_SUB_BASE + ( uint ) ( idx - ( int ) NUM_PERIODIC_TASKS_INITIAL ); /* 10..13 */
        return pdTRUE;
    }

    return pdFALSE;
}

static BaseType_t prvGetCBSPinFromIndex( int idx, uint * pinOut )
{
    if( ( idx >= 0 ) && ( idx < ( int ) NUM_CBS_TASKS_TOTAL ) )
    {
        *pinOut = PIN_CBS_BASE + ( uint ) idx; /* 18..21 */
        return pdTRUE;
    }

    return pdFALSE;
}

static void prvBusyWait(TickType_t xTicks)
{
    TickType_t xStart = xTaskGetTickCount();
    while ((xTaskGetTickCount() - xStart) < xTicks) {}
}

static void vPeriodicTask(void *pvParameters)
{
    int idx = (int)(uintptr_t)pvParameters;
    uint pin = 0U;
    BaseType_t xHasPin = prvGetPeriodicPinFromIndex( idx, &pin );

    if( xHasPin != pdFALSE )
    {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
    }

    TickType_t xLastWake = xTaskGetTickCount();

    if( xHasPin != pdFALSE )
    {
        printf("[CBS TEST] [PERIODIC_%d] pin=%u started at tick %lu\n",
               idx,
               pin,
               (unsigned long)xTaskGetTickCount());
    }
    else
    {
        printf("[CBS TEST] [PERIODIC_%d] pin=none started at tick %lu\n",
               idx,
               (unsigned long)xTaskGetTickCount());
    }

    for (;;)
    {
        if( xHasPin != pdFALSE )
        {
            gpio_put(pin, 1);
        }

        ulPeriodicRunCount[ idx ]++;
        prvBusyWait(pdMS_TO_TICKS(PERIODIC_WCET_MS));

        if( xHasPin != pdFALSE )
        {
            gpio_put(pin, 0);
        }

        vTaskDelayUntilNextPeriod(&xLastWake);
    }
}

static void vCBSAperiodicTask(void *pvParameters)
{
    int idx = ( int ) ( uintptr_t ) pvParameters;
    uint pin = 0U;

    if( prvGetCBSPinFromIndex( idx, &pin ) != pdFALSE )
    {
        gpio_init( pin );
        gpio_set_dir( pin, GPIO_OUT );
    }

    printf("[CBS TEST] [CBS_%d] pin=%u started at tick %lu\n",
           idx,
           pin,
           (unsigned long)xTaskGetTickCount());

    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        xLastCBSStartTick[ idx ] = xTaskGetTickCount();

        gpio_put( pin, 1 );

        /* Keep CBS runtime < budget so periodic tasks should remain unaffected. */
        prvBusyWait(pdMS_TO_TICKS(CBS_WCET_MS));

        gpio_put( pin, 0 );
        xLastCBSEndTick[ idx ] = xTaskGetTickCount();
        ulCBSRunCount[ idx ]++;

        printf("[CBS TEST] [CBS_%d] job=%lu START=%lu END=%lu trigger=%lu\n",
               idx,
               (unsigned long)ulCBSRunCount[ idx ],
               (unsigned long)xLastCBSStartTick[ idx ],
               (unsigned long)xLastCBSEndTick[ idx ],
               (unsigned long)xLastCBSTriggerTick[ idx ]);
    }
}

static void vCBSTriggerTask(void *pvParameters)
{
    (void)pvParameters;
    for (;;)
    {
        /* Trigger one CBS task roughly every 200ms. Actual execution can be delayed by EDF load. */
        vTaskDelay(pdMS_TO_TICKS(APPROX_CBS_TRIGGER_PERIOD_MS));

        {
            TaskHandle_t xTargetHandle = NULL;
            uint32_t ulTargetIndex;

            taskENTER_CRITICAL();
            {
                ulTargetIndex = ulNextCBSToTrigger % NUM_CBS_TASKS_TOTAL;
                xTargetHandle = xCBSTaskHandles[ ulTargetIndex ];
                ulNextCBSToTrigger = ( ulNextCBSToTrigger + 1U ) % NUM_CBS_TASKS_TOTAL;
            }
            taskEXIT_CRITICAL();

            if( xTargetHandle != NULL )
            {
                xLastCBSTriggerTick[ ulTargetIndex ] = xTaskGetTickCount();
                xTaskNotifyGive( xTargetHandle );

                printf( "[CBS TEST] [CBS_trigger] tick %lu notifying CBS_%lu\n",
                        ( unsigned long ) xLastCBSTriggerTick[ ulTargetIndex ],
                        ( unsigned long ) ulTargetIndex );
            }
        }
    }
}

static void vPeriodicTriggerTask(void *pvParameters)
{
    (void)pvParameters;

    printf("[CBS TEST] [PERIODIC_DISPATCH] tick %lu creating %d subsequent tasks\n",
           (unsigned long)xTaskGetTickCount(),
           NUM_PERIODIC_TASKS_SUBSEQUENT);

    for( int i = 0; i < NUM_PERIODIC_TASKS_SUBSEQUENT; i++ )
    {
        vTaskDelay(pdMS_TO_TICKS(PERIODIC_SUBSEQUENT_DISPATCH_DELAY_MS));

        int idx = ( int ) NUM_PERIODIC_TASKS_INITIAL + i;
        char name[20];
        uint pin = 0U;
        ( void ) prvGetPeriodicPinFromIndex( idx, &pin );

        snprintf(name, sizeof(name), "PERIODIC_%d", idx);

        configASSERT( xTaskCreateEDF(vPeriodicTask,
                                     name,
                                     1024,
                                     (void *)(uintptr_t)idx,
                                     pdMS_TO_TICKS(PERIODIC_PERIOD_MS),
                                     pdMS_TO_TICKS(PERIODIC_PERIOD_MS),
                                     pdMS_TO_TICKS(PERIODIC_WCET_MS),
                                     &xPeriodicTaskHandles[idx]) == pdPASS );

        printf("[CBS TEST] [PERIODIC_DISPATCH] created %s on pin=%u at tick %lu\n",
               name,
               pin,
               (unsigned long)xTaskGetTickCount());

        if( i < NUM_CBS_TASKS_SUBSEQUENT )
        {
            int cbsIdx = NUM_CBS_TASKS_INITIAL + i;
            char cbsName[16];

            snprintf( cbsName, sizeof( cbsName ), "CBS_%d", cbsIdx );

            configASSERT( xTaskCreateCBS( vCBSAperiodicTask,
                          cbsName,
                          1280,
                          ( void * ) ( uintptr_t ) cbsIdx,
                          pdMS_TO_TICKS( CBS_BUDGET_MS ),
                          pdMS_TO_TICKS( CBS_PERIOD_MS ),
                          &xCBSTaskHandles[ cbsIdx ] ) == pdPASS );

            printf( "[CBS TEST] [CBS_DISPATCH] created %s on pin=%u at tick %lu\n",
                cbsName,
                PIN_CBS_BASE + ( uint ) cbsIdx,
                ( unsigned long ) xTaskGetTickCount() );
        }
    }

    printf("[CBS TEST] [PERIODIC_DISPATCH] completed, deleting self at tick %lu\n",
           (unsigned long)xTaskGetTickCount());
    vTaskDelete(NULL);
}


void main_cbs_test(void)
{
    stdio_init_all();
    printf("[CBS TEST] \n");
    printf("[CBS TEST] Starting with %d periodic tasks.\n", NUM_PERIODIC_TASKS_INITIAL);
    printf("[CBS TEST] Starting with %d CBS tasks.\n", NUM_CBS_TASKS_INITIAL);
    printf("[CBS TEST] Version 2.\n");

    printf("[CBS TEST] Pin map: initial periodic tasks use no GPIOs, subsequent PERIODIC_%d..%d -> GPIO 10..13, CBS_0..3 -> GPIO 18..21\n",
           NUM_PERIODIC_TASKS_INITIAL,
           NUM_PERIODIC_TASKS_TOTAL - 1 );

    // Spawn periodic tasks
    for (int i = 0; i < NUM_PERIODIC_TASKS_INITIAL; ++i)
    {
        char name[16];
        snprintf(name, sizeof(name), "PERIODIC_%d", i);
        configASSERT( xTaskCreateEDF(vPeriodicTask,
                                     name,
                                     1024,
                                     (void *)(uintptr_t)i,
                                     pdMS_TO_TICKS(PERIODIC_PERIOD_MS),
                                     pdMS_TO_TICKS(PERIODIC_PERIOD_MS),
                                     pdMS_TO_TICKS(PERIODIC_WCET_MS),
                                     &xPeriodicTaskHandles[i]) == pdPASS );
    }

    // Initial CBS aperiodic tasks
    for( int i = 0; i < NUM_CBS_TASKS_INITIAL; ++i )
    {
        char name[16];

        snprintf( name, sizeof( name ), "CBS_%d", i );

        configASSERT( xTaskCreateCBS( vCBSAperiodicTask,
                                      name,
                                      1280,
                                      ( void * ) ( uintptr_t ) i,
                                      pdMS_TO_TICKS( CBS_BUDGET_MS ),
                                      pdMS_TO_TICKS( CBS_PERIOD_MS ),
                                      &xCBSTaskHandles[ i ] ) == pdPASS );
    }

    // CBS trigger task
    configASSERT( xTaskCreate(vCBSTriggerTask,
                              "CBS_TRIGGER",
                              1024,
                              NULL,
                              tskIDLE_PRIORITY + 1,
                              &xCBSTriggerHandle) == pdPASS );

    /* One-shot dispatcher that adds the subsequent periodic task set after start. */
    configASSERT( xTaskCreate(vPeriodicTriggerTask,
                              "PERIODIC_DISPATCH",
                              1024,
                              NULL,
                              tskIDLE_PRIORITY + 1,
                              &xPeriodicTaskDispatchHandle) == pdPASS );

    vTaskStartScheduler();
    for (;;)
    {
    }
}
// End FreeRTOS CPSC_538G related - CBS - CBS test with configurable periodic tasks
