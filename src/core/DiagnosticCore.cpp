#include "DiagnosticCore.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace DiagnosticCore {

static unsigned long last_diag = 0;
static const unsigned long DIAG_INTERVAL_MS = 2000;  // Co 2 sekundy

void update() {
    if (millis() - last_diag < DIAG_INTERVAL_MS) {
        return;
    }
    last_diag = millis();
    
    // Pobierz info o taskach
    TaskStatus_t* pxTaskStatusArray = nullptr;
    UBaseType_t taskCount = 0;
    uint32_t ulTotalRunTime = 0;
    
    taskCount = uxTaskGetNumberOfTasks();
    pxTaskStatusArray = static_cast<TaskStatus_t*>(pvPortMalloc(taskCount * sizeof(TaskStatus_t)));
    
    if (pxTaskStatusArray == NULL) {
        Serial.println("[DIAG] Brak pamięci na task info");
        return;
    }
    
    taskCount = uxTaskGetSystemState(pxTaskStatusArray, taskCount, &ulTotalRunTime);
    
    Serial.println("\n=== DIAGNOSTIC TASK INFO (every 2s) ===");
    Serial.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
    Serial.println("Task Name\t\tState\tPrio\tRunTime%%");
    Serial.println("-----------------------------------------");
    
    for (UBaseType_t taskIndex = 0; taskIndex < taskCount; ++taskIndex) {
        if (pxTaskStatusArray[taskIndex].ulRunTimeCounter > 0) {
            const char* state_str = "?";
            switch (pxTaskStatusArray[taskIndex].eCurrentState) {
                case eRunning:   state_str = "RUN"; break;
                case eReady:     state_str = "RDY"; break;
                case eBlocked:   state_str = "BLK"; break;
                case eSuspended: state_str = "SUS"; break;
                case eDeleted:   state_str = "DEL"; break;
                default:         state_str = "???"; break;
            }
            
            uint32_t percent = (pxTaskStatusArray[taskIndex].ulRunTimeCounter * 100) / (ulTotalRunTime ? ulTotalRunTime : 1);
            
            Serial.printf("%-20s\t%s\t%u\t%u%%\n",
                pxTaskStatusArray[taskIndex].pcTaskName,
                state_str,
                pxTaskStatusArray[taskIndex].uxCurrentPriority,
                percent);
        }
    }
    
    Serial.println("-----------------------------------------\n");
    
    vPortFree(pxTaskStatusArray);
}

} // namespace DiagnosticCore

