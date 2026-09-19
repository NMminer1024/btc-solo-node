#!/bin/bash
set -e
BUILDROOT_PATH=$(pwd)
#BINARIES_DIR=/home/wangjianxin/k230_linux_sdk/output/k230_canmv_defconfig/images
UBOOT_BUILD_DIR=${BUILD_DIR}/uboot-2022.10
K230_SDK_ROOT=$(dirname $(dirname ${BASE_DIR}))
GENIMAGE_CFG_SD=$(dirname $(realpath "$0"))/genimage.cfg
env_dir=$(dirname $(realpath "$0"))
# WSL 默认导出到 Windows（/mnt/d）；纯 Linux 物理机未显式指定时不导出、镜像保留在项目目录。
# 显式设置 K230_IMAGE_OUT_DIR 时，任何环境都导出到该目录。
K230_IMAGE_OUT_DIR="${K230_IMAGE_OUT_DIR:-}"
K230_IMAGE_GZIP=${K230_IMAGE_GZIP:-0}
BLOCK_SEED_DIR_DEFAULT="${K230_SDK_ROOT}/seed/root-block"
BLOCK_SEED_DIR="${BLOCK_SEED_DIR:-${ROCKBASE_BLOCK_SEED_DIR:-${BLOCK_SEED_DIR_DEFAULT}}}"
K230_BLOCKDATA_SIZE=${K230_BLOCKDATA_SIZE:-20G}
K230_ROCKCFG_SIZE=${K230_ROCKCFG_SIZE:-1G}



#放到post build
gz_file_add_ver()
{
	[ $# -lt 1 ] && return
	local f="$1"
	local CONF=$(basename ${BASE_DIR})

	local sdk_ver="v0.0.0";
	local nncase_ver="2.9.0";

	local sdk_ver_file="${K230_SDK_ROOT}/buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/etc/version/release_version"
	local nncase_ver_file="${K230_SDK_ROOT}/output/${CONF}/build/libnncase/nncase/include/nncase/version.h"


	local storage="$(echo "$f" | sed -nE "s#[^-]*-([^\.]*).*#\1#p")"

	if [ "${CONF}" = "k230_canmv_defconfig" ] ; then
		canaan_site_name="CanMV-K230_V1P0_P1";
	elif [ "${CONF}" = "k230_evb_defconfig" ] ; then
		canaan_site_name="EVB-K230";
	elif [ "${CONF}" = "k230_canmv_lckfb_defconfig" ] ; then
		canaan_site_name="CanMV-K230_LCKFB";
	elif [ "${CONF}" = "k230_rockbase_defconfig" ] ; then
		canaan_site_name="RockBase-K230";
	elif [ "${CONF}" = "k230d_canmv_defconfig" ] ; then
		canaan_site_name="CanMV-K230D";
	elif [ "${CONF}" = "k230d_canmv_junroc_ai_cam_defconfig" ] ; then
		canaan_site_name="CanMV_K230D_JUNROC_AI_CAM";
	else
		canaan_site_name="${CONF%%_defconfig}"	;
	fi



	sdk_ver=$(awk -F- '/^sdk:/ { print $1}' ${sdk_ver_file}  | cut -d: -f2 )
	sdk_ver=${sdk_ver////-}
	sdk_ver=${sdk_ver// /-}

	if [ -e "${nncase_ver_file}" ]; then
		cat ${nncase_ver_file} | grep NNCASE_VERSION -w | cut -d\" -f 2 > /dev/null && \
			nncase_ver=$(cat ${nncase_ver_file} | grep NNCASE_VERSION -w | cut -d\" -f 2)
	fi
	rm -rf  ${canaan_site_name}_linux_${sdk_ver}_nncase_v${nncase_ver}.img.gz;
	ln -s  $f ${canaan_site_name}_linux_${sdk_ver}_nncase_v${nncase_ver}.img.gz;
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

validate_block_seed()
{
	local seed_dir="${1%/}"

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
}

gen_blockdata_ext4()
{
	local image="${BINARIES_DIR}/blockdata.ext4"
	local seed_dir="${BLOCK_SEED_DIR%/}"
	local mkfs_args=(-F -L blockdata -m 0 -O "^64bit" "${image}" "${K230_BLOCKDATA_SIZE}")

	rm -f "${image}"

	if [ -d "${seed_dir}" ] && [ -n "$(find -L "${seed_dir}" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]; then
		validate_block_seed "${seed_dir}"
		echo "blockdata: generating ${image} from ${seed_dir} (${K230_BLOCKDATA_SIZE})"
		fakeroot mkfs.ext4 -d "${seed_dir}" "${mkfs_args[@]}"
	else
		echo "blockdata: generating empty ${image} (${K230_BLOCKDATA_SIZE})"
		fakeroot mkfs.ext4 "${mkfs_args[@]}"
	fi

	chmod a+rw "${image}"
}

# User-config partition (A1 v2): empty 1G ext4, LABEL=config.
# Contents are seeded on first boot by S05config from
# /etc/rockbase-factory-defaults/ (single seeding path, no duplication here).
gen_rockcfg_ext4()
{
	local image="${BINARIES_DIR}/config.ext4"

	rm -f "${image}"
	echo "config: generating empty ${image} (${K230_ROCKCFG_SIZE})"
	fakeroot mkfs.ext4 -F -L config -m 0 -O "^64bit" "${image}" "${K230_ROCKCFG_SIZE}"
	chmod a+rw "${image}"
}


#add_firmHead  xxx.bin  "-n"
#output fn_$1 fa_$1 fs_$1
add_firmHead()
{

	# $(UBOOT_BUILD_DIR)/tools/firmware_gen_no_securiy.py
	local filename="$1"
	#export PATH=\"${PATH#*host\/bin:}
	local firmware_gen="python3  ${UBOOT_BUILD_DIR}/tools/firmware_gen_no_securiy.py "

	if [ $# -ge 2 ]; then
		firmArgs="$2" #add k230 firmware head
		cp ${filename} ${filename}.t; ${firmware_gen}   -i ${filename}.t -o f${firmArgs##-}${filename} ${firmArgs};

	else
		#add k230 firmware head
		firmArgs="-n"; cp ${filename} ${filename}.t; ${firmware_gen}   -i ${filename}.t -o f${firmArgs##-}_${filename} ${firmArgs};
		if [ "${CONFIG_GEN_SECURITY_IMG}" = "y" ];then
			firmArgs="-s";cp ${filename} ${filename}.t;	 ${firmware_gen}   -i ${filename}.t -o f${firmArgs##-}_${filename} ${firmArgs};
			firmArgs="-a";cp ${filename} ${filename}.t;	${firmware_gen}   -i ${filename}.t -o f${firmArgs##-}_${filename} ${firmArgs};
		fi
	fi
	rm -rf  ${filename}.t

}

k230_gzip()
{
	local filename="$1"
	local k230_gzip_tool="${K230_SDK_ROOT}/tools/k230_priv_gzip "
	${k230_gzip_tool} -n8  -f -k ${filename}  ||   ${k230_gzip_tool} -n9 -f -k ${filename} ||  \
	${k230_gzip_tool} -n7 -f -k ${filename}   ||   ${k230_gzip_tool} -n6 -f -k ${filename} || \
	${k230_gzip_tool} -n5 -f -k ${filename}   ||   ${k230_gzip_tool} -n4 -f -k ${filename}
	set -e ;sed -i -e "1s/\x08/\x09/"  ${filename}.gz; set +e;
}

# "-O linux -T firmware  -a ${add} -e ${add} -n ${name}"
# "-n/-a/-s"
#file_gzip_ubootHead_firmHead ${quick_boot_cfg_data_file}  "-O linux -T firmware  -a ${add} -e ${add} -n ${name}"   "-n"
#output fn_ug_xx fa_ug_xx fs_ug_xx;
bin_gzip_ubootHead_firmHead()
{

	local mkimage="${UBOOT_BUILD_DIR}/tools/mkimage"
	local file_full_path="$1"
	local filename=$(basename ${file_full_path})
	local mkimgArgs="$2"
	local firmArgs="$3"

	#[ -f ${file_full_path} ] || (echo ${filename} >${file_full_path} )
	# cd  "${BUILD_DIR}/images/";
	[ "$(dirname ${file_full_path})" == "$(pwd)" ] || cp ${file_full_path} .

	k230_gzip ${filename}

	#add uboot head
	${mkimage} -A riscv -C gzip  ${mkimgArgs} -d ${filename}.gz  ug_${filename} # ${filename}.gzu;

	add_firmHead ug_${filename}
	rm -rf ${filename}  ${filename}.gz ug_${filename}
}
#K230_SDK_ROOT  UBOOT_BUILD_DIR
gen_uboot_bin()
{
	# if [ "${CONFIG_SUPPORT_RTSMART}" = "y" ] && [ "${CONFIG_SUPPORT_LINUX}" != "y" ]; then
    #     CONFIG_MEM_LINUX_SYS_BASE="${CONFIG_MEM_RTT_SYS_BASE}"
    # fi;
	#buildroot-overlay/boot/uboot/u-boot-2022.10-overlay/board/canaan/common/sdk_autoconf.h
	local CONFIG_MEM_LINUX_SYS_BASE=$(cat ${UBOOT_BUILD_DIR}/board/canaan/common/sdk_autoconf.h | grep CONFIG_MEM_LINUX_SYS_BASE | awk '{print $3}')
	mkdir -p "${BINARIES_DIR}/uboot"

	cd ${BINARIES_DIR}/uboot
	cp ${UBOOT_BUILD_DIR}/u-boot.bin  .
	cp ${UBOOT_BUILD_DIR}/spl/u-boot-spl.bin  .


	# "-O linux -T firmware  -a ${add} -e ${add} -n ${name}"
	# "-n/-a/-s"  "-n/-a/-s"
	#fn_ug_xxx
	bin_gzip_ubootHead_firmHead  $("pwd")/u-boot.bin   \
					"-O u-boot -T firmware  -a ${CONFIG_MEM_LINUX_SYS_BASE} -e ${CONFIG_MEM_LINUX_SYS_BASE} -n uboot"

	add_firmHead  u-boot-spl.bin #
	${UBOOT_BUILD_DIR}/tools/endian-swap.py   fn_u-boot-spl.bin  swap_fn_u-boot-spl.bin

	#rm -rf u-boot-spl.bin
}


#生成可用uboot引导的linux版本文件
gen_linux_bin ()
{
	local mkimage="${UBOOT_BUILD_DIR}/tools/mkimage"
	local first_dtb="$(grep BR2_LINUX_KERNEL_INTREE_DTS_NAME ${BR2_CONFIG} | cut -d / -f2 | tr -d '"' |  cut -d ' ' -f1).dtb"
	local CONFIG_MEM_LINUX_SYS_BASE=$(cat ${UBOOT_BUILD_DIR}/board/canaan/common/sdk_autoconf.h | grep CONFIG_MEM_LINUX_SYS_BASE | awk '{print $3}')

	cd  "${BINARIES_DIR}/";

	# local LINUX_SRC_PATH="src/little/linux"
	# local LINUX_DTS_PATH="src/little/linux/arch/riscv/boot/dts/kendryte/${CONFIG_LINUX_DTB}.dts"

	# return ;
	# cd  "${BUILD_DIR}/images/little-core/" ;
	# cpp -nostdinc -I ${K230_SDK_ROOT}/${LINUX_SRC_PATH}/include -I ${K230_SDK_ROOT}/${LINUX_SRC_PATH}/arch  -undef -x assembler-with-cpp ${K230_SDK_ROOT}/${LINUX_DTS_PATH}  hw/k230.dts.txt


	# ROOTFS_BASE=`cat hw/k230.dts.txt | grep initrd-start | awk -F " " '{print $4}' | awk -F ">" '{print $1}'`
	# ROOTFS_SIZE=`ls -lt rootfs-final.cpio.gz | awk '{print $5}'`
	# ((ROOTFS_END= $ROOTFS_BASE + $ROOTFS_SIZE))
	# ROOTFS_END=`printf "0x%x" $ROOTFS_END`
	# sed -i "s/linux,initrd-end = <0x0 .*/linux,initrd-end = <0x0 $ROOTFS_END>;/g" hw/k230.dts.txt

	# ${LINUX_BUILD_DIR}/scripts/dtc/dtc -I dts -q -O dtb hw/k230.dts.txt  >k230.dtb;
	ln -s ${first_dtb} k.dtb
	k230_gzip fw_payload.bin;
	echo a>rd;
	${mkimage} -A riscv -O linux -T multi -C gzip -a ${CONFIG_MEM_LINUX_SYS_BASE} -e ${CONFIG_MEM_LINUX_SYS_BASE} -n linux -d fw_payload.bin.gz:rd:k.dtb  ulinux.bin;

	add_firmHead  ulinux.bin
	mv fn_ulinux.bin  linux_system.bin
	[ -f fa_ulinux.bin ] && mv fa_ulinux.bin  linux_system_aes.bin
	[ -f fs_ulinux.bin ] && mv fs_ulinux.bin  linux_system_sm.bin
	rm -rf rd;
}




#生成sd卡镜像文件
#--$1--cfg
#--$2 iamge name
gen_image()
{
	#set -x;

	local genimage="genimage "
	local cfg="$1" ; #"genimage-sdcard.cfg"
	local image_name="$2"; #"sysimage-sdcard.img"
	cd  "${BINARIES_DIR}/";

	[ -z "${rootfs_ext4_file}" ] ||  cp  ${rootfs_ext4_file}  rootfs.ext4;

	GENIMAGE_TMP="genimage.tmp" ;	rm -rf "${GENIMAGE_TMP}";
	${genimage}   	--rootpath "${TARGET_DIR}"  --tmppath "${GENIMAGE_TMP}"    \
					--inputpath "$(pwd)"  	--outputpath "$(pwd)"	--config "${cfg}"

	rm -rf "${GENIMAGE_TMP}"
	chmod a+rw ${image_name}
	if [ "${K230_IMAGE_GZIP}" = "1" ]; then
		gzip -k -f ${image_name}
		chmod a+rw ${image_name}.gz
		gz_file_add_ver ${image_name}.gz
	else
		rm -f ${image_name}.gz
		echo "skip gzip for ${image_name} (set K230_IMAGE_GZIP=1 to enable)"
	fi
}

sync_image_outputs()
{
	local build_stamp
	local export_dir
	local exported_image
	local CONF=$(basename ${BASE_DIR})
	# Unified network image (docs/rockbase-net-unified-image-plan.md):
	# single "-net" suffix — the image always carries both SDIO WiFi and
	# USB network drivers; runtime selection is done by rockbase_netsel.sh.
	local net_sfx="-net"
	# Image flavor suffix: -min- (minimal, no real seed) vs -full- (with seed).
	# Derived from K230_BLOCKDATA_SIZE: >= 1G means a full (seed) image, otherwise minimal.
	local blockdata_size="${K230_BLOCKDATA_SIZE:-20G}"
	local flavor_sfx="-min"
	local size_num="${blockdata_size%[MGTmgt]}"
	local size_unit="${blockdata_size: -1}"
	case "${size_unit}" in
	  G|g) size_mb=$(( size_num * 1024 )) ;;
	  T|t) size_mb=$(( size_num * 1024 * 1024 )) ;;
	  *)   size_mb=$(( size_num )) ;;
	esac
	if [ "${size_mb}" -ge 1024 ]; then
		flavor_sfx="-full"
	fi
	local image_prefix="sysimage-sdcard-rockbase${flavor_sfx}${net_sfx}"

	# ---- 导出目录决策：WSL 导出 Windows / 纯 Linux 保留项目目录 ----
	if [ -z "${K230_IMAGE_OUT_DIR}" ]; then
		# /proc/version 含 "microsoft" 或存在 WSL_DISTRO_NAME 即视为 WSL
		if grep -qi microsoft /proc/version 2>/dev/null || [ -n "${WSL_DISTRO_NAME:-}" ]; then
			K230_IMAGE_OUT_DIR="/mnt/d/code/pony/IoT/k230/img"
		else
			echo "non-WSL build: skipping image export, keeping images in ${BINARIES_DIR}"
			return 0
		fi
	fi

	# 从 release_version 中提取首个 YYYYMMDD-HHMMSS 时间戳，保证每次导出落到新目录
	build_stamp=$(grep -m1 ^sdk: "${TARGET_DIR}/etc/version/release_version" | grep -oE [0-9]{8}-[0-9]{6} | head -n1)
	[ -z "$build_stamp" ] && build_stamp=$(date +%Y%m%d-%H%M%S)-$$
	export_dir="${K230_IMAGE_OUT_DIR}/${image_prefix}-${build_stamp}"
	exported_image="${export_dir}/sysimage-sdcard.img"
	mkdir -p "${export_dir}" || {
		echo "ERROR: failed to create image export directory: ${export_dir}" >&2
		exit 1
	}
	cp --sparse=never -f "${BINARIES_DIR}/sysimage-sdcard.img" "${exported_image}" || {
		echo "ERROR: failed to export flash image to: ${exported_image}" >&2
		exit 1
	}
	echo "exported flash image to ${exported_image}"

	if [ "${K230_KEEP_WSL_IMAGES:-0}" != "1" ]; then
		rm -f "${BINARIES_DIR}/sysimage-sdcard.img" "${BINARIES_DIR}/sysimage-sdcard.img.gz"
		rm -f "${BINARIES_DIR}/rootfs.tar" "${BINARIES_DIR}"/rootfs.tar.*
		if [ "${K230_KEEP_WSL_BLOCKDATA_IMAGE:-0}" != "1" ]; then
			rm -f "${BINARIES_DIR}/blockdata.ext4" "${BINARIES_DIR}/config.ext4"
		fi
		echo "removed WSL-side flash image from ${BINARIES_DIR}"
	fi
}

gen_env_bin()
{
	local mkenvimage="${UBOOT_BUILD_DIR}/tools/mkenvimage"
	local CONFT=${BUILD_DIR%/build*}
	local CONF=${CONFT##*/output/};


	cd  "${BINARIES_DIR}/";
	local default_env_file=${env_dir}/default.env;

	# if [ ${DTB} == "k230-canmv.dtb" ]; then
	# 	default_env_file=${env_dir}/k230_canmv.env;
	# fi

	if [ ${CONF} == "k230d_canmv_ilp32_defconfig" ] || [ ${CONF} == "BPI-CanMV-K230D-Zero_ilp32_defconfig" ]; then
		sed -i 's/^bootcmd=.*$/bootcmd=run bnuttx;run blinuxilp32;/g' ${default_env_file}
	elif [ ${CONF} == "k230d_canmv_defconfig" ] || [ ${CONF} == "BPI-CanMV-K230D-Zero_defconfig" ]; then
		sed -i 's/^bootcmd=.*$/bootcmd=run bnuttx;run blinux;/g' ${default_env_file}
	else
		sed -i 's/^bootcmd=.*$/bootcmd=run blinux;/g' ${default_env_file}
	fi
	${mkenvimage} -s 0x10000 -o uboot/env.env  ${default_env_file}
}
gen_boot_ext4()
{
	local first_dtb="$(grep BR2_LINUX_KERNEL_INTREE_DTS_NAME ${BR2_CONFIG} | cut -d / -f2 | tr -d '"' |  cut -d ' ' -f1).dtb"
	local logo=$(grep CONFIG_K230_BARE_DISP_LOGO_PATH ${UBOOT_BUILD_DIR}/.config  | cut -d '"' -f2 |  sed 's/\.png$/.yuv/')



	echo "${first_dtb}"
	cd  "${BINARIES_DIR}/";
	rm -rf boot; mkdir -p boot;

	cp ${K230_SDK_ROOT}/buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/boot/nuttx-7000000-uart2.bin  boot/;
	cp Image boot/;
	[ ! -f "Image_ilp32" ] ||  cp Image_ilp32 boot/;
	cp *.dtb boot;
	[ -z "${logo}" ]  ||  cp ${BUILDROOT_PATH}/${logo} boot/logo.yuv;
	cd boot; rm -rf k.dtb;ln -s ${first_dtb} k.dtb; cd -;
	${UBOOT_BUILD_DIR}/tools/mkimage -A riscv -O linux -T kernel -C none -a 0 -e 0 -n linux -d ${BINARIES_DIR}/fw_jump.bin  boot/fw_jump_add_uboot_head.bin
	rm -rf boot.ext4 ;fakeroot mkfs.ext4 -d boot  -r 1 -N 0 -m 1 -L "boot" -O ^64bit boot.ext4 80M
}
gen_deb_packages_gz()
{
	cd  "${BINARIES_DIR}/deb/";
	rm -rf  Packages.gz;dpkg-scanpackages . /dev/null | gzip -9c > Packages.gz
	cd -;
}
gen_uboot_bin
gen_env_bin
#gen_linux_bin;
# OTA A/B (stage D): no shared boot.ext4 anymore — each rootfs slot carries
# its own /boot (see gen_target_boot in post-build.sh), and genimage writes
# slot_b from the same rootfs.ext4 (factory copy of slot_a).
gen_blockdata_ext4
gen_rockcfg_ext4
#gen_deb_packages_gz
gen_image ${GENIMAGE_CFG_SD}   sysimage-sdcard.img
sync_image_outputs
