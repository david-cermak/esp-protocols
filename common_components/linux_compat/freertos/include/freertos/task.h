/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TaskHandle_t TaskHandle_t
#define vSemaphoreDelete( xSemaphore ) vQueueDelete( ( QueueHandle_t ) ( xSemaphore ) )

void vTaskDelay( const TickType_t xTicksToDelay );

void xTaskNotifyGive(TaskHandle_t task);

TaskHandle_t xTaskGetCurrentTaskHandle(void);

BaseType_t xTaskNotifyWait(uint32_t bits_entry_clear, uint32_t bits_exit_clear, uint32_t *value, TickType_t wait_time );
uint32_t ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait );

BaseType_t xTaskCreatePinnedToCore( TaskFunction_t pvTaskCode,
                                    const char *const pcName,
                                    const uint32_t usStackDepth,
                                    void *const pvParameters,
                                    UBaseType_t uxPriority,
                                    TaskHandle_t *const pvCreatedTask,
                                    const BaseType_t xCoreID);

BaseType_t xTaskCreate(TaskFunction_t pvTaskCode, const char *const pcName, const uint32_t usStackDepth, void *const pvParameters, UBaseType_t uxPriority, TaskHandle_t *const pvCreatedTask);

TickType_t xTaskGetTickCount( void );

void vQueueDelete( QueueHandle_t xQueue );

QueueHandle_t xSemaphoreCreateBinary(void);

QueueHandle_t xSemaphoreCreateMutex(void);
QueueHandle_t xSemaphoreCreateRecursiveMutex(void);

BaseType_t xSemaphoreGive( QueueHandle_t xQueue);

BaseType_t xSemaphoreTake( QueueHandle_t xQueue, TickType_t pvTask );

BaseType_t xSemaphoreGiveRecursive( QueueHandle_t xQueue);

BaseType_t xSemaphoreTakeRecursive( QueueHandle_t xQueue, TickType_t pvTask );

void vTaskDelete(TaskHandle_t *task);

QueueHandle_t xQueueCreate( uint32_t uxQueueLength,
                            uint32_t uxItemSize );

uint32_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait);

uint32_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait);

void vTaskSuspend(void *task);

EventGroupHandle_t xEventGroupCreate( void );
void vEventGroupDelete( EventGroupHandle_t xEventGroup );
EventBits_t xEventGroupClearBits( EventGroupHandle_t xEventGroup,
                                  const EventBits_t uxBitsToClear );

EventBits_t xEventGroupWaitBits( EventGroupHandle_t xEventGroup,
                                 const EventBits_t uxBitsToWaitFor,
                                 const BaseType_t xClearOnExit,
                                 const BaseType_t xWaitForAllBits,
                                 TickType_t xTicksToWait );

EventBits_t xEventGroupGetBits( EventGroupHandle_t xEventGroup);

EventBits_t xEventGroupSetBits( EventGroupHandle_t xEventGroup,
                                const EventBits_t uxBitsToSet );

uint32_t xQueueSendToBack(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait );

#ifdef __cplusplus
}
#endif //__cplusplus
