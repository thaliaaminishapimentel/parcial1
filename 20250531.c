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

/*******************************************************************************
 * Controlador de PWM por Hardware (CTIMER1 en P3_12 / ALT4 CT1_MAT2)
 * Soporta frecuencias: 500 Hz, 1000 Hz, 2000 Hz
 * Manejo estricto de 0% (nivel bajo constante) y 100% (nivel alto constante)
 * sin pulsos residuales ni alterar la base de tiempo de CTIMER0.
 ******************************************************************************/

static uint32_t s_currentPwmFreqHz   = PWM_DEFAULT_FREQ_HZ;
static uint32_t s_currentPwmDuty     = PWM_DEFAULT_DUTY_PERCENT;
static uint32_t s_pwmPeriodCounts    = 0U;

static void Pwm_ConfigurePinMux(bool enablePwmAlt)
{
    const port_pin_config_t pinConfig = {
        .pullSelect = kPORT_PullDisable,
        .slewRate = kPORT_FastSlewRate,
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
        .openDrainEnable = kPORT_OpenDrainDisable,
        .driveStrength = kPORT_LowDriveStrength,
        /* ALT4: CT1_MAT2 (salida PWM), ALT0: GPIO3_12 para forzar nivel constante en 0% o 100% */
        .mux = enablePwmAlt ? kPORT_MuxAlt4 : kPORT_MuxAlt0,
        .inputBuffer = kPORT_InputBufferEnable,
        .invertInput = kPORT_InputNormal,
        .lockRegister = kPORT_UnlockRegister
    };
    PORT_SetPinConfig(PORT3, 12U, &pinConfig);
}

status_t Pwm_Init(uint32_t freqHz, uint32_t dutyPercent)
{
    if ((freqHz != 500U && freqHz != 1000U && freqHz != 2000U) || dutyPercent > 100U)
    {
        return kStatus_InvalidArgument;
    }

    s_currentPwmFreqHz = freqHz;
    s_currentPwmDuty   = dutyPercent;

    /* 1. Habilitar reloj de CTIMER1 */
    CLOCK_SetClockDiv(kCLOCK_DivCTIMER1, 1U);
    CLOCK_AttachClk(kFRO_HF_to_CTIMER1);
    RESET_ReleasePeripheralReset(kCTIMER1_RST_SHIFT_RSTn);

    /* 2. Configurar timer en modo contador básico */
    CTIMER1->TCR = 0U;   /* Desactivar timer */
    CTIMER1->CTCR = 0U;  /* Modo timer con reloj de periférico */
    CTIMER1->PR = (APP_PWM_CLK_FREQ / 1000000U) - 1U; /* Prescaler a 1 MHz (1 us por tick) */

    /* 3. Calcular cuentas de período: 1,000,000 / freqHz */
    s_pwmPeriodCounts = 1000000U / freqHz;

    /* Match 3 define el período y reinicia el contador */
    CTIMER1->MR[3] = s_pwmPeriodCounts - 1U;
    CTIMER1->MCR = (1UL << 10U); /* MR3R: Reset en Match 3 */

    /* Habilitar PWM en canal Match 2 (PWMCON bit 2) */
    CTIMER1->PWMC |= (1UL << 2U);

    /* 4. Aplicar duty cycle inicial */
    Pwm_SetDuty(dutyPercent);

    /* 5. Iniciar CTIMER1 */
    CTIMER1->TCR = (1UL << 0U);

    return kStatus_Success;
}

status_t Pwm_SetFrequency(uint32_t freqHz)
{
    if (freqHz != 500U && freqHz != 1000U && freqHz != 2000U)
    {
        return kStatus_InvalidArgument;
    }

    s_currentPwmFreqHz = freqHz;
    s_pwmPeriodCounts = 1000000U / freqHz;

    /* Actualizar registro de período Match 3 */
    CTIMER1->MR[3] = s_pwmPeriodCounts - 1U;

    /* Recalcular Match 2 para el duty cycle actual */
    Pwm_SetDuty(s_currentPwmDuty);

    return kStatus_Success;
}

status_t Pwm_SetDuty(uint32_t dutyPercent)
{
    if (dutyPercent > 100U)
    {
        return kStatus_InvalidArgument;
    }

    s_currentPwmDuty = dutyPercent;

    /* Manejo especial de 0% y 100% para garantizar niveles DC perfectos sin pulsos residuales */
    if (dutyPercent == 0U)
    {
        /* Desconectar del periférico PWM y forzar nivel BAJO constante mediante GPIO */
        Pwm_ConfigurePinMux(false);
        GPIO_PinWrite(GPIO3, 12U, 0U);
        CTIMER1->MR[2] = s_pwmPeriodCounts; /* Sin coincidencia activa */
    }
    else if (dutyPercent == 100U)
    {
        /* Desconectar del periférico PWM y forzar nivel ALTO constante mediante GPIO */
        Pwm_ConfigurePinMux(false);
        GPIO_PinWrite(GPIO3, 12U, 1U);
        CTIMER1->MR[2] = 0U;
    }
    else
    {
        /* Configurar pin en ALT4 (CT1_MAT2) */
        Pwm_ConfigurePinMux(true);

        /* En CTIMER PWM con salida activa alta:
         * El pulso está en alto mientras TC < MR[periodo - matchValue].
         * matchValue = (periodCounts * (100 - duty)) / 100
         */
        uint32_t matchVal = (s_pwmPeriodCounts * (100U - dutyPercent)) / 100U;
        CTIMER1->MR[2] = matchVal;
    }

    return kStatus_Success;
}

uint32_t Pwm_GetFrequency(void)
{
    return s_currentPwmFreqHz;
}

uint32_t Pwm_GetDuty(void)
{
    return s_currentPwmDuty;
}

/*******************************************************************************
 * Driver LPUART0 con Búfer Circular (128 bytes) y Parser de Comandos
 * - Reconoce CR, LF y CRLF sin generar órdenes dobles.
 * - Validación numérica exhaustiva (rechaza "DUTY 20abc", "-1", "101").
 * - Respuestas normalizadas: OK, ERR COMMAND, ERR ARG, ERR RANGE, ERR STATE, ERR OVERFLOW.
 ******************************************************************************/

#include "fsl_lpuart.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* Búfer circular de recepción de al menos 128 bytes */
static volatile uint8_t  s_rxRingBuffer[UART_RING_BUFFER_SIZE];
static volatile uint16_t s_rxHead = 0U;
static volatile uint16_t s_rxTail = 0U;
static volatile uint32_t s_rxOverflowErrors = 0U;

/* Búfer de ensamblado de línea */
static char     s_lineBuffer[UART_MAX_LINE_LENGTH + 1U];
static uint16_t s_lineIndex = 0U;
static bool     s_lineOverflowDiscard = false;
static bool     s_lastCharWasCr = false;

/* Variables globales compartidas de estado (se sincronizarán con la FSM) */
extern app_state_t g_appCurrentState;
extern uint32_t    g_manualSavedDuty;
extern bool        g_streamEnabled;

void LPUART0_IRQHandler(void)
{
    uint32_t status = LPUART0->STAT;

    /* Verificar si hay dato en el registro receptor */
    if (status & LPUART_STAT_RDRF_MASK)
    {
        uint8_t byte = (uint8_t)(LPUART0->DATA);
        uint16_t nextHead = (s_rxHead + 1U) % UART_RING_BUFFER_SIZE;

        if (nextHead != s_rxTail)
        {
            s_rxRingBuffer[s_rxHead] = byte;
            s_rxHead = nextHead;
        }
        else
        {
            s_rxOverflowErrors++; /* Búfer lleno: registrar desbordamiento */
        }
    }

    /* Limpiar flags de error si ocurrieron */
    if (status & (LPUART_STAT_OR_MASK | LPUART_STAT_NF_MASK | LPUART_STAT_FE_MASK | LPUART_STAT_PF_MASK))
    {
        LPUART0->STAT |= (LPUART_STAT_OR_MASK | LPUART_STAT_NF_MASK | LPUART_STAT_FE_MASK | LPUART_STAT_PF_MASK);
    }

    SDK_ISR_EXIT_BARRIER;
}

void Uart_InitConsole(void)
{
    /* Habilitar reloj de LPUART0 usando FRO12M (12 MHz) */
    CLOCK_SetClockDiv(kCLOCK_DivLPUART0, 1U);
    CLOCK_AttachClk(kFRO12M_to_LPUART0);
    RESET_ReleasePeripheralReset(kLPUART0_RST_SHIFT_RSTn);

    /* Pines P0_2 (RX) y P0_3 (TX) configurados en ALT2 */
    CLOCK_EnableClock(kCLOCK_GatePORT0);
    const port_pin_config_t uartPinConfig = {
        .pullSelect = kPORT_PullDisable,
        .slewRate = kPORT_FastSlewRate,
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
        .openDrainEnable = kPORT_OpenDrainDisable,
        .driveStrength = kPORT_LowDriveStrength,
        .mux = kPORT_MuxAlt2,
        .inputBuffer = kPORT_InputBufferEnable,
        .invertInput = kPORT_InputNormal,
        .lockRegister = kPORT_UnlockRegister
    };
    PORT_SetPinConfig(PORT0, 2U, &uartPinConfig);
    PORT_SetPinConfig(PORT0, 3U, &uartPinConfig);

    lpuart_config_t config;
    LPUART_GetDefaultConfig(&config);
    config.baudRate_Bps = 115200U;
    config.enableTx     = true;
    config.enableRx     = true;

    LPUART_Init(LPUART0, &config, 12000000U);

    /* Habilitar interrupción de recepción */
    LPUART_EnableInterrupts(LPUART0, kLPUART_RxDataRegFullInterruptEnable);
    NVIC_SetPriority(LPUART0_IRQn, 3);
    EnableIRQ(LPUART0_IRQn);
}

void Uart_SendString(const char *str)
{
    if (str == NULL) return;
    while (*str != '\0')
    {
        while ((LPUART0->STAT & LPUART_STAT_TDRE_MASK) == 0U)
        {
            /* Esperar buffer TX listo */
        }
        LPUART0->DATA = (uint8_t)(*str++);
    }
}

static bool Uart_GetRxByte(uint8_t *byte)
{
    if (s_rxHead == s_rxTail)
    {
        return false;
    }
    *byte = s_rxRingBuffer[s_rxTail];
    s_rxTail = (s_rxTail + 1U) % UART_RING_BUFFER_SIZE;
    return true;
}

/* Función auxiliar para validar cadenas de enteros estrictamente */
static bool ParseStrictUnsignedInt(const char *str, uint32_t *outVal)
{
    if (str == NULL || *str == '\0') return false;

    /* Ignorar espacios iniciales */
    while (*str == ' ') str++;
    if (*str == '\0') return false;

    uint64_t val = 0;
    while (*str != '\0' && *str != ' ')
    {
        if (!isdigit((unsigned char)*str))
        {
            return false; /* Rechaza '20abc', caracteres negativos, etc. */
        }
        val = val * 10U + (uint32_t)(*str - '0');
        if (val > 1000000U) return false; /* Prevenir desbordamiento aritmético */
        str++;
    }

    /* Verificar si hay caracteres residuales tras espacios finales */
    while (*str == ' ') str++;
    if (*str != '\0') return false;

    *outVal = (uint32_t)val;
    return true;
}

/* Ejecutor y validador de comandos */
static void Uart_ExecuteCommand(char *cmdLine)
{
    /* 1. Recortar espacios al inicio y fin */
    char *p = cmdLine;
    while (*p == ' ') p++;
    if (*p == '\0') return; /* Línea vacía */

    int len = strlen(p);
    while (len > 0 && p[len - 1] == ' ')
    {
        p[--len] = '\0';
    }

    /* 2. Procesamiento de comandos */
    if (strcmp(p, "HELP") == 0)
    {
        Uart_SendString("Comandos disponibles:\r\n"
                        "  HELP\r\n"
                        "  STATUS\r\n"
                        "  MODE MANUAL | MODE AUTO\r\n"
                        "  DUTY <0-100>\r\n"
                        "  FREQ <500|1000|2000>\r\n"
                        "  PAUSE | RESUME\r\n"
                        "  STREAM ON | STREAM OFF\r\n");
    }
    else if (strcmp(p, "STATUS") == 0)
    {
        char resp[128];
        const char *stateStr = (g_appCurrentState == STATE_MANUAL) ? "MANUAL" :
                               (g_appCurrentState == STATE_AUTO)   ? "AUTO" : "PAUSA";
        snprintf(resp, sizeof(resp),
                 "STATUS: Modo=%s, Freq=%luHz, Duty=%lu%%, SavedDuty=%lu%%, Uptime=%lus, BtnShort=%lu, BtnLong=%lu, RxErr=%lu\r\n",
                 stateStr,
                 (unsigned long)Pwm_GetFrequency(),
                 (unsigned long)Pwm_GetDuty(),
                 (unsigned long)g_manualSavedDuty,
                 (unsigned long)(Timer_GetMs() / 1000U),
                 (unsigned long)Button_GetShortPressCount(),
                 (unsigned long)Button_GetLongPressCount(),
                 (unsigned long)s_rxOverflowErrors);
        Uart_SendString(resp);
    }
    else if (strncmp(p, "MODE ", 5) == 0)
    {
        if (g_appCurrentState == STATE_PAUSA)
        {
            Uart_SendString("ERR STATE\r\n");
            return;
        }
        char *arg = p + 5;
        while (*arg == ' ') arg++;

        if (strcmp(arg, "MANUAL") == 0)
        {
            g_appCurrentState = STATE_MANUAL;
            Pwm_SetDuty(g_manualSavedDuty);
            StatusLed_Set(true); /* Fijo encendido en MANUAL */
            Uart_SendString("OK\r\n");
        }
        else if (strcmp(arg, "AUTO") == 0)
        {
            g_appCurrentState = STATE_AUTO;
            Uart_SendString("OK\r\n");
        }
        else
        {
            Uart_SendString("ERR ARG\r\n");
        }
    }
    else if (strncmp(p, "DUTY", 4) == 0)
    {
        if (p[4] != ' ')
        {
            Uart_SendString("ERR COMMAND\r\n");
            return;
        }
        if (g_appCurrentState == STATE_PAUSA)
        {
            Uart_SendString("ERR STATE\r\n");
            return;
        }
        if (g_appCurrentState == STATE_AUTO)
        {
            Uart_SendString("ERR STATE\r\n"); /* En AUTO se rechaza */
            return;
        }

        uint32_t dutyVal = 0;
        if (!ParseStrictUnsignedInt(p + 5, &dutyVal))
        {
            Uart_SendString("ERR ARG\r\n");
            return;
        }
        if (dutyVal > 100U)
        {
            Uart_SendString("ERR RANGE\r\n");
            return;
        }

        g_manualSavedDuty = dutyVal;
        Pwm_SetDuty(dutyVal);
        Uart_SendString("OK\r\n");
    }
    else if (strncmp(p, "FREQ", 4) == 0)
    {
        if (p[4] != ' ')
        {
            Uart_SendString("ERR COMMAND\r\n");
            return;
        }
        if (g_appCurrentState == STATE_PAUSA)
        {
            Uart_SendString("ERR STATE\r\n");
            return;
        }

        uint32_t freqVal = 0;
        if (!ParseStrictUnsignedInt(p + 5, &freqVal))
        {
            Uart_SendString("ERR ARG\r\n");
            return;
        }
        if (freqVal != 500U && freqVal != 1000U && freqVal != 2000U)
        {
            Uart_SendString("ERR RANGE\r\n");
            return;
        }

        Pwm_SetFrequency(freqVal);
        Uart_SendString("OK\r\n");
    }
    else if (strcmp(p, "PAUSE") == 0)
    {
        if (g_appCurrentState == STATE_PAUSA)
        {
            Uart_SendString("OK\r\n"); /* Si ya está pausado responde OK sin alterar */
        }
        else
        {
            /* Entrar a pausa */
            g_appCurrentState = STATE_PAUSA;
            Pwm_SetDuty(0U);
            Uart_SendString("OK\r\n");
        }
    }
    else if (strcmp(p, "RESUME") == 0)
    {
        if (g_appCurrentState != STATE_PAUSA)
        {
            Uart_SendString("ERR STATE\r\n");
        }
        else
        {
            /* Reanudar al estado previo */
            g_appCurrentState = STATE_MANUAL;
            Pwm_SetDuty(g_manualSavedDuty);
            StatusLed_Set(true);
            Uart_SendString("OK\r\n");
        }
    }
    else if (strcmp(p, "STREAM ON") == 0)
    {
        g_streamEnabled = true;
        Uart_SendString("OK\r\n");
    }
    else if (strcmp(p, "STREAM OFF") == 0)
    {
        g_streamEnabled = false;
        Uart_SendString("OK\r\n");
    }
    else
    {
        Uart_SendString("ERR COMMAND\r\n");
    }
}

void Uart_ProcessConsole(void)
{
    uint8_t ch = 0;
    while (Uart_GetRxByte(&ch))
    {
        /* Tratamiento de terminadores: CR, LF, CRLF */
        if (ch == '\r')
        {
            s_lastCharWasCr = true;
            if (!s_lineOverflowDiscard)
            {
                s_lineBuffer[s_lineIndex] = '\0';
                Uart_ExecuteCommand(s_lineBuffer);
            }
            else
            {
                Uart_SendString("ERR OVERFLOW\r\n");
                s_lineOverflowDiscard = false;
            }
            s_lineIndex = 0U;
            continue;
        }

        if (ch == '\n')
        {
            if (s_lastCharWasCr)
            {
                /* Parte del par CRLF: ignorar para no duplicar orden */
                s_lastCharWasCr = false;
                continue;
            }

            /* LF solitario */
            s_lastCharWasCr = false;
            if (!s_lineOverflowDiscard)
            {
                s_lineBuffer[s_lineIndex] = '\0';
                Uart_ExecuteCommand(s_lineBuffer);
            }
            else
            {
                Uart_SendString("ERR OVERFLOW\r\n");
                s_lineOverflowDiscard = false;
            }
            s_lineIndex = 0U;
            continue;
        }

        s_lastCharWasCr = false;

        /* Ensamblar carácter en la línea */
        if (!s_lineOverflowDiscard)
        {
            if (s_lineIndex < UART_MAX_LINE_LENGTH)
            {
                s_lineBuffer[s_lineIndex++] = (char)ch;
            }
            else
            {
                /* Desbordamiento de línea (> 63 caracteres) */
                s_lineOverflowDiscard = true;
                s_rxOverflowErrors++;
            }
        }
    }
}

/*******************************************************************************
 * Máquina de Estados Finito (FSM) y Rampa Triangular
 * - MANUAL (inicial): PWM 1 kHz, duty 25%, LED encendido fijo. Pulsación corta -> AUTO.
 * - AUTO: Rampa triangular 10% a 90% (paso 1% cada 20 ms, ciclo total 3.2s). LED alterna 500 ms.
 *         Pulsación corta -> MANUAL (restaura último duty manual).
 * - PAUSA: PWM 0%, LED alterna 125 ms. Conserva modo, duty y dirección.
 *          Pulsación larga o RESUME -> reanuda estado previo.
 *          Pulsación corta en PAUSA se ignora.
 ******************************************************************************/

static app_state_t s_previousState = STATE_MANUAL;
static int32_t     s_autoDuty      = 10;
static bool        s_autoAscending = true;
static uint32_t    s_lastRampTimeMs = 0U;
static uint32_t    s_lastLedBlinkTimeMs = 0U;

void Fsm_Init(void)
{
    g_appCurrentState = STATE_MANUAL;
    g_manualSavedDuty = PWM_DEFAULT_DUTY_PERCENT;
    s_previousState   = STATE_MANUAL;

    s_autoDuty        = 10;
    s_autoAscending   = true;

    /* Estado inicial MANUAL: LED encendido fijo, duty 25% */
    StatusLed_Set(true);
    Pwm_SetFrequency(PWM_DEFAULT_FREQ_HZ);
    Pwm_SetDuty(g_manualSavedDuty);

    s_lastRampTimeMs = Timer_GetMs();
    s_lastLedBlinkTimeMs = Timer_GetMs();
}

app_state_t Fsm_GetState(void)
{
    return g_appCurrentState;
}

void Fsm_Process(button_event_t btnEvent)
{
    uint32_t now = Timer_GetMs();

    /* 1. Transiciones por pulsador */
    switch (g_appCurrentState)
    {
        case STATE_MANUAL:
            if (btnEvent == BTN_EVENT_SHORT)
            {
                /* Transición MANUAL -> AUTO: inicia en 10% ascendiendo */
                g_appCurrentState = STATE_AUTO;
                s_autoDuty = 10;
                s_autoAscending = true;
                Pwm_SetDuty((uint32_t)s_autoDuty);
                s_lastRampTimeMs = now;
                s_lastLedBlinkTimeMs = now;
            }
            else if (btnEvent == BTN_EVENT_LONG)
            {
                /* Transición MANUAL -> PAUSA */
                s_previousState = STATE_MANUAL;
                g_appCurrentState = STATE_PAUSA;
                Pwm_SetDuty(0U);
                s_lastLedBlinkTimeMs = now;
            }
            break;

        case STATE_AUTO:
            if (btnEvent == BTN_EVENT_SHORT)
            {
                /* Transición AUTO -> MANUAL: restaura último duty manual */
                g_appCurrentState = STATE_MANUAL;
                Pwm_SetDuty(g_manualSavedDuty);
                StatusLed_Set(true); /* Fijo encendido en MANUAL */
            }
            else if (btnEvent == BTN_EVENT_LONG)
            {
                /* Transición AUTO -> PAUSA: conserva duty, dirección y parámetros */
                s_previousState = STATE_AUTO;
                g_appCurrentState = STATE_PAUSA;
                Pwm_SetDuty(0U);
                s_lastLedBlinkTimeMs = now;
            }
            break;

        case STATE_PAUSA:
            /* Pulsación corta en PAUSA se ignora estrictamente */
            if (btnEvent == BTN_EVENT_LONG)
            {
                /* Pulsación larga en PAUSA: reanuda estado previo */
                g_appCurrentState = s_previousState;
                if (g_appCurrentState == STATE_MANUAL)
                {
                    Pwm_SetDuty(g_manualSavedDuty);
                    StatusLed_Set(true);
                }
                else if (g_appCurrentState == STATE_AUTO)
                {
                    /* Continúa rampa desde valores y dirección guardados */
                    Pwm_SetDuty((uint32_t)s_autoDuty);
                    s_lastRampTimeMs = now;
                    s_lastLedBlinkTimeMs = now;
                }
            }
            break;

        default:
            g_appCurrentState = STATE_MANUAL;
            break;
    }

    /* 2. Comportamiento continuo de cada estado */
    switch (g_appCurrentState)
    {
        case STATE_MANUAL:
            /* LED de estado encendido fijo */
            StatusLed_Set(true);
            break;

        case STATE_AUTO:
            /* A. Actualización de rampa triangular: cada 20 ms, paso de 1 punto porcentual */
            if ((uint32_t)(now - s_lastRampTimeMs) >= 20U)
            {
                s_lastRampTimeMs = now;

                if (s_autoAscending)
                {
                    s_autoDuty++;
                    if (s_autoDuty >= 90)
                    {
                        s_autoDuty = 90;
                        s_autoAscending = false; /* Alcanzó cresta: comienza descenso */
                    }
                }
                else
                {
                    s_autoDuty--;
                    if (s_autoDuty <= 10)
                    {
                        s_autoDuty = 10;
                        s_autoAscending = true; /* Alcanzó valle: comienza ascenso */
                    }
                }
                Pwm_SetDuty((uint32_t)s_autoDuty);
            }

            /* B. LED de estado en AUTO: alterna cada 500 ms */
            if ((uint32_t)(now - s_lastLedBlinkTimeMs) >= 500U)
            {
                s_lastLedBlinkTimeMs = now;
                StatusLed_Toggle();
            }
            break;

        case STATE_PAUSA:
            /* PWM en 0% asegurado */
            /* LED de estado en PAUSA: alterna rápidamente cada 125 ms */
            if ((uint32_t)(now - s_lastLedBlinkTimeMs) >= 125U)
            {
                s_lastLedBlinkTimeMs = now;
                StatusLed_Toggle();
            }
            break;
    }
}
