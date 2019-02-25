/*******************************************************************************
 *
 * dbclient_y.c
 *
 * Copyright 2019, Kohei Tokunaga
 * Licensed under Apache License, Version 2.0
 *
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
  const char **args = calloc(sizeof(char *), argc + 1);
  int argpos = 0;
  
  args[argpos++] = "/dbclient";
  args[argpos++] = "-y";
  for (int i = 1; i < argc; i++) {
    args[argpos++] = argv[i];
  }
  args[argpos] = NULL;
  
  /* fprintf (stderr, "Exec arguments: ["); */
  /* for (int i = 0; i < argpos; i++) { */
  /*   fprintf(stderr, " \"%s\" ,", args[i]); */
  /* } */
  /* fprintf (stderr, " NULL ]\n"); */

  execv(args[0], (char * const*)args);
}
