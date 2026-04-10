
// Begin FreeRTOS CPSC_538G related - CBS - CBS test with configurable periodic tasks
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "TaskNotify.h"
#include "pico/stdlib.h"

#define PIN_PERIODIC_BASE  2
#define PIN_CBS_TASK       15

#define NUM_PERIODIC_TASKS 3

#define PERIODIC_PERIOD_MS   1000
#define PERIODIC_WCET_MS     200

#define CBS_BUDGET_MS       150
#define CBS_PERIOD_MS       1000

static TaskHandle_t xCBSTaskHandle = NULL;

static void prvBusyWait(TickType_t xTicks)
{
    TickType_t xStart = xTaskGetTickCount();
    while ((xTaskGetTickCount() - xStart) < xTicks) {}
}

static void vPeriodicTask(void *pvParameters)
{
    int idx = (int)(uintptr_t)pvParameters;
    char name[16];
    snprintf(name, sizeof(name), "PERIODIC_%d", idx);
    const uint pin = PIN_PERIODIC_BASE + idx;
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    TickType_t xLastWake = xTaskGetTickCount();
    for (;;)
    {
        gpio_put(pin, 1);
        printf("[%s] tick %lu running\n", name, (unsigned long)xTaskGetTickCount());
        prvBusyWait(pdMS_TO_TICKS(PERIODIC_WCET_MS));
        gpio_put(pin, 0);
        vTaskDelayUntilNextPeriod(&xLastWake);
    }
}

static void vCBSAperiodicTask(void *pvParameters)
{
    gpio_init(PIN_CBS_TASK);
    gpio_set_dir(PIN_CBS_TASK, GPIO_OUT);
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        gpio_put(PIN_CBS_TASK, 1);
        printf("[CBS] tick %lu running\n", (unsigned long)xTaskGetTickCount());
        prvBusyWait(pdMS_TO_TICKS(120)); // Simulate aperiodic work (less than CBS budget)
        gpio_put(PIN_CBS_TASK, 0);
    }
}

static void vCBSTriggerTask(void *pvParameters)
{
    (void)pvParameters;
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(700)); // Trigger CBS roughly every 700ms
        if (xCBSTaskHandle)
        {
            xTaskNotifyGive(xCBSTaskHandle);
        }
    }
}

void main_cbs_test(void)
{
    stdio_init_all();
    printf("[CBS TEST] Starting with %d periodic tasks.\n", NUM_PERIODIC_TASKS);

    // Spawn periodic tasks
    for (int i = 0; i < NUM_PERIODIC_TASKS; ++i)
    {
        char name[16];
        snprintf(name, sizeof(name), "PERIODIC_%d", i);
        xTaskCreateEDF(vPeriodicTask, name, 256, (void *)(uintptr_t)i,
                      pdMS_TO_TICKS(PERIODIC_PERIOD_MS),
                      pdMS_TO_TICKS(PERIODIC_PERIOD_MS),
                      pdMS_TO_TICKS(PERIODIC_WCET_MS),
                      NULL);
    }

    // CBS aperiodic task
    xTaskCreateCBS(vCBSAperiodicTask, "CBS", 256, NULL,
                  pdMS_TO_TICKS(CBS_BUDGET_MS),
                  pdMS_TO_TICKS(CBS_PERIOD_MS),
                  &xCBSTaskHandle);

    // CBS trigger task
    xTaskCreate(vCBSTriggerTask, "CBS_TRIGGER", 128, NULL, tskIDLE_PRIORITY + 1, NULL);

    vTaskStartScheduler();
    for (;;)
    {
    }
}
// End FreeRTOS CPSC_538G related - CBS - CBS test with configurable periodic tasks
