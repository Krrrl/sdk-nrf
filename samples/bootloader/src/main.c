/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <pm_config.h>
#include <fw_info.h>
#include <fprotect.h>
#include <bl_storage.h>
#include <bl_boot.h>
#include <bl_validation.h>
#include <nrfx_nvmc.h>

#if defined(CONFIG_HW_UNIQUE_KEY_LOAD)
#include <zephyr/init.h>
#include <hw_unique_key.h>

#define HUK_FLAG_OFFSET 0xFFC /* When this word is set, expect HUK to be written. */

int load_huk(void)
{
	if (!hw_unique_key_is_written(HUK_KEYSLOT_KDR)) {
		uint32_t huk_flag_addr = PM_HW_UNIQUE_KEY_PARTITION_ADDRESS + HUK_FLAG_OFFSET;

		if (*(uint32_t *)huk_flag_addr == 0xFFFFFFFF) {
			printk("First boot, expecting app to write HUK.\n");
			nrfx_nvmc_word_write(huk_flag_addr, 0);
			return 0;
		}
		printk("Error: Hardware Unique Key not present.\n");
		k_panic();
		return -1;

	}

	hw_unique_key_load_kdr();

	return 0;
}

SYS_INIT(load_huk, PRE_KERNEL_2, 0);
#endif


static void validate_and_boot(const struct fw_info *fw_info, uint16_t slot)
{
	printk("Attempting to boot slot %d.\r\n", slot);

	if (fw_info == NULL) {
		printk("No fw_info struct found.\r\n");
		return;
	}

	printk("Attempting to boot from address 0x%x.\n\r",
		fw_info->address);

	if (!bl_validate_firmware_local(fw_info->address,
					fw_info)) {
		printk("Failed to validate, permanently invalidating!\n\r");
		fw_info_invalidate(fw_info);
		return;
	}

	printk("Firmware version %d\r\n", fw_info->version);

	uint16_t stored_version;
	int err = get_monotonic_version(&stored_version);

	if (err) {
		printk("Failed to read the monotonic counter!\n\r");
		printk("We assume this is due to the firmware version not being enabled.\n\r");

		/*
		 * Errors in reading the firmware version are assumed to be
		 * due to the firmware version not being enabled. When the
		 * firmware version is disabled, no version checking should
		 * be done. The version is then set to 0 as it is not permitted
		 * in fwinfo and will therefore pass all version checks.
		 */
		stored_version = 0;
	}

	if (fw_info->version > stored_version) {
		int err = set_monotonic_version(fw_info->version, slot);

		if (err) {
			/*
			 * Errors in writing the firmware version are assumed to be
			 * due to the firmware version not being enabled. When the
			 * firmware version is disabled, no version updates should
			 * be done and this case can be ignored.
			 *
			 * The body of this if-statement is intentionally empty.
			 * It is left here solely for documentation purposes,
			 * describing why we ignore the error.
			 */
		}
	}

	bl_boot(fw_info);
}

#define BOOT_SLOT_0 0
#define BOOT_SLOT_1 1

int main(void)
{
	int err = fprotect_area(PM_B0_ADDRESS, PM_B0_SIZE);

	if (err) {
		printk("Failed to protect B0 flash, cancel startup.\n\r");
		return 0;
	}

	uint32_t s0_addr = s0_address_read();
	uint32_t s1_addr = s1_address_read();
	const struct fw_info *s0_info = fw_info_find(s0_addr);
	const struct fw_info *s1_info = fw_info_find(s1_addr);

	if (!s1_info || (s0_info->version >= s1_info->version)) {
		validate_and_boot(s0_info, BOOT_SLOT_0);
		validate_and_boot(s1_info, BOOT_SLOT_1);
	} else {
		validate_and_boot(s1_info, BOOT_SLOT_1);
		validate_and_boot(s0_info, BOOT_SLOT_0);
	}

	printk("No bootable image found. Aborting boot.\n\r");
	return 0;
}
