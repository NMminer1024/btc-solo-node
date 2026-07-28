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
	echo -e "CONF:${CONF%_defconfig}" >> "${rootfs_dir}/${ver_file}"
	echo -e "##############################################################" >> "${rootfs_dir}/${ver_file}"
	echo "build version: ${sdk_ver}"

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

	if $(cat ${config} | grep BR2_PACKAGE_RTL8189FS=y >/dev/null 2>&1); then
		echo " modprobe 8189fs " >> "${auto_boot_f}"
	fi

	if $(cat ${config} | grep BR2_PACKAGE_BCMDHD=y >/dev/null 2>&1); then
		echo " modprobe bcmdhd " >> "${auto_boot_f}"
	fi

	if $(cat ${config} | grep BR2_PACKAGE_RTL8733BS=y >/dev/null 2>&1); then
		echo " modprobe 8733bs " >> "${auto_boot_f}"
	fi

	if $(cat ${config} | grep BR2_PACKAGE_AIC8800=y >/dev/null 2>&1); then
		echo " modprobe aic_load_fw " >> "${auto_boot_f}"
		echo " modprobe aic8800_fdrv " >> "${auto_boot_f}"
		echo " modprobe aic_btusb " >> "${auto_boot_f}"
	fi

	if $(cat ${config} | grep BR2_PACKAGE_AIC8800_SDIO=y >/dev/null 2>&1); then
		echo 'modprobe  aic8800_bsp.ko  aic_fw_path="/lib/firmware/aic8800D80"' >> "${auto_boot_f}"
		echo "modprobe aic8800_fdrv.ko aicwf_dbg_level=1" >> "${auto_boot_f}"
	fi

	if [ ! -z "$BR2_CANAAN_AUTO_RUN_CMD" ]; then
		echo "$BR2_CANAAN_AUTO_RUN_CMD" >> "${auto_boot_f}"
	fi

	chmod a+x "${auto_boot_f}"
}

gen_version
auto_boot_proc
prepare_block_seed_mountpoint
[ -f "${STAGING_DIR}/lib/libasan.so.8" ] && cp "${STAGING_DIR}/lib/libasan.so.8" "${TARGET_DIR}/lib/"
# Remove non-essential init scripts (mining-only K230)
rm -f "${TARGET_DIR}/etc/init.d/S50telnet"
rm -f "${TARGET_DIR}/etc/init.d/S31canaan_isp"
rm -f "${TARGET_DIR}/etc/init.d/S41adb_mtp"
cd ${TARGET_DIR}; rm -rf app; ln -s root/app app; cd -
