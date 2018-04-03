# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include common.mk

### Rules to generate the mojom header and source files.

GEN_MOJO_TEMPLATES_DIR := $(OUT)/mojo/templates
MOJOM_BINDINGS_GENERATOR := \
	$(SYSROOT)/usr/src/libmojo-$(BASE_VER)/mojo/mojom_bindings_generator.py
MOJOM_FILES := \
	mojo/cros_camera_service.mojom \
	mojo/camera3.mojom \
	mojo/camera_common.mojom \
	mojo/camera_metadata.mojom \
	mojo/camera_metadata_tags.mojom \
	mojo/algorithm/camera_algorithm.mojom \
	mojo/jda/geometry.mojom \
	mojo/jda/jpeg_decode_accelerator.mojom \
	mojo/jda/time.mojom

mojo/mojo_templates:
	$(QUIET)echo generate_mojo_templates: $(GEN_MOJO_TEMPLATES_DIR)
	$(QUIET)rm -rf $(GEN_MOJO_TEMPLATES_DIR)
	$(QUIET)mkdir -p $(GEN_MOJO_TEMPLATES_DIR)
	$(QUIET)python $(MOJOM_BINDINGS_GENERATOR) \
		--use_bundled_pylibs precompile -o $(GEN_MOJO_TEMPLATES_DIR)
	cd $(SRC) && \
		python $(abspath $(MOJOM_BINDINGS_GENERATOR)) \
		--use_bundled_pylibs generate \
		$(MOJOM_FILES) \
		-I $(SRC) \
		-o $(SRC) \
		--bytecode_path $(abspath $(GEN_MOJO_TEMPLATES_DIR)) \
		-g c++

clean: CLEAN($(patsubst %,%.h,$(MOJOM_FILES)))
clean: CLEAN($(patsubst %,%.cc,$(MOJOM_FILES)))
clean: CLEAN($(patsubst %,%-internal.h,$(MOJOM_FILES)))

.PHONY: mojo/mojo_templates