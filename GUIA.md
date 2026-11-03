# Guía paso a paso — TP #4: Módulos de kernel

Esta guía es para **hacer el práctico siguiéndola en orden**. Cada parte dice qué
comandos correr, qué se tiene que ver y **qué capturar** (📸). Las capturas van en
`capturas/` con el prefijo de su parte, y el resultado de cada parte se registra en
[`PROCEDIMIENTO.md`](PROCEDIMIENTO.md), igual que en los TP anteriores.

La consigna está desglosada en [`CONSIGNA.md`](CONSIGNA.md). La teoría sale de la
presentación de la cátedra *Linux Kernel Module Programming I*, de
[*The Linux Kernel Module Programming Guide*](https://sysprog21.github.io/lkmpg/) (LKMPG)
y de la documentación de Red Hat sobre
[firma de módulos para Secure Boot](https://docs.redhat.com/es/documentation/red_hat_enterprise_linux/8/html/managing_monitoring_and_updating_the_kernel/signing-kernel-modules-for-secure-boot_managing-kernel-modules).

> **Cómo leer los bloques de comandos:** los `#` son comentarios y no se escriben. Lo
> que empieza con `sudo` pide la contraseña de **tu usuario** (no la de root).

> **Antes de empezar:** todo lo que sigue se hace en una **PC real con Linux**, no en
> WSL (WSL no deja cargar módulos propios). Una máquina virtual sirve para todo menos la
> Parte 5 (`hw-probe` tiene que ver hardware real). Probado en Ubuntu 26.04 con kernel
> 7.0 y gcc 15.

---

## Parte 0 — Qué estamos haciendo, en criollo

### Qué es un módulo

El kernel de Linux es **monolítico pero modular**: todo corre en un único espacio de
memoria privilegiado, pero se le puede **agregar y quitar código mientras está andando**.
Ese código que se carga y descarga bajo demanda es un **módulo** (un archivo `.ko`,
*kernel object*).

Sin módulos, cada driver nuevo obligaría a recompilar el kernel entero y reiniciar.
Con módulos, el kernel arranca chico y carga lo que necesita: el driver de la placa de
Wi-Fi, el de la placa de video, el sistema de archivos de un pendrive, etc.

Un **driver** (controlador de dispositivo) es el tipo más común de módulo: es el que le
permite al kernel hablar con un hardware concreto.

### En qué se diferencia de un programa

En el TP #2 y el TP #3 el código corría como un **programa**. Un módulo es otra cosa:

| | Programa (espacio de usuario) | Módulo (espacio del kernel) |
|---|---|---|
| Empieza en | `main()`, y corre de arriba a abajo | la función registrada con `module_init()` |
| Termina | cuando `main` retorna o llama a `exit()` | **no termina**: `init` registra lo que ofrece y retorna; el código queda en memoria hasta que se descarga y corre `module_exit()` |
| Qué funciones puede usar | la libc (`printf`, `malloc`…) y, por debajo, las **syscalls** | **solo las que exporta el kernel** (`printk`, `kmalloc`…). No hay libc |
| Memoria | su propio espacio virtual, aislado | el espacio del kernel, **compartido** con todo el kernel |
| Si falla | el kernel lo mata (por ejemplo, `Segmentation fault`) y el resto sigue | falla **el kernel**: un *oops*, y a veces un *kernel panic* |
| Privilegio | anillo 3 (modo usuario) | anillo 0 (modo supervisor) |
| Se ejecuta con | `./programa` | `insmod modulo.ko` / `modprobe modulo` |
| Imprime con | `printf` → `write(1, …)` → la terminal | `printk` → el *ring buffer* del kernel → se lee con `dmesg` |

### Espacio de usuario y espacio del kernel

```
            ┌──────────────────────────────────────────────────────┐
  usuario   │  bash   gcc   firefox   ./ejemplo_printf   ...        │  anillo 3
            │  (cada proceso con su propio espacio de direcciones)  │
            └───────────────────────┬──────────────────────────────┘
                                    │  syscalls: write, read, openat, init_module...
            ┌───────────────────────▼──────────────────────────────┐
  kernel    │  procesos · memoria · VFS · red · drivers             │  anillo 0
            │  + mimodulo.ko (queda acá adentro al cargarlo)        │
            └───────────────────────┬──────────────────────────────┘
                                    │
            ┌───────────────────────▼──────────────────────────────┐
  hardware  │  CPU · RAM · disco · placa de red · USB ...           │
            └──────────────────────────────────────────────────────┘
```

La CPU tiene niveles de privilegio. El código de usuario no puede tocar el hardware ni
la memoria de otros procesos: para cualquier cosa de esas le **pide al kernel** con una
**llamada al sistema** (syscall). En la Parte 7 las vamos a ver con `strace`.

Un módulo, en cambio, **es parte del kernel** apenas se carga: no pide permiso, no tiene
protección de memoria y comparte todos los símbolos del kernel. Por eso un puntero mal
usado en un módulo no da un `Segmentation fault` prolijo: puede colgar la máquina.

### Las herramientas

| Comando | Qué hace |
|---|---|
| `make` (con el `Makefile` de kbuild) | Compila el módulo usando el sistema de compilación del kernel instalado |
| `insmod archivo.ko` | Carga un módulo desde un archivo (no resuelve dependencias) |
| `rmmod nombre` | Descarga un módulo |
| `modprobe nombre` | Carga un módulo del sistema por nombre, con sus dependencias |
| `lsmod` | Lista los módulos cargados (lee `/proc/modules`) |
| `modinfo` | Muestra los metadatos de un `.ko`: licencia, autor, firma, `vermagic`... |
| `dmesg` | Muestra el *ring buffer* del kernel, donde escribe `printk` |
| `strace` | Muestra las syscalls que hace un programa |
| `sign-file` | Firma un `.ko` con una clave (viene con los headers del kernel) |
| `mokutil` | Administra las claves propias (MOK) que acepta Secure Boot |

---

## Parte 1 — Preparar el entorno

### 1.1 Instalar los paquetes

```bash
sudo apt update
sudo apt-get install build-essential checkinstall bison flex libncurses-dev \
     libssl-dev libelf-dev bc linux-source linux-headers-$(uname -r) strace git
```

- `build-essential`: gcc, make y la libc de desarrollo.
- `linux-headers-$(uname -r)`: los headers y el `Makefile` del **kernel que está
  corriendo**. Es lo único estrictamente necesario para compilar un módulo.
- `linux-source`: el código fuente completo del kernel (lo pide la cátedra; queda como
  un `.tar.xz` en `/usr/src/`). Sirve para leer el código o compilar el kernel entero.
- `bison`, `flex`, `libncurses-dev`, `libssl-dev`, `libelf-dev`, `bc`: hacen falta para
  compilar el kernel completo y para firmar módulos.
- `checkinstall`: lo usamos en el Desafío #1.

> **Errores que ya nos aparecieron:**
> - `No se ha podido localizar el paquete build-essentials` → es `build-essential`,
>   **sin** la "s" final.
> - `kernel-package no tiene un candidato` → ese paquete **ya no existe** en
>   Debian/Ubuntu. Hoy se reemplaza con `make bindeb-pkg` dentro del fuente del kernel.
>   No hace falta para este TP.
> - `404 Not Found` al descargar → la lista de paquetes estaba vieja. Correr
>   `sudo apt update` y repetir.

`linux-source` puede arrastrar un kernel nuevo (por ejemplo, el 7.0.0-31 cuando estás
usando el 7.0.0-29). No rompe nada; al reiniciar arrancás con el nuevo y sus headers se
instalan solos. Lo importante es que **los headers coincidan con `uname -r`**.

### 1.2 Clonar el repositorio

Este repo ya trae los ejemplos de la cátedra (`part1/`). Los originales están en
<https://gitlab.com/sistemas-de-computacion-unc/kenel-modules>.

```bash
git clone https://github.com/nicoborsottib/practico-4 ~/practico-4
cd ~/practico-4
```

### 1.3 Ver si Secure Boot está activo

```bash
mokutil --sb-state
cat /sys/kernel/security/lockdown
```

- `SecureBoot enabled` + `[integrity]` en lockdown → el kernel **solo carga módulos
  firmados** con una clave en la que confía. Nuestro módulo va a fallar con
  `Key was rejected by service` hasta que lo firmemos (Parte 3).
- `SecureBoot disabled` → carga cualquier módulo, pero deja una advertencia en `dmesg`.

### 1.4 Verificación

```bash
uname -r
gcc --version | head -1
ls -d /lib/modules/$(uname -r)/build
ls /usr/src/
mokutil --sb-state
echo "Estamos listos para compilar nuestro primer módulo..!!"
```

`/lib/modules/$(uname -r)/build` tiene que existir: es un enlace a
`/usr/src/linux-headers-<versión>` y es donde el `Makefile` va a buscar el kernel.

📸 **`P1-01-entorno`** — la salida de todos esos comandos.

---

## Parte 2 — Mi primer módulo

### 2.1 El fuente, línea por línea

`part1/module/mimodulo.c` (el de la cátedra, sin cambios):

```c
#include <linux/module.h>   /* Requerido por todos los módulos */
#include <linux/kernel.h>   /* Definición de KERN_INFO */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Primer modulo ejemplo");
MODULE_AUTHOR("Catedra de SdeC");

int modulo_lin_init(void)
{
	printk(KERN_INFO "Modulo cargado en el kernel.\n");
	return 0;
}

void modulo_lin_clean(void)
{
	printk(KERN_INFO "Modulo descargado del kernel.\n");
}

module_init(modulo_lin_init);
module_exit(modulo_lin_clean);
```

- **Los `#include` son de `linux/`, no de `stdio.h`.** Un módulo no tiene libc: los
  headers vienen de `/lib/modules/$(uname -r)/build/include`.
- **`MODULE_LICENSE("GPL")`** no es decorativo. Si la licencia no es compatible con
  GPL, el kernel queda marcado (*tainted*, flag `P`) y el módulo **no puede usar** los
  símbolos exportados con `EXPORT_SYMBOL_GPL`. En kernels nuevos, si falta, `modpost`
  da error.
- **`MODULE_DESCRIPTION` y `MODULE_AUTHOR`** se guardan en una sección `.modinfo` del
  `.ko`. Es lo que después muestra `modinfo`.
- **`printk(KERN_INFO ...)`** es el `printf` del kernel. `KERN_INFO` es la prioridad
  del mensaje (de `KERN_EMERG` = 0 a `KERN_DEBUG` = 7). No va a la terminal: va al
  *ring buffer* del kernel, que se lee con `dmesg`. Hoy se prefiere escribir
  `pr_info(...)`, que es lo mismo.
- **`init` devuelve `0`** para decir "cargué bien". Si devuelve un número negativo
  (`-ENOMEM`, por ejemplo), `insmod` falla y el módulo no queda cargado.
- **`module_init()` y `module_exit()`** le dicen al kernel cuál es la función de
  entrada y cuál la de salida. Es el reemplazo de `main`.

> Al compilarlo, gcc avisa `no previous prototype for 'modulo_lin_init'`. Es porque las
> funciones no son `static`: quedarían visibles para todo el kernel y podrían chocar con
> otro símbolo del mismo nombre (el problema del *espacio de nombres* de la
> presentación). Nuestro módulo propio (Parte 4) ya lo hace bien con `static`,
> `__init` y `__exit`.

### 2.2 El Makefile

```make
obj-m +=  mimodulo.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
```

No compila nada por sí mismo: le pasa el control al sistema de compilación del kernel
(**kbuild**). `-C` entra al directorio de los headers y `M=` le dice dónde está nuestro
módulo. `obj-m` significa "compilar `mimodulo.o` como **m**ódulo" (en vez de `obj-y`,
que lo metería adentro de la imagen del kernel).

### 2.3 Compilar

```bash
cd ~/practico-4/part1/module
make
ls
```

Aparecen `mimodulo.ko` (el módulo), `mimodulo.o`, `mimodulo.mod.c` (código que genera
kbuild con la información de `modinfo` y la `vermagic`), `Module.symvers` y
`modules.order`.

Dos advertencias son **normales** y no hay que hacer nada:

- `warning: the compiler differs from the one used to build the kernel`: compara el
  texto de la versión de gcc. Si los números son iguales (`15.2.0-16ubuntu1` en los
  dos), es el mismo compilador llamado de otra forma (`x86_64-linux-gnu-gcc` vs `gcc`).
- `Skipping BTF generation ... due to unavailability of vmlinux`: ver el
  [Anexo A](#anexo-a--la-advertencia-de-btf).

📸 **`P2-01-compilacion`** — la salida de `make` y el `ls`.

### 2.4 Cargar, ver y descargar

```bash
sudo insmod mimodulo.ko
sudo dmesg | tail
lsmod | grep mimodulo
cat /proc/modules | grep mimodulo
```

> **Si da `insmod: ERROR: could not insert module mimodulo.ko: Key was rejected by
> service`**, tenés Secure Boot activo. Hay que firmar el módulo: ir a la **Parte 3** y
> después volver acá.

En `dmesg` se ve algo así:

```
[67375.506122] mimodulo: loading out-of-tree module taints kernel.
[67375.506166] mimodulo: module verification failed: signature and/or required key missing - tainting kernel
[67375.506348] Modulo cargado en el kernel.
```

- **`out-of-tree module taints kernel`**: el módulo no vino con el kernel. El kernel
  queda marcado como *tainted* (manchado) para que, si después hay un error, quien lo
  analice sepa que había código de terceros.
- **`module verification failed`**: el módulo no está firmado. Sin Secure Boot es solo
  una advertencia; con Secure Boot es un rechazo.
- **`Modulo cargado en el kernel.`**: nuestro `printk`.

En `/proc/modules`:

```
mimodulo 16384 0 - Live 0xffffffffc097e000 (OE)
```

| Campo | Significado |
|---|---|
| `mimodulo` | nombre |
| `16384` | tamaño en memoria (bytes) |
| `0` | cuántos lo están usando (si es > 0, `rmmod` se niega) |
| `-` | de qué otros módulos depende |
| `Live` | estado (`Loading`, `Live`, `Unloading`) |
| `0xffffffffc097e000` | **dirección donde quedó cargado**: arriba de todo del espacio virtual, en la zona de módulos del kernel (sin `sudo` se ve como `0x0000000000000000`) |
| `(OE)` | flags de *taint*: **O** = *out-of-tree*, **E** = sin firma (*unsigned*) |

Descargar:

```bash
sudo rmmod mimodulo
sudo dmesg | tail -3
lsmod | grep mimodulo          # ya no aparece
```

📸 **`P2-02-carga`** — `insmod`, `dmesg`, `lsmod` y `/proc/modules` con el módulo cargado.
📸 **`P2-03-descarga`** — `rmmod` y el `dmesg` con "Modulo descargado del kernel.".

### 2.5 modinfo: nuestro módulo contra uno del sistema (pregunta 1)

```bash
modinfo mimodulo.ko
modinfo des_generic
```

En Ubuntu los módulos del sistema vienen **comprimidos** (`.ko.zst`), así que la ruta
de la consigna (`.../crypto/des_generic.ko`) no existe tal cual. `modinfo` con el
**nombre** lo encuentra solo. Para ver la ruta real:

```bash
find /lib/modules/$(uname -r) -name 'des_generic*'
```

Si en tu kernel no está `des_generic`, usá cualquier otro: `modinfo btusb`,
`modinfo snd_hda_intel`, etc.

Qué comparar:

| Campo | `mimodulo.ko` | módulo del sistema |
|---|---|---|
| `filename` | tu carpeta | `/lib/modules/<versión>/kernel/...ko.zst` |
| `intree` | no aparece | `Y`: vino con el kernel |
| `signer`, `sig_key`, `sig_hashalgo`, `signature` | no aparecen | firmado por Canonical con la clave del build del kernel |
| `alias` | no tiene | si es un driver, los IDs de hardware que maneja (lo usa udev para cargarlo solo) |
| `depends` | vacío | otros módulos que necesita |
| `vermagic` | igual al del kernel | igual al del kernel |
| `author`, `description`, `license` | los nuestros | los del autor del driver |

La **`vermagic`** es la "huella" del kernel para el que se compiló el módulo (versión,
SMP, preempt, `modversions`...). Si no coincide con el kernel que corre, `insmod` falla
con `Invalid module format`. Por eso un `.ko` compilado en una máquina no sirve en otra
con otro kernel.

📸 **`P2-04-modinfo`** — las dos salidas de `modinfo`, una al lado de la otra.

---

## Parte 3 — Firmar el módulo (preguntas 8 y 10)

Esta parte es **obligatoria si tenés Secure Boot activo** y es la respuesta a la
pregunta 8 de todas formas.

### 3.1 Por qué hace falta

Con Secure Boot, la cadena de confianza va así:

```
firmware UEFI ──verifica──▶ shim ──verifica──▶ GRUB ──verifica──▶ kernel ──verifica──▶ módulos
  (claves en db)        (firmado por Microsoft) (firmado por   (firmado por      (firmados con una clave
                                                 Canonical)     Canonical)        que el kernel conoce)
```

Cada eslabón solo ejecuta lo que esté firmado por una clave en la que confía. Si el
kernel aceptara cualquier módulo, alguien podría meter un rootkit en el anillo 0 y toda
la cadena anterior no serviría de nada. Por eso, con Secure Boot, Ubuntu activa el
*lockdown* del kernel, y lockdown exige módulos firmados.

El kernel confía en las claves de sus *keyrings*:

- `.builtin_trusted_keys`: la clave con la que se firmó el kernel (los módulos de
  Ubuntu).
- `.platform`: las claves de la `db` del firmware y las **MOK** (*Machine Owner Keys*),
  que son las que agrega el dueño de la máquina con `mokutil`.

Lo que vamos a hacer: crear un par de claves propio, registrar la pública como MOK y
firmar el módulo con la privada.

> **Atajo:** si alguna vez instalaste drivers de NVIDIA o VirtualBox con Secure Boot,
> Ubuntu ya te creó una MOK en `/var/lib/shim-signed/mok/` (`MOK.der` y `MOK.priv`) y
> probablemente ya esté registrada (`mokutil --test-key /var/lib/shim-signed/mok/MOK.der`
> dice *is already enrolled*). En ese caso podés saltar a 3.4 con
> `MOK_DIR=/var/lib/shim-signed/mok`.

### 3.2 Crear la clave y pedir que se registre (una sola vez)

```bash
cd ~/practico-4
./scripts/crear-clave.sh
```

El script hace esto:

```bash
mkdir -p ~/mok && chmod 700 ~/mok
openssl req -new -x509 -newkey rsa:2048 -nodes -days 36500 -sha256 \
    -subj "/CN=SdeC TP4 $(hostname) module signing key/" \
    -addext "extendedKeyUsage=codeSigning" \
    -keyout ~/mok/MOK.priv -outform DER -out ~/mok/MOK.der
sudo mokutil --import ~/mok/MOK.der
```

- `MOK.priv` es la **clave privada**: firma. **Nunca** se sube al repo ni se comparte
  (el `.gitignore` la excluye).
- `MOK.der` es el **certificado** con la clave pública: verifica. Es lo único que se
  registra en el firmware.
- `mokutil --import` no registra nada todavía: deja un pedido pendiente y te hace
  inventar una contraseña **de un solo uso**. Anotala.

📸 **`P3-01-clave`** — la creación de la clave y el `mokutil --import`.

### 3.3 Reiniciar y registrar la MOK

```bash
sudo reboot
```

Antes de que arranque Ubuntu aparece una pantalla azul, el **MOK Manager**:

1. *Press any key to perform MOK management* → apretar una tecla (**tenés 10
   segundos**; si se pasa, arranca normal y hay que repetir el `mokutil --import`).
2. **Enroll MOK** → **Continue** → **Yes**.
3. Escribir la contraseña del paso anterior.
4. **Reboot**.

📸 (opcional) una foto con el celular de la pantalla azul.

Ya en Ubuntu, verificar:

```bash
mokutil --test-key ~/mok/MOK.der        # "is already enrolled"
mokutil --list-enrolled | grep -A1 Subject: | grep SdeC
```

### 3.4 Firmar y cargar

```bash
cd ~/practico-4/part1/module
make
../../scripts/firmar.sh mimodulo.ko
```

El script corre:

```bash
sudo /lib/modules/$(uname -r)/build/scripts/sign-file sha256 ~/mok/MOK.priv ~/mok/MOK.der mimodulo.ko
```

`sign-file` calcula el hash SHA-256 del módulo, lo firma con la clave privada y
**agrega la firma al final del `.ko`** (termina con el texto
`~Module signature appended~`). Ahora `modinfo` muestra la firma:

```bash
modinfo mimodulo.ko | grep -E 'signer|sig_key|sig_hashalgo'
```

```
signer:         SdeC TP4 nico-HP-Laptop-15-ef2xxx module signing key
sig_key:        7F:DC:8D:1D:...
sig_hashalgo:   sha256
```

Y ahora sí:

```bash
sudo insmod mimodulo.ko
sudo dmesg | tail -3
cat /proc/modules | grep mimodulo     # el taint ahora es solo (O), ya no (OE)
sudo rmmod mimodulo
```

> **Ojo:** cada `make` genera un `.ko` nuevo **sin firma**. Hay que volver a firmar
> después de cada compilación.

📸 **`P3-02-firmado`** — `modinfo` con la firma, el `insmod` sin error y el `dmesg`.

### 3.5 ¿Y si mi compañero carga mi módulo firmado? (pregunta 10)

Probarlo: pasarle `mimodulo.ko` (ya firmado) a un compañero con Secure Boot activo y
que haga `sudo insmod mimodulo.ko`. Pasan dos cosas, en este orden:

1. **Si su kernel no es exactamente el mismo**, falla por la `vermagic`
   (`Invalid module format`). Ni siquiera llega a mirar la firma.
2. **Si el kernel es el mismo**, falla con `Key was rejected by service`: mi clave no
   está en **sus** keyrings. Secure Boot no confía en "módulos firmados", confía en
   módulos firmados **por una clave que el dueño de esa máquina aceptó**.

Para que lo acepte, él tendría que importar **mi certificado público** (`MOK.der`,
nunca la `.priv`) con `sudo mokutil --import MOK.der` y registrarlo en su MOK Manager.
Eso es justamente lo que el sistema quiere evitar que pase sin que el dueño se entere:
por eso pide reiniciar y estar físicamente frente a la máquina.

📸 (opcional) el error en la máquina del compañero.

---

## Parte 4 — Nuestro propio módulo (pregunta 9)

La consigna pide un módulo que imprima **el nombre del equipo** en el log del kernel.
Está en `part1/equipo/equipo.c`:

```c
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/utsname.h>  /* utsname(): hostname, versión del kernel */

static char *grupo = "Borsotti Bosco - Ciacci - Leguizamon";
module_param(grupo, charp, 0444);

static int __init equipo_init(void)
{
	pr_info("equipo: modulo cargado en el equipo '%s' (kernel %s)\n",
		utsname()->nodename, utsname()->release);
	pr_info("equipo: grupo: %s\n", grupo);
	return 0;
}

static void __exit equipo_exit(void)
{
	pr_info("equipo: modulo descargado del equipo '%s'\n",
		utsname()->nodename);
}
```

Lo nuevo respecto de `mimodulo.c`:

- **`utsname()->nodename`**: el hostname. Es la misma estructura que en espacio de
  usuario lee `uname -n`, pero acá la leemos **directo de la memoria del kernel**, sin
  syscall.
- **`static`**: las funciones no se exportan a todo el kernel (no hay advertencia de
  prototipos y no contaminan el espacio de nombres).
- **`__init` / `__exit`**: marcan código que se usa una sola vez. El kernel libera la
  memoria de las funciones `__init` después de cargar el módulo.
- **`module_param`**: un parámetro que se pasa al cargar. Queda visible en
  `/sys/module/equipo/parameters/grupo`.

```bash
cd ~/practico-4/part1/equipo
make
make firmar                       # solo con Secure Boot (Parte 3)
modinfo equipo.ko
sudo insmod equipo.ko
sudo dmesg | tail -3
cat /sys/module/equipo/parameters/grupo
sudo rmmod equipo
sudo dmesg | tail -1

# Con otro valor del parámetro
sudo insmod equipo.ko grupo="Grupo de prueba"
sudo dmesg | tail -2
sudo rmmod equipo
```

Salida esperada:

```
equipo: modulo cargado en el equipo 'nico-HP-Laptop-15-ef2xxx' (kernel 7.0.0-29-generic)
equipo: grupo: Borsotti Bosco - Ciacci - Leguizamon
equipo: modulo descargado del equipo 'nico-HP-Laptop-15-ef2xxx'
```

📸 **`P4-01-compilacion`** — `make` y `modinfo equipo.ko` (con el `parm:`).
📸 **`P4-02-carga-descarga-<nombre>`** — `insmod`, `dmesg` con el hostname, `rmmod` y su `dmesg` (una por integrante: `santi`, `nico`, `nacho`).

> Si cada integrante lo corre en su PC, cada `dmesg` muestra un hostname distinto: es
> una buena evidencia para el informe.

---

## Parte 5 — Los módulos de mi máquina (preguntas 2, 3 y 4)

### 5.1 Qué hay cargado y qué hay disponible

```bash
lsmod | head
lsmod | wc -l                                   # cargados (+1 por el encabezado)
ls /lib/modules/$(uname -r)/kernel              # carpetas de módulos disponibles
find /lib/modules/$(uname -r) -name '*.ko*' | wc -l   # cuántos hay instalados
```

Para el informe, cada integrante corre en **su** PC:

```bash
cd ~/practico-4
./scripts/modulos.sh nico          # cada uno con su nombre
```

El script deja en `salidas/`:

| Archivo | Contenido |
|---|---|
| `cargados-<nombre>.txt` | los nombres de `lsmod`, ordenados |
| `disponibles-<nombre>.txt` | todos los `.ko` del kernel en uso |
| `no-cargados-<nombre>.txt` | los disponibles que no están cargados (pregunta 3) |
| `resumen-<nombre>.txt` | equipo, kernel y los tres totales |

Se suben los `.txt` al repo y se comparan:

```bash
diff salidas/cargados-nico.txt salidas/cargados-santi.txt
diff salidas/cargados-nico.txt salidas/cargados-santi.txt > salidas/diff-nico-santi.txt
```

En el `diff`, `<` es lo que tiene solo el primero y `>` lo que tiene solo el segundo.
Qué buscar para explicar las diferencias:

- **Placa de video**: `amdgpu` (AMD), `i915` o `xe` (Intel), `nvidia` / `nouveau`.
- **Wi-Fi y Bluetooth**: `iwlwifi` (Intel), `rtw88_*` / `rtw89_*` (Realtek),
  `mt7921e` (MediaTek), `btusb`.
- **Procesador**: `kvm_amd` o `kvm_intel`, `amd_pstate`, `intel_rapl_*`.
- **Si alguno usa una VM**: `virtio_*`, `vboxguest`, `vmw_*`.
- **Periféricos conectados en ese momento**: `uvcvideo` (cámara), `usb_storage`
  (pendrive), `snd_*` (audio).
- **Versión de kernel distinta**: cambian nombres y la cantidad de disponibles.

📸 **`P5-01a-modulos-<nombre>`** — la salida de `modulos.sh` (una por integrante).

### 5.2 Qué driver usa cada dispositivo

```bash
lspci -k            # cada dispositivo PCI con "Kernel driver in use" y "Kernel modules"
lsusb -t            # el árbol USB con el driver de cada puerto
```

Si un dispositivo aparece en `lspci` pero **sin** la línea `Kernel driver in use`, no
hay ningún driver manejándolo.

**¿Qué pasa cuando el driver de un dispositivo no está disponible?** (pregunta 3)

1. Al detectar el dispositivo, el kernel emite un evento (*uevent*) con su `MODALIAS`
   (los IDs de fabricante y modelo).
2. **udev** busca ese alias en `/lib/modules/$(uname -r)/modules.alias` y, si encuentra
   un módulo, corre `modprobe` para cargarlo. (La presentación menciona *Kudzu*, que era
   lo que hacía esto en Red Hat viejo; hoy lo hace udev.)
3. Si **no hay** ningún módulo con ese alias, no pasa nada: el dispositivo se ve en
   `lspci`/`lsusb`, pero **no funciona** (no aparece la interfaz de red, no hay sonido,
   la pantalla queda con un modo genérico, etc.).
4. A veces el módulo existe pero le falta el *firmware* (un blob que se carga en el
   dispositivo): en `dmesg` aparece `failed to load firmware` o `Direct firmware load
   ... failed`.

Para forzar cargar un módulo en cada arranque: un archivo en `/etc/modules-load.d/`.
Para impedir que se cargue: `blacklist <nombre>` en `/etc/modprobe.d/`.

```bash
cat /lib/modules/$(uname -r)/modules.alias | grep -i iwlwifi | head -3   # ejemplo
sudo dmesg | grep -i firmware
```

📸 **`P5-02-lspci`** — `lspci -k` y, si hay, algún dispositivo sin driver o un error de
firmware.

### 5.3 hwinfo y hw-probe (pregunta 4)

En una **PC real** (no VM):

```bash
sudo apt update && sudo apt install -y hwinfo hw-probe dmidecode hdparm smartmontools \
     nvme-cli pciutils usbutils lm-sensors sysstat acpica-tools ethtool wireless-tools \
     mesa-utils curl 7zip memtester

hwinfo --short
sudo -E hw-probe -all -upload -check -decode-acpi
```

- `hwinfo --short` lista todo el hardware detectado, por categoría.
- `hw-probe` junta la información del hardware, prueba los dispositivos (`-check`) y la
  sube a <https://linux-hardware.org>. Al final imprime una URL del tipo
  `https://linux-hardware.org/?probe=xxxxxxxxxx`. **Esa URL va en el informe.**
- `-check` corre pruebas (memoria, disco, gráficos) y puede tardar varios minutos.

📸 **`P5-03-hwinfo`** — `hwinfo --short`.
📸 **`P5-04-hw-probe`** — el final de `hw-probe` con la URL, y la página abierta en el
navegador.

---

## Parte 6 — Programas: syscalls y file descriptors (preguntas 5 y 6)

### 6.1 Las syscalls de un hello world (pregunta 6)

```bash
cd ~/practico-4/part1/syscalls
gcc -Wall -o ejemplo_printf ejemplo_printf.c
./ejemplo_printf
strace ./ejemplo_printf
strace -tt ./ejemplo_printf          # con la hora de cada syscall (microsegundos)
strace -c ./ejemplo_printf           # tabla resumen: cuántas veces se llamó cada una
```

La traza, resumida:

```
execve("./ejemplo_printf", ["./ejemplo_printf"], ...) = 0     ← el kernel carga el programa
brk(NULL)                                                     ← dónde termina el heap
mmap(NULL, 8192, ...)                                         ← memoria para el cargador
openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3  ← el cargador dinámico (ld.so)
openat(AT_FDCWD, "/lib/x86_64-linux-gnu/libc.so.6", ...) = 3  ← busca y abre la libc
read(3, "\177ELF\2\1\1\3...", 832) = 832                      ← lee su cabecera ELF
mmap(...)  mprotect(...)  close(3)                            ← la mapea en memoria
arch_prctl(ARCH_SET_FS, ...)  set_tid_address(...)  rseq(...) ← prepara el hilo
fstat(1, ...)                                                 ← printf mira qué es el stdout
write(1, "We can and we must undrestand co"..., 44) = 44      ← el printf de verdad
write(1, "\n", 1) = 1                                         ← el segundo printf
exit_group(0) = ?                                             ← el return 0 de main
```

Qué hay que explicar:

- **Casi todo es preparación.** De ~35 syscalls, solo las de `write` son "nuestras". El
  resto es el cargador dinámico trayendo la libc a memoria y preparando el proceso.
- **`printf` no es una syscall.** Es una función de la libc que arma el texto en un
  buffer y, cuando corresponde, llama a la syscall **`write(1, ...)`**. El `1` es el
  file descriptor de la salida estándar.
- **El buffer depende de adónde va la salida.** En la terminal hay **dos** `write`
  (el stdout de una terminal tiene buffer *por línea*). Si se redirige:
  `strace -e trace=write ./ejemplo_printf > /dev/null` muestra **uno solo** de 45 bytes
  (con un archivo o un pipe, el buffer es completo y se vacía al salir).
- **`exit_group`** es la syscall con la que termina el proceso (el `return 0` de `main`
  pasa por `exit()` de la libc).
- Con `-c`, la columna `errors` muestra que alguna syscall falla y está bien (por
  ejemplo, `access("/etc/ld.so.preload")`: el archivo no existe).

Para ver la diferencia entre funciones de biblioteca y syscalls: `ltrace ./ejemplo_printf`
(hay que instalarlo) muestra las llamadas a la libc (`puts`, `putchar`), y `strace` las
syscalls que esas funciones terminan haciendo.

📸 **`P6-01-strace`** — `strace ./ejemplo_printf`.
📸 **`P6-02-strace-tt-c`** — `strace -tt` y `strace -c`.

### 6.2 La API de archivos: `copiar_archivo.c`

La presentación usa este programa para ver `open`, `read`, `write` y `close`:

```bash
gcc -Wall -o copiar_archivo copiar_archivo.c     # avisa que main debería ser int
./copiar_archivo ../testdir/file1 ../testdir/file2
cat ../testdir/file2
ls -l ../testdir
strace -e trace=openat,read,write,close ./copiar_archivo ../testdir/file1 ../testdir/file2
```

- `open()` devuelve el **file descriptor** más chico libre: **3** para el de entrada y
  **4** para el de salida (0, 1 y 2 ya están ocupados por stdin, stdout y stderr).
- El bucle hace `read(3, buffer, 512)` y `write(4, buffer, n)` hasta que `read`
  devuelve `0` (fin de archivo).
- `file2` se crea con permisos `0751` → `-rwxr-x--x` (menos lo que saque el `umask`).

📸 **`P6-03-copiar-archivo`** — la copia, el `ls -l` y el `strace`.

### 6.3 File descriptors en `/proc`

Cada proceso tiene en `/proc/<PID>/fd/` un enlace por cada archivo abierto:

```bash
ls -l /proc/self/fd                 # los FDs del propio ls
ls -l /proc/$$/fd                   # los de tu shell ($$ es su PID)
tty                                 # en qué terminal estás (por ejemplo /dev/pts/0)
```

```
lrwx------ 1 nico nico 64 ... 0 -> /dev/pts/0    # stdin
lrwx------ 1 nico nico 64 ... 1 -> /dev/pts/0    # stdout
lrwx------ 1 nico nico 64 ... 2 -> /dev/pts/0    # stderr
```

Redirecciones (todas son "cambiar a qué apunta un FD"):

```bash
echo "Texto de prueba" > archivo.txt        # el FD 1 apunta al archivo (lo pisa)
echo "Nueva línea" >> archivo.txt           # agrega al final
cat archivo.txt
echo "Mensaje de error" >&2                 # escribe en el FD 2
echo "Hola desde el FD 1" > /proc/self/fd/1 # escribir en la ruta del FD
```

Para ver los FDs de otro proceso: abrir una **segunda terminal**, ver cuál es con `tty`
(por ejemplo `/dev/pts/1`) y desde la primera:

```bash
pgrep -a bash                               # los PIDs de las shells abiertas
ls -l /proc/<PID>/fd                        # los FDs de la otra shell
echo "Mensaje enviado a otra terminal" > /dev/pts/1   # aparece en la otra ventana
```

Esto conecta con la idea de la presentación: **en Linux todo es un archivo**. La
terminal es un archivo de dispositivo (`/dev/pts/1`), y escribir en ella es un
`write()` como cualquier otro.

📸 **`P6-04-file-descriptors`** — `ls -l /proc/self/fd`, las redirecciones y el mensaje
llegando a la otra terminal.

### 6.4 ¿Qué diferencia hay entre un módulo y un programa? (pregunta 5)

Con lo hecho hasta acá ya se puede responder con evidencia propia: la tabla de la
Parte 0 más lo observado.

- El programa necesitó **~35 syscalls** para imprimir dos líneas; el módulo imprime con
  `printk` **sin ninguna syscall**, porque ya está adentro del kernel.
- El programa termina solo (`exit_group`); el módulo quedó cargado hasta el `rmmod`.
- El programa vive en su propio espacio de direcciones (direcciones bajas, `0x55...`,
  `0x7f...` en el `strace`); el módulo quedó en `0xffffffffc0...`, el espacio del
  kernel.
- El programa usa la libc; el módulo solo símbolos del kernel (Parte 9).

---

## Parte 7 — Segmentation fault (pregunta 7)

### 7.1 Qué es

Un *segmentation fault* es un acceso a memoria que el proceso **no tiene permitido**:
leer o escribir una dirección que no está mapeada (por ejemplo, `NULL`), escribir en
una página de solo lectura (el código, un string literal) o ejecutar donde no se puede.

### 7.2 Cómo lo maneja el kernel

1. La instrucción que accede a memoria hace que la **MMU** busque la dirección en las
   tablas de páginas del proceso. No hay entrada válida → la CPU lanza una
   **excepción de fallo de página** (*page fault*, vector 14 en x86) y salta al kernel.
2. El manejador de fallos de página del kernel mira si la dirección pertenece a alguna
   zona válida del proceso (una *VMA*). Si es válida pero la página no estaba cargada
   (swap, *lazy allocation*), la carga y el programa sigue sin enterarse.
3. Si no es válida, el kernel **le manda la señal `SIGSEGV`** al proceso, con la
   dirección culpable en `si_addr`.
4. La acción por defecto de `SIGSEGV` es **terminar el proceso** (y generar un *core
   dump* si está habilitado). La shell muestra `Segmentation fault` y el código de
   salida es `139` (128 + 11, el número de `SIGSEGV`).
5. El kernel deja constancia en `dmesg`: `segv[1234]: segfault at 0 ip ... error 6`.

**Si el que accede mal es un módulo**, no hay a quién mandarle una señal: el fallo es
del propio kernel. Se produce un **oops** (`BUG: kernel NULL pointer dereference`, con
el volcado de registros y la pila en `dmesg`), se mata el proceso que estaba en curso,
el kernel queda marcado con *taint* `D` y puede quedar inestable o directamente entrar
en **kernel panic**. Es lo que dice la presentación: *cuando un módulo falla, falla el
kernel*.

### 7.3 Cómo lo maneja un programa

Un programa puede **atrapar** la señal con `sigaction()` y, por ejemplo, informar dónde
falló antes de terminar. Está en `part1/segfault/segv.c`:

```bash
cd ~/practico-4/part1/segfault
gcc -Wall -g -o segv segv.c
./segv ; echo "código de salida: $?"
./segv manejar ; echo "código de salida: $?"
sudo dmesg | tail -2
```

```
Voy a escribir en la dirección (nil)...
Segmentation fault (core dumped)
código de salida: 139

Manejador de SIGSEGV instalado
Voy a escribir en la dirección (nil)...
Recibí la señal 11 (SIGSEGV) al acceder a (nil)
código de salida: 1
```

El manejador **no puede "arreglar" el error y seguir**: si retorna, la CPU vuelve a
ejecutar la misma instrucción y falla otra vez. Por eso termina con `_exit(1)`.

📸 **`P7-01-segfault`** — las dos ejecuciones y el `dmesg`.

> **No hagan un módulo que desreferencie `NULL` para probar**, salvo en una máquina
> virtual con una instantánea guardada: puede colgar la máquina.

---

## Parte 8 — Secure Boot y el parche de Microsoft (pregunta 11)

Leer la nota de Ars Technica:
<https://arstechnica.com/security/2024/08/a-patch-microsoft-spent-2-years-preparing-is-making-a-mess-for-some-linux-users/>

Contexto para entenderla:

- **SBAT** (*Secure Boot Advanced Targeting*) es un mecanismo para **revocar versiones
  viejas** de shim y GRUB que tienen vulnerabilidades conocidas, sin tener que revocar
  todos los certificados. Cada binario lleva un número de "generación" y el firmware
  guarda en una variable de la NVRAM la generación mínima aceptada.
- En agosto de 2024, Microsoft publicó en Windows Update una política SBAT para
  bloquear GRUB vulnerables a **CVE-2022-2601** (una falla que permitía saltear Secure
  Boot). Se suponía que **no** se iba a aplicar en máquinas con arranque dual.

### Preguntas

1. **¿Cuál fue la consecuencia principal del parche sobre GRUB en sistemas con
   arranque dual?**
   La detección de arranque dual falló y la política se aplicó igual. En esas máquinas,
   el shim/GRUB de distribuciones como Ubuntu, Debian, Linux Mint o Zorin quedó por
   debajo de la generación mínima y **Linux dejó de arrancar** con el mensaje
   `Verifying shim SBAT data failed: Security Policy Violation. Something has gone
   seriously wrong: SBAT self-check failed`. Windows seguía andando.
2. **¿Qué implicancia tiene desactivar Secure Boot como solución?**
   Hace que arranque Linux, pero **se pierde toda la cadena de verificación**: el
   firmware ejecuta cualquier cargador, y el kernel acepta módulos sin firmar. Queda
   abierta la puerta a *bootkits* y rootkits que se cargan antes que el sistema, que es
   justo lo que el parche quería evitar. Además, en máquinas con BitLocker puede pedir
   la clave de recuperación. La salida correcta es usarlo de forma **temporal**: entrar
   a Linux, borrar la política (`sudo mokutil --set-sbat-policy delete`), reiniciar,
   **volver a activar Secure Boot** y actualizar shim/GRUB a versiones parcheadas.
3. **¿Cuál es el propósito principal del Secure Boot?**
   Garantizar que **solo se ejecute software de arranque firmado** por claves en las que
   confía el firmware: cada etapa (firmware → shim → GRUB → kernel → módulos) verifica la
   firma de la siguiente antes de ejecutarla. Así se arma una **cadena de confianza**
   desde el encendido y se impide que un malware se instale por debajo del sistema
   operativo, donde el antivirus no lo ve.

Relación con este TP: el `Key was rejected by service` de la Parte 2 es el **último
eslabón** de esa misma cadena.

---

## Parte 9 — Desafío #2: módulos vs programas

Las respuestas van en `PROCEDIMIENTO.md`. Acá están los comandos y los puntos a
explicar.

### 9.1 ¿Cómo empiezan y terminan unos y otros?

Ya visto: `main()` → `exit_group` (Parte 6) contra `module_init()` → queda residente →
`module_exit()` (Parte 2).

### 9.2 ¿Qué funciones tiene disponible un programa y cuáles un módulo?

- **Un programa** tiene la **libc** (y cualquier biblioteca que enlace), y por debajo
  las **syscalls**, que son la única puerta al kernel. La lista de syscalls:
  `man 2 syscalls`, o `ausyscall --dump` (paquete `auditd`).
- **Un módulo** solo puede usar los **símbolos que exporta el kernel** (con
  `EXPORT_SYMBOL` o `EXPORT_SYMBOL_GPL`). No hay `printf`, ni `malloc`, ni `fopen`: hay
  `printk`, `kmalloc`, `filp_open`...

```bash
# Todos los símbolos que conoce el kernel (funciones y variables)
sudo head /proc/kallsyms
sudo wc -l /proc/kallsyms

# printk (en kernels nuevos la función real se llama _printk)
sudo grep -w -e printk -e _printk /proc/kallsyms

# Los símbolos de nuestro módulo cargado
sudo insmod ~/practico-4/part1/equipo/equipo.ko
sudo grep '\[equipo\]' /proc/kallsyms
sudo rmmod equipo

# Los que realmente se pueden usar desde un módulo (los exportados)
wc -l /lib/modules/$(uname -r)/build/Module.symvers
grep -w _printk /lib/modules/$(uname -r)/build/Module.symvers

# Sin sudo las direcciones salen en cero (protección kptr_restrict)
head -3 /proc/kallsyms
```

En `/proc/kallsyms` la letra del medio es el tipo: `T`/`t` = código (global/local),
`D`/`d` = datos, `B`/`b` = BSS.

📸 **`P9-01-kallsyms`** — `printk` en `/proc/kallsyms`, los símbolos de `[equipo]` y la
cuenta de `Module.symvers`.

### 9.3 Espacio de usuario, espacio del kernel y espacio de datos

- **Espacio de usuario**: cada proceso tiene su propio **espacio de direcciones
  virtual** (en x86-64, la mitad baja: `0x0000...` a `0x00007fff...`). Adentro, el
  código (`.text`), los datos (`.data`, `.bss`), el heap y la pila. Un proceso no puede
  ver la memoria de otro: la MMU lo impide.
- **Espacio del kernel**: la mitad alta (`0xffff8000...` en adelante), **la misma para
  todos los procesos** y accesible solo en anillo 0. Los módulos se cargan en una zona
  propia de esa mitad (`0xffffffffc0000000`, la dirección que vimos en
  `/proc/modules`).
- **Espacio de datos y de código**: los datos de un módulo (sus variables globales)
  quedan en la memoria del kernel, **compartida** con todo el kernel y con los demás
  módulos. **Riesgos:** un módulo puede pisar estructuras de otro subsistema, un
  desborde de buffer en el kernel no tiene protección como en usuario, y el kernel
  **nunca debe confiar en un puntero que viene del usuario**: para copiar datos entre
  los dos espacios se usa `copy_from_user()` / `copy_to_user()`, que verifican la
  dirección.

```bash
cat /proc/self/maps          # el mapa de memoria de un proceso (todo en la mitad baja)
sudo cat /proc/modules | head -5   # las direcciones de los módulos (mitad alta)
```

📸 **`P9-02-espacios`** — `/proc/self/maps` y `/proc/modules` con las direcciones.

### 9.4 Drivers: el contenido de `/dev`

```bash
ls -l /dev | head -30
ls -l /dev/null /dev/zero /dev/urandom /dev/tty /dev/pts/ /dev/nvme0n1 /dev/sda 2>/dev/null
cat /proc/devices
```

```
crw-rw-rw- 1 root root   1,   3 ... /dev/null
crw-rw-rw- 1 root root   1,   5 ... /dev/zero
crw-rw-rw- 1 root root   1,   9 ... /dev/urandom
brw-rw---- 1 root disk 259,   0 ... /dev/nvme0n1
```

- La **primera letra** es el tipo: **`c`** = dispositivo de **caracteres** (se lee y
  escribe como un flujo de bytes: terminales, `/dev/null`, el mouse) y **`b`** = de
  **bloques** (acceso por bloques y con caché: discos, particiones).
- Donde un archivo normal tiene el tamaño, un dispositivo tiene **dos números**:
  - **Mayor**: identifica **al driver**. En `/proc/devices` está la tabla (`1 mem`,
    `4 tty`, `259 blkext`...).
  - **Menor**: identifica **cuál dispositivo** de los que maneja ese driver (el `3` de
    `mem` es `null`, el `5` es `zero`...).
- Cuando un programa hace `open("/dev/xxx")`, el kernel mira el número mayor y le pasa
  las llamadas (`read`, `write`, `ioctl`) a las funciones de ese driver: es la
  `struct file_operations` de la presentación.
- Los archivos de `/dev` los crea **udev** cuando aparece un dispositivo.

```bash
echo "esto desaparece" > /dev/null
head -c 16 /dev/zero | xxd
head -c 16 /dev/urandom | xxd
udevadm info /dev/nvme0n1 2>/dev/null | head      # o /dev/sda
```

📸 **`P9-03-dev`** — `ls -l /dev`, `/proc/devices` y las pruebas con `null`, `zero` y
`urandom`.

---

## Parte 10 — Desafío #1: checkinstall y seguridad del kernel

### 10.1 ¿Qué es checkinstall y para qué sirve?

Cuando se instala un programa desde el código fuente con `sudo make install`, los
archivos se copian por todo el sistema y **el gestor de paquetes no se entera**: no se
puede desinstalar limpio, ni saber qué archivos instaló, ni pasarlo a otra máquina.

**checkinstall** corre el `make install` en un entorno vigilado, anota cada archivo que
se copia y con eso arma un **paquete `.deb`** (o `.rpm`). Después se instala, se lista
y se desinstala con `dpkg`/`apt` como cualquier otro paquete, y se puede copiar a otra
máquina.

### 10.2 Empaquetar un hello world

```bash
cd ~/practico-4/desafio1/hola
make
sudo checkinstall --pkgname=hola-sdc --pkgversion=1.0 --pkgrelease=1 \
     --maintainer="grupo-sdc@unc.edu.ar" --default --install=no
ls *.deb
dpkg -c hola-sdc_1.0-1_amd64.deb          # qué archivos trae
```

- `--default` acepta todas las preguntas con el valor por defecto.
- `--install=no` solo arma el `.deb`, no lo instala.

Instalar, probar y desinstalar:

```bash
sudo apt install ./hola-sdc_1.0-1_amd64.deb
hola-sdc
dpkg -L hola-sdc                           # dónde quedó cada archivo
sudo apt remove hola-sdc
which hola-sdc                             # ya no está
```

📸 **`P10-01-checkinstall`** — la creación del `.deb`, `dpkg -c`, la instalación y
`hola-sdc` funcionando.

### 10.3 El programa del stack frame en dos máquinas

Hacer lo mismo con el programa del **TP #2**: agregarle al `Makefile` una regla
`install` como la de `desafio1/hola/Makefile`:

```make
PREFIX ?= /usr/local
install: <binario>
	install -D -m 755 <binario> $(DESTDIR)$(PREFIX)/bin/<binario>
```

```bash
sudo checkinstall --pkgname=stackframe-sdc --pkgversion=1.0 --default --install=no
```

Pasarle el `.deb` a un compañero (pendrive, Telegram, `scp`...) y que lo instale con
`sudo apt install ./stackframe-sdc_1.0-1_amd64.deb`.

> Si el programa del TP #2 depende de bibliotecas (por ejemplo, Python o una `.so`
> propia), hay que declararlas con `--requires="python3, ..."` o el paquete se va a
> instalar pero no va a andar en la otra máquina.

📸 **`P10-02-maquina-1`** y **`P10-03-maquina-2`** — la instalación y la ejecución en
cada máquina (con el `hostname` visible en la captura).

### 10.4 Seguridad del kernel: módulos sin firmar y rootkits

Un **rootkit de kernel** es justamente un módulo malicioso: una vez cargado está en el
anillo 0 y puede esconder procesos, archivos y conexiones modificando las estructuras
del kernel. Por eso la defensa principal es **controlar qué módulos se pueden cargar**.

**Para evitar que se carguen módulos sin firmar:**

| Medida | Cómo |
|---|---|
| Secure Boot activo | En el firmware. Con eso Ubuntu activa el lockdown y exige firma |
| Forzar la firma aunque no haya Secure Boot | Parámetro de arranque `module.sig_enforce=1` (en `GRUB_CMDLINE_LINUX` de `/etc/default/grub` y `sudo update-grub`), o compilar el kernel con `CONFIG_MODULE_SIG_FORCE=y` |
| Lockdown del kernel | Parámetro `lockdown=integrity` (o `confidentiality`). Además de la firma, bloquea `/dev/mem`, `kexec` sin firmar, etc. |
| No permitir cargar más módulos | `sudo sysctl kernel.modules_disabled=1` después del arranque. Es **irreversible** hasta reiniciar |
| Bloquear módulos que no se usan | `blacklist` / `install <mod> /bin/false` en `/etc/modprobe.d/` |

```bash
cat /proc/cmdline                          # parámetros con los que arrancó el kernel
cat /sys/kernel/security/lockdown
cat /proc/sys/kernel/modules_disabled
cat /proc/sys/kernel/tainted               # 0 = kernel "limpio"
```

**Herramientas para detectar rootkits e intrusiones:**

- **rkhunter** y **chkrootkit**: buscan firmas de rootkits conocidos, módulos ocultos,
  binarios del sistema modificados.
- **Lynis**: auditoría general de seguridad del sistema, con recomendaciones.
- **AIDE** / **Tripwire**: guardan el hash de los archivos del sistema y avisan si
  cambian.
- **auditd**: registrar cada carga de módulo con reglas sobre las syscalls
  `init_module` y `finit_module`.
- **Falco**, **Tracee** (basados en eBPF): detección de comportamiento sospechoso en
  tiempo real.

```bash
sudo apt install rkhunter chkrootkit lynis
sudo rkhunter --check --sk
sudo chkrootkit
sudo lynis audit system
```

**Técnicas de seguridad que trae el kernel:**

- **Firma de módulos** y **lockdown** (lo visto en la Parte 3).
- **KASLR**: el kernel se carga en una dirección aleatoria en cada arranque.
- **SMEP / SMAP**: el kernel no puede ejecutar (SMEP) ni leer/escribir (SMAP) memoria
  de usuario por accidente.
- **KPTI**: separa las tablas de páginas de usuario y kernel (mitigación de Meltdown).
- **Stack protector** (*canaries*), **FORTIFY_SOURCE**, **CFI** (*Control-Flow
  Integrity*): contra desbordes de pila y saltos a código no previsto.
- **`kptr_restrict`** y **`dmesg_restrict`**: esconden direcciones del kernel a usuarios
  comunes (lo vimos en `/proc/kallsyms`).
- **LSM**: AppArmor (Ubuntu), SELinux (Red Hat), Yama, Landlock.
- **IMA / EVM**: miden y verifican la integridad de los archivos que se ejecutan.

📸 **`P10-04a-seguridad`** — `/proc/cmdline`, lockdown y `tainted`.
📸 **`P10-04b-rkhunter`** — el resumen de `rkhunter`.

---

## Parte 11 — Raspberry Pi: compilación nativa y cruzada

La cátedra pidió llevar el módulo a una **Raspberry Pi 4**, que tiene un procesador ARM
de 64 bits (**aarch64**) en lugar de x86-64. Se hace de dos formas:

| | Nativa (11.1) | Cruzada (11.2 a 11.7) |
|---|---|---|
| Dónde se compila | En la propia Raspberry | En la VM x86-64 |
| Compilador | `gcc` de la Raspberry (ARM → ARM) | `aarch64-linux-gnu-gcc` (x86 → ARM) |
| Headers del kernel | Los del paquete de la Raspberry (`/lib/modules/$(uname -r)/build`) | El fuente del kernel de la Raspberry, de la **versión exacta**, preparado en la VM |
| Dónde se carga | En la Raspberry | Se copia el `.ko` a la Raspberry y se carga ahí |

### 11.1 Compilación nativa (ya hecha)

Con Raspberry Pi OS Lite (64-bit) grabado con Raspberry Pi Imager y acceso por SSH, se
instalaron `build-essential`, `git` y `linux-headers-$(uname -r)` en la Raspberry, se
clonó el repo y se compilaron y cargaron `mimodulo` y `equipo` con los mismos comandos de
las Partes 2 y 4.

📸 **`P11-01-rpi-entorno`**, **`P11-02-rpi-mimodulo`** y **`P11-03-rpi-equipo`**
(resultados en `PROCEDIMIENTO.md`).

### 11.2 Por qué compilar en cruzado

Un **compilador cruzado** corre en una arquitectura (el *host*, acá la VM x86-64) y
genera código para otra (el *target*, la Raspberry aarch64). Es la forma normal de
trabajar en sistemas embebidos:

- El target suele ser **lento o chico**: la Raspberry tiene 1 GB de RAM y una microSD.
  Compilar ahí un kernel entero tardaría horas; en una PC, minutos.
- Muchos dispositivos **no tienen compilador ni headers** instalados (routers, placas
  industriales, celulares).
- Se compila una vez y se distribuye el binario a muchos equipos iguales.

El nombre del compilador dice para qué genera código: `aarch64-linux-gnu-gcc` =
arquitectura `aarch64`, sistema `linux`, ABI `gnu`. Al kbuild del kernel se le indica con
dos variables:

```bash
ARCH=arm64                          # arquitectura del kernel destino (arm64 = aarch64)
CROSS_COMPILE=aarch64-linux-gnu-    # prefijo de gcc, ld, objcopy... del toolchain cruzado
```

### 11.3 Qué tiene que coincidir con el kernel de la Raspberry

Un módulo se engancha a **un kernel en particular**. Para que la Raspberry lo acepte,
hay que compilarlo contra un árbol del kernel igual al que corre:

| Qué | Por qué | De dónde sale |
|---|---|---|
| **Arquitectura** | Un `.ko` de x86 no se puede ejecutar en ARM | `ARCH=arm64` + compilador `aarch64-linux-gnu-` |
| **Versión exacta** (`6.18.50+rpt-rpi-v8`) | Va en la `vermagic`; si no coincide, `insmod` da `Invalid module format` | `uname -r` en la Raspberry; el fuente de esa versión en GitHub |
| **Configuración** (`.config`) | Define opciones (SMP, PREEMPT, MODVERSIONS) y el tamaño de las estructuras del kernel | `.config` de los headers de la Raspberry |
| **Versiones de los símbolos** (`Module.symvers`) | Con `CONFIG_MODVERSIONS`, cada símbolo (`_printk`, `param_ops_charp`...) lleva un CRC; si no coincide: `disagrees about version of symbol` | `Module.symvers` de los headers de la Raspberry |

Por eso no alcanza con `apt install linux-headers-...` en la VM: esos son los headers de
Ubuntu x86. Hay que **buscar el fuente del kernel de Raspberry Pi de la misma versión**.

**Cómo se busca la versión.** En la Raspberry:

```bash
uname -r      # 6.18.50+rpt-rpi-v8   → kernel 6.18.50, variante rpi-v8 (64 bits)
uname -v      # #1 SMP PREEMPT Debian 1:6.18.50-1+rpt1 (2026-09-11)
```

El kernel de Raspberry Pi OS sale del repo
[raspberrypi/linux](https://github.com/raspberrypi/linux), rama `rpi-6.18.y`. Esa rama
avanza (hoy está en 6.18.60), así que se busca en su historia el último commit en el que
el `Makefile` dice `SUBLEVEL = 50`:

```bash
git clone --branch rpi-6.18.y --shallow-since=2026-08-15 https://github.com/raspberrypi/linux
cd linux
for c in $(git rev-list --first-parent HEAD); do
    git show $c:Makefile | grep -q '^SUBLEVEL = 50$' && { git log -1 --format='%H %ad %s' $c; break; }
done
```

Resultado (ya buscado, no hace falta repetirlo): `0c0042cbe1711760cf8e8c213db3c05fca16b81e`, del 11/09/2026, el mismo día que
dice `uname -v`. En 11.4 se baja directamente ese commit.

### 11.4 En la Raspberry: la versión y los archivos del kernel

```bash
uname -r
uname -v
uname -m
ls -l /lib/modules/$(uname -r)/build/.config /lib/modules/$(uname -r)/build/Module.symvers
hostname -I
```

`hostname -I` da la IP de la Raspberry (por ejemplo `192.168.0.233`), que se usa para
copiar archivos desde la VM.

📸 **`P11-04-rpi-version`** — los cinco comandos.

> Si no está el `.config`: `zcat /proc/config.gz > ~/config-rpi` (o
> `sudo modprobe configs` antes) y se copia ese archivo en su lugar.

### 11.5 En la VM: toolchain cruzado y fuente del kernel

```bash
sudo apt update
sudo apt install -y gcc-aarch64-linux-gnu make bc bison flex libssl-dev libelf-dev git
df -h ~                               # hacen falta unos 4 GB libres
```

Bajar **solo** el commit de la versión 6.18.50 (unos 300 MB de descarga, 2 GB en disco):

```bash
mkdir ~/rpi-linux && cd ~/rpi-linux
git init
git remote add origin https://github.com/raspberrypi/linux
git fetch --depth 1 origin 0c0042cbe1711760cf8e8c213db3c05fca16b81e
git checkout FETCH_HEAD
```

Traer de la Raspberry la configuración y las versiones de los símbolos (cambiar la IP
por la de 11.4):

```bash
scp santiago@192.168.0.233:/lib/modules/6.18.50+rpt-rpi-v8/build/.config ~/rpi-linux/.config
scp santiago@192.168.0.233:/lib/modules/6.18.50+rpt-rpi-v8/build/Module.symvers ~/rpi-linux/Module.symvers
```

Verificación:

```bash
aarch64-linux-gnu-gcc --version | head -1
head -4 ~/rpi-linux/Makefile
ls -l ~/rpi-linux/.config ~/rpi-linux/Module.symvers
```

📸 **`P11-05-toolchain-fuente`** — la versión del compilador cruzado, `SUBLEVEL = 50` y
los dos archivos traídos de la Raspberry.

### 11.6 Preparar el árbol del kernel para compilar módulos

```bash
cd ~/rpi-linux
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- LOCALVERSION= -j$(nproc) modules_prepare
cat include/config/kernel.release
```

- `olddefconfig` adapta el `.config` de la Raspberry a este compilador sin hacer
  preguntas.
- `modules_prepare` no compila el kernel: genera solo lo que hace falta para compilar
  módulos externos (headers generados como `utsrelease.h` y las herramientas `modpost` y
  `fixdep`, compiladas para x86 porque corren en la VM). Tarda menos de un minuto.
- `LOCALVERSION=` (vacío) evita que kbuild le agregue un `+` a la versión porque el
  fuente no es un release oficial.

`kernel.release` tiene que dar **exactamente** lo mismo que `uname -r` en la Raspberry:
`6.18.50+rpt-rpi-v8`. Con el `.config` de Raspberry Pi OS da **`6.18.50-v8`**: el
`.config` trae `CONFIG_LOCALVERSION="-v8"` (el sufijo de la Raspberry Pi Foundation), y
el sufijo `+rpt-rpi-v8` se lo pone el empaquetado de Debian al compilar el paquete, sin
guardarlo en el `.config`. Por eso se fija el sufijo a mano y se repite:

```bash
./scripts/config --set-str LOCALVERSION "+rpt-rpi-v8"
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- LOCALVERSION= -j$(nproc) modules_prepare
cat include/config/kernel.release
```

📸 **`P11-06-modules-prepare`** — el final de `modules_prepare` y `kernel.release`.

### 11.7 Compilar en cruzado, probar en la VM y cargar en la Raspberry

Se trabaja en una copia para no mezclar el `.ko` de ARM con el de x86:

```bash
cp -r ~/practico-4/part1/equipo ~/equipo-arm
cd ~/equipo-arm
rm -f *.ko *.o *.mod *.mod.c .*.cmd Module.symvers modules.order
make KDIR=~/rpi-linux ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- LOCALVERSION=
file equipo.ko
modinfo equipo.ko | grep -E 'vermagic|parm'
file ~/practico-4/part1/equipo/equipo.ko
sudo insmod equipo.ko
```

- Es el mismo `Makefile` de la Parte 4: solo cambia `KDIR` (el árbol del kernel de la
  Raspberry en vez de los headers de Ubuntu) y se agregan `ARCH` y `CROSS_COMPILE`.
- `file` muestra **`ARM aarch64`** para el nuevo y **`x86-64`** para el de la Parte 4.
- La `vermagic` tiene que ser `6.18.50+rpt-rpi-v8 SMP preempt mod_unload modversions
  aarch64`, idéntica a la del módulo compilado en la Raspberry (11.1).
- El `insmod` en la VM **tiene que fallar** con `Invalid module format`: el kernel x86
  rechaza un módulo de otra arquitectura y de otra versión.

📸 **`P11-07-cross-compilacion`** — desde el `make` hasta el `insmod` fallido.

Copiarlo a la Raspberry:

```bash
scp equipo.ko santiago@192.168.0.233:~/equipo-cross.ko
```

En la Raspberry (por SSH):

```bash
sudo insmod ~/equipo-cross.ko
sudo dmesg | tail -2
cat /sys/module/equipo/parameters/grupo
sudo rmmod equipo
sudo dmesg | tail -1
```

Tiene que mostrar `equipo: modulo cargado en el equipo 'tp4-santi' (kernel
6.18.50+rpt-rpi-v8)`: el módulo compilado en una PC x86 corre en la Raspberry.

📸 **`P11-08-cross-en-rpi`** — el bloque completo.

### 11.8 Si algo no anda

| Síntoma | Causa | Solución |
|---|---|---|
| `insmod: ERROR: ... Invalid module format` en la Raspberry | La `vermagic` no coincide (`kernel.release` distinto de `uname -r`) | Revisar 11.6 y recompilar |
| `disagrees about version of symbol ...` en el `dmesg` de la Raspberry | `Module.symvers` no es el de la Raspberry | Volver a copiarlo (11.5), `modules_prepare` y recompilar |
| `modpost: "_printk" [...] undefined!` al compilar | Falta `Module.symvers` en `~/rpi-linux` | Copiarlo de la Raspberry (11.5) |
| `aarch64-linux-gnu-gcc: not found` | Falta el toolchain cruzado | `sudo apt install gcc-aarch64-linux-gnu` |
| `scp: ... Connection refused` o se queda colgado | La IP cambió o la Raspberry está apagada | `hostname -I` en la Raspberry, o `ping -4 tp4-santi.local` en Windows |
| `ssh: Could not resolve hostname tp4-santi.local` desde la VM | La VM en NAT no resuelve nombres `.local` | Usar la IP |

---

## Anexo A — La advertencia de BTF

Al compilar aparece:

```
Skipping BTF generation for /ruta/mimodulo.ko due to unavailability of vmlinux
```

**No es un error.** BTF (*BPF Type Format*) son metadatos de tipos que usan las
herramientas eBPF (`bpftrace`, `bpftool`) para inspeccionar el módulo en vivo. Para
generarlos, `pahole` necesita el kernel completo sin comprimir (`vmlinux`), y los
headers de Ubuntu no lo traen. El módulo se compila, carga y descarga igual.

Si se quiere generar el BTF (opcional):

```bash
sudo cp /sys/kernel/btf/vmlinux /lib/modules/$(uname -r)/build/
make clean && make
sudo bpftool btf dump file mimodulo.ko      # los tipos del módulo
```

Con BTF se puede, por ejemplo, trazar las funciones del módulo sin agregar `printk`:

```bash
sudo bpftrace -e 'fentry:equipo:equipo_init { printf("init!\n"); }'
```

---

## Anexo B — Si algo no anda

| Síntoma | Causa | Solución |
|---|---|---|
| `No se ha podido localizar el paquete build-essentials` | Mal escrito | `build-essential` |
| `kernel-package no tiene un candidato` | Ya no existe | No hace falta (Parte 1.1) |
| `404 Not Found` en `apt` | Lista de paquetes vieja | `sudo apt update` y repetir |
| `make: *** /lib/modules/.../build: No existe el archivo` | Faltan los headers del kernel que corre | `sudo apt install linux-headers-$(uname -r)` |
| `the compiler differs from the one used to build the kernel` | Mismo gcc con otro nombre | Ignorar si las versiones coinciden |
| `Skipping BTF generation` | Falta `vmlinux` | Ignorar (Anexo A) |
| `no previous prototype for 'modulo_lin_init'` | Funciones no `static` en `mimodulo.c` | Es una advertencia; ver Parte 2.1 |
| `Key was rejected by service` | Secure Boot activo y módulo sin firmar (o firmado con una clave no registrada) | Parte 3 |
| `Invalid module format` | El `.ko` se compiló para otro kernel (por ejemplo, se actualizó el kernel y no se reinició, o al revés) | Reiniciar, `make clean && make` |
| `insmod: ERROR: ... File exists` | El módulo ya está cargado | `sudo rmmod <nombre>` primero |
| `rmmod: ERROR: Module ... is in use` | Otro módulo o proceso lo usa | Ver la columna "Used by" de `lsmod` |
| `dmesg: read kernel buffer failed: Operation not permitted` | `dmesg_restrict` | Usar `sudo dmesg` |
| La pantalla azul del MOK Manager no apareció | Pasaron los 10 segundos | `sudo mokutil --import ~/mok/MOK.der` y reiniciar de nuevo |
