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

int main(void)
{
    /* 1. Inicialización de la placa, pines y consola de depuración */
    BOARD_InitHardware();

    /* 2. Inicialización de periféricos propios del reto */
    GPIO_InitApp();
    Timer_TickInit();

    /* Variables de temporización no bloqueante basadas en diferencias de 32 bits */
    uint32_t lastTickService = Timer_GetMs();
    uint32_t lastLedBlinkMs  = Timer_GetMs();

    while (1)
    {
        uint32_t now = Timer_GetMs();

        /* Tarea de 1 ms: Procesar antirrebote y eventos del pulsador */
        if ((uint32_t)(now - lastTickService) >= 1U)
        {
            lastTickService = now;

            button_event_t btnEvent = Button_ProcessTick();
            if (btnEvent == BTN_EVENT_SHORT)
            {
                /* En MANUAL, una corta alternará estado básico por ahora */
                StatusLed_Toggle();
            }
            else if (btnEvent == BTN_EVENT_LONG)
            {
                /* Pulsación larga detectada */
                StatusLed_Set(false);
            }
        }

        /* Demostración de temporización concurrente sin delay (ej. cada 500 ms) */
        if ((uint32_t)(now - lastLedBlinkMs) >= 500U)
        {
            lastLedBlinkMs = now;
            /* Heartbeat concurrente sin bloquear la ejecución */
        }
    }
}
