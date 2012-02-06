/*
 * telepathy-example-list-managers - list installed connection managers
 *
 * Copyright (C) 2008 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2008 Nokia Corporation
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */

#include "config.h"

#include <telepathy-glib/telepathy-glib.h>

typedef struct {
    GMainLoop *mainloop;
    int exit_code;
} ExampleData;

static void
got_connections (const gchar * const *bus_names,
                 gsize n,
                 const gchar * const *cms,
                 const gchar * const *protocols,
                 const GError *error,
                 gpointer user_data,
                 GObject *unused)
{
  ExampleData *data = user_data;

  if (error != NULL)
    {
      g_warning ("%s", error->message);
      data->exit_code = 1;
    }
  else
    {
      gsize i;

      g_message ("Found %" G_GSIZE_FORMAT " connections:", n);

      for (i = 0; i < n; i++)
        {
          g_message ("%s", bus_names[i]);
          g_message ("- CM %s, protocol %s", cms[i], protocols[i]);
        }

      /* all the arrays are also NULL-terminated */
      g_assert (bus_names[n] == NULL && cms[n] == NULL &&
          protocols[n] == NULL);
    }

  g_main_loop_quit (data->mainloop);
}

int
main (int argc,
      char **argv)
{
  ExampleData data = { g_main_loop_new (NULL, FALSE), 0 };
  TpDBusDaemon *bus_daemon;
  GError *error = NULL;

  g_type_init ();
  tp_debug_set_flags (g_getenv ("EXAMPLE_DEBUG"));

  bus_daemon = tp_dbus_daemon_dup (&error);

  if (bus_daemon == NULL)
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      data.exit_code = 1;
      goto out;
    }

  tp_list_connection_names (bus_daemon, got_connections, &data, NULL, NULL);

  g_main_loop_run (data.mainloop);

out:
  if (data.mainloop != NULL)
    g_main_loop_unref (data.mainloop);

  if (bus_daemon != NULL)
    g_object_unref (bus_daemon);

  return data.exit_code;
}
