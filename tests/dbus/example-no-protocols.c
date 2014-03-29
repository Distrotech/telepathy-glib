#include "config.h"

#include <glib-object.h>
#include <telepathy-glib/cli-connection.h>
#include <telepathy-glib/cli-misc.h>
#include <telepathy-glib/connection-manager.h>
#include <telepathy-glib/debug.h>
#include <telepathy-glib/defs.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/interfaces.h>

#include "telepathy-glib/reentrants.h"

#include "tests/lib/util.h"

typedef struct {
    int dummy;
} Fixture;

static void
setup (Fixture *f,
    gconstpointer data)
{
}

static void
prepare (void)
{
  GError *error = NULL;
  const gchar *abs_top_builddir = g_getenv ("abs_top_builddir");
  const gchar *libexec = g_getenv ("libexec");
  gchar *command[] = { NULL, NULL };

  g_assert (abs_top_builddir != NULL || libexec != NULL);

  if (abs_top_builddir != NULL)
    {
      command[0] = g_strdup_printf ("%s/%s",
            abs_top_builddir,
            "examples/cm/no-protocols/telepathy-1-example-no-protocols");
    }
  else
    {
      command[0] = g_strdup_printf ("%s/%s",
          libexec,
          "telepathy-1-example-no-protocols");
    }

  if (!g_spawn_async (NULL, command, NULL, 0, NULL, NULL, NULL, &error))
    {
      g_error ("g_spawn_async: %s", error->message);
    }

  g_free (command[0]);
}

static void
connection_manager_got_info (TpConnectionManager *cm,
                             guint source,
                             GMainLoop *mainloop)
{
  GHashTable *empty = g_hash_table_new (NULL, NULL);
  gchar *bus_name = NULL;
  gchar *object_path = NULL;
  GError *error = NULL;

  g_message ("Emitted got-info (source=%d)", source);

  if (source < TP_CM_INFO_SOURCE_LIVE)
    return;

  tp_cli_connection_manager_run_request_connection (cm, -1,
      "jabber", empty, &bus_name, &object_path, &error, NULL);

  g_assert (error != NULL);
  g_assert (error->domain == TP_ERROR);
  g_assert (error->code == TP_ERROR_NOT_IMPLEMENTED);

  g_error_free (error);

  g_main_loop_quit (mainloop);

  g_hash_table_unref (empty);
}

static void
wait_for_name_owner_cb (GDBusConnection *connection,
    const gchar *name,
    const gchar *new_owner,
    gpointer main_loop)
{
  g_main_loop_quit (main_loop);
}

static void
early_cm_exited (TpConnectionManager *cm,
    gboolean *saw_exited)
{
  *saw_exited = TRUE;
}

static void
test (Fixture *f,
    gconstpointer data)
{
  GMainLoop *mainloop;
  TpConnectionManager *early_cm, *late_cm;
  TpClientFactory *factory;
  gulong handler;
  GError *error = NULL;
  gboolean saw_exited;
  GTestDBus *test_dbus;
  guint name_owner_watch;

  /* If we're running slowly (for instance in a parallel build)
   * we don't want the CM process in the background to time out and exit. */
  g_setenv ("EXAMPLE_PERSIST", "1", TRUE);

  tp_tests_abort_after (5);

  tp_debug_set_flags ("all");

  g_test_dbus_unset ();
  test_dbus = g_test_dbus_new (G_TEST_DBUS_NONE);
  g_test_dbus_up (test_dbus);

  mainloop = g_main_loop_new (NULL, FALSE);

  factory = tp_client_factory_dup (&error);
  g_assert_no_error (error);

  /* First try making a TpConnectionManager before the CM is available. This
   * will fail. */
  early_cm = tp_client_factory_ensure_connection_manager (factory,
      "example_no_protocols", NULL, NULL);
  g_assert (early_cm != NULL);

  /* Failure to introspect is signalled as 'exited' */
  handler = g_signal_connect (early_cm, "exited",
      G_CALLBACK (early_cm_exited), &saw_exited);

  tp_tests_proxy_run_until_prepared_or_failed (early_cm, NULL, &error);
  g_assert (error != NULL);
  g_assert (tp_proxy_get_invalidated (early_cm) == NULL);
  g_assert_cmpuint (error->domain, ==, G_DBUS_ERROR);
  g_assert_cmpint (error->code, ==, G_DBUS_ERROR_SERVICE_UNKNOWN);
  g_clear_error (&error);

  if (!saw_exited)
    {
      g_debug ("waiting for 'exited'...");

      while (!saw_exited)
        g_main_context_iteration (NULL, TRUE);
    }

  g_signal_handler_disconnect (early_cm, handler);

  /* Now start the connection manager and wait for it to start */
  prepare ();
  name_owner_watch = g_bus_watch_name_on_connection (
      tp_client_factory_get_dbus_connection (factory),
      TP_CM_BUS_NAME_BASE "example_no_protocols",
      G_BUS_NAME_WATCHER_FLAGS_NONE,
      wait_for_name_owner_cb, NULL,
      g_main_loop_ref (mainloop), (GDestroyNotify) g_main_loop_unref);
  g_main_loop_run (mainloop);
  g_bus_unwatch_name (name_owner_watch);

  /* This TpConnectionManager works fine. */
  late_cm = tp_client_factory_ensure_connection_manager (factory,
      "example_no_protocols", NULL, NULL);
  g_assert (late_cm != NULL);

  handler = g_signal_connect (late_cm, "got-info",
      G_CALLBACK (connection_manager_got_info), mainloop);
  g_main_loop_run (mainloop);
  g_signal_handler_disconnect (late_cm, handler);

  /* Now both objects can become ready */
  tp_tests_proxy_run_until_prepared (early_cm, NULL);
  tp_tests_proxy_run_until_prepared (late_cm, NULL);

  g_object_unref (late_cm);
  g_object_unref (early_cm);
  g_object_unref (factory);
  g_main_loop_unref (mainloop);

  g_test_dbus_down (test_dbus);
  tp_tests_assert_last_unref (&test_dbus);
}

static void
teardown (Fixture *f,
    gconstpointer data)
{
}

int
main (int argc,
    char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_bug_base ("http://bugs.freedesktop.org/show_bug.cgi?id=");

  g_test_add ("/example-no-protocols", Fixture, NULL, setup, test, teardown);

  return g_test_run ();
}
