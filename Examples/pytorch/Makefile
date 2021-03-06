# Build PyTorch as follows:
#
# - make               -- create non-SGX no-debug-log manifest
# - make SGX=1         -- create SGX no-debug-log manifest
# - make SGX=1 DEBUG=1 -- create SGX debug-log manifest
#
# PyTorch and the pre-trained model must be installed on the system.
# See README for details.
#
# Use `make clean` to remove Graphene-generated files.

GRAPHENEDIR ?= ../..

ifeq ($(DEBUG),1)
GRAPHENEDEBUG = inline
else
GRAPHENEDEBUG = none
endif

DISTRIB_ID ?= $(shell lsb_release --short --id)
DISTRIB_RELEASE ?= $(shell lsb_release --short --release)

ifeq ($(SGX), 1)
ifneq ($(DISTRIB_ID),Ubuntu)
$(error This example requires Ubuntu when building for SGX.)
endif
endif

UBUNTU_VERSION = $(DISTRIB_ID)$(DISTRIB_RELEASE)

.PHONY: default
ifeq ($(SGX),1)
default: pytorch.token python3.token pal_loader
else
default: pytorch.manifest python3.manifest pal_loader
endif

include ../../Scripts/Makefile.configs

pytorch.sig: python3.sig

# .manifest.template contains stanzas for both Ubuntu 16 and 18. The last rule selectively enables
# those lines based on the Ubuntu version detected.
%.manifest: %.manifest.template
	sed -e 's|$$(GRAPHENEDIR)|'"$(GRAPHENEDIR)"'|g' \
		-e 's|$$(GRAPHENEDEBUG)|'"$(GRAPHENEDEBUG)"'|g' \
		-e 's|$$(HOME)|'"$(HOME)"'|g' \
		-e 's|# '"$(UBUNTU_VERSION)"' ||g' \
		-e 's|$$(ARCH_LIBDIR)|'"$(ARCH_LIBDIR)"'|g' \
		$< > $@

pytorch.sig: pytorch.manifest
	$(GRAPHENEDIR)/Pal/src/host/Linux-SGX/signer/pal-sgx-sign \
		-libpal $(GRAPHENEDIR)/Runtime/libpal-Linux-SGX.so \
		-key $(GRAPHENEDIR)/Pal/src/host/Linux-SGX/signer/enclave-key.pem \
		-output pytorch.manifest.sgx -exec /usr/bin/python3 -manifest $<

%.token: %.sig
	$(GRAPHENEDIR)/Pal/src/host/Linux-SGX/signer/pal-sgx-get-token -output $@ -sig $<

# The PyTorch workload forks/execve()s at some point. This rule generates the required additional
# manifest for the child process.
python3.manifest: pytorch.manifest
	cp $< $@
	sed -i '/trusted_children/d' $@

python3.sig: python3.manifest
	$(GRAPHENEDIR)/Pal/src/host/Linux-SGX/signer/pal-sgx-sign \
		-libpal $(GRAPHENEDIR)/Runtime/libpal-Linux-SGX.so \
		-key $(GRAPHENEDIR)/Pal/src/host/Linux-SGX/signer/enclave-key.pem \
		-output python3.manifest.sgx -exec /usr/bin/python3 -manifest $<

pal_loader:
	ln -s $(GRAPHENEDIR)/Runtime/pal_loader $@

.PHONY: clean
clean:
	$(RM) *.token *.sig *.manifest.sgx *.manifest pal_loader

.PHONY: distclean
distclean: clean
