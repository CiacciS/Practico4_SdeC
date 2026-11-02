# Este Makefile no compila nada por sí mismo: le pasa el trabajo al sistema de
# compilación del kernel (kbuild), que sabe con qué opciones compilar un módulo.

# "obj-m" = compilar equipo.c como Módulo (equipo.ko), no meterlo dentro del kernel
obj-m += equipo.o

# Dónde están los headers del kernel que está corriendo (`uname -r` da la versión).
# Se puede cambiar: make KDIR=/otra/ruta
KDIR ?= /lib/modules/$(shell uname -r)/build

# `make`: -C entra a los headers del kernel y M= le dice dónde está nuestro módulo
all:
	make -C $(KDIR) M=$(PWD) modules

# `make clean`: borra todo lo generado (.ko, .o, .mod.c, ...)
clean:
	make -C $(KDIR) M=$(PWD) clean

# `make firmar`: compila y firma el .ko con la clave MOK (ver Parte 3 de GUIA.md).
# Solo hace falta con Secure Boot activo.
firmar: all
	../../scripts/firmar.sh equipo.ko
