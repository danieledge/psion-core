# psihold.exe: keeps the Psion's Internet connection up after use (see
# psihold.cpp and psilink.h). Every program's build makes it and installs it
# in that program's own folder; whichever brings the link up starts one.
#   make -f core/ssh/psihold.mk PSION_SDK=... TOP=<program's checkout>
#                                            -> $(TOP)/build/psihold/psihold.exe
# (TOP defaults to the folder above this core checkout, which is the
# program's own when core is its submodule.)
# Plain EUSER/EFSRV/NIFMAN: no ESTLIB, so a few KB.
CORE    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
TOP     ?= $(abspath $(CORE)/..)
O       ?= $(TOP)/build/psihold
SDK     ?= $(PSION_SDK)
E       := $(SDK)/epoc_cpp_sdk/epoc32
REL     := $(E)/release/marm/rel
export PATH := /usr/bin:/bin:$(SDK)/gcc-3.0-psion-98r2-9/bin:$(E)/tools
export WINEDEBUG := -all
export TMP := /tmp/
CC      := arm-pe-gcc
ARCH    := -s -fomit-frame-pointer -O2 -fno-strict-aliasing -nostdinc -mcpu=arm710 -mapcs-32 -mshort-load-bytes -msoft-float
DEFS    := -D__SYMBIAN32__ -D__PSISOFT32__ -D__GCC32__ -D__EPOC32__ -D__MARM__ -D__EXE__ -DNDEBUG
CXXFLAGS := $(ARCH) $(DEFS) -Wno-ctor-dtor-privacy -fcheck-new -fvtable-thunks -w -I$(CORE)/ssh -I$(E)/include
V4BX_DIR := $(O)
include $(CORE)/tools/v4bx.mk
SYSLIBS := $(REL)/euser.lib $(REL)/efsrv.lib $(REL)/nifman.lib $(LIBGCC)
OUT     := $(O)/psihold.exe

all: $(OUT)

$(O)/psihold.o: $(CORE)/ssh/psihold.cpp $(CORE)/ssh/psilink.h
	@mkdir -p $(dir $@)
	$(CC) -c $(CXXFLAGS) $< -o $@

LINKOBJS = $(REL)/eexe.o $(O)/psihold.o $(SYSLIBS)
# uid3 0x01000A86; heap 4 KB to 64 KB, 8 KB stack
$(OUT): $(O)/psihold.o $(LIBGCC)
	arm-pe-ld -s -e _E32Startup --base-file $(O)/psihold.bas -o $(O)/psihold.tmp.exe $(LINKOBJS)
	arm-pe-dlltool --as=arm-pe-as --output-exp $(O)/psihold.exp --base-file $(O)/psihold.bas $(LINKOBJS)
	arm-pe-ld -s -e _E32Startup -Map $(O)/psihold.map -o $(O)/psihold.tmp.exe $(O)/psihold.exp $(LINKOBJS)
	wine $(E)/tools/petran.exe $(O)/psihold.tmp.exe $@ -nocall -uid1 0x1000007a -uid2 0x00000000 -uid3 0x01000A86 -heap 0x1000 0x10000 -stack 0x2000
	@ls -la $@

.PHONY: all
