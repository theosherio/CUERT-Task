# CUERT Embedded & Control: RTOS Sensor-to-CAN Bridge
An actuation node that sits on a CAN bus, receives commands (throttle/steer/brake), and must fail safely if commands stop arriving.

This repository contains the firmware submission for the Cairo University Eco-Racing Team (CUERT) Pre-Interview Task[cite: 1]. It implements a command-driven RTOS controller that simulates a safety-critical actuation node on a vehicle's CAN bus[cite: 1].

## Hardware & Software Requirements
*   **Microcontroller:** Arduino Uno (ATmega328P).
*   **External Components:** None. The system uses the onboard LED (Pin 13) and the USB-serial connection[cite: 1].
*   **Development Environment:** PlatformIO with the Arduino framework.
*   **RTOS:** AVR FreeRTOS (`feilipu/FreeRTOS@^11.1.0-3`).

## How It Works (RTOS Structure & Design Choices)
The system is built on four FreeRTOS tasks operating as a priority-based pipeline[cite: 1]. Because the ATmega328P has extremely limited SRAM (2KB), stack sizes and queue depths were strictly optimized, and string literals were moved to Flash memory using the `F()` macro to prevent stack overflow crashes. 

*   **Task 1: COMMAND_RX (Priority 4 - Highest)**
    Listens to the UART port for incoming plain-text commands[cite: 1]. It parses inputs into a `Command_t` structure, updates the safety watchdog timestamp, and pushes the command to the actuation queue[cite: 1]. `BRAKE` commands bypass the back of the queue and are sent to the front for immediate processing[cite: 1].
*   **Task 2: ACTUATE (Priority 3 - Medium)**
    Consumes commands from the queue and translates them into physical outputs[cite: 1]. Because the Arduino Uno's Pin 13 does not support hardware PWM, this task implements a non-blocking **Software PWM** using FreeRTOS tick delays to scale the LED brightness based on the throttle percentage[cite: 1]. If a `BRAKE` command is received, it instantly overrides the output to 0[cite: 1].
*   **Task 3: WATCHDOG (Priority 2 - Low)**
    Continuously monitors the timestamp of the last valid command received[cite: 1]. If 500ms elapse without a valid command, it assumes the sender crashed or the CAN link dropped, immediately enforcing a fail-safe blinking pattern[cite: 1].
*   **Task 4: STATUS (Priority 1 - Lowest)**
    Acts as debug telemetry, printing the system uptime, connection state, and current actuation targets to the Serial Monitor every 1 second[cite: 1]. 

## How to Run & Test It

1.  **Build and Upload** the code to the Arduino Uno using PlatformIO.
2.  **Configure the Terminal:** Ensure your `platformio.ini` includes `monitor_filters = send_on_enter` so that the terminal sends complete lines rather than individual keystrokes.
3.  **Open the Serial Monitor** at `115200` baud.
4.  **Reset the Board:** Press the physical RESET button on the Arduino to view the clean boot sequence. 

### Serial Monitor Test Sequence
Type the following commands exactly as shown and press Enter[cite: 1]:

*   `PING` -> The board acknowledges with `PONG`, proving the RX link is alive[cite: 1].
*   `THROTTLE 40` -> LED begins pulsing at ~40% brightness (software PWM)[cite: 1].
*   `STEER -60` -> Steer value updates in the status log without disrupting throttle[cite: 1].
*   `THROTTLE 90` then `BRAKE 100` -> LED immediately snaps to off, proving the brake overrides pending throttle commands[cite: 1].
*   *(Wait 600ms)* -> Terminal logs `LINK LOST, failing safe` and the LED switches to a fast fail-safe blink pattern[cite: 1].
*   `THROTTLE 20` -> Immediately recovers from fail-safe and resumes normal output at 20%[cite: 1].

## Required Answers

**1. Why did you assign the task priorities the way you did?**
`COMMAND_RX` is given the highest priority because dropping incoming bytes from the UART buffer could result in missing a critical safety command[cite: 1]. `ACTUATE` is second because once a command (especially a brake) is parsed, applying that physical logic is the system's primary directive[cite: 1]. `WATCHDOG` operates reliably at a lower priority because it enforces timing constraints rather than processing asynchronous data bursts, and `STATUS` is the lowest priority so that telemetry logging never preempts vehicle control or safety functions[cite: 1].

**2. Why does a stale or missing BRAKE matter more than a stale STEER? How does your watchdog design guarantee the system actually fails safe?**
A stale steering command may lock the vehicle in a turn—which is dangerous, but physically recoverable in many cases[cite: 1]. A missing brake command when approaching an obstacle is an immediate, catastrophic failure[cite: 1]. The watchdog guarantees a fail-safe state by measuring silence rather than waiting for a specific "stop" message that might never arrive[cite: 1]. If the link is silent for more than 500ms, the watchdog independently bypasses the queue and asserts a safe idle state, assuming system failure by default[cite: 1].

**3. What would you add or fix first if you had one more day?**
I would replace the plain-text UART parsing with a binary struct wrapped in a CRC check or integrate a real CAN transceiver (like the MCP2515) to emulate true automotive networking[cite: 1]. Additionally, I would implement a hardware Watchdog Timer (WDT) to ensure that if the FreeRTOS scheduler itself hangs due to memory corruption, the AVR chip physically resets. Finally, I would migrate to FreeRTOS static allocation functions (`xTaskCreateStatic`) to guarantee zero heap fragmentation during long-term operation.
