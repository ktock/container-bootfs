/*******************************************************************************
 *
 * boot.c
 *
 * Copyright 2019, Kohei Tokunaga
 * Licensed under Apache License, Version 2.0
 *
 ******************************************************************************/
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/loop.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <unistd.h>
#include "parson/parson.h"
#include "path.h"

/* Limit configuration */
#define EXISTENCE_CHECK_LIMIT    1000000
#define MAX_FILENAME_PATH_LENGTH 1000000

#define access_file(path) access(path, F_OK)

int access_dir(const char *path)
{
  DIR* dir = opendir(path);
  
  if (dir) {
    closedir(dir);
    return 0;
  } else {
    return -1;
  }
}

int try_access_all(const char *targets[])
{
  int i = 0;
  
  while (targets[i] != NULL) {
    if (access_file(targets[i])
        && access_dir(targets[i])) {
      fprintf(stderr, "Failed to access %s\n", targets[i]);
      return -1;
    }
    i++;
  }
  
  return 0;
}

int is_empty_dir(const char *dirname)
{
  int n = 0;
  DIR *dir = opendir(dirname);
  
  if (dir == NULL) {
    return -1;
  } else {
    while (readdir(dir) != NULL) {
      n++;
      if (n > 2) {
        closedir(dir);
        return 0;
      }
    }
    closedir(dir);
  }
  
  return 1;
}

int wait_if_empty_dir(const char *trydir, int trynum)
{
  int try;
  
  while ((try = is_empty_dir(trydir)) && trynum > 0) {
    if (try == -1) {
      fprintf(stderr, "Failed to check if empty: %s\n", strerror(errno));
      return -1;
    }
    trynum--;
  }
  if (trynum == 0) {
    fprintf(stderr, "%s is empty(timeout).\n", trydir);
    return -1;
  }
  
  return 0;
}

int mount_archive_from_caibx_lazily()
{
  pid_t pid = fork();
  
  if (pid == 0) {
    setenv("CASYNC_SSH_PATH", DBCLIENT_Y_BIN, 1);
    int devnull;
    devnull = open("/dev/null",O_WRONLY | O_CREAT, 0666);
    dup2(devnull, 1);
    dup2(devnull, 2);
    char *const desync_mount_args[]
      = { DESYNC_BIN,
          "mount-index",
          "-c",
          CASTR_CACHE_DIR,
          "--store",
          getenv("BLOB_STORE"),
          CAIBX_FILE,
          ARCHIVE_MOUNT_DIR,
          NULL };
    execv(desync_mount_args[0], desync_mount_args);
    close(devnull);
  } else if (pid < 0) {
    fprintf(stderr, "Failed to fork desync process.\n");
    return -1;
  }
  if (wait_if_empty_dir(ARCHIVE_MOUNT_DIR,
                        EXISTENCE_CHECK_LIMIT)) {
    fprintf(stderr, "Failed to mount archive with desync.\n");
    return -1;
  }
  return 0;
}

int get_loopdev_unused_minor_num()
{
  int max = -1, this;
  char *sys_block = SYS_DEV_BLOCK;
  DIR *dir;
  struct dirent *dirent;
  char *backing_file = calloc(sizeof(char), MAX_FILENAME_PATH_LENGTH);
  
  if((dir = opendir(sys_block))) {
    for(dirent = readdir(dir);
        dirent;
        dirent = readdir(dir)) {
      if(strcmp(dirent->d_name, "." ) != 0
         && strcmp(dirent->d_name, ".." ) != 0) {
        
        /* extract interger and compare from filename matchs "loop[0-9]+" */
        if (strstr(dirent->d_name, "loop") == dirent->d_name) {
          errno = 0;
          this = (int)strtol(dirent->d_name + 4, NULL, 0);
          if (errno == 0 && this > max) {
            max = this;

            /* 
             * Check if any file mapped to the loopback device. 
             * https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-block-loop
             */
            snprintf(backing_file, MAX_FILENAME_PATH_LENGTH,
                     "%s/%s/%s", sys_block, dirent->d_name, "loop/backing_file");
            if (access_file(backing_file)) {
          
              /* unused loop device found. */
              return this;
            }
          }
        }
      }
    }
  } else {
    fprintf(stderr, "Failed to open %s.: %s\n", sys_block, strerror(errno));
    return -1;
  }
  
  return max + 1;
}

static void rmloopdev ()
{
  if (access_file(DEV_LOOP_ISO) == 0) {
    if (unlink(DEV_LOOP_ISO)) {
      fprintf(stderr, "Failed to remove loopdev %s.: %s\n",
              DEV_LOOP_ISO, strerror(errno));
    }
  }
}

int mount_rootfs_from_iso9660(const char *archive, const char *target)
{
  int archive_fd = -1, loopdev_fd = -1, minor;
  struct loop_info64 info;

  /* Get unused loopback device minor num. */
  if ((minor = get_loopdev_unused_minor_num()) < 0) {
    fprintf(stderr, "Failed to find usable loopback device.\n");
    goto error;
  }

  /* Mknod loopback device node. */
  if(mknod(DEV_LOOP_ISO,
           S_IRUSR | S_IWUSR |
           S_IRGRP | S_IWGRP |
           S_IROTH | S_IWOTH |
           S_IFBLK,
           makedev(LOOP_DEV_MAJOR_NUM, minor))) {
    fprintf(stderr, "Failed to mknod device %s(minor: %d): %s\n",
            DEV_LOOP_ISO, minor, strerror(errno));
    goto error;
  }
  atexit(rmloopdev);

  /* Register the loopback device to kernel. */
  if((archive_fd = open(archive, O_RDONLY)) < 0) {
    fprintf(stderr, "Failed to open backing archive(%s): %s\n",
            archive, strerror(errno));
    goto error;
  }
  if((loopdev_fd = open(DEV_LOOP_ISO, O_RDWR)) < 0) {
    fprintf(stderr, "Failed to open device(%s): %s\n",
            DEV_LOOP_ISO, strerror(errno));
    goto error;
  }
  if(ioctl(loopdev_fd, LOOP_SET_FD, archive_fd) < 0) {
    fprintf(stderr, "Failed to set fd: %s\n", strerror(errno));
    goto error;
  }

  /* Configure */
  memset(&info, 0, sizeof(struct loop_info64));
  info.lo_offset = 0;                   /* map isofile from topmost */
  info.lo_sizelimit = 0;                /* max available */
  info.lo_encrypt_type = LO_CRYPT_NONE; /* no encription */
  info.lo_encrypt_key_size = 0;
  info.lo_flags = LO_FLAGS_AUTOCLEAR;   /* detatch automatically on exit */
  if(ioctl(loopdev_fd, LOOP_SET_STATUS64, &info)) {
    fprintf(stderr, "Failed to set loop info: %s\n", strerror(errno));
    goto error;
  }
  close(loopdev_fd);
  close(archive_fd);
  loopdev_fd = -1;
  archive_fd = -1; 

  /* Mount iso image. */
  if (mount(DEV_LOOP_ISO, target, ISO_FS_TYPE, MS_RDONLY, NULL)) {
    fprintf(stderr, "Failed to mount rootfs: %s\n", strerror(errno));
    goto error;
  }
  
  return 0;
  
  error:
    if(archive_fd >= 0) {
      close(archive_fd);
    }   
    if(loopdev_fd >= 0) {
      ioctl(loopdev_fd, LOOP_CLR_FD, 0); 
      close(loopdev_fd);
    }
    
    return -1;
}

int mount_rootfs_from_catar(const char *archive, const char *target)
{
  pid_t pid = fork();
  
  if (pid == 0) {
    char *const casync_mount_args[]
      = { CASYNC_BIN,
          "mount",
          (char *)archive,
          (char *)target,
          NULL };
    execv(casync_mount_args[0], casync_mount_args);
  } else if (pid < 0) {
    fprintf(stderr, "Failed to fork casync process.\n");
    return -1;
  }
  if (wait_if_empty_dir(target, EXISTENCE_CHECK_LIMIT)) {
    fprintf(stderr, "Failed to mount rootfs with casync.\n");
    return -1;
  }
  
  return 0;
}

int switch_root(const char *new_rootfs)
{
  struct mntent *ent;
  FILE *proc_mounts;
  char *target;

  /* Each iteratoin has different role.
   * 1st: Trying to mount all target mount points.
   * 2nd: Finding all mount points which hasn't been moved to under new rootfs
   *      during 1st iteration.
   */
  target = calloc(sizeof(char), MAX_FILENAME_PATH_LENGTH);
  for (int i = 0; i < 2; i++) {
    const char *proc_mounts_path = (i == 0) ? PROC_MOUNTS : MOVED_PROC_MOUNTS;
    proc_mounts = setmntent(proc_mounts_path, "r");
    if (proc_mounts == NULL) {
      fprintf(stderr, "Failed to open %s\n", PROC_MOUNTS);
      return -1;
    }
    while ((ent = getmntent(proc_mounts))) {
      
      /* Directories which have prefix '/.bootfs/rootfs' won't be moved. */
      if (strcmp(ent->mnt_dir, "/")
          && strstr(ent->mnt_dir, ROOTFS_MOUNT_DIR) - ent->mnt_dir) {
        snprintf(target, MAX_FILENAME_PATH_LENGTH,
                 "%s/%s", new_rootfs, ent->mnt_dir);
        if (mount(ent->mnt_dir, target, NULL, MS_MOVE, NULL)
            && i) {
          fprintf(stderr, "Warning: Failed to move mount %s: %s.\n",
                  ent->mnt_dir, strerror(errno));
        }
      }
    }
    endmntent(proc_mounts);
  }
  free(target);
  if (chdir(new_rootfs)) {
    fprintf(stderr, "Failed to chdir to %s: %s.\n",
            new_rootfs, strerror(errno));
    return -1;
  };
  if (mount(new_rootfs, "/", NULL, MS_MOVE, NULL)) {
    fprintf(stderr, "Failed to move mount %s -> %s: %s.\n",
            new_rootfs, "/", strerror(errno));
    return -1;
  }
  if (chroot(".")) {
    fprintf(stderr, "Failed to chroot: %s.\n", strerror(errno));
    return -1;
  }
  if (chdir("/")) {
    fprintf(stderr, "Failed to chdir to newroot: %s.\n",
            strerror(errno));
    return -1;
  }
  
  return 0;
}

const char **restore_entrypoint_args(int argc, char *argv[])
{
  const char **args;
  int argpos = 0;
  JSON_Value *root_value;
  JSON_Array *entrypoint_args;
  
  root_value = json_parse_file(ENTRYPOINT_MEMO);
  if (json_value_get_type(root_value) != JSONArray) {
    fprintf(stderr, "ENTRYPOINT is empty or not array.\n");
    args = calloc(sizeof(char *), argc);
  } else {
    entrypoint_args = json_value_get_array(root_value);
    int entrypoint_args_num = json_array_get_count(entrypoint_args);
    args = calloc(sizeof(char *), entrypoint_args_num + argc);
    for (int i = 0; i < entrypoint_args_num; i++) {
      args[argpos++] = json_array_get_string(entrypoint_args, i);
    }
  }
  for (int i = 1; i < argc; i++) {
    args[argpos++] = argv[i];
  }
  args[argpos] = NULL;
  /* fprintf (stderr, "Exec arguments: [");
   * for (int i = 0; i < argpos; i++) {
   *   fprintf(stderr, " \"%s\" ,", args[i]);
   * }
   * fprintf (stderr, " NULL ]\n");
   */

  return args;
}

int main(int argc, char *argv[])
{
  /* Emulate original rootfs. */
  fprintf(stderr, "Checking dependencies...\n");
  char const* reqired_files[]
    = { // CASYNC_BIN, // Uncomment if use casync as mount wrapper.
        DESYNC_BIN,
        DBCLIENT_BIN,
        DBCLIENT_Y_BIN,
        FUSERMOUNT_BIN,
        PROC_MOUNTS,
        DEV_FUSE,
        ETC_PASSWD,
        ROOTFS_MOUNT_DIR,
        CASTR_CACHE_DIR,
        ARCHIVE_MOUNT_DIR,
        CAIBX_FILE,
        ENTRYPOINT_MEMO,
        NULL };
  if (try_access_all(reqired_files)) {
    fprintf(stderr, "Required file doesnt exist.\n");
    return -1;
  }
  fprintf(stderr, "Mounting archive file lazily with desync...\n");
  if (mount_archive_from_caibx_lazily()) {
    fprintf(stderr, "Failed to prepare archive file.\n");
    return 1;
  }
  fprintf(stderr, "Mounting rootfs...\n");
  if (
      // Uncomment and switch if use casync as mount wrapper.
      // mount_rootfs_from_catar(MOUNTED_ARCHIVE, ROOTFS_MOUNT_DIR)
      mount_rootfs_from_iso9660(MOUNTED_ARCHIVE, ROOTFS_MOUNT_DIR)
      ) {
    fprintf(stderr, "Failed to prepare rootfs: %s\n", strerror(errno));
    return 1;
  }

  /* Restore original entrypoint. */
  fprintf(stderr, "Restoring original ENTRYPOINT information...\n");
  const char **args = restore_entrypoint_args(argc, argv);

  /* Switch rootfs. */
  fprintf(stderr, "Switching rootfs...\n");
  if (switch_root(ROOTFS_MOUNT_DIR)) {
    fprintf(stderr, "Failed to switch rootfs.\n");
    return 1;
  }

  /* Execute app. */
  fprintf(stderr, "Now, diving into your app...\n");
  execvp(args[0], (char * const*)args);
}
