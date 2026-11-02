# TP #4 — Módulos de kernel

Hoja de ruta por partes. Sistemas de Computación (UNC) — Ing. Javier Jorge.

Este documento no es la consigna oficial: es la consigna traducida a pasos ejecutables
y ordenada de forma que cada parte dependa solo de la anterior.

**Integrantes:** Nicolás Borsotti Bosco · Santiago Valentín Ciacci · Ignacio Ariel Leguizamón

---

## La consigna

> Hacer un fork de <https://gitlab.com/sistemas-de-computacion-unc/kenel-modules>,
> compilar, cargar y descargar el módulo de ejemplo, y responder:
>
> 1. ¿Qué diferencias se pueden observar entre los dos `modinfo`?
> 2. ¿Qué drivers/módulos están cargados en sus propias PC? Comparar las salidas con las
>    computadoras de cada integrante. Cargar un `.txt` con la salida de cada integrante
>    en el repo y poner un `diff` en el informe.
> 3. ¿Cuáles no están cargados pero están disponibles? ¿Qué pasa cuando el driver de un
>    dispositivo no está disponible?
> 4. Correr `hwinfo` en una PC real con Linux, correr `hw-probe` y agregar la URL de la
>    información de hardware en el reporte.
> 5. ¿Qué diferencia existe entre un módulo y un programa?
> 6. ¿Cómo puede ver una lista de las llamadas al sistema que realiza un simple hello
>    world en C?
> 7. ¿Qué es un segmentation fault? ¿Cómo lo maneja el kernel y cómo lo hace un programa?
> 8. ¿Se animan a intentar firmar un módulo de kernel y documentar el proceso?
> 9. Agregar evidencia de la compilación, carga y descarga de su propio módulo
>    imprimiendo el nombre del equipo en los registros del kernel.
> 10. ¿Qué pasa si mi compañero con Secure Boot habilitado intenta cargar un módulo
>     firmado por mí?
> 11. Sobre la nota de Ars Technica del parche de Microsoft: consecuencia en sistemas con
>     arranque dual, implicancia de desactivar Secure Boot y propósito de Secure Boot.
>
> **Desafío #1:** ¿Qué es `checkinstall` y para qué sirve? Empaquetar un hello world y
> la aplicación del stack frame, e instalarla en dos máquinas distintas. Acciones para
> mejorar la seguridad del kernel: evitar cargar módulos sin firmar, evitar rootkits.
> ¿Qué herramientas conocen? ¿Qué técnicas y estrategias de seguridad incorpora el
> kernel?
>
> **Desafío #2:** ¿Qué funciones tiene disponible un programa y cuáles un módulo?
> Espacio de usuario, espacio del kernel y espacio de datos. Drivers: investigar el
> contenido de `/dev`.
>
> Los desafíos se evalúan en coloquio grupal con la entrega de la segunda parte del TP #4.

## Qué pide, en una frase

Escribir, compilar, **firmar**, cargar y descargar un módulo del kernel de Linux, y con
eso explicar **qué cambia cuando el código corre adentro del kernel** en vez de como un
programa: qué funciones tiene, dónde vive en memoria, qué pasa cuando falla y cómo hace
el sistema para no cargar código en el que no confía.

Igual que en los TP anteriores, no alcanza con que el módulo cargue: lo que se entrega es
el código **más la evidencia** (capturas y salidas) de cada paso, y las respuestas
justificadas con lo observado.

## Requisitos y dónde se cumplen

| # | Requisito | Dónde se cumple |
|---|---|---|
| R0 | Compilar, cargar y descargar el módulo de ejemplo | Parte 2 (`part1/module/`) |
| P1 | Diferencias entre los dos `modinfo` | Parte 2.5 |
| P2 | Módulos cargados en cada PC, `.txt` por integrante y `diff` | Parte 5.1 (`scripts/modulos.sh`, `salidas/`) |
| P3 | Disponibles no cargados; qué pasa sin driver | Partes 5.1 y 5.2 |
| P4 | `hwinfo` y `hw-probe` con la URL | Parte 5.3 |
| P5 | Diferencia entre módulo y programa | Partes 0 y 6.4 |
| P6 | Syscalls de un hello world | Parte 6.1 (`strace`) |
| P7 | Segmentation fault en kernel y en programa | Parte 7 (`part1/segfault/segv.c`) |
| P8 | Firmar un módulo y documentarlo | Parte 3 (`scripts/crear-clave.sh`, `scripts/firmar.sh`) |
| P9 | Módulo propio que imprime el nombre del equipo | Parte 4 (`part1/equipo/`) |
| P10 | Módulo firmado por mí en la PC de un compañero | Parte 3.5 |
| P11 | Nota de Ars Technica sobre Secure Boot | Parte 8 |
| D1 | checkinstall, paquete en dos máquinas, seguridad del kernel | Parte 10 (`desafio1/hola/`) |
| D2 | Funciones de programa y módulo, espacios de memoria, `/dev` | Parte 9 |
| RPi | Pedido en clase: compilar el módulo para la Raspberry Pi (ARM), en forma nativa y con un compilador cruzado x86 → ARM, buscando los headers de la versión exacta del kernel | Parte 11 |

## Plan por partes

| Parte | Qué se hace | Qué se demuestra |
|---|---|---|
| 0 | Teoría | Qué es un módulo, espacio de usuario vs. kernel, herramientas |
| 1 | Entorno: paquetes, headers, estado de Secure Boot | — |
| 2 | `mimodulo`: compilar, `insmod`, `dmesg`, `lsmod`, `/proc/modules`, `rmmod`, `modinfo` | El ciclo de vida de un módulo; taint `O` y `E`; `vermagic` |
| 3 | Firmar con una MOK propia | La cadena de confianza de Secure Boot llega hasta los módulos |
| 4 | Módulo propio `equipo` | `utsname()`, `static`/`__init`/`__exit`, parámetros |
| 5 | Módulos de cada PC, drivers, `hw-probe` | Por qué cambian entre máquinas; cómo udev carga drivers |
| 6 | `strace` del hello world, `copiar_archivo`, file descriptors | Un programa solo llega al kernel por syscalls |
| 7 | Segmentation fault | Page fault → `SIGSEGV` en usuario; oops en el kernel |
| 8 | Nota de Secure Boot / SBAT | Qué protege Secure Boot y qué se pierde al desactivarlo |
| 9 | Desafío #2: `/proc/kallsyms`, mapas de memoria, `/dev` | Símbolos exportados, mitades del espacio virtual, números mayor/menor |
| 10 | Desafío #1: checkinstall y seguridad | Paquete `.deb` instalable en otra máquina; defensas contra rootkits |
| 11 | Raspberry Pi: compilación nativa y cruzada | Un `.ko` se compila para una arquitectura y una versión exacta del kernel (`vermagic`, `Module.symvers`) |

## Decisiones tomadas

- **Repo propio en GitHub con copia de los ejemplos**, en lugar del fork de GitLab, para
  seguir la misma estructura que los TP anteriores. Los archivos de `part1/module/` y
  `part1/syscalls/` son los de la cátedra **sin modificar**.
- **El módulo propio va aparte** (`part1/equipo/`) para no tocar el ejemplo de la
  cátedra y poder comparar los dos.
- **Firmar en vez de desactivar Secure Boot.** Desactivarlo es más rápido, pero firmar
  responde la pregunta 8 y la 10, y es lo que se haría en una máquina real.
- **La clave privada vive fuera del repo** (`~/mok/`) y el `.gitignore` excluye
  cualquier `MOK.*`.
- **No hacemos un módulo que provoque un oops**: el comportamiento del kernel ante un
  fallo se explica con la teoría y el `segfault` se demuestra en espacio de usuario.
