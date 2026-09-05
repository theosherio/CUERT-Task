#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include <queue.h>
#include <task.h>
#include <semphr.h>

#define LED_PIN 13
#define BAUD_RATE 115200
#define LINK_TIMEOUT_MS 500

typedef struct {
    char type;           
    int16_t value;       
    uint32_t timestamp_ms;
} Command_t;

QueueHandle_t commandQueue;
SemaphoreHandle_t dataMutex;

volatile uint32_t lastCommandTime = 0;
volatile bool isLinkLost = true; 
volatile int16_t currentThrottle = 0;
volatile int16_t currentSteer = 0;
volatile bool isBraking = false;

void Task_CommandRX(void *pvParameters);
void Task_Actuate(void *pvParameters);
void Task_Watchdog(void *pvParameters);
void Task_Status(void *pvParameters);

void setup() {
    Serial.begin(BAUD_RATE);
    while (!Serial) {} 
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Queue depth reduced to 2 items to prevent SRAM exhaustion
    commandQueue = xQueueCreate(2, sizeof(Command_t));
    dataMutex = xSemaphoreCreateMutex();

    if (commandQueue != NULL && dataMutex != NULL) {
        // Stack sizes strictly allocated in bytes for ATmega328P
        xTaskCreate(Task_CommandRX, "RX",  140, NULL, 4, NULL);
        xTaskCreate(Task_Actuate,   "ACT", 120, NULL, 3, NULL);
        xTaskCreate(Task_Watchdog,  "WDG",  85, NULL, 2, NULL);
        xTaskCreate(Task_Status,    "STAT", 120, NULL, 1, NULL);
        
        vTaskStartScheduler();
    }
}

void loop() {}

// Task 1: COMMAND_RX (Priority 4)
void Task_CommandRX(void *pvParameters) {
    (void) pvParameters;
    char buffer[16];
    uint8_t bufIndex = 0;

    for (;;) {
        while (Serial.available() > 0) {
            char c = Serial.read();
            
            if (c == '\n' || c == '\r') {
                if (bufIndex > 0) {
                    buffer[bufIndex] = '\0';
                    // Echo the typed command back to the terminal
                    Serial.print(F("> "));
                    Serial.println(buffer);
                    Command_t cmd;
                    cmd.timestamp_ms = millis();
                    bool valid = false;
                    bool isBrakeCmd = false;

                    if (strncmp(buffer, "THROTTLE ", 9) == 0) {
                        cmd.type = 'T'; cmd.value = atoi(&buffer[9]);
                        if (cmd.value >= 0 && cmd.value <= 100) valid = true;
                    } 
                    else if (strncmp(buffer, "STEER ", 6) == 0) {
                        cmd.type = 'S'; cmd.value = atoi(&buffer[6]);
                        if (cmd.value >= -100 && cmd.value <= 100) valid = true;
                    } 
                    else if (strncmp(buffer, "BRAKE ", 6) == 0) {
                        cmd.type = 'B'; cmd.value = atoi(&buffer[6]);
                        if (cmd.value >= 0 && cmd.value <= 100) {
                            valid = true; isBrakeCmd = true;
                        }
                    } 
                    else if (strcmp(buffer, "PING") == 0) {
                        cmd.type = 'P'; cmd.value = 0;
                        valid = true; 
                        Serial.println(F("PONG"));
                    }

                    if (valid) {
                        if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
                            lastCommandTime = cmd.timestamp_ms;
                            isLinkLost = false;
                            xSemaphoreGive(dataMutex);
                        }
                        if (isBrakeCmd) {
                            xQueueSendToFront(commandQueue, &cmd, 0);
                        } else {
                            xQueueSendToBack(commandQueue, &cmd, 0);
                        }
                    }
                    bufIndex = 0; 
                }
            } else if (bufIndex < sizeof(buffer) - 1) {
                buffer[bufIndex++] = c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20)); 
    }
}

// Task 2: ACTUATE (Priority 3)
void Task_Actuate(void *pvParameters) {
    (void) pvParameters;
    Command_t rxCmd;
    const TickType_t period_ticks = pdMS_TO_TICKS(50); 

    for (;;) {
        if (xQueueReceive(commandQueue, &rxCmd, 0) == pdPASS) {
            if (rxCmd.type == 'B') isBraking = (rxCmd.value > 0);
            else if (rxCmd.type == 'T') currentThrottle = rxCmd.value;
            else if (rxCmd.type == 'S') currentSteer = rxCmd.value;

            // Compact output logs
            Serial.print(F("ACT:")); Serial.print(rxCmd.type);
            Serial.print(F(" T:")); Serial.print(currentThrottle);
            Serial.print(F(" S:")); Serial.print(currentSteer);
            Serial.print(F(" B:")); Serial.println(isBraking);
        }

        if (isLinkLost) {
            digitalWrite(LED_PIN, (millis() / 100) % 2); 
            vTaskDelay(pdMS_TO_TICKS(50));
        } 
        else if (isBraking || currentThrottle == 0) {
            digitalWrite(LED_PIN, LOW);
            vTaskDelay(pdMS_TO_TICKS(50));
        } 
        else {
            TickType_t on_ticks = (currentThrottle * period_ticks) / 100;
            TickType_t off_ticks = period_ticks - on_ticks;

            if (on_ticks > 0) {
                digitalWrite(LED_PIN, HIGH);
                vTaskDelay(on_ticks);
            }
            if (off_ticks > 0) {
                digitalWrite(LED_PIN, LOW);
                vTaskDelay(off_ticks);
            }
        }
    }
}

// Task 3: WATCHDOG (Priority 2)
void Task_Watchdog(void *pvParameters) {
    (void) pvParameters;
    bool loggedLost = false;

    for (;;) {
        uint32_t currentTime = millis();
        uint32_t lastTime = 0;

        if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
            lastTime = lastCommandTime;
            xSemaphoreGive(dataMutex);
        }

        if ((currentTime - lastTime) > LINK_TIMEOUT_MS) {
            isLinkLost = true;
            if (!loggedLost) {
                Serial.println(F("LINK LOST, failing safe"));
                loggedLost = true;
            }
        } else {
            isLinkLost = false;
            loggedLost = false;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Task 4: STATUS (Priority 1)
void Task_Status(void *pvParameters) {
    (void) pvParameters;

    for (;;) {
        uint32_t uptime = millis() / 1000;
        
        Serial.print(F("[STAT] "));
        Serial.print(uptime);
        Serial.print(F("s L:"));
        Serial.print(isLinkLost ? F("LOST") : F("OK"));
        Serial.print(F(" T:"));
        Serial.print(currentThrottle);
        Serial.print(F(" S:"));
        Serial.print(currentSteer);
        Serial.print(F(" B:"));
        Serial.println(isBraking ? F("1") : F("0"));

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
