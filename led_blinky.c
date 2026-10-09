/*
 * Copyright 2019 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "board.h"
#include "app.h"
#include "app_challenge.h"

/*******************************************************************************
 * Programa Principal - Planificación No Bloqueante
 ******************************************************************************/

/* Variables globales de estado compartidas */
app_state_t g_appCurrentState = STATE_MANUAL;
uint32_t    g_manualSavedDuty = PWM_DEFAULT_DUTY_PERCENT;
bool        g_streamEnabled   = false;

int main(void)
{
    /* 1. Inicialización de la placa, pines y consola de depuración */
    BOARD_InitHardware();

    /* 2. Inicialización de periféricos propios del reto */
    GPIO_InitApp();
    Timer_TickInit();
    Pwm_Init(PWM_DEFAULT_FREQ_HZ, PWM_DEFAULT_DUTY_PERCENT);
    Uart_InitConsole();

    Uart_SendString("\r\n========================================\r\n");
    Uart_SendString("FRDM-MCXA156: Controlador de Iluminacion\r\n");
    Uart_SendString("Estudiante: Thalia Pimentel (20250531)\r\n");
    Uart_SendString("Escriba HELP para ver la lista de comandos\r\n");
    Uart_SendString("========================================\r\n");

    /* Variables de temporización no bloqueante basadas en diferencias de 32 bits */
    uint32_t lastTickService = Timer_GetMs();
    uint32_t lastStreamMs    = Timer_GetMs();

    while (1)
    {
        uint32_t now = Timer_GetMs();

        /* Tarea 1: Procesar comandos UART de forma continua y no bloqueante */
        Uart_ProcessConsole();

        /* Tarea 2 (cada 1 ms): Procesar antirrebote y eventos del pulsador */
        if ((uint32_t)(now - lastTickService) >= 1U)
        {
            lastTickService = now;

            button_event_t btnEvent = Button_ProcessTick();
            if (btnEvent == BTN_EVENT_SHORT)
            {
                StatusLed_Toggle();
            }
            else if (btnEvent == BTN_EVENT_LONG)
            {
                StatusLed_Set(false);
            }
        }

        /* Tarea 3 (cada 1000 ms): Telemetría periódica si STREAM ON está activo */
        if (g_streamEnabled && ((uint32_t)(now - lastStreamMs) >= 1000U))
        {
            lastStreamMs = now;
            char streamBuf[96];
            snprintf(streamBuf, sizeof(streamBuf),
                     "STREAM: Modo=%d, Freq=%luHz, Duty=%lu%%, Uptime=%lus\r\n",
                     g_appCurrentState,
                     (unsigned long)Pwm_GetFrequency(),
                     (unsigned long)Pwm_GetDuty(),
                     (unsigned long)(now / 1000U));
            Uart_SendString(streamBuf);
        }
    }
}
