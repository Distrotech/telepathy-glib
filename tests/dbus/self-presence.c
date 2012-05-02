/* Feature test for setting your own presence.
 *
 * Copyright (C) 2009 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2009 Nokia Corporation
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */

#include "config.h"

#include <telepathy-glib/connection.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/debug.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>

#include "tests/lib/contacts-conn.h"
#include "tests/lib/debug.h"
#include "tests/lib/myassert.h"
#include "tests/lib/util.h"

static void
test_simple_presence (TpTestsContactsConnection *service_conn,
                      TpConnection *client_conn)
{
  GError *error = NULL;
  GValue *value = NULL;
  GHashTable *statuses;
  GValueArray *spec;

  MYASSERT (tp_cli_dbus_properties_run_get (client_conn, -1,
        TP_IFACE_CONNECTION_INTERFACE_SIMPLE_PRESENCE, "Statuses",
        &value, &error, NULL), "");
  g_assert_no_error (error);

  MYASSERT (G_VALUE_TYPE (value) == TP_HASH_TYPE_SIMPLE_STATUS_SPEC_MAP,
      ": %s != %s", G_VALUE_TYPE_NAME (value),
      g_type_name (TP_HASH_TYPE_SIMPLE_STATUS_SPEC_MAP));

  statuses = g_value_get_boxed (value);

  spec = g_hash_table_lookup (statuses, "available");
  MYASSERT (spec != NULL, "");
  g_assert_cmpuint (g_value_get_uint (spec->values + 0), ==,
      TP_CONNECTION_PRESENCE_TYPE_AVAILABLE);
  MYASSERT (g_value_get_boolean (spec->values + 1), ""); /* can set on self */
  MYASSERT (g_value_get_boolean (spec->values + 2), ""); /* can have message */

  spec = g_hash_table_lookup (statuses, "busy");
  MYASSERT (spec != NULL, "");
  g_assert_cmpuint (g_value_get_uint (spec->values + 0), ==,
      TP_CONNECTION_PRESENCE_TYPE_BUSY);
  MYASSERT (g_value_get_boolean (spec->values + 1), ""); /* can set on self */
  MYASSERT (g_value_get_boolean (spec->values + 2), ""); /* can have message */

  spec = g_hash_table_lookup (statuses, "away");
  MYASSERT (spec != NULL, "");
  g_assert_cmpuint (g_value_get_uint (spec->values + 0), ==,
      TP_CONNECTION_PRESENCE_TYPE_AWAY);
  MYASSERT (g_value_get_boolean (spec->values + 1), ""); /* can set on self */
  MYASSERT (g_value_get_boolean (spec->values + 2), ""); /* can have message */

  spec = g_hash_table_lookup (statuses, "offline");
  MYASSERT (spec != NULL, "");
  g_assert_cmpuint (g_value_get_uint (spec->values + 0), ==,
      TP_CONNECTION_PRESENCE_TYPE_OFFLINE);
  MYASSERT (!g_value_get_boolean (spec->values + 1), ""); /* can set on self */

  spec = g_hash_table_lookup (statuses, "unknown");
  MYASSERT (spec != NULL, "");
  g_assert_cmpuint (g_value_get_uint (spec->values + 0), ==,
      TP_CONNECTION_PRESENCE_TYPE_UNKNOWN);
  MYASSERT (!g_value_get_boolean (spec->values + 1), ""); /* can set on self */

  spec = g_hash_table_lookup (statuses, "error");
  MYASSERT (spec != NULL, "");
  g_assert_cmpuint (g_value_get_uint (spec->values + 0), ==,
      TP_CONNECTION_PRESENCE_TYPE_ERROR);
  MYASSERT (!g_value_get_boolean (spec->values + 1), ""); /* can set on self */

  g_value_unset (value);
  g_free (value);

  MYASSERT (!tp_cli_connection_interface_simple_presence_run_set_presence (
        client_conn, -1, "offline", "", &error, NULL), "");
  g_assert_cmpstr (g_quark_to_string (error->domain), ==,
      g_quark_to_string (TP_ERROR));
  g_error_free (error);
  error = NULL;

  MYASSERT (tp_cli_connection_interface_simple_presence_run_set_presence (
        client_conn, -1, "available", "Here I am", &error, NULL), "");
  g_assert_no_error (error);

  value = NULL;

  MYASSERT (tp_cli_dbus_properties_run_get (client_conn, -1,
        TP_IFACE_CONNECTION_INTERFACE_SIMPLE_PRESENCE,
        "MaximumStatusMessageLength",
        &value, &error, NULL), "");
  g_assert_no_error (error);

  MYASSERT (G_VALUE_TYPE (value) == G_TYPE_UINT,
      ": %s != %s", G_VALUE_TYPE_NAME (value),
      g_type_name (G_TYPE_UINT));
  g_assert_cmpuint (g_value_get_uint (value), ==,
      512);

  g_value_unset (value);
  g_free (value);
}

static void
test_complex_presence (TpTestsContactsConnection *service_conn,
              TpConnection *client_conn)
{
  GHashTable *statuses = NULL;
  GValueArray *spec;
  GHashTable *params;
  GError *error = NULL;
  GHashTable *monster;

  MYASSERT (tp_cli_connection_interface_presence_run_get_statuses (
        client_conn, -1, &statuses, &error, NULL), "");
  g_assert_no_error (error);

  spec = g_hash_table_lookup (statuses, "available");
  MYASSERT (spec != NULL, "");
  g_assert_cmpuint (g_value_get_uint (spec->values + 0), ==,
      TP_CONNECTION_PRESENCE_TYPE_AVAILABLE);
  MYASSERT (g_value_get_boolean (spec->values + 1), ""); /* can set on self */
  MYASSERT (g_value_get_boolean (spec->values + 2), ""); /* exclusive */
  params = g_value_get_boxed (spec->values + 3);
  MYASSERT (params != NULL, "");
  g_assert_cmpuint (g_hash_table_size (params), ==, 1);
  g_assert_cmpstr (
      (const gchar *) g_hash_table_lookup (params, "message"), ==, "s");

  spec = g_hash_table_lookup (statuses, "away");
  MYASSERT (spec != NULL, "");
  g_assert_cmpuint (g_value_get_uint (spec->values + 0), ==,
      TP_CONNECTION_PRESENCE_TYPE_AWAY);
  MYASSERT (g_value_get_boolean (spec->values + 1), ""); /* can set on self */
  MYASSERT (g_value_get_boolean (spec->values + 2), ""); /* exclusive */
  params = g_value_get_boxed (spec->values + 3);
  MYASSERT (params != NULL, "");
  g_assert_cmpuint (g_hash_table_size (params), ==, 1);
  g_assert_cmpstr (
      (const gchar *) g_hash_table_lookup (params, "message"), ==, "s");

  spec = g_hash_table_lookup (statuses, "busy");
  MYASSERT (spec != NULL, "");
  g_assert_cmpuint (g_value_get_uint (spec->values + 0), ==,
      TP_CONNECTION_PRESENCE_TYPE_BUSY);
  MYASSERT (g_value_get_boolean (spec->values + 1), ""); /* can set on self */
  MYASSERT (g_value_get_boolean (spec->values + 2), ""); /* exclusive */
  params = g_value_get_boxed (spec->values + 3);
  MYASSERT (params != NULL, "");
  g_assert_cmpuint (g_hash_table_size (params), ==, 1);
  g_assert_cmpstr (
      (const gchar *) g_hash_table_lookup (params, "message"), ==, "s");

  spec = g_hash_table_lookup (statuses, "offline");
  MYASSERT (spec != NULL, "");
  g_assert_cmpuint (g_value_get_uint (spec->values + 0), ==,
      TP_CONNECTION_PRESENCE_TYPE_OFFLINE);
  MYASSERT (!g_value_get_boolean (spec->values + 1), ""); /* can set on self */
  MYASSERT (g_value_get_boolean (spec->values + 2), ""); /* exclusive */
  params = g_value_get_boxed (spec->values + 3);
  MYASSERT (params != NULL, "");
  g_assert_cmpuint (g_hash_table_size (params), ==, 0);

  spec = g_hash_table_lookup (statuses, "error");
  MYASSERT (spec != NULL, "");
  g_assert_cmpuint (g_value_get_uint (spec->values + 0), ==,
      TP_CONNECTION_PRESENCE_TYPE_ERROR);
  MYASSERT (!g_value_get_boolean (spec->values + 1), ""); /* can set on self */
  MYASSERT (g_value_get_boolean (spec->values + 2), ""); /* exclusive */
  params = g_value_get_boxed (spec->values + 3);
  MYASSERT (params != NULL, "");
  g_assert_cmpuint (g_hash_table_size (params), ==, 0);

  spec = g_hash_table_lookup (statuses, "unknown");
  MYASSERT (spec != NULL, "");
  g_assert_cmpuint (g_value_get_uint (spec->values + 0), ==,
      TP_CONNECTION_PRESENCE_TYPE_UNKNOWN);
  MYASSERT (!g_value_get_boolean (spec->values + 1), ""); /* can set on self */
  MYASSERT (g_value_get_boolean (spec->values + 2), ""); /* exclusive */
  params = g_value_get_boxed (spec->values + 3);
  MYASSERT (params != NULL, "");
  g_assert_cmpuint (g_hash_table_size (params), ==, 0);

  monster = g_hash_table_new (g_str_hash, g_str_equal);
  params = g_hash_table_new (g_str_hash, g_str_equal);

  g_hash_table_insert (monster, "offline", params);

  MYASSERT (!tp_cli_connection_interface_presence_run_set_status (
        client_conn, -1, monster, &error, NULL), "");
  g_assert_cmpstr (g_quark_to_string (error->domain), ==,
      g_quark_to_string (TP_ERROR));
  g_error_free (error);
  error = NULL;

  g_hash_table_remove (monster, "offline");
  g_hash_table_insert (monster, "available", params);

  MYASSERT (tp_cli_connection_interface_presence_run_set_status (
        client_conn, -1, monster, &error, NULL), "");
  g_assert_no_error (error);

  g_hash_table_unref (params);
  params = NULL;
  g_hash_table_unref (monster);
  monster = NULL;
  g_hash_table_unref (statuses);
  statuses = NULL;
}

int
main (int argc,
      char **argv)
{
  TpDBusDaemon *dbus;
  TpTestsContactsConnection *service_conn;
  TpBaseConnection *service_conn_as_base;
  gchar *name;
  gchar *conn_path;
  GError *error = NULL;
  TpConnection *client_conn;
  guint status;
  gchar **interfaces;

  /* Setup */

  tp_tests_abort_after (10);
  g_type_init ();
  tp_debug_set_flags ("all");
  dbus = tp_tests_dbus_daemon_dup_or_die ();

  service_conn = TP_TESTS_CONTACTS_CONNECTION (
      tp_tests_object_new_static_class (
        TP_TESTS_TYPE_CONTACTS_CONNECTION,
        "account", "me@example.com",
        "protocol", "simple",
        NULL));
  service_conn_as_base = TP_BASE_CONNECTION (service_conn);
  MYASSERT (service_conn != NULL, "");
  MYASSERT (service_conn_as_base != NULL, "");

  MYASSERT (tp_base_connection_register (service_conn_as_base, "simple",
        &name, &conn_path, &error), "");
  g_assert_no_error (error);

  client_conn = tp_connection_new (dbus, name, conn_path, &error);
  MYASSERT (client_conn != NULL, "");
  g_assert_no_error (error);

  /* Assert that GetInterfaces succeeds before we're CONNECTED */
  MYASSERT (tp_cli_connection_run_get_interfaces (client_conn, -1, &interfaces,
        &error, NULL), "");
  g_assert_no_error (error);
  MYASSERT (tp_strv_contains ((const gchar * const *) interfaces,
      TP_IFACE_CONNECTION_INTERFACE_ALIASING), "");
  MYASSERT (tp_strv_contains ((const gchar * const *) interfaces,
      TP_IFACE_CONNECTION_INTERFACE_AVATARS), "");
  MYASSERT (tp_strv_contains ((const gchar * const *) interfaces,
      TP_IFACE_CONNECTION_INTERFACE_CONTACTS), "");
  MYASSERT (tp_strv_contains ((const gchar * const *) interfaces,
      TP_IFACE_CONNECTION_INTERFACE_PRESENCE), "");
  MYASSERT (tp_strv_contains ((const gchar * const *) interfaces,
      TP_IFACE_CONNECTION_INTERFACE_SIMPLE_PRESENCE), "");
  g_strfreev (interfaces);

  MYASSERT (tp_cli_connection_run_get_status (client_conn, -1, &status,
        &error, NULL), "");
  g_assert_cmpuint (status, ==, (guint) TP_CONNECTION_STATUS_DISCONNECTED);
  g_assert_no_error (error);

  MYASSERT (tp_connection_run_until_ready (client_conn, TRUE, &error, NULL),
      "");
  g_assert_no_error (error);

  /* Tests */

  test_simple_presence (service_conn, client_conn);
  test_complex_presence (service_conn, client_conn);

  /* Teardown */

  tp_tests_connection_assert_disconnect_succeeds (client_conn);
  g_object_unref (client_conn);

  service_conn_as_base = NULL;
  g_object_unref (service_conn);
  g_free (name);
  g_free (conn_path);

  g_object_unref (dbus);

  return 0;
}
