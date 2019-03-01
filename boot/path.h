/*******************************************************************************
 *
 * path.h
 *
 * Copyright 2019, Kohei Tokunaga
 * Licensed under Apache License, Version 2.0
 *
 ******************************************************************************/

/* Files required on boot */
#define CASYNC_BIN       "/bin/casync"
#define DESYNC_BIN       "/bin/desync"
#define DBCLIENT_BIN     "/bin/dbclient"
#define DBCLIENT_Y_BIN   "/bin/dbclient_y"
#define FUSERMOUNT_BIN   "/bin/fusermount"
#define PROC_MOUNTS      "/proc/mounts"
#define DEV_FUSE         "/dev/fuse"
#define ETC_PASSWD       "/etc/passwd"
#define ROOTFS_MOUNT_DIR "/.bootfs/rootfs"
#define CASTR_CACHE_DIR  "/.bootfs/rootfs.castr"
#define CATAR_MOUNT_DIR  "/.bootfs/rootfs.catar"
#define CAIBX_FILE       "/.bootfs/rootfs.caibx"
#define ENTRYPOINT_MEMO  "/.bootfs/entrypoint_memo"

/* Files generated during boot */
#define MOUNTED_CATAR     "/.bootfs/rootfs.catar/rootfs"
#define MOVED_PROC_MOUNTS "/.bootfs/rootfs/proc/mounts"
