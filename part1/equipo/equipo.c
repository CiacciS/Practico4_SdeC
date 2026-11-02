/*
 * Módulo propio del TP #4 (pregunta 9).
 *
 * Al cargarse imprime en el log del kernel el nombre del equipo (hostname) y
 * los integrantes del grupo; al descargarse se despide. Se ve con `sudo dmesg`.
 *
 * Ciclo de vida:
 *   sudo insmod equipo.ko   -> el kernel ejecuta equipo_init() y el módulo queda cargado
 *   (mientras está cargado no corre nada: solo ocupa memoria del kernel)
 *   sudo rmmod equipo       -> el kernel ejecuta equipo_exit() y lo saca de memoria
 */

/*
 * Un módulo NO puede usar la libc: no hay <stdio.h>, ni printf, ni malloc.
 * Todos los headers vienen del propio kernel (/lib/modules/$(uname -r)/build/include).
 */
#include <linux/init.h>     /* Las macros __init y __exit, y module_init/module_exit */
#include <linux/module.h>   /* Requerido por todos los módulos: MODULE_*, module_param */
#include <linux/kernel.h>   /* pr_info(), el "printf" del kernel */
#include <linux/utsname.h>  /* utsname(): hostname y versión del kernel (lo que muestra `uname`) */

/*
 * Metadatos del módulo. No generan código: se guardan en una sección especial
 * (.modinfo) del archivo equipo.ko, y son lo que muestra `modinfo equipo.ko`.
 */

/* Licencia. Con "GPL" el kernel no queda marcado como "propietario" (taint P) y el
 * módulo puede usar las funciones que el kernel exporta solo para código GPL. */
MODULE_LICENSE("GPL");
/* Descripción: aparece en `modinfo` como "description:" */
MODULE_DESCRIPTION("TP4: imprime el nombre del equipo en el log del kernel");
/* Autores: aparece en `modinfo` como "author:" */
MODULE_AUTHOR("Borsotti Bosco, Ciacci, Leguizamon");
/* Versión: aparece en `modinfo` como "version:" */
MODULE_VERSION("1.0");

/*
 * Parámetro del módulo: un valor que se puede cambiar al cargarlo, sin recompilar.
 *   sudo insmod equipo.ko                      -> usa el valor por defecto
 *   sudo insmod equipo.ko grupo='"Otro nombre"' -> usa "Otro nombre"
 *
 * `static` hace que la variable sea privada de este archivo: así no choca con
 * otra variable "grupo" de otro módulo, porque todos comparten el mismo kernel.
 */
static char *grupo = "Borsotti Bosco - Ciacci - Leguizamon";
/* Registra la variable como parámetro:
 *   grupo  -> la variable (y el nombre del parámetro)
 *   charp  -> el tipo: un puntero a texto (char pointer)
 *   0444   -> permisos del archivo /sys/module/equipo/parameters/grupo:
 *             lectura para todos, nadie lo puede modificar */
module_param(grupo, charp, 0444);
/* Descripción del parámetro: aparece en `modinfo` como "parm: grupo:..." */
MODULE_PARM_DESC(grupo, "Integrantes del grupo");

/*
 * Función de entrada: la ejecuta el kernel una sola vez, cuando hacemos `insmod`.
 * Es el equivalente a main() de un programa, pero no se queda corriendo: imprime,
 * devuelve un número y termina. El módulo sigue cargado igual.
 *
 * static  -> la función no es visible para el resto del kernel (no ensucia el espacio
 *            de nombres y evita la advertencia "no previous prototype" de mimodulo.c)
 * __init  -> código que se usa una sola vez: el kernel libera su memoria después de
 *            cargar el módulo (por eso equipo_init no aparece en /proc/kallsyms)
 */
static int __init equipo_init(void)
{
	/*
	 * pr_info() escribe en el registro del kernel (ring buffer) con prioridad
	 * "información". No va a la terminal: se lee con `sudo dmesg`.
	 * Empezamos cada mensaje con "equipo:" para encontrarlo fácil en dmesg.
	 *
	 * utsname() devuelve la misma estructura que lee el comando `uname`:
	 *   ->nodename  el nombre del equipo (hostname), ej. "nico-HP-Laptop-15-ef2xxx"
	 *   ->release   la versión del kernel, ej. "7.0.0-38-generic"
	 * La leemos directo de la memoria del kernel, sin ninguna syscall.
	 */
	pr_info("equipo: modulo cargado en el equipo '%s' (kernel %s)\n",
		utsname()->nodename, utsname()->release);
	/* Imprime el valor del parámetro: el de por defecto o el que se pasó en insmod */
	pr_info("equipo: grupo: %s\n", grupo);

	/*
	 * Devolver 0 indica una carga correcta. Si devolviera un número negativo
	 * (un código de error, como -ENOMEM), insmod fallaría y el módulo no quedaría
	 * cargado.
	 */
	return 0;
}

/*
 * Función de salida: la ejecuta el kernel cuando hacemos `rmmod equipo`, justo
 * antes de sacar el módulo de memoria. Acá se libera todo lo que se haya pedido en
 * init (en este módulo no pedimos nada, solo nos despedimos).
 *
 * void    -> no devuelve nada: descargar no puede "fallar"
 * __exit  -> código que solo hace falta para descargar el módulo
 */
static void __exit equipo_exit(void)
{
	pr_info("equipo: modulo descargado del equipo '%s'\n",
		utsname()->nodename);
}

/*
 * Le dicen al kernel cuál es la función de entrada y cuál la de salida.
 * Sin estas dos líneas, el kernel no sabría qué ejecutar en insmod y rmmod.
 */
module_init(equipo_init);
module_exit(equipo_exit);
