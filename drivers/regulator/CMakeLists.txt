# Copyright 2020 Peter Bigot Consulting, LLC
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_REGULATOR_FIXED regulator_fixed.c)
zephyr_library_sources_ifdef(CONFIG_REGULATOR_PMIC regulator_pmic.c)
zephyr_library_sources_ifdef(CONFIG_REGULATOR_SHELL regulator_shell.c)
