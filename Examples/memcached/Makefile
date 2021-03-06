# Build Memcached as follows:
#
# - make               -- create non-SGX no-debug-log manifest
# - make SGX=1         -- create SGX no-debug-log manifest
# - make SGX=1 DEBUG=1 -- create SGX debug-log manifest
#
# Any of these invocations downloads Memcached and builds Memcached in default
# configuration.
#
# Use `make clean` to remove Graphene-generated files and `make distclean` to
# additionally remove the Memcached source.

################################# CONSTANTS ###################################

# Relative path to Graphene root
GRAPHENEDIR = ../..

SRCDIR = src
MEMCACHED_URL ?= https://memcached.org/files/memcached-1.5.21.tar.gz
MEMCACHED_SHA256 ?= e3d10c06db755b220f43d26d3b68d15ebf737a69c7663529b504ab047efe92f4

ifeq ($(DEBUG),1)
GRAPHENEDEBUG = inline
else
GRAPHENEDEBUG = none
endif

.PHONY=all
all: memcached memcached.manifest pal_loader
ifeq ($(SGX),1)
all: memcached.manifest.sgx
endif

include ../../Scripts/Makefile.configs

############################ MEMCACHED EXECUTABLE #############################

# Memcached is built as usual, without any changes to the build process. The
# source is downloaded from memcached.org and built via classic ./configure &&
# make. The result of this build process is the final executable
# "src/memcached".

$(SRCDIR)/configure:
	$(GRAPHENEDIR)/Scripts/download --output memcached.tar.gz \
		--sha256 $(MEMCACHED_SHA256) --url $(MEMCACHED_URL)
	mkdir $(SRCDIR)
	tar -C $(SRCDIR) --strip-components=1 -xf memcached.tar.gz

$(SRCDIR)/memcached: $(SRCDIR)/configure
	cd $(SRCDIR) && ./configure
	cd $(SRCDIR) && make

############################## MEMCACHED MANIFEST #############################

# The template file contains almost all necessary information to run Memcached
# under Graphene / Graphene-SGX. We create memcached.manifest (to be run under
# non-SGX Graphene) by simply replacing variables in the template file via sed.

memcached.manifest: memcached.manifest.template
	sed -e 's|$$(GRAPHENEDIR)|'"$(GRAPHENEDIR)"'|g' \
		-e 's|$$(GRAPHENEDEBUG)|'"$(GRAPHENEDEBUG)"'|g' \
		-e 's|$$(ARCH_LIBDIR)|'"$(ARCH_LIBDIR)"'|g' \
		$< > $@

# Manifest for Graphene-SGX requires special "pal-sgx-sign" procedure. This
# procedure measures all Memcached dependencies (shared libraries and trusted
# files), measures Memcached code/data pages, and adds measurements in the
# resulting manifest.sgx file (among other, less important SGX options).
#
# Additionally, Graphene-SGX requires EINITTOKEN and SIGSTRUCT objects (see
# SGX hardware ABI, in particular EINIT instruction). The "pal-sgx-get-token"
# script generates these objects and puts them in files .token and .sig
# respectively. Note that filenames must be the same as the executable/manifest
# name (i.e., "memcached").

memcached.manifest.sgx: memcached.manifest $(SRCDIR)/memcached
	$(GRAPHENEDIR)/Pal/src/host/Linux-SGX/signer/pal-sgx-sign \
		-libpal $(GRAPHENEDIR)/Runtime/libpal-Linux-SGX.so \
		-key $(GRAPHENEDIR)/Pal/src/host/Linux-SGX/signer/enclave-key.pem \
		-manifest $< -output $@ \
		-exec $(SRCDIR)/memcached
	$(GRAPHENEDIR)/Pal/src/host/Linux-SGX/signer/pal-sgx-get-token \
		-output memcached.token -sig memcached.sig

########################### COPIES OF EXECUTABLES #############################

# Memcached build process creates the final executable as src/memcached. For
# simplicity, copy it into our root directory.
# Also, create a link to pal_loader for simplicity.

memcached: $(SRCDIR)/memcached
	cp $< $@

pal_loader:
	ln -s $(GRAPHENEDIR)/Runtime/pal_loader $@

################################## CLEANUP ####################################

.PHONY=clean
clean:
	$(RM) *.token *.sig *.manifest.sgx *.manifest pal_loader memcached

.PHONY=distclean
distclean: clean
	$(RM) -r $(SRCDIR) memcached.tar.gz
