
// Begin FreeRTOS CPSC_538G related - CBS - CBS test with configurable periodic tasks
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

#define PIN_PERIODIC_BASE  10
#define PIN_CBS_TASK       21

#define NUM_PERIODIC_TASKS 3

#define PERIODIC_PERIOD_MS   1000
#define PERIODIC_WCET_MS     250

#define CBS_PERIOD_MS       1000
#define CBS_BUDGET_MS       400

#define APPROX_CBS_TRIGGER_PERIOD_MS  500
#define CBS_WCET_MS                   300

static TaskHandle_t xCBSTaskHandle = NULL;
static TaskHandle_t xPeriodicTaskHandles[ NUM_PERIODIC_TASKS ] = { NULL };

static volatile uint32_t ulPeriodicRunCount[ NUM_PERIODIC_TASKS ] = { 0U };
static volatile uint32_t ulCBSRunCount = 0U;
static volatile TickType_t xLastCBSTriggerTick = 0U;
static volatile TickType_t xLastCBSStartTick = 0U;
static volatile TickType_t xLastCBSEndTick = 0U;

static volatile bool bIsCBSFinished = true;

static void prvBusyWait(TickType_t xTicks)
{
    TickType_t xStart = xTaskGetTickCount();
    while ((xTaskGetTickCount() - xStart) < xTicks) {}
}

static void vPeriodicTask(void *pvParameters)
{
    int idx = (int)(uintptr_t)pvParameters;
    const uint pin = PIN_PERIODIC_BASE + idx;
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    TickType_t xLastWake = xTaskGetTickCount();

    printf("[CBS TEST] [PERIODIC_%d] pin=%u started at tick %lu\n",
           idx,
           pin,
           (unsigned long)xTaskGetTickCount());

    for (;;)
    {
        BaseType_t xShouldNotifyCBS = pdFALSE;
        TickType_t xNow = xTaskGetTickCount();

        taskENTER_CRITICAL();
        {
            if( ( xCBSTaskHandle != NULL ) &&
                ( bIsCBSFinished != false ) &&
                ( ( xNow - xLastCBSTriggerTick ) >= pdMS_TO_TICKS( APPROX_CBS_TRIGGER_PERIOD_MS ) ) )
            {
                xLastCBSTriggerTick = xNow;
                xShouldNotifyCBS = pdTRUE;
            }
        }
        taskEXIT_CRITICAL();

        if( xShouldNotifyCBS != pdFALSE )
        {
            xTaskNotifyGive( xCBSTaskHandle );
        }

        gpio_put(pin, 1);
        ulPeriodicRunCount[ idx ]++;
        prvBusyWait(pdMS_TO_TICKS(PERIODIC_WCET_MS));
        gpio_put(pin, 0);
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
        bIsCBSFinished = false;

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
        
        bIsCBSFinished = true;
    }
}

void main_cbs_test(void)
{
    stdio_init_all();
    printf("[CBS TEST] \n");
    printf("[CBS TEST] Starting with %d periodic tasks.\n", NUM_PERIODIC_TASKS);
    printf("[CBS TEST] Version 2.\n");

    printf("[CBS TEST] Pin map: PERIODIC_%d..%d -> GPIO %d..%d, CBS -> GPIO %d\n",
           0,
           NUM_PERIODIC_TASKS - 1,
           PIN_PERIODIC_BASE,
           PIN_PERIODIC_BASE + NUM_PERIODIC_TASKS - 1,
           PIN_CBS_TASK);

    // Spawn periodic tasks
    for (int i = 0; i < NUM_PERIODIC_TASKS; ++i)
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

    vTaskStartScheduler();
    for (;;)
    {
    }
}
// End FreeRTOS CPSC_538G related - CBS - CBS test with configurable periodic tasks
