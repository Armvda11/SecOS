#!/usr/bin/make -f

include ../utils/config.mk

# 1. On localise libgcc
LGCC := $(shell $(CC) -print-libgcc-file-name)

# 2. On l'ajoute aux options du linker (et PAS aux objets)
LDFLAGS += $(LGCC)

# 3. Tes objets à toi
objects += tp.o

include ../utils/rules.mk
