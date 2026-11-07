# Developer entry points. This file is an ALIAS LAYER, not a build system:
# every target is .PHONY and shells out to the tool that actually knows about
# dependencies (ninja/cmake for code, the content-hash cache in
# tools/vms/vm-install.sh for packages). Adding file rules here would create a
# second dependency store, and the last time two of them disagreed (mtimes vs
# the fingerprint) the grid tested a package nobody had built. So: no
# prerequisites, no stamps, no caching in this file.
#
# The VM is a variable, not a target name:  make e2e VM=ubuntu-gnome
# Targets say what they cost in their own help line.

VM    ?= arch-kde
PY    ?= .venv/bin/python
ARGS  ?=
VMT   := ./tools/vms/vmctl.sh

.DEFAULT_GOAL := help

.PHONY: help
help: ## list the targets (this is the default)
	@echo "traduku - one entry point, the work is done by the tools below"
	@grep -hE '^[a-z-]+:.*## ' $(MAKEFILE_LIST) \
	  | awk -F':.*## ' '{printf "  \033[1m%-12s\033[0m%s\n", $$1, $$2}'
	@echo
	@echo "  VM=$(VM) selects the grid profile (arch-kde debian-kde debian-gnome"
	@echo "  ubuntu-kde ubuntu-gnome). VM= is also what 'clean' leaves alone."
	@echo
	@echo "  'install' means the HOST prefix here. Installing the package into a"
	@echo "  guest is 'make guest' - the two have different Depends, different"
	@echo "  shells and different evidence, so they do not share a name."

# ---------------------------------------------------------------- host side

.PHONY: build
build: ## compile both shells (kde + gnome), ~1 min cold, seconds warm
	cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
	cmake --build build
	cmake -B build-gnome -G Ninja -DCMAKE_BUILD_TYPE=Release -DLT_SHELL=gnome >/dev/null
	cmake --build build-gnome

.PHONY: test
test: ## ctest in both shells: the gate before any commit
	ctest --test-dir build --output-on-failure
	ctest --test-dir build-gnome --output-on-failure

.PHONY: install
install: ## build + ctest + install to $HOME/.local (the HOST, not a VM)
	./install.sh

.PHONY: selftest
selftest: ## run the app's own headless self-check on this machine
	QT_QPA_PLATFORM=offscreen ./build/src/app/kde/traduku --selftest

# ---------------------------------------------------------------- guest side

.PHONY: check
check: ## is the VM a machine a user could have? (session, DE, package manager)
	./tools/vms/check-grid.sh $(VM)

.PHONY: guest
guest: ## package for the VM's distro -> install -> gate (ARGS=--rebuild)
	./tools/vms/vm-install.sh $(VM) $(ARGS)

.PHONY: e2e
e2e: ## run the GUI grid tests against the VM; installs nothing on purpose
	TRADUKU_VM=$(VM) $(PY) -u -m pytest tests/e2e -v -rA --color=no $(ARGS)

.PHONY: cycle
cycle: ## the one command: package -> guest -> full e2e, timings included
	$(MAKE) --no-print-directory guest VM=$(VM)
	$(MAKE) --no-print-directory e2e VM=$(VM)

.PHONY: up down scratch reset
up:      ## start the VM writing to its golden disk (how it gets installed)
	$(VMT) start $(VM)
down:    ## power off the VM
	$(VMT) stop $(VM)
scratch: ## start the VM from a throwaway overlay: a trashed guest costs a reset
	$(VMT) start $(VM) scratch
reset:   ## delete that overlay - back to the post-install state
	$(VMT) stop $(VM) 2>/dev/null || true
	$(VMT) reset $(VM)

.PHONY: ssh shot
ssh:  ## a shell inside the VM
	$(VMT) ssh $(VM) 'exec bash -i'
shot: ## QMP screenshot of the VM (frames land in ~/vms/run/<vm>/shots/)
	$(VMT) shot $(VM)

# ------------------------------------------------------------------ cleanup

.PHONY: clean
clean: ## remove build trees and test caches (keeps packages and the guest)
	rm -rf build build-gnome .pytest_cache tests/e2e/__pycache__ __pycache__
	find . -name '*.pyc' -delete

.PHONY: distclean
distclean: clean ## clean + everything a build produced, including the .deb cache
	rm -rf release-out .cache
