
// Begin FreeRTOS CPSC_538G related - CBS - CBS test with configurable periodic tasks
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

#define PIN_PERIODIC_INI_BASE  10
#define PIN_PERIODIC_SUB_BASE  18
#define PIN_CBS_TASK       21

#define NUM_PERIODIC_TASKS_INITIAL 10
#define NUM_PERIODIC_TASKS_SUBSEQUENT 3
#define PERIODIC_SUBSEQUENT_DISPATCH_DELAY_MS  2000

#define PERIODIC_PERIOD_MS   3000
#define PERIODIC_WCET_MS     120

#define CBS_PERIOD_MS       1000
#define CBS_BUDGET_MS       200

#define APPROX_CBS_TRIGGER_PERIOD_MS    200
#define CBS_WCET_MS                     20

static TaskHandle_t xCBSTaskHandle = NULL;
static TaskHandle_t xCBSTriggerHandle = NULL;
static TaskHandle_t xPeriodicTaskDispatchHandle = NULL;

#define NUM_PERIODIC_TASKS_TOTAL ( NUM_PERIODIC_TASKS_INITIAL + NUM_PERIODIC_TASKS_SUBSEQUENT )
static TaskHandle_t xPeriodicTaskHandles[ NUM_PERIODIC_TASKS_TOTAL ] = { NULL };

static volatile uint32_t ulPeriodicRunCount[ NUM_PERIODIC_TASKS_TOTAL ] = { 0U };
static volatile uint32_t ulCBSRunCount = 0U;
static volatile TickType_t xLastCBSTriggerTick = 0U;
static volatile TickType_t xLastCBSStartTick = 0U;
static volatile TickType_t xLastCBSEndTick = 0U;

static BaseType_t prvGetPeriodicPinFromIndex( int idx, uint * pinOut )
{
    if( ( idx >= 0 ) && ( idx < 4 ) )
    {
        *pinOut = PIN_PERIODIC_INI_BASE + ( uint ) idx; /* 10..13 */
        return pdTRUE;
    }

    if( ( idx >= ( int ) NUM_PERIODIC_TASKS_INITIAL ) &&
        ( idx < ( int ) NUM_PERIODIC_TASKS_TOTAL ) )
    {
        *pinOut = PIN_PERIODIC_SUB_BASE + ( uint ) ( idx - ( int ) NUM_PERIODIC_TASKS_INITIAL ); /* 18..20 */
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
    (void)pvParameters;
    gpio_init(PIN_CBS_TASK);
    gpio_set_dir(PIN_CBS_TASK, GPIO_OUT);
    printf("[CBS TEST] [CBS_aperiodic] pin=%u started at tick %lu\n",
           PIN_CBS_TASK,
           (unsigned long)xTaskGetTickCount());

    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        xLastCBSStartTick = xTaskGetTickCount();

        gpio_put(PIN_CBS_TASK, 1);

        /* Keep CBS runtime < budget so periodic tasks should remain unaffected. */
        prvBusyWait(pdMS_TO_TICKS(CBS_WCET_MS));

        gpio_put(PIN_CBS_TASK, 0);
        xLastCBSEndTick = xTaskGetTickCount();
        ulCBSRunCount++;

        printf("[CBS TEST] [CBS_aperiodic] job=%lu START=%lu END=%lu trigger=%lu\n",
               (unsigned long)ulCBSRunCount,
               (unsigned long)xLastCBSStartTick,
               (unsigned long)xLastCBSEndTick,
               (unsigned long)xLastCBSTriggerTick);
    }
}

static void vCBSTriggerTask(void *pvParameters)
{
    (void)pvParameters;
    for (;;)
    {
        /* Trigger CBS roughly every 700ms. Actual execution can be delayed by EDF load. */
        vTaskDelay(pdMS_TO_TICKS(APPROX_CBS_TRIGGER_PERIOD_MS));
        if (xCBSTaskHandle)
        {
            xLastCBSTriggerTick = xTaskGetTickCount();
            // printf("[CBS TEST] [CBS_trigger] tick %lu notifying CBS\n",
            //        (unsigned long)xLastCBSTriggerTick);
            xTaskNotifyGive(xCBSTaskHandle);
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
    printf("[CBS TEST] Version 2.\n");

        printf("[CBS TEST] Pin map: initial PERIODIC_0..3 -> GPIO 10..13, subsequent PERIODIC_%d..%d -> GPIO 18..20, CBS -> GPIO %d\n",
            NUM_PERIODIC_TASKS_INITIAL,
            NUM_PERIODIC_TASKS_TOTAL - 1,
            PIN_CBS_TASK);

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

    // CBS aperiodic task
    configASSERT( xTaskCreateCBS(vCBSAperiodicTask,
                                 "CBS",
                                 1280,
                                 NULL,
                                 pdMS_TO_TICKS(CBS_BUDGET_MS),
                                 pdMS_TO_TICKS(CBS_PERIOD_MS),
                                 &xCBSTaskHandle) == pdPASS );

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
