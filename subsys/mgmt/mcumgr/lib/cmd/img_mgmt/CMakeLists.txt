#
# Copyright (c) 2018-2021 mcumgr authors
#
# SPDX-License-Identifier: Apache-2.0
#

target_include_directories(MCUMGR INTERFACE
    include
)

zephyr_library_sources(
    src/img_mgmt.c
    src/img_mgmt_state.c
    src/img_mgmt_util.c
    src/img_mgmt_priv.c
)

if(CONFIG_MCUBOOT_IMG_MANAGER)
    zephyr_library_link_libraries(MCUBOOT_BOOTUTIL)
endif()
