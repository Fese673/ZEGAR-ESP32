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
    TaskStatus_t* pxTaskStatusArray;
    volatile UBaseType_t uxArraySize, x;
    uint32_t ulTotalRunTime = 0;
    
    uxArraySize = uxTaskGetNumberOfTasks();
    pxTaskStatusArray = (TaskStatus_t*)pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));
    
    if (pxTaskStatusArray == NULL) {
        Serial.println("[DIAG] Brak pamięci na task info");
        return;
    }
    
    uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);
    
    Serial.println("\n=== DIAGNOSTIC TASK INFO (every 2s) ===");
    Serial.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
    Serial.println("Task Name\t\tState\tPrio\tRunTime%%");
    Serial.println("-----------------------------------------");
    
    for (x = 0; x < uxArraySize; x++) {
        if (pxTaskStatusArray[x].ulRunTimeCounter > 0) {
            const char* state_str = "?";
            switch (pxTaskStatusArray[x].eCurrentState) {
                case eRunning:   state_str = "RUN"; break;
                case eReady:     state_str = "RDY"; break;
                case eBlocked:   state_str = "BLK"; break;
                case eSuspended: state_str = "SUS"; break;
                case eDeleted:   state_str = "DEL"; break;
                default:         state_str = "???"; break;
            }
            
            uint32_t percent = (pxTaskStatusArray[x].ulRunTimeCounter * 100) / (ulTotalRunTime ? ulTotalRunTime : 1);
            
            Serial.printf("%-20s\t%s\t%u\t%u%%\n",
                pxTaskStatusArray[x].pcTaskName,
                state_str,
                pxTaskStatusArray[x].uxCurrentPriority,
                percent);
        }
    }
    
    Serial.println("-----------------------------------------\n");
    
    vPortFree(pxTaskStatusArray);
}

} // namespace DiagnosticCore

