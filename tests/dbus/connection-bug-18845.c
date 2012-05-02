/* Regression test for https://bugs.freedesktop.org/show_bug.cgi?id=18845
 *
 * Copyright (C) 2008 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2008 Nokia Corporation
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */

#include "config.h"

#include <telepathy-glib/connection.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/debug.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/proxy-subclass.h>

#include "tests/lib/myassert.h"
#include "tests/lib/simple-conn.h"
#include "tests/lib/util.h"

static GMainLoop *mainloop;

static GError invalidated_for_test = { 0, TP_ERROR_PERMISSION_DENIED,
      "No connection for you!" };

static gboolean
no_more_idling_around (gpointer data)
{
  g_main_loop_quit (data);
  return FALSE;
}

int
main (int argc,
      char **argv)
{
  TpDBusDaemon *dbus;
  TpTestsSimpleConnection *service_conn;
  TpBaseConnection *service_conn_as_base;
  gchar *name;
  gchar *conn_path;
  GError *error = NULL;
  TpConnection *conn;
  DBusGProxy *proxy;

  tp_tests_abort_after (10);
  g_type_init ();
  invalidated_for_test.domain = TP_ERROR;

  tp_debug_set_flags ("all");
  mainloop = g_main_loop_new (NULL, FALSE);
  dbus = tp_tests_dbus_daemon_dup_or_die ();

  service_conn = TP_TESTS_SIMPLE_CONNECTION (tp_tests_object_new_static_class (
        TP_TESTS_TYPE_SIMPLE_CONNECTION,
        "account", "me@example.com",
        "protocol", "simple",
        NULL));
  service_conn_as_base = TP_BASE_CONNECTION (service_conn);
  MYASSERT (service_conn != NULL, "");
  MYASSERT (service_conn_as_base != NULL, "");

  MYASSERT (tp_base_connection_register (service_conn_as_base, "simple",
        &name, &conn_path, &error), "");
  g_assert_no_error (error);

  conn = tp_connection_new (dbus, name, conn_path, &error);
  MYASSERT (conn != NULL, "");
  g_assert_no_error (error);
  MYASSERT (tp_connection_run_until_ready (conn, TRUE, &error, NULL),
      "");
  g_assert_no_error (error);

  {
    const gchar *ids[] = {
        "flarglybadger",
        NULL
    };
    GArray *handles = NULL;

    MYASSERT (tp_cli_connection_run_request_handles (conn, -1,
        TP_HANDLE_TYPE_CONTACT, ids, &handles, &error, NULL), "");
    g_assert_no_error (error);

    g_array_unref (handles);
  }

  /* The bug was in cleaning up handle refs when the CM fell off the bus.
   * Emitting "destroy" on the proxy simulates the CM falling off the bus.
   */
  proxy = tp_proxy_borrow_interface_by_id ((TpProxy *) conn,
      TP_IFACE_QUARK_CONNECTION, &error);
  g_assert_no_error (error);
  g_signal_emit_by_name (proxy, "destroy");

  g_idle_add_full (G_PRIORITY_LOW, no_more_idling_around, mainloop, NULL);

  g_main_loop_run (mainloop);

  g_object_unref (conn);

  /* Make a new connection proxy so that we can call Disconnect() on the
   * connection.
   */
  conn = tp_connection_new (dbus, name, conn_path, &error);
  MYASSERT (conn != NULL, "");
  g_assert_no_error (error);
  MYASSERT (tp_connection_run_until_ready (conn, TRUE, &error, NULL), "");
  g_assert_no_error (error);

  tp_tests_connection_assert_disconnect_succeeds (conn);
  g_object_unref (conn);

  service_conn_as_base = NULL;
  g_object_unref (service_conn);
  g_free (name);
  g_free (conn_path);

  g_object_unref (dbus);
  g_main_loop_unref (mainloop);

  return 0;
}
