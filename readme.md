# Parcial 1 - Microcontroladores / Algoritmos en C

- **Estudiante:** Thalía Aminisha Pimentel
- **Matrícula:** 20250531
- **Plataforma / Tarjeta:** FRDM-MCXA156 (NXP MCX A156)
- **Repositorio:** [thaliaaminishapimentel/parcial1](https://github.com/thaliaaminishapimentel/parcial1)

---

## Descripción del Proyecto
Proyecto correspondiente al Primer Parcial. Implementación de control embebido en C sobre la tarjeta de desarrollo NXP FRDM-MCXA156, con configuración de pines, reloj, periféricos GPIO e interrupción por SysTick.

## Estructura del Repositorio
- `led_blinky.c`: Programa principal de control.
- `frdmmcxa156/`: Archivos de configuración de hardware, pines, reloj y tarjeta.
- `analisis/`: Análisis del problema, diseño y especificaciones.
- `pruebas/`: Casos de prueba de entrada/salida y validaciones.
- `evidencias/`: Pruebas de escritorio, capturas y evidencias de ejecución en hardware.

## Compilación y Ejecución
1. Conectar la tarjeta FRDM-MCXA156 mediante cable USB Tipo-C al puerto MCU-Link.
2. Abrir el proyecto en MCUXpresso for Visual Studio Code / IDE compatible con MCUXpresso SDK.
3. Compilar usando CMake / Ninja y cargar el binario a la tarjeta.
4. Ejecutar o depurar mediante el depurador integrado MCU-Link.

## Estado Actual
Configuración inicial y periféricos listos. Estructura de entrega preparada de acuerdo con la rúbrica del parcial.
