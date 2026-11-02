# Trabajo Práctico N°4

## Módulos de kernel

**Código que se carga adentro del kernel de Linux, mientras está andando**

---

**Universidad Nacional de Córdoba**
Facultad de Ciencias Exactas, Físicas y Naturales
Cátedra de Sistemas de Computación

**Profesor:** Ing. Javier Jorge

**Integrantes:**

- Nicolás Borsotti Bosco
- Santiago Valentín Ciacci
- Ignacio Ariel Leguizamón

---

## De qué se trata

El trabajo consiste en **escribir, compilar, firmar, cargar y descargar módulos del
kernel de Linux**, y usarlos para entender qué cambia cuando el código corre **adentro
del kernel** en lugar de como un programa común.

En el TP #2 el código corría como un programa sobre Linux, y en el TP #3 corría antes del
sistema operativo, dentro del firmware. Acá corre **en el propio kernel**: no tiene libc,
imprime con `printk` en lugar de `printf`, comparte la memoria con todo el kernel y, si
falla, no muere solo él, sino que puede tirar abajo la máquina. Por eso, con Secure Boot,
el kernel solo acepta módulos **firmados** con una clave en la que confía, y una parte
del TP es justamente firmar el nuestro.

Seguimos los ejemplos de la cátedra
([kenel-modules](https://gitlab.com/sistemas-de-computacion-unc/kenel-modules)), la
presentación *Linux Kernel Module Programming I* y
[*The Linux Kernel Module Programming Guide*](https://sysprog21.github.io/lkmpg/).

## Organización del repositorio

```
practico-4/
├── README.md              este documento
├── CONSIGNA.md            qué pide el TP y en qué orden abordarlo
├── GUIA.md                la teoría y el paso a paso, con lo que hay que capturar
├── PROCEDIMIENTO.md       el registro de lo que fuimos haciendo, con capturas
├── capturas/              las imágenes referenciadas por PROCEDIMIENTO.md
├── salidas/               los .txt de módulos de cada integrante y sus diff
├── part1/
│   ├── module/            el módulo de ejemplo de la cátedra (mimodulo.c)
│   ├── equipo/            nuestro módulo: imprime el nombre del equipo (Parte 4)
│   ├── syscalls/          ejemplo_printf.c y copiar_archivo.c, para strace (Parte 6)
│   ├── testdir/           archivos de prueba para copiar_archivo
│   └── segfault/          segv.c: un segmentation fault con y sin manejador (Parte 7)
├── desafio1/
│   └── hola/              hello world con regla install, para empaquetar con checkinstall
└── scripts/
    ├── crear-clave.sh     crea la clave MOK y pide registrarla (Parte 3)
    ├── firmar.sh          firma un .ko con esa clave (Parte 3)
    └── modulos.sh         guarda los módulos cargados y disponibles en salidas/ (Parte 5)
```

| Documento | Para qué sirve |
|---|---|
| [`CONSIGNA.md`](CONSIGNA.md) | Los requisitos desglosados, el plan por partes y las decisiones tomadas |
| [`GUIA.md`](GUIA.md) | La teoría (Parte 0) y los pasos de cada parte, con los comandos y las capturas a sacar |
| [`PROCEDIMIENTO.md`](PROCEDIMIENTO.md) | El registro del trabajo realizado, parte por parte, con las capturas que verifican cada resultado |

| Parte | Tema | Capturas | Estado |
|---|---|---|---|
| Parte 1 | Entorno: paquetes, headers del kernel y Secure Boot | 1 | terminada |
| Parte 2 | Mi primer módulo: compilar, cargar, descargar y `modinfo` | 4 | terminada |
| Parte 3 | Firmar el módulo con una MOK propia | 2 | terminada |
| Parte 4 | Nuestro módulo, que imprime el nombre del equipo (en las 3 máquinas) | 4 | terminada |
| Parte 5 | Módulos de cada máquina, drivers y `hw-probe` | 6 | terminada |
| Parte 6 | Syscalls con `strace` y file descriptors | 4 | terminada |
| Parte 7 | Segmentation fault | 1 | terminada |
| Parte 8 | Secure Boot y el parche de Microsoft | — | terminada |
| Parte 9 | Desafío #2: módulos vs. programas, `/proc/kallsyms`, `/dev` | 3 | terminada |
| Parte 10 | Desafío #1: checkinstall y seguridad del kernel | 6 | terminada |
| Parte 11 | Extra: los módulos en una Raspberry Pi (aarch64) | 3 | terminada |

## Cómo compilar y usar

Requiere Linux en una PC real (no WSL) con los headers del kernel que está corriendo.
Los pasos para preparar todo están en la Parte 1 de [`GUIA.md`](GUIA.md).

```bash
sudo apt install build-essential linux-headers-$(uname -r)
cd part1/equipo
make
sudo insmod equipo.ko
sudo dmesg | tail -2
sudo rmmod equipo
```

```
equipo: modulo cargado en el equipo 'nico-HP-Laptop-15-ef2xxx' (kernel 7.0.0-29-generic)
equipo: grupo: Borsotti Bosco - Ciacci - Leguizamon
```

Con **Secure Boot activo**, `insmod` falla con `Key was rejected by service` hasta que el
módulo se firma (detalle en la Parte 3 de la guía):

```bash
./scripts/crear-clave.sh       # una sola vez; después reiniciar y registrar la MOK
cd part1/equipo
make firmar                    # compila y firma
sudo insmod equipo.ko
```
