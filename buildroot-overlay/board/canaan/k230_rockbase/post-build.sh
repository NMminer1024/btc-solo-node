#!/bin/bash

#BINARIES_DIR=/home/wangjianxin/k230_linux_sdk/output/k230_canmv_defconfig/images
#BUILDROOT_DIR=${PWD}
set -e
rootfs_dir="$1"

UBOOT_BUILD_DIR=${BUILD_DIR}/uboot-2022.10
K230_SDK_ROOT=$(dirname $(dirname ${BASE_DIR}))
GENIMAGE_CFG_SD=$(dirname $(realpath "$0"))/genimage.cfg

BR2_ROOTFS_OVERLAY=$(cat ${BASE_DIR}/.config | grep BR2_ROOTFS_OVERLAY | cut -d= -f2 | tr -d '"')

BR_OVERLAY_DIR=${K230_SDK_ROOT}/buildroot-overlay
BLOCK_SEED_DIR_DEFAULT="${K230_SDK_ROOT}/seed/root-block"
BLOCK_SEED_DIR="${BLOCK_SEED_DIR:-${ROCKBASE_BLOCK_SEED_DIR:-${BLOCK_SEED_DIR_DEFAULT}}}"

cleanup_block_seed_runtime_files()
{
	local target_block_dir="$1"

	[ -d "${target_block_dir}" ] || return 0

	find "${target_block_dir}" -type f \
		\( -name ".lock" -o -name ".cookie" -o -name "bitcoind.pid" -o -name "LOCK" \) \
		-delete 2>/dev/null || true
	find "${target_block_dir}" -type s -delete 2>/dev/null || true
}

validate_leveldb_manifest()
{
	local db_dir="$1"
	local label="$2"
	local current_file
	local manifest_file

	current_file="${db_dir}/CURRENT"

	if [ ! -f "${current_file}" ]; then
		echo "ERROR: block seed ${label} is missing CURRENT: ${current_file}"
		exit 1
	fi

	manifest_file=$(tr -d '\r\n' < "${current_file}")
	if [ -z "${manifest_file}" ]; then
		echo "ERROR: block seed ${label} CURRENT is empty: ${current_file}"
		exit 1
	fi

	if [ ! -f "${db_dir}/${manifest_file}" ]; then
		echo "ERROR: block seed ${label} manifest is missing: ${db_dir}/${manifest_file}"
		exit 1
	fi
}

prepare_block_seed_mountpoint()
{
	local seed_dir="${BLOCK_SEED_DIR%/}"
	local target_block_dir="${TARGET_DIR}/root/block"

	rm -rf "${target_block_dir}"
	mkdir -p "${target_block_dir}"

	[ -n "${seed_dir}" ] || return 0
	if [ ! -e "${seed_dir}" ]; then
		echo "block seed: partition source skip (path not found: ${seed_dir})"
		return 0
	fi

	if [ ! -d "${seed_dir}" ]; then
		echo "ERROR: block seed path is not a directory: ${seed_dir}"
		exit 1
	fi

	if [ -z "$(find -L "${seed_dir}" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]; then
		echo "block seed: skip (directory is empty: ${seed_dir})"
		return 0
	fi

	if [ ! -d "${seed_dir}/blocks" ] || [ ! -d "${seed_dir}/chainstate" ]; then
		echo "ERROR: block seed must contain blocks/ and chainstate/: ${seed_dir}"
		exit 1
	fi

	if [ ! -d "${seed_dir}/blocks/index" ]; then
		echo "ERROR: block seed must contain blocks/index/: ${seed_dir}"
		exit 1
	fi

	validate_leveldb_manifest "${seed_dir}/chainstate" "chainstate"
	validate_leveldb_manifest "${seed_dir}/blocks/index" "blocks/index"
	echo "block seed: validated for blockdata partition (${seed_dir})"
}

# Generate the build version file inside the target rootfs only.
gen_version()
{
	local ver_file="etc/version/release_version"
	local sdk_ver="unkonwn"
	local CONF=$(basename ${BASE_DIR})

	local nncase_ver="0.0.0"
	local nncase_ver_file="${K230_SDK_ROOT}/output/${CONF}/build/libnncase/nncase/include/nncase/version.h"
	if [ -e "${nncase_ver_file}" ]; then
		cat ${nncase_ver_file} | grep NNCASE_VERSION -w | cut -d\" -f 2 > /dev/null && \
			nncase_ver=$(cat ${nncase_ver_file} | grep NNCASE_VERSION -w | cut -d\" -f 2)
	fi

	# Human-readable product version (MAJOR.MINOR.PATCH), single source of truth
	# is the repo-root VERSION file. See skill rockbase-version for the bump rules.
	local product_ver_file="${K230_SDK_ROOT}/VERSION"
	local product_ver="0.0.0"
	if [ -e "${product_ver_file}" ]; then
		product_ver=$(head -n1 "${product_ver_file}" | tr -d ' \t\r\n')
	fi
	[ -n "${product_ver}" ] || product_ver="0.0.0"

	local tracked_ver_file="${BR_OVERLAY_DIR}/${BR2_ROOTFS_OVERLAY}/${ver_file}"

	set +e; commitid=$(awk -F- '/^sdk:/ { print $6}' "${tracked_ver_file}"); set -e
	set +e; last_tag=$(awk -F- '/^sdk:/ { print $1}' "${tracked_ver_file}" | cut -d: -f2); set -e

	[ "${commitid}" != "" ] || commitid="unkonwn"
	[ "${last_tag}" != "" ] || last_tag="unkonwn"

	git rev-parse --short HEAD && commitid=$(git rev-parse --short HEAD)
	git describe --tags `git rev-list --tags --max-count=1` && last_tag=$(git describe --tags `git rev-list --tags --max-count=1`)
	git describe --tags --exact-match && last_tag=$(git describe --tags --exact-match)

	cd "${TARGET_DIR}"
	mkdir -p etc/version/
	sdk_ver="${last_tag}-$(date "+%Y%m%d-%H%M%S")-$(whoami)-$(hostname)-${commitid}-nncase${nncase_ver}"
	echo -e "#############SDK VERSION######################################" > "${rootfs_dir}/${ver_file}"
	echo -e "sdk:${sdk_ver}" >> "${rootfs_dir}/${ver_file}"
	echo -e "product:v${product_ver}" >> "${rootfs_dir}/${ver_file}"
	echo -e "CONF:${CONF%_defconfig}" >> "${rootfs_dir}/${ver_file}"
	echo -e "##############################################################" >> "${rootfs_dir}/${ver_file}"
	# Plain, single-line readable version for LCD UI / web / OTA check to read
	# without parsing the multi-line sdk banner above.
	echo -n "v${product_ver}" > "${rootfs_dir}/etc/version/product_version"
	echo "build version: ${sdk_ver} (product v${product_ver})"

	cd -
}

auto_boot_proc()
{
	set -x
	local config="${BASE_DIR}/.config"
	local CONF=$(basename ${BASE_DIR})
	local auto_boot_f="${rootfs_dir}/etc/init.d/S40${CONF}"

	cat > "${auto_boot_f}" <<EOF
[ "\$1" = "stop" ] && exit 0

EOF
	BR2_CANAAN_AUTO_RUN_CMD=$(cat ${config} | grep BR2_CANAAN_AUTO_RUN_CMD | cut -d= -f2 | tr -d '"')

	# Unified network image (docs/rockbase-net-unified-image-plan.md):
	# - No unconditional "modprobe 8189fs" here: the runtime selector
	#   /usr/bin/rockbase_netsel.sh (init.d/S37netsel) decides between
	#   USB wired / USB WiFi / SDIO WiFi and only loads 8189fs when the
	#   SDIO path is chosen.
	# - No build-time "unbind sdhci0" here: both SDIO and USB drivers are
	#   always present, so the build cannot know which path the board will
	#   take. The SDIO controller unbind (freeze-mitigation) is now done at
	#   runtime by rockbase_netsel.sh when a USB network path is selected.

	if [ ! -z "$BR2_CANAAN_AUTO_RUN_CMD" ]; then
		echo "$BR2_CANAAN_AUTO_RUN_CMD" >> "${auto_boot_f}"
	fi

	chmod a+x "${auto_boot_f}"
}

# OTA A/B (stage D): each rootfs slot is self-contained — kernel, dtb and the
# opensbi/uboot-head boot blob live in /boot/ of the slot itself, so uboot can
# ext4load them straight from the active slot partition (no shared boot
# partition anymore). Mirrors what gen_boot_ext4() used to pack into boot.ext4.
gen_target_boot()
{
	local first_dtb="$(grep BR2_LINUX_KERNEL_INTREE_DTS_NAME ${BR2_CONFIG} | cut -d / -f2 | tr -d '"' | cut -d ' ' -f1).dtb"
	local boot_dir="${TARGET_DIR}/boot"

	mkdir -p "${boot_dir}"
	cp "${BINARIES_DIR}/Image" "${boot_dir}/Image"
	# real file (not a symlink) so uboot ext4load never has to follow links
	cp "${BINARIES_DIR}/${first_dtb}" "${boot_dir}/k.dtb"
	${UBOOT_BUILD_DIR}/tools/mkimage -A riscv -O linux -T kernel -C none -a 0 -e 0 -n linux \
		-d "${BINARIES_DIR}/fw_jump.bin" "${boot_dir}/fw_jump_add_uboot_head.bin"
}

gen_version
auto_boot_proc
prepare_block_seed_mountpoint
gen_target_boot
[ -f "${STAGING_DIR}/lib/libasan.so.8" ] && cp "${STAGING_DIR}/lib/libasan.so.8" "${TARGET_DIR}/lib/"
# Remove non-essential init scripts (mining-only K230)
rm -f "${TARGET_DIR}/etc/init.d/S50telnet"
rm -f "${TARGET_DIR}/etc/init.d/S31canaan_isp"
rm -f "${TARGET_DIR}/etc/init.d/S41adb_mtp"

# Keep the runtime network selector executable in the final rootfs; some
# overlay sync/build steps preserve the file content but drop the x-bit.
chmod 755 "${TARGET_DIR}/etc/init.d/S37netsel" 2>/dev/null || true
chmod 755 "${TARGET_DIR}/etc/init.d/S38wifi" 2>/dev/null || true
chmod 755 "${TARGET_DIR}/usr/bin/rockbase_netsel.sh" 2>/dev/null || true
chmod 755 "${TARGET_DIR}/usr/bin/rockbase_usb_wifi.sh" 2>/dev/null || true

cd ${TARGET_DIR}; rm -rf app; ln -s root/app app; cd -
