/*
 * Parcial 1: Módulo Principal y Control de Entradas/Salidas
 * Estudiante: Thalía Aminisha Pimentel
 * Matrícula: 20250531
 * Placa: FRDM-MCXA156
 */

#include "app_challenge.h"

/*******************************************************************************
 * Variables de Interrupción y Antirrebote
 ******************************************************************************/

/* Bandera volátil señalizada por la ISR en ambos flancos */
static volatile bool s_buttonEdgeDetected = false;

/* Variables internas del antirrebote y conteo */
static uint32_t s_debounceCounter = 0U;
static uint32_t s_pressDurationMs = 0U;
static bool     s_buttonStableState = false; /* true = presionado (nivel bajo) */
static bool     s_lastRawPinState   = false;
static bool     s_longPressReported = false;

/* Contadores exigidos por el mandato */
static uint32_t s_shortPressCount = 0U;
static uint32_t s_longPressCount  = 0U;

/*******************************************************************************
 * Rutina de Interrupción (ISR) - Corta y Atómica
 ******************************************************************************/

void APP_BTN_IRQHandler(void)
{
    uint32_t flags = GPIO_GpioGetInterruptFlags(APP_BTN_GPIO);
    uint32_t mask  = (1UL << APP_BTN_PIN);

    if (flags & mask)
    {
        /* Limpiar bandera de interrupción de hardware */
        GPIO_GpioClearInterruptFlags(APP_BTN_GPIO, mask);
        s_buttonEdgeDetected = true;
    }
    SDK_ISR_EXIT_BARRIER;
}

/*******************************************************************************
 * Inicialización de GPIO
 ******************************************************************************/

void GPIO_InitApp(void)
{
    /* Habilitar relojes de periféricos GPIO y PORT */
    CLOCK_EnableClock(kCLOCK_GateGPIO0);
    CLOCK_EnableClock(kCLOCK_GatePORT0);
    CLOCK_EnableClock(kCLOCK_GateGPIO3);
    CLOCK_EnableClock(kCLOCK_GatePORT3);

    RESET_ReleasePeripheralReset(kGPIO0_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kPORT0_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kGPIO3_RST_SHIFT_RSTn);
    RESET_ReleasePeripheralReset(kPORT3_RST_SHIFT_RSTn);

    /* 1. Configurar LED de Estado: P3_13 como salida digital, inicialmente APAGADO */
    const port_pin_config_t ledPortConfig = {
        .pullSelect = kPORT_PullDisable,
        .slewRate = kPORT_FastSlewRate,
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
        .openDrainEnable = kPORT_OpenDrainDisable,
        .driveStrength = kPORT_LowDriveStrength,
        .mux = kPORT_MuxAlt0, /* Alt0: GPIO */
        .inputBuffer = kPORT_InputBufferEnable,
        .invertInput = kPORT_InputNormal,
        .lockRegister = kPORT_UnlockRegister
    };
    PORT_SetPinConfig(APP_STATUS_LED_PORT, APP_STATUS_LED_PIN, &ledPortConfig);

    gpio_pin_config_t ledGpioConfig = {
        .pinDirection = kGPIO_DigitalOutput,
        .outputLogic = 1U /* Inicia apagado (nivel alto) */
    };
    GPIO_PinInit(APP_STATUS_LED_GPIO, APP_STATUS_LED_PIN, &ledGpioConfig);

    /* 2. Configurar Pulsador SW3: P0_6 como entrada con Pull-up interno e interrupción por ambos flancos */
    const port_pin_config_t btnPortConfig = {
        .pullSelect = kPORT_PullUp, /* Pull-up activo para SW3 */
        .slewRate = kPORT_FastSlewRate,
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
        .openDrainEnable = kPORT_OpenDrainDisable,
        .driveStrength = kPORT_LowDriveStrength,
        .mux = kPORT_MuxAlt0, /* Alt0: GPIO */
        .inputBuffer = kPORT_InputBufferEnable,
        .invertInput = kPORT_InputNormal,
        .lockRegister = kPORT_UnlockRegister
    };
    PORT_SetPinConfig(APP_BTN_PORT, APP_BTN_PIN, &btnPortConfig);

    gpio_pin_config_t btnGpioConfig = {
        .pinDirection = kGPIO_DigitalInput,
        .outputLogic = 0U
    };
    GPIO_PinInit(APP_BTN_GPIO, APP_BTN_PIN, &btnGpioConfig);

    /* Interrupción en ambos flancos (presión y liberación) sin bloqueos */
    GPIO_SetPinInterruptChannel(APP_BTN_GPIO, APP_BTN_PIN, kGPIO_InterruptOutput0);
    GPIO_GpioClearInterruptFlags(APP_BTN_GPIO, 1UL << APP_BTN_PIN);
    GPIO_SetPinInterruptConfig(APP_BTN_GPIO, APP_BTN_PIN, kGPIO_InterruptEitherEdge);

    NVIC_SetPriority(APP_BTN_IRQn, 3);
    EnableIRQ(APP_BTN_IRQn);
}

void StatusLed_Set(bool on)
{
    /* Cátodo a pin: nivel 0 enciende, 1 apaga */
    GPIO_PinWrite(APP_STATUS_LED_GPIO, APP_STATUS_LED_PIN, on ? 0U : 1U);
}

void StatusLed_Toggle(void)
{
    GPIO_PortToggle(APP_STATUS_LED_GPIO, 1UL << APP_STATUS_LED_PIN);
}

/*******************************************************************************
 * Procesamiento de Antirrebote y Clasificación de Pulsaciones (Tick de 1 ms)
 ******************************************************************************/

button_event_t Button_ProcessTick(void)
{
    button_event_t event = BTN_EVENT_NONE;

    /* Leer nivel físico: 0 = presionado (activo en bajo), 1 = liberado */
    bool currentRawPin = (GPIO_PinRead(APP_BTN_GPIO, APP_BTN_PIN) == 0U);

    /* Antirrebote: verificar si el estado cambia durante la ventana */
    if (currentRawPin != s_lastRawPinState)
    {
        s_lastRawPinState = currentRawPin;
        s_debounceCounter = 0U; /* Reiniciar ventana ante oscilación */
    }
    else
    {
        if (s_debounceCounter < BTN_DEBOUNCE_TIME_MS)
        {
            s_debounceCounter++;
            if (s_debounceCounter >= BTN_DEBOUNCE_TIME_MS)
            {
                /* Nivel estable confirmado tras 30 ms continuos */
                s_buttonStableState = currentRawPin;
            }
        }
    }

    /* Medición sobre el estado estable validado */
    if (s_buttonStableState)
    {
        s_pressDurationMs++;

        /* Pulsación Larga: se dispara una única vez al alcanzar exactamente 1500 ms */
        if ((s_pressDurationMs >= BTN_LONG_PRESS_MS) && !s_longPressReported)
        {
            s_longPressReported = true;
            s_longPressCount++;
            event = BTN_EVENT_LONG;
        }
    }
    else
    {
        /* El botón se ha soltado */
        if (s_pressDurationMs >= BTN_SHORT_PRESS_MIN_MS)
        {
            /* Si fue menor a 1500 ms y no se había reportado como larga */
            if (!s_longPressReported)
            {
                s_shortPressCount++;
                event = BTN_EVENT_SHORT;
            }
        }
        /* Reset de duración y bandera de larga */
        s_pressDurationMs = 0U;
        s_longPressReported = false;
    }

    return event;
}

uint32_t Button_GetShortPressCount(void)
{
    return s_shortPressCount;
}

uint32_t Button_GetLongPressCount(void)
{
    return s_longPressCount;
}
