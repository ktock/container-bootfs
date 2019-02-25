#!/bin/bash
############################################################
#
# mkimage.sh
#
# Copyright 2019, Kohei Tokunaga
# Licensed under Apache License, Version 2.0
# 
############################################################

function check {
    if [ $? -ne 0 ] ; then
        (>&2 echo "Failed: ${1}")
        exit 1
    else
        echo "Succeeded: ${1}"
    fi
}

function import_so_dependency {
    local TARGET_BIN="${1}"
    local TARGET_DIR="${2}"

    ldd "${TARGET_BIN}" \
        | grep so \
        | sed -e '/^[^\t]/ d' \
        | sed -e 's/\t//' \
        | sed -e 's/.*=..//' \
        | sed -e 's/ (0.*)//' \
        | sort \
        | uniq \
        | while read TARGET_SO
    do
        local ORG_TARGET_SO_DIR=$(dirname "${TARGET_SO}")
        local NEW_TARGET_SO_DIR="${TARGET_DIR}/${ORG_TARGET_SO_DIR}"
        mkdir -p "${NEW_TARGET_SO_DIR}"
        cp "${TARGET_SO}" "${NEW_TARGET_SO_DIR}"
    done
}

function install_layer {
    local LAYER_DIR="${1}"
    local IMAGE_DIR="${2}"
    local MANIFEST_JSON="${3}"
    local CONFIG_JSON="${4}"

    local TMPFILE=$(mktemp)
    local LAYER_NAME=layer.tar
    find "${LAYER_DIR}" | xargs -I{} touch -d "1955/11/5 00:00:00" {}
    tar cf "${IMAGE_DIR}/${LAYER_NAME}" --directory="${LAYER_DIR}" .
    local LAYER_HASH=""
    LAYER_HASH=$(sha256sum "${IMAGE_DIR}/${LAYER_NAME}" | sed -E 's/^([^ ]*).+$/\1/g')
    mkdir "${IMAGE_DIR}/${LAYER_HASH}"
    mv "${IMAGE_DIR}/${LAYER_NAME}" \
       "${IMAGE_DIR}/${LAYER_HASH}/${LAYER_NAME}"
    jq '.[0].Layers |= . + [ "'"${LAYER_HASH}/${LAYER_NAME}"'" ]' \
       "${MANIFEST_JSON}" > "${TMPFILE}"
    cat "${TMPFILE}" > "${MANIFEST_JSON}"
    jq '.rootfs.diff_ids |= . + [ "sha256:'"${LAYER_HASH}"'" ]' \
       "${CONFIG_JSON}" > "${TMPFILE}"
    cat "${TMPFILE}" > "${CONFIG_JSON}"
    rm "${TMPFILE}"
}


if [ $# -lt 2 ] ; then
    echo "Specify args."
    echo "${0} ORG_IMAGE_TAG NEW_IMAGE_TAG"
    exit 1
fi

ORG_IMAGE_TAG="${1}"
NEW_IMAGE_TAG="${2}"
BUSYBOX_BIN=/busybox
DROPBEAR_BIN=/dbclient
BOOT_BIN=/boot.src/boot
DBCLIENT_Y_BIN=/boot.src/dbclient_y
BOOT_BIN_ROOTREL=/boot
CASYNC_BIN=$(which casync)
DESYNC_BIN=$(which desync)
FUSERMOUNT_BIN=$(which fusermount)
ROOTFS_CATAR=/rootfs.catar
ROOTFS_CAIBX=/rootfs.caibx
ORG_ROOTFS_TAR=/org-rootfs.tar
ORG_IMAGE_TAR=/org-image.tar
OUTPUT_DIR=/output
ORG_ROOTFS_DIR="${OUTPUT_DIR}"/org-rootfs
ORG_IMAGE_DIR="${OUTPUT_DIR}"/org-image
NEW_ROOTFS_DIR="${OUTPUT_DIR}"/new-rootfs
NEW_IMAGE_DIR="${OUTPUT_DIR}"/new-image
NEW_IMAGE_TAR="${OUTPUT_DIR}"/new-image.tar
OUT_ROOTFS_STORE="${OUTPUT_DIR}"/rootfs.castr
ROOTFS_UPPER_DIR="${NEW_ROOTFS_DIR}"/upper
ROOTFS_LOWER_DIR="${NEW_ROOTFS_DIR}"/lower
ROOTFS_BIN_DIR="${ROOTFS_LOWER_DIR}"/bin
ROOTFS_CACHE_DIR="${ROOTFS_LOWER_DIR}"/rootfs.castr
ROOTFS_CATAR_DIR="${ROOTFS_LOWER_DIR}"/rootfs.catar
ROOTFS_MOUNT_DIR="${ROOTFS_LOWER_DIR}"/rootfs
ENTRYPOINT_MEMO="${ROOTFS_UPPER_DIR}"/entrypoint_memo
if find "${OUTPUT_DIR}" -mindepth 1 -print -quit 2>/dev/null | grep -q . ; then
    echo "Fatal: Attached output volume is not empty."
    exit 1;
fi
mkdir -p \
      "${ORG_ROOTFS_DIR}" \
      "${ORG_IMAGE_DIR}" \
      "${NEW_ROOTFS_DIR}" \
      "${NEW_IMAGE_DIR}" \
      "${OUT_ROOTFS_STORE}" \
      "${ROOTFS_UPPER_DIR}" \
      "${ROOTFS_LOWER_DIR}" \
      "${ROOTFS_BIN_DIR}" \
      "${ROOTFS_CACHE_DIR}" \
      "${ROOTFS_CATAR_DIR}" \
      "${ROOTFS_MOUNT_DIR}"

# Check existing Docker.
docker -v
check "Checking Docker existance."

# Extract original image and rootfs.
echo "Saving original image..."
docker save "${ORG_IMAGE_TAG}" -o "${ORG_IMAGE_TAR}"
ORG_CONTAINER_ID=$(docker create "${ORG_IMAGE_TAG}")
check "Creating target container."
docker export "${ORG_CONTAINER_ID}" -o "${ORG_ROOTFS_TAR}"
check "Exporting target rootfs."
docker rm "${ORG_CONTAINER_ID}"
check "Removing target container."
tar xf "${ORG_IMAGE_TAR}" -C "${ORG_IMAGE_DIR}"
check "Extracting original image."
tar xf "${ORG_ROOTFS_TAR}" -C "${ORG_ROOTFS_DIR}"
check "Extracting original rootfs."
mkdir "${ORG_ROOTFS_DIR}"/dev \
      "${ORG_ROOTFS_DIR}"/proc \
      "${ORG_ROOTFS_DIR}"/sys

# Generating catar, caibx, castr from rootfs.
echo "Generating casync related files..."
casync make "${ROOTFS_CATAR}" "${ORG_ROOTFS_DIR}"
check "Generating catar."
casync make --store="${OUT_ROOTFS_STORE}" "${ROOTFS_CAIBX}" "${ROOTFS_CATAR}"
check "Generating castr and caibx."

# Construct lower layer of rootfs.
echo "Constructing rootfs lower layer..."
cp "${FUSERMOUNT_BIN}" "${ROOTFS_BIN_DIR}"/
cp "${CASYNC_BIN}" "${ROOTFS_LOWER_DIR}"
cp "${DESYNC_BIN}" "${ROOTFS_LOWER_DIR}"
cp "${BUSYBOX_BIN}" "${ROOTFS_LOWER_DIR}"
cp "${DROPBEAR_BIN}" "${ROOTFS_LOWER_DIR}"
cp "${BOOT_BIN}" "${ROOTFS_LOWER_DIR}"
cp "${DBCLIENT_Y_BIN}" "${ROOTFS_LOWER_DIR}"
import_so_dependency "${FUSERMOUNT_BIN}" "${ROOTFS_LOWER_DIR}"
import_so_dependency "${CASYNC_BIN}" "${ROOTFS_LOWER_DIR}"
import_so_dependency "${DESYNC_BIN}" "${ROOTFS_LOWER_DIR}"
import_so_dependency "${DROPBEAR_BIN}" "${ROOTFS_LOWER_DIR}"
find /lib -name libnss* | while read TARGET_SO  # for getpwuid() in SSH
do
    ORG_TARGET_SO_DIR=$(dirname "${TARGET_SO}")
    NEW_TARGET_SO_DIR="${ROOTFS_LOWER_DIR}/${ORG_TARGET_SO_DIR}"
    mkdir -p "${NEW_TARGET_SO_DIR}"
    cp "${TARGET_SO}" "${NEW_TARGET_SO_DIR}"
done

# Construct upper layer of rootfs.
echo "Constructing rootfs upper layer..."
cp -r "${ORG_ROOTFS_DIR}"/etc "${ROOTFS_UPPER_DIR}" # for getpwuid() in SSH
ORG_IMAGE_MANIFEST_JSON="${ORG_IMAGE_DIR}"/manifest.json
ORG_IMAGE_CONFIG_JSON="${ORG_IMAGE_DIR}"/$(jq -r '.[0].Config' "${ORG_IMAGE_MANIFEST_JSON}")
cp "${ROOTFS_CAIBX}" "${ROOTFS_UPPER_DIR}"
jq '.config.Entrypoint' "${ORG_IMAGE_CONFIG_JSON}" > "${ENTRYPOINT_MEMO}"
check "Memorize entrypoint bin."

# Generate new image.
echo "Generating new image..."
NEW_IMAGE_MANIFEST_JSON="${NEW_IMAGE_DIR}"/manifest.json
NEW_IMAGE_CONFIG_JSON="${NEW_IMAGE_DIR}"/org-config.json
cat "${ORG_IMAGE_CONFIG_JSON}" \
    | jq '.config.Entrypoint = [ "'"${BOOT_BIN_ROOTREL}"'" ]' \
    | jq '.history = []' \
    | jq '.rootfs.diff_ids = []' > "${NEW_IMAGE_CONFIG_JSON}"
check "Generating new image config json."
cat "${ORG_IMAGE_MANIFEST_JSON}" \
    | jq '.[0].Layers = []' > "${NEW_IMAGE_MANIFEST_JSON}"
check "Generating new image manifest json."
install_layer "${ROOTFS_LOWER_DIR}" \
              "${NEW_IMAGE_DIR}" \
              "${NEW_IMAGE_MANIFEST_JSON}" \
              "${NEW_IMAGE_CONFIG_JSON}"
install_layer "${ROOTFS_UPPER_DIR}" \
              "${NEW_IMAGE_DIR}" \
              "${NEW_IMAGE_MANIFEST_JSON}" \
              "${NEW_IMAGE_CONFIG_JSON}"
CONFIG_HASH=$(sha256sum "${NEW_IMAGE_CONFIG_JSON}" | sed -E 's/^([^ ]*).+$/\1/g')
mv "${NEW_IMAGE_CONFIG_JSON}" "${NEW_IMAGE_DIR}/${CONFIG_HASH}.json"
TMPFILE=$(mktemp)
jq '.[0].Config = "'"${CONFIG_HASH}.json"'" | .[0].RepoTags = [ "'"${NEW_IMAGE_TAG}"'" ]' \
   "${NEW_IMAGE_MANIFEST_JSON}" > "${TMPFILE}"
cat "${TMPFILE}" > "${NEW_IMAGE_MANIFEST_JSON}"
tar cf "${NEW_IMAGE_TAR}" --directory="${NEW_IMAGE_DIR}" .
check "Generating new image tarball."

docker load -i "${NEW_IMAGE_TAR}"
check "Loading new image."
