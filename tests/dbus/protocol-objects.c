/* Regression test for Protocol objects in the echo-2 example CM.
 *
 * Copyright © 2010 Collabora Ltd. <http://www.collabora.co.uk/>
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */

#include <telepathy-glib/protocol.h>
#include <telepathy-glib/telepathy-glib.h>

#include "examples/cm/echo/connection-manager.h"

#include "examples/cm/echo-message-parts/connection-manager.h"
#include "examples/cm/echo-message-parts/chan.h"
#include "examples/cm/echo-message-parts/conn.h"

#include "tests/lib/util.h"

#define CLEAR_OBJECT(o) \
  G_STMT_START { \
      if (*(o) != NULL) \
        { \
          g_object_unref (*(o)); \
          *(o) = NULL; \
        } \
  } G_STMT_END

typedef struct
{
  GMainLoop *mainloop;
  TpDBusDaemon *dbus;
  GError *error /* statically initialized to NULL */ ;

  ExampleEcho2ConnectionManager *service_cm;

  TpConnectionManager *cm;
  TpProtocol *protocol;

  ExampleEchoConnectionManager *old_service_cm;
  TpConnectionManager *old_cm;
  TpProtocol *old_protocol;
} Test;

static void
setup (Test *test,
       gconstpointer data G_GNUC_UNUSED)
{
  TpBaseConnectionManager *service_cm_as_base;
  gboolean ok;

  g_type_init ();
  tp_debug_set_flags ("all");

  test->mainloop = g_main_loop_new (NULL, FALSE);
  test->dbus = tp_dbus_daemon_dup (NULL);
  g_assert (test->dbus != NULL);

  test->service_cm = EXAMPLE_ECHO_2_CONNECTION_MANAGER (g_object_new (
        EXAMPLE_TYPE_ECHO_2_CONNECTION_MANAGER,
        NULL));
  g_assert (test->service_cm != NULL);
  service_cm_as_base = TP_BASE_CONNECTION_MANAGER (test->service_cm);
  g_assert (service_cm_as_base != NULL);

  ok = tp_base_connection_manager_register (service_cm_as_base);
  g_assert (ok);

  test->cm = tp_connection_manager_new (test->dbus, "example_echo_2",
      NULL, &test->error);
  g_assert (test->cm != NULL);
  test_connection_manager_run_until_ready (test->cm);

  test->protocol = tp_protocol_new (test->dbus, "example_echo_2",
      "example", NULL, NULL);
  g_assert (test->protocol != NULL);

  test->old_service_cm = EXAMPLE_ECHO_CONNECTION_MANAGER (g_object_new (
        EXAMPLE_TYPE_ECHO_CONNECTION_MANAGER,
        NULL));
  g_assert (test->old_service_cm != NULL);
  service_cm_as_base = TP_BASE_CONNECTION_MANAGER (test->old_service_cm);
  g_assert (service_cm_as_base != NULL);

  ok = tp_base_connection_manager_register (service_cm_as_base);
  g_assert (ok);

  test->old_cm = tp_connection_manager_new (test->dbus, "example_echo",
      NULL, &test->error);
  g_assert (test->old_cm != NULL);
  test_connection_manager_run_until_ready (test->old_cm);

  test->old_protocol = NULL;
}

static void
teardown (Test *test,
          gconstpointer data G_GNUC_UNUSED)
{
  CLEAR_OBJECT (&test->protocol);
  CLEAR_OBJECT (&test->cm);
  CLEAR_OBJECT (&test->service_cm);
  CLEAR_OBJECT (&test->old_service_cm);

  CLEAR_OBJECT (&test->dbus);
  g_main_loop_unref (test->mainloop);
  test->mainloop = NULL;
}

const gchar * const no_interfaces[] = { NULL };
const gchar * const expected_interfaces[] = {
    TP_IFACE_CONNECTION_INTERFACE_REQUESTS,
    NULL };

static void
test_protocol_properties (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  GHashTable *properties = NULL;
  GPtrArray *arr;
  GValueArray *va;
  GHashTable *fixed;

  tp_cli_dbus_properties_run_get_all (test->protocol, -1,
      TP_IFACE_PROTOCOL, &properties, &test->error, NULL);
  test_assert_no_error (test->error);

  test_assert_strv_equals (tp_asv_get_boxed (properties, "Interfaces",
        G_TYPE_STRV), no_interfaces);

  g_assert_cmpstr (tp_asv_get_string (properties, "Icon"), ==, "im-icq");
  g_assert_cmpstr (tp_asv_get_string (properties, "EnglishName"), ==,
      "Echo II example");
  g_assert_cmpstr (tp_asv_get_string (properties, "VCardField"), ==,
      "x-telepathy-example");
  g_assert_cmpstr (tp_asv_get_string (properties, "VCardField"), ==,
      "x-telepathy-example");

  test_assert_strv_equals (tp_asv_get_boxed (properties,
        "ConnectionInterfaces", G_TYPE_STRV), expected_interfaces);

  arr = tp_asv_get_boxed (properties, "RequestableChannelClasses",
      TP_ARRAY_TYPE_REQUESTABLE_CHANNEL_CLASS_LIST);
  g_assert (arr != NULL);
  g_assert_cmpuint (arr->len, ==, 1);

  va = g_ptr_array_index (arr, 0);
  g_assert (G_VALUE_HOLDS (va->values + 0, TP_HASH_TYPE_CHANNEL_CLASS));
  g_assert (G_VALUE_HOLDS (va->values + 1, G_TYPE_STRV));

  fixed = g_value_get_boxed (va->values + 0);
  g_assert_cmpstr (tp_asv_get_string (fixed, TP_PROP_CHANNEL_CHANNEL_TYPE), ==,
      TP_IFACE_CHANNEL_TYPE_TEXT);

  arr = tp_asv_get_boxed (properties, "Parameters",
      TP_ARRAY_TYPE_PARAM_SPEC_LIST);
  g_assert (arr != NULL);
  g_assert_cmpuint (arr->len, >=, 1);
}

static void
test_protocols_property (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  GHashTable *properties = NULL;
  GHashTable *protocols;
  GHashTable *pp;
  GPtrArray *arr;
  GValueArray *va;
  GHashTable *fixed;

  tp_cli_dbus_properties_run_get_all (test->cm, -1,
      TP_IFACE_CONNECTION_MANAGER, &properties, &test->error, NULL);
  test_assert_no_error (test->error);

  g_assert (tp_asv_lookup (properties, "Interfaces") != NULL);
  test_assert_strv_equals (tp_asv_get_boxed (properties, "Interfaces",
        G_TYPE_STRV), no_interfaces);

  protocols = tp_asv_get_boxed (properties, "Protocols",
      TP_HASH_TYPE_PROTOCOL_PROPERTIES_MAP);
  g_assert (protocols != NULL);
  g_assert_cmpuint (g_hash_table_size (protocols), ==, 1);

  pp = g_hash_table_lookup (protocols, "example");
  g_assert (pp != NULL);

  test_assert_strv_equals (tp_asv_get_boxed (pp, TP_PROP_PROTOCOL_INTERFACES,
        G_TYPE_STRV), no_interfaces);

  g_assert_cmpstr (tp_asv_get_string (pp, TP_PROP_PROTOCOL_ICON), ==,
      "im-icq");
  g_assert_cmpstr (tp_asv_get_string (pp, TP_PROP_PROTOCOL_ENGLISH_NAME), ==,
      "Echo II example");
  g_assert_cmpstr (tp_asv_get_string (pp, TP_PROP_PROTOCOL_VCARD_FIELD), ==,
      "x-telepathy-example");

  test_assert_strv_equals (tp_asv_get_boxed (pp,
        TP_PROP_PROTOCOL_CONNECTION_INTERFACES, G_TYPE_STRV),
      expected_interfaces);

  arr = tp_asv_get_boxed (pp, TP_PROP_PROTOCOL_REQUESTABLE_CHANNEL_CLASSES,
      TP_ARRAY_TYPE_REQUESTABLE_CHANNEL_CLASS_LIST);
  g_assert (arr != NULL);
  g_assert_cmpuint (arr->len, ==, 1);

  va = g_ptr_array_index (arr, 0);
  g_assert (G_VALUE_HOLDS (va->values + 0, TP_HASH_TYPE_CHANNEL_CLASS));
  g_assert (G_VALUE_HOLDS (va->values + 1, G_TYPE_STRV));

  fixed = g_value_get_boxed (va->values + 0);
  g_assert_cmpstr (tp_asv_get_string (fixed, TP_PROP_CHANNEL_CHANNEL_TYPE), ==,
      TP_IFACE_CHANNEL_TYPE_TEXT);

  arr = tp_asv_get_boxed (pp, TP_PROP_PROTOCOL_PARAMETERS,
      TP_ARRAY_TYPE_PARAM_SPEC_LIST);
  g_assert (arr != NULL);
  g_assert_cmpuint (arr->len, >=, 1);
}

static void
test_protocols_property_old (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  GHashTable *properties = NULL;
  GHashTable *protocols;
  GHashTable *pp;
  GPtrArray *arr;

  tp_cli_dbus_properties_run_get_all (test->old_cm, -1,
      TP_IFACE_CONNECTION_MANAGER, &properties, &test->error, NULL);
  test_assert_no_error (test->error);

  g_assert (tp_asv_lookup (properties, "Interfaces") != NULL);
  test_assert_empty_strv (tp_asv_get_boxed (properties, "Interfaces",
        G_TYPE_STRV));

  protocols = tp_asv_get_boxed (properties, "Protocols",
      TP_HASH_TYPE_PROTOCOL_PROPERTIES_MAP);
  g_assert (protocols != NULL);
  g_assert_cmpuint (g_hash_table_size (protocols), ==, 1);

  pp = g_hash_table_lookup (protocols, "example");
  g_assert (pp != NULL);

  g_assert (tp_asv_lookup (pp, TP_PROP_PROTOCOL_INTERFACES) == NULL);
  g_assert (tp_asv_lookup (pp, TP_PROP_PROTOCOL_ICON) == NULL);
  g_assert (tp_asv_lookup (pp, TP_PROP_PROTOCOL_ENGLISH_NAME) == NULL);
  g_assert (tp_asv_lookup (pp, TP_PROP_PROTOCOL_VCARD_FIELD) == NULL);
  g_assert (tp_asv_lookup (pp,
        TP_PROP_PROTOCOL_CONNECTION_INTERFACES) == NULL);
  g_assert (tp_asv_lookup (pp, TP_PROP_PROTOCOL_REQUESTABLE_CHANNEL_CLASSES)
      == NULL);

  arr = tp_asv_get_boxed (pp, TP_PROP_PROTOCOL_PARAMETERS,
      TP_ARRAY_TYPE_PARAM_SPEC_LIST);
  g_assert (arr != NULL);
  g_assert_cmpuint (arr->len, >=, 1);

}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_bug_base ("http://bugs.freedesktop.org/show_bug.cgi?id=");

  g_test_add ("/protocol-objects/protocol-properties", Test, NULL, setup,
      test_protocol_properties, teardown);
  g_test_add ("/protocol-objects/protocols-property", Test, NULL, setup,
      test_protocols_property, teardown);
  g_test_add ("/protocol-objects/protocols-property-old", Test, NULL, setup,
      test_protocols_property_old, teardown);

  return g_test_run ();
}
