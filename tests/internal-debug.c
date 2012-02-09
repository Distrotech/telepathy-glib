#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <glib.h>

#include <telepathy-glib/debug.h>

#undef DEBUG_FLAG
#define DEBUG_FLAG TP_DEBUG_IM
#include "telepathy-glib/debug-internal.h"

static void
test_debugging (void)
{
  DEBUG ("internal-debug.h should always define DEBUG %s",
    "(either as a macro or as a no-op static inline function");

#ifndef DEBUGGING
#error internal-debug.h should always define DEBUGGING
#endif

#ifdef ENABLE_DEBUG
  g_assert (DEBUGGING == 1);
#else
  g_assert (DEBUGGING == 0);
#endif
}

#undef DEBUG_FLAG
#define DEBUG_FLAG TP_DEBUG_CONNECTION
#include "telepathy-glib/debug-internal.h"

static void
test_not_debugging (void)
{
  DEBUG ("internal-debug.h should always define DEBUG %s",
    "(either as a macro or as a no-op static inline function");

#ifndef DEBUGGING
#error internal-debug.h should always define DEBUGGING
#endif

  g_assert (DEBUGGING == 0);
}

#undef DEBUG_FLAG
#define DEBUG_FLAG TP_DEBUG_IM
#include "telepathy-glib/debug-internal.h"

static void
test_debugging_again (void)
{
  DEBUG ("internal-debug.h should always define DEBUG %s",
    "(either as a macro or as a no-op static inline function");

#ifndef DEBUGGING
#error internal-debug.h should always define DEBUGGING
#endif

#ifdef ENABLE_DEBUG
  g_assert (DEBUGGING == 1);
#else
  g_assert (DEBUGGING == 0);
#endif
}

int
main (int argc, char **argv)
{
  /* We enable debugging for IM, but not for the connection. */
  tp_debug_set_flags ("im");
  test_debugging ();
  test_not_debugging ();
  test_debugging_again ();
  return 0;
}
