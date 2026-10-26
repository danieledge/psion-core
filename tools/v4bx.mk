# tools/v4bx.mk - LIBGCC for an engine's link: the SDK's libgcc.a with every
# BX made MOV PC (tools/v4bx.py explains), so that gcc's helpers (64-bit
# multiply, divide and shifts, soft float) also run on the netBook's SA-1100,
# which has no Thumb and no BX. Set SDK and V4BX_DIR (the build folder) before
# including this, and list $(LIBGCC) among the program's prerequisites.
V4BX_PY     := $(dir $(lastword $(MAKEFILE_LIST)))v4bx.py
V4BX_GOAL   := $(.DEFAULT_GOAL)
LIBGCC_SDK  := $(SDK)/gcc-3.0-psion-98r2-9/lib/gcc-lib/arm-epoc-pe/3.0-psion-98r2/libgcc.a
LIBGCC      := $(V4BX_DIR)/libgcc-v4.a

$(LIBGCC): $(LIBGCC_SDK) $(V4BX_PY)
	@mkdir -p $(dir $@)
	python3 $(V4BX_PY) $< $@

# (this rule is not the including Makefile's default goal)
.DEFAULT_GOAL := $(V4BX_GOAL)
