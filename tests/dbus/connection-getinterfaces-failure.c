/* Regression test for https://bugs.freedesktop.org/show_bug.cgi?id=15306
 *
 * Copyright (C) 2007-2008 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2007-2008 Nokia Corporation
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

#include "tests/lib/myassert.h"
#include "tests/lib/simple-conn.h"
#include "tests/lib/util.h"

static GType bug15306_connection_get_type (void);

typedef TpTestsSimpleConnection Bug15306Connection;
typedef TpTestsSimpleConnectionClass Bug15306ConnectionClass;

static void bug15306_conn_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (Bug15306Connection,
    bug15306_connection,
    TP_TESTS_TYPE_SIMPLE_CONNECTION,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION, bug15306_conn_iface_init))

static void
bug15306_connection_init (Bug15306Connection *self)
{
}

static void
bug15306_connection_class_init (Bug15306ConnectionClass *klass)
{
}

static void
bug15306_get_interfaces (TpSvcConnection *iface,
                   DBusGMethodInvocation *context)
{
  GError e = { TP_ERROR, TP_ERROR_NOT_AVAILABLE, "testing fd.o #15306" };

  dbus_g_method_return_error (context, &e);
}

static void
bug15306_conn_iface_init (gpointer g_iface,
                    gpointer iface_data)
{
  TpSvcConnectionClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_connection_implement_##x (klass, \
    bug15306_##x)
  IMPLEMENT (get_interfaces);
#undef IMPLEMENT
}

static GMainLoop *mainloop;

static void
on_status_changed (TpConnection *connection,
                   guint status,
                   guint reason,
                   gpointer user_data,
                   GObject *weak_object)
{
  MYASSERT (status == TP_CONNECTION_STATUS_DISCONNECTED, "%u", status);
}

static void
on_shutdown_finished (TpBaseConnection *base_conn,
                      gpointer user_data)
{
  g_main_loop_quit (mainloop);
}

int
main (int argc,
      char **argv)
{
  TpTestsSimpleConnection *service_conn;
  TpBaseConnection *service_conn_as_base;
  TpDBusDaemon *dbus;
  TpConnection *conn;
  GError *error = NULL;
  gchar *name;
  gchar *conn_path;

  tp_tests_abort_after (10);
  g_type_init ();
  tp_debug_set_flags ("all");
  mainloop = g_main_loop_new (NULL, FALSE);
  dbus = tp_tests_dbus_daemon_dup_or_die ();

  service_conn = TP_TESTS_SIMPLE_CONNECTION (tp_tests_object_new_static_class (
        bug15306_connection_get_type (),
        "account", "me@example.com",
        "protocol", "simple",
        NULL));
  service_conn_as_base = TP_BASE_CONNECTION (service_conn);
  MYASSERT (service_conn != NULL, "");
  MYASSERT (service_conn_as_base != NULL, "");

  g_signal_connect (service_conn, "shutdown-finished",
      G_CALLBACK (on_shutdown_finished), NULL);

  MYASSERT (tp_base_connection_register (service_conn_as_base, "simple",
        &name, &conn_path, &error), "");
  g_assert_no_error (error);

  conn = tp_connection_new (dbus, name, conn_path, &error);
  MYASSERT (conn != NULL, "");
  g_assert_no_error (error);

  MYASSERT (tp_connection_run_until_ready (conn, TRUE, &error, NULL),
      "");
  g_assert_no_error (error);

  /* disconnect the service_conn */
  MYASSERT (tp_cli_connection_connect_to_status_changed (conn,
        on_status_changed, NULL, NULL, NULL, NULL), "");
  tp_tests_simple_connection_inject_disconnect (service_conn);
  g_main_loop_run (mainloop);

  g_object_unref (conn);

  service_conn_as_base = NULL;
  g_object_unref (service_conn);
  g_object_unref (dbus);
  g_main_loop_unref (mainloop);
  g_free (name);
  g_free (conn_path);

  return 0;
}
