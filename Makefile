include Scripts/Makefile.configs

targets = all clean test sgx-tokens distclean

ifneq ($(filter sgx-tokens,$(MAKECMDGOALS)),)
ifneq ($(SGX),1)
$(error "The 'sgx-tokens' target requires SGX=1")
endif
endif

.PHONY: $(targets)
$(targets):
	$(MAKE) -C LibOS $@
	$(MAKE) -C Scripts $@
	$(MAKE) -C Tools $@
	@echo
	@echo NOTE: We are in the middle of a transition to the Meson buildsystem.
	@echo You have successfully built part of Graphene, now please compile the rest and install
	@echo using Meson. See https://graphene.readthedocs.io/en/latest/building.html for more details.
	@echo
