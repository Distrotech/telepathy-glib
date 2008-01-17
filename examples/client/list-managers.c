/*
 * telepathy-example-list-managers - list installed connection managers
 *
 * Copyright (C) 2007 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2007 Nokia Corporation
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */

#include <telepathy-glib/connection-manager.h>
#include <telepathy-glib/debug.h>

void
got_connection_managers (TpConnectionManager * const *cms,
                         gsize n_cms,
                         const GError *error,
                         gpointer user_data,
                         GObject *unused)
{
  GMainLoop *mainloop = user_data;

  if (error != NULL)
    {
      g_warning ("%s", error->message);
    }
  else
    {
      TpConnectionManager * const *iter = cms;

      g_message ("Found %" G_GSIZE_FORMAT " connection managers:", n_cms);

      for (iter = cms; *iter != NULL; iter++)
        {
          gchar *name;

          g_object_get (*iter,
              "connection-manager", &name,
              NULL);

          g_message ("- %s", name);

          g_free (name);
        }
    }

  g_main_loop_quit (mainloop);
}

int
main (int argc,
      char **argv)
{
  GMainLoop *mainloop;

  g_type_init ();
  tp_debug_set_flags (g_getenv ("EXAMPLE_DEBUG"));

  mainloop = g_main_loop_new (NULL, FALSE);

  tp_list_connection_managers (tp_dbus_daemon_new (tp_get_bus ()),
      got_connection_managers, mainloop, NULL, NULL);

  g_main_loop_run (mainloop);

  g_main_loop_unref (mainloop);
  return 0;
}
