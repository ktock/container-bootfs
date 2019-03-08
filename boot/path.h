/*******************************************************************************
 *
 * path.h
 *
 * Copyright 2019, Kohei Tokunaga
 * Licensed under Apache License, Version 2.0
 *
 ******************************************************************************/

/* Files required on boot */
#define CASYNC_BIN         "/bin/casync"
#define DESYNC_BIN         "/bin/desync"
#define DBCLIENT_BIN       "/bin/dbclient"
#define DBCLIENT_Y_BIN     "/bin/dbclient_y"
#define FUSERMOUNT_BIN     "/bin/fusermount"
#define PROC_MOUNTS        "/proc/mounts"
#define DEV_FUSE           "/dev/fuse"
#define ETC_PASSWD         "/etc/passwd"
#define SYS_DEV_BLOCK      "/sys/block"
#define ROOTFS_MOUNT_DIR   "/.bootfs/rootfs"
#define DEV_LOOP_ISO       "/.bootfs/rootfs.dev/loopiso"
#define CASTR_CACHE_DIR    "/.bootfs/rootfs.castr"
#define ARCHIVE_MOUNT_DIR  "/.bootfs/rootfs.ar"
#define CAIBX_FILE         "/.bootfs/rootfs.caibx"
#define ENTRYPOINT_MEMO    "/.bootfs/entrypoint_memo"

/* Files generated during boot */
#define MOUNTED_ARCHIVE    "/.bootfs/rootfs.ar/rootfs"
#define MOVED_PROC_MOUNTS  "/.bootfs/rootfs/proc/mounts"

/* Archive information */
#define ISO_FS_TYPE        "iso9660"

/* Other */
#define LOOP_DEV_MAJOR_NUM 7
