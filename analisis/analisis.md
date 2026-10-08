# Análisis Técnico y Diseño de la Solución
**Asignatura:** Microcontroladores / Algoritmos en C  
**Estudiante:** Thalía Aminisha Pimentel  
**Matrícula:** 20250531  
**Placa:** NXP FRDM-MCXA156  
**Reto:** Controlador de iluminación con consola serial, PWM y máquina de estados  

---

## 1. Tabla de Recursos de Hardware

| Función | Señal / Puerto-Pin | Conector | Polaridad | Periférico / Canal | Frecuencia de Reloj | IRQ |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **LED de estado** | `P3_13` (Verde) | Onboard RGB | Activo en BAJO (cátodo común) | `GPIO3` Pin 13 | System Clock (96 MHz) | N/A |
| **Pulsador de usuario** | `P0_6` (`SW3`) | Onboard SW | Activo en BAJO (Pull-up interno) | `GPIO0` Pin 6 | System Clock (96 MHz) | `GPIO0_IRQn` |
| **Salida PWM LED** | `P3_12` | J1 pin 15 | Activo en ALTO (LED externo a GND con 1kΩ) | `CTIMER1`, Match 2 (`CT1_MAT2`, ALT4) | Functional Clock (48/96 MHz) | N/A |
| **Tick de 1 ms** | Interno | N/A | Periódico | `CTIMER0`, Match 0 | 48 MHz (Prescaler a 1 kHz) | `CTIMER0_IRQn` |
| **Consola Serial** | `P0_2` (RX) / `P0_3` (TX) | MCU-Link USB | Lógica estándar UART | `LPUART0` | 12 MHz (FRO12M) | `LPUART0_IRQn` |

*Nota:* No existe conflicto de Pin Mux entre la salida PWM (`P3_12` ALT4), el LED de estado (`P3_13` GPIO) y el pulsador (`P0_6` GPIO).

---

## 2. Diagrama de la Máquina de Estados Finito (FSM)

```
        +-------------------------------------------------------+
        |                                                       |
        v                                                       |
+-------------------+   Pulsación corta (30ms - 1500ms)    +-------------------+
|      MANUAL       | -----------------------------------> |       AUTO        |
| - PWM: 1 kHz      | <----------------------------------- | - PWM: Rampa      |
| - Duty: 25% (def) |    Pulsación corta / MODE MANUAL     |   10% a 90% (3.2s)|
| - LED: Fijo ON    |                                      | - LED: Parpadeo   |
+-------------------+                                      |   cada 500 ms     |
        |                                                  +-------------------+
        | Pulsación larga                                            | Pulsación larga
        | (>= 1500ms)                                                | (>= 1500ms)
        | o comando PAUSE                                            | o comando PAUSE
        v                                                            v
+------------------------------------------------------------------------------+
|                                    PAUSA                                     |
| - PWM: 0% fijo (salida inactiva sin pulsos residuales)                       |
| - LED: Parpadeo rápido cada 125 ms                                           |
| - Conserva: modo previo (MANUAL o AUTO), duty, dirección y acumuladores      |
| - Transición de salida: Pulsación larga o comando RESUME -> Estado Previo    |
+------------------------------------------------------------------------------+
```

---

## 3. Especificación de Temporizaciones y Antirrebote

1. **Base de tiempo periférica:**
   - Temporizador `CTIMER0` configurado a 1 ms por interrupción de hardware.
   - Variable `g_ms` incrementada en la ISR.
   - Las comparaciones temporales en `main` se realizan usando diferencias sin signo:
     `if ((uint32_t)(now - lastService) >= INTERVAL_MS)` para garantizar invarianza ante desbordamiento de 32 bits (`UINT32_MAX`).

2. **Antirrebote de Pulsador:**
   - La ISR de `GPIO0` detecta ambos flancos (presión y liberación).
   - En el bucle de 1 ms, el estado debe permanecer estable durante **30 ms continuos** para ser validado.
   - **Pulsación Corta:** Duración de nivel presionado estable entre `30 ms` y `< 1500 ms`. Se ejecuta al soltar el botón.
   - **Pulsación Larga:** Duración de nivel presionado continuo `>= 1500 ms`. Se activa de inmediato una sola vez al alcanzar 1500 ms; al soltar no genera pulsación corta.

3. **Rampa Triangular en Modo AUTO:**
   - Límites: 10% a 90% en pasos de 1% cada 20 ms.
   - Tramo de subida: `(90 - 10) = 80` pasos * 20 ms = **1600 ms (1.6 s)**.
   - Tramo de bajada: `80` pasos * 20 ms = **1600 ms (1.6 s)**.
   - Período completo del ciclo: **3200 ms (3.2 s)**.

---

## 4. Protocolo de Comunicación Serial (LPUART0)

- Formato de trama: 115200 baudios, 8 bits de datos, sin paridad, 1 bit de parada.
- Búfer circular de recepción: 128 bytes.
- Longitud máxima de línea: 63 caracteres + `\0`.
- Terminadores aceptados: `\r`, `\n`, `\r\n` (se procesa una sola orden).
- Respuestas normalizadas:
  - Éxito: `OK` (o datos solicitados en `STATUS`).
  - Errores: `ERR COMMAND`, `ERR ARG`, `ERR RANGE`, `ERR STATE`, `ERR OVERFLOW`.
