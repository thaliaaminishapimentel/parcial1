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

/*******************************************************************************
 * Base de Tiempo Periférica de 1 ms (CTIMER0 Match 0)
 * Requisito: SysTick no sustituye este temporizador periférico.
 ******************************************************************************/

static volatile uint32_t g_systemMs = 0U;

void APP_TICK_IRQHandler(void)
{
    /* Verificar y limpiar bandera de Match 0 de CTIMER0 */
    uint32_t intFlags = CTIMER0->MSR;
    if (intFlags & (1UL << 0U))
    {
        CTIMER0->MSR = (1UL << 0U); /* Limpiar Match 0 flag */
        g_systemMs++;
    }
    SDK_ISR_EXIT_BARRIER;
}

void Timer_TickInit(void)
{
    /* Habilitar reloj periférico para CTIMER0 */
    CLOCK_SetClockDiv(kCLOCK_DivCTIMER0, 1U);
    CLOCK_AttachClk(kFRO_HF_to_CTIMER0);
    RESET_ReleasePeripheralReset(kCTIMER0_RST_SHIFT_RSTn);

    /* Desactivar timer durante configuración */
    CTIMER0->TCR = 0U;

    /* Modo timer estándar incrementando por reloj de bus/funcional */
    CTIMER0->CTCR = 0U;

    /* Prescaler: FRO_HF corre a 48 MHz.
     * Prescaler = (48000000 / 1000000) - 1 = 47 para contar a 1 MHz (1 us por conteo)
     */
    CTIMER0->PR = (48000000U / 1000000U) - 1U;

    /* Match 0 para generar interrupción cada 1000 microsegundos = 1 ms */
    CTIMER0->MR[0] = 1000U - 1U;

    /* MCR:
     * Bit 0 (MR0I): Interrupción al coincidir MR0
     * Bit 1 (MR0R): Reiniciar contador al coincidir MR0
     * Bit 2 (MR0S): No detener contador
     */
    CTIMER0->MCR = (1UL << 0U) | (1UL << 1U);

    /* Limpiar flags previos y configurar NVIC */
    CTIMER0->MSR = 0xFFFFFFFFU;
    NVIC_SetPriority(APP_TICK_IRQn, 2);
    EnableIRQ(APP_TICK_IRQn);

    /* Iniciar temporizador (TCR bit 0 = CEN) */
    CTIMER0->TCR = (1UL << 0U);
}

uint32_t Timer_GetMs(void)
{
    /* Lectura de 32 bits atómica en ARM Cortex-M33 */
    return g_systemMs;
}
