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
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
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

int mount_catar_from_caibx_lazily()
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
          CATAR_MOUNT_DIR,
          NULL };
    execv(desync_mount_args[0], desync_mount_args);
    close(devnull);
  } else if (pid < 0) {
    fprintf(stderr, "Failed to fork desync process.\n");
    return -1;
  }
  if (wait_if_empty_dir(CATAR_MOUNT_DIR,
                        EXISTENCE_CHECK_LIMIT)) {
    fprintf(stderr, "Failed to mount catar with desync.\n");
    return -1;
  }
  return 0;
}

int mount_rootfs_from_catar()
{
  pid_t pid = fork();
  
  if (pid == 0) {
    char *const casync_mount_args[]
      = { CASYNC_BIN,
          "mount",
          MOUNTED_CATAR,
          ROOTFS_MOUNT_DIR,
          NULL };
    execv(casync_mount_args[0], casync_mount_args);
  } else if (pid < 0) {
    fprintf(stderr, "Failed to fork casync process.\n");
    return -1;
  }
  if (wait_if_empty_dir(ROOTFS_MOUNT_DIR,
                        EXISTENCE_CHECK_LIMIT)) {
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
    = { CASYNC_BIN,
        DESYNC_BIN,
        DBCLIENT_BIN,
        DBCLIENT_Y_BIN,
        FUSERMOUNT_BIN,
        PROC_MOUNTS,
        DEV_FUSE,
        ETC_PASSWD,
        ROOTFS_MOUNT_DIR,
        CASTR_CACHE_DIR,
        CATAR_MOUNT_DIR,
        CAIBX_FILE,
        ENTRYPOINT_MEMO,
        NULL };
  if (try_access_all(reqired_files)) {
    fprintf(stderr, "Required file doesnt exist.\n");
    return -1;
  }
  fprintf(stderr, "Mounting catar lazily with desync...\n");
  if (mount_catar_from_caibx_lazily()) {
    fprintf(stderr, "Failed to prepare catar.\n");
    return 1;
  }
  fprintf(stderr, "Mounting rootfs with casync...\n");
  if (mount_rootfs_from_catar()) {
    fprintf(stderr, "Failed to prepare rootfs.\n");
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
