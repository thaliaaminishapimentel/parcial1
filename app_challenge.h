/*
 * Parcial 1: Controlador de Iluminación con Consola Serial
 * Estudiante: Thalía Aminisha Pimentel
 * Matrícula: 20250531
 * Placa: FRDM-MCXA156
 */

#ifndef _APP_CHALLENGE_H_
#define _APP_CHALLENGE_H_

#include <stdint.h>
#include <stdbool.h>
#include "fsl_common.h"
#include "fsl_gpio.h"
#include "fsl_port.h"

/*******************************************************************************
 * Definiciones de Hardware y Pines
 ******************************************************************************/

/* LED de Estado: P3_13 (LED Verde Onboard, activo en nivel bajo) */
#define APP_STATUS_LED_GPIO        GPIO3
#define APP_STATUS_LED_PIN         13U
#define APP_STATUS_LED_PORT        PORT3

/* Pulsador de Usuario: P0_6 (SW3 Onboard, activo en nivel bajo con pull-up) */
#define APP_BTN_GPIO               GPIO0
#define APP_BTN_PIN                6U
#define APP_BTN_PORT               PORT0
#define APP_BTN_IRQn               GPIO0_IRQn
#define APP_BTN_IRQHandler         GPIO0_IRQHandler

/* Temporizaciones de Botón (en milisegundos) */
#define BTN_DEBOUNCE_TIME_MS       30U
#define BTN_SHORT_PRESS_MIN_MS     30U
#define BTN_LONG_PRESS_MS          1500U

/*******************************************************************************
 * Enumeraciones y Tipos
 ******************************************************************************/

typedef enum {
    BTN_EVENT_NONE = 0,
    BTN_EVENT_SHORT,
    BTN_EVENT_LONG
} button_event_t;

typedef enum {
    STATE_MANUAL = 0,
    STATE_AUTO,
    STATE_PAUSA
} app_state_t;

/*******************************************************************************
 * Definiciones de Temporizador CTIMER0 (Tick de 1 ms)
 ******************************************************************************/
#define APP_TICK_TIMER             CTIMER0
#define APP_TICK_CLK_ATTACH        kFRO_HF_to_CTIMER0
#define APP_TICK_CLK_FREQ          48000000U
#define APP_TICK_IRQn              CTIMER0_IRQn
#define APP_TICK_IRQHandler        CTIMER0_IRQHandler

/*******************************************************************************
 * Prototipos de Funciones de GPIO, Botón y Temporizador
 ******************************************************************************/

void GPIO_InitApp(void);
void StatusLed_Set(bool on);
void StatusLed_Toggle(void);

/* Procesamiento periódico de botón (debe llamarse cada 1 ms en el tick) */
button_event_t Button_ProcessTick(void);

/* Contadores de pulsaciones */
uint32_t Button_GetShortPressCount(void);
uint32_t Button_GetLongPressCount(void);

/* Base de tiempo periférica de 1 ms */
void Timer_TickInit(void);
uint32_t Timer_GetMs(void);

#endif /* _APP_CHALLENGE_H_ */
