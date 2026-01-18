# --- Variables ---
# Use ?= to allow environment overrides

KVER ?= $(shell uname -r)
KDIR	?= /lib/modules/$(KVER)/build
PWD	:= $(shell pwd)
MDIR	?=

ROOT_DIR :=$(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
# does not work export KBUILD_EXTMOD_OUTPUT = /tmp/build

# --- Module Configurations ---
MODDEFS := CONFIG_DVB_CORE=m \
	   CONFIG_DVB_TBSECP3=m \
	CONFIG_TBS_PCIE_CI=m \
	CONFIG_TBS_PCIE_MOD=m \
	CONFIG_DVB_SI2168=m \
	CONFIG_DVB_TAS2101=m \
	CONFIG_DVB_GX1133=m \
	CONFIG_DVB_SI2183=m \
	CONFIG_DVB_CXD2878=m \
	CONFIG_DVB_STV090x=m \
	CONFIG_DVB_STV6110x=m \
	CONFIG_DVB_STV091X=m \
	CONFIG_DVB_STV0299=m \
	CONFIG_DVB_STB6000=m  \
	CONFIG_DVB_STV0288=m \
	CONFIG_DVB_TBSPRIV=m \
	CONFIG_DVB_STID135=m \
	CONFIG_DVB_MN88436=m \
	CONFIG_DVB_M88RS6060=m \
	CONFIG_DVB_STV0910=m \
	CONFIG_DVB_STV6111=m \
	CONFIG_DVB_CX24117=m \
	CONFIG_DVB_MXL58X=m \
	CONFIG_DVB_MXL5XX=m \
	CONFIG_DVB_GX1503=m \
	CONFIG_DVB_MTV23X=m \
	CONFIG_MEDIA_TUNER_SI2157=m \
	CONFIG_MEDIA_TUNER_RDA5816=m \
	CONFIG_MEDIA_TUNER_AV201X=m \
	CONFIG_MEDIA_TUNER_STV6120=m \
	CONFIG_MEDIA_TUNER_R848=m \
	CONFIG_MEDIA_TUNER_TDA18212=m \
	CONFIG_MEDIA_TUNER_TDA8290=m \
	CONFIG_MEDIA_TUNER_TDA18271=m \
	CONFIG_DVB_USB=m \
	CONFIG_DVB_USB_TBSQBOX=m \
	CONFIG_DVB_USB_TBSQBOX2=m \
	CONFIG_DVB_USB_TBSQBOX22=m \
	CONFIG_DVB_USB_TBS5520=m \
	CONFIG_DVB_USB_TBS5230=m \
	CONFIG_DVB_USB_TBS5220=m  \
	CONFIG_DVB_USB_TBS5301=m \
	CONFIG_DVB_USB_TBS5520se=m \
	CONFIG_DVB_USB_TBS5580=m \
	CONFIG_DVB_USB_TBS5590=m \
	CONFIG_DVB_USB_TBS5880=m \
	CONFIG_DVB_USB_TBS5881=m \
	CONFIG_DVB_USB_TBS5922SE=m \
	CONFIG_DVB_USB_TBS5925=m \
	CONFIG_DVB_USB_TBS5927=m \
	CONFIG_DVB_USB_TBS5930=m \
	CONFIG_DVB_USB_TBS5931=m \
	CONFIG_DVB_USB_TBS5530=m CONFIG_DVB_NET=y

EXTRA_CFLAGS += --include=$(PWD)/include/kernel_compat.h
EXTRA_CFLAGS += -I$(PWD)/include \
                -I$(PWD)/include/linux \
                -I$(PWD)/dvb-core \
                -I$(PWD)/frontends \
                -I$(PWD)/stid135 \
                -I$(PWD)/tuners

EXTRA_CFLAGS += -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
# --- Targets ---
.PHONY: all install clean dep

# Build modules using Kbuild M= syntax
all:
	$(MAKE) $(ROOT_DIR)/include/linux/dvb/neumo-frontend.h $(ROOT_DIR)/include/linux/dvb/neumo-dmx.h
	$(MAKE) -C $(KDIR) M=$(PWD) $(MODDEFS) NOSTDINC_FLAGS="$(EXTRA_CFLAGS)" modules

$(ROOT_DIR)/include/linux/dvb/neumo-frontend.h: $(ROOT_DIR)/templates/common-frontend.h
	sed -s -e 's/common_//g'  -e 's/neumo_//g'  -e 's/COMMON_//g' \
	-e 's/NEUMO_//g' < $< > $@

$(ROOT_DIR)/include/linux/dvb/neumo-dmx.h: $(ROOT_DIR)/templates/common-dmx.h
	sed -s -e 's/common_//g'  -e 's/neumo_//g'  -e 's/COMMON_//g' \
	-e 's/NEUMO_//g' < $< > $@

dep:
	$(MAKE) -C $(KDIR) M=$(PWD) dep

install: all
	$(MAKE) -C $(KDIR) M=$(PWD) INSTALL_MOD_PATH=$(MDIR) modules_install
	@echo "Updating module dependencies..."
	@if [ -n "$(MDIR)" ]; then \
		depmod -b "$(MDIR)" -a $(KVER); \
	else \
		depmod -a $(KVER); \
	fi

clean:
	@echo "Cleaning up..."
	@find . -type f -name "*.o" -o -name "*.ko" -o -name "*.mod" -o \
	              -name "*.mod.c" -o -name ".*.cmd" -o -name ".*.o.d" -o \
	              -name "*.order" -o -name "*.dwo" -o -name "modules.order" -o \
	              -name "Module.symvers" | xargs rm -f
	@rm -rf .tmp_versions
