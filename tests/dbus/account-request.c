/* A very basic feature test for TpAccountRequest
 *
 * Copyright (C) 2012 Collabora Ltd. <http://www.collabora.co.uk/>
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */

#include "config.h"

#include <string.h>

#include <telepathy-glib/account-request.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>

#include "tests/lib/simple-account.h"
#include "tests/lib/simple-account-manager.h"
#include "tests/lib/util.h"

typedef struct {
  GMainLoop *mainloop;
  TpDBusDaemon *dbus;

  TpTestsSimpleAccountManager *am;
  TpTestsSimpleAccount *account_service;

  TpAccountManager *account_manager;
  TpAccountRequest *ar;

  GAsyncResult *result;
  GError *error /* initialized where needed */;
} Test;

static void
setup (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  test->mainloop = g_main_loop_new (NULL, FALSE);
  test->dbus = tp_tests_dbus_daemon_dup_or_die ();
  g_assert (test->dbus != NULL);

  /* create the account manager service */
  tp_dbus_daemon_request_name (test->dbus,
      TP_ACCOUNT_MANAGER_BUS_NAME, FALSE, &test->error);
  g_assert_no_error (test->error);
  test->am = tp_tests_object_new_static_class (
      TP_TESTS_TYPE_SIMPLE_ACCOUNT_MANAGER, NULL);
  tp_dbus_daemon_register_object (test->dbus, TP_ACCOUNT_MANAGER_OBJECT_PATH,
      test->am);

  /* and now the account manager proxy */
  test->account_manager = tp_account_manager_dup ();
  g_assert (test->account_manager != NULL);

  /* finally create the account service */
  test->account_service = tp_tests_object_new_static_class (
      TP_TESTS_TYPE_SIMPLE_ACCOUNT, NULL);
  tp_dbus_daemon_register_object (test->dbus,
      TP_ACCOUNT_OBJECT_PATH_BASE "gabble/jabber/lospolloshermanos",
      test->account_service);

  test->ar = NULL;
}

static void
teardown (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  /* holds a ref to the TpAccountRequest and to the TpAccount */
  if (test->result != NULL)
    tp_tests_assert_last_unref (&test->result);

  tp_tests_assert_last_unref (&test->ar);

  /* It might have a GetAll() in-flight, so we have to wait */
  tp_tests_await_last_unref (&test->account_manager);

  tp_dbus_daemon_release_name (test->dbus, TP_ACCOUNT_MANAGER_BUS_NAME,
      &test->error);
  g_assert_no_error (test->error);
  tp_dbus_daemon_unregister_object (test->dbus, test->am);
  tp_tests_assert_last_unref (&test->am);

  tp_dbus_daemon_unregister_object (test->dbus, test->account_service);
  tp_tests_assert_last_unref (&test->account_service);

  tp_tests_assert_last_unref (&test->dbus);
  tp_clear_pointer (&test->mainloop, g_main_loop_unref);

  g_clear_error (&test->error);
}

static void
test_new (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  test->ar = tp_account_request_new (test->account_manager,
      "gabble", "jabber", "Gustavo Fring");
  g_assert (TP_IS_ACCOUNT_REQUEST (test->ar));
}

static void
test_gobject_properties (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  TpAccountManager *am;
  gchar *manager, *protocol, *display_name;

  test->ar = tp_account_request_new (test->account_manager,
      "gabble", "jabber", "Charles Dickens");

  g_object_get (test->ar,
      "account-manager", &am,
      "cm-name", &manager,
      "protocol-name", &protocol,
      "display-name", &display_name,
      NULL);

  g_assert (am == test->account_manager);
  g_assert_cmpstr (manager, ==, "gabble");
  g_assert_cmpstr (protocol, ==, "jabber");
  g_assert_cmpstr (display_name, ==, "Charles Dickens");

  g_object_unref (am);
  g_free (manager);
  g_free (protocol);
  g_free (display_name);
}

static void
test_parameters (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  GVariant *v_str, *v_int;
  GVariant *params;

  gboolean found;
  const gchar *s;
  guint u;

  test->ar = tp_account_request_new (test->account_manager,
      "gabble", "jabber", "Mike Ehrmantraut");

  v_str = g_variant_new_string ("banana");
  tp_account_request_set_parameter (test->ar, "cheese", v_str);
  g_variant_unref (v_str);

  v_int = g_variant_new_uint32 (42);
  tp_account_request_set_parameter (test->ar, "life", v_int);
  g_variant_unref (v_int);

  tp_account_request_set_parameter_string (test->ar,
      "great", "expectations");

  g_object_get (test->ar,
      "parameters", &params,
      NULL);

  g_assert_cmpuint (g_variant_n_children (params), ==, 3);

  found = g_variant_lookup (params, "cheese", "&s", &s);
  g_assert (found);
  g_assert_cmpstr (s, ==, "banana");
  found = g_variant_lookup (params, "life", "u", &u);
  g_assert (found);
  g_assert_cmpuint (u, ==, 42);
  found = g_variant_lookup (params, "great", "&s", &s);
  g_assert (found);
  g_assert_cmpstr (s, ==, "expectations");

  g_variant_unref (params);

  /* now let's unset one and see if it's okay */
  tp_account_request_unset_parameter (test->ar, "cheese");

  g_object_get (test->ar,
      "parameters", &params,
      NULL);

  g_assert_cmpuint (g_variant_n_children (params), ==, 2);

  found = g_variant_lookup (params, "life", "u", &u);
  g_assert (found);
  g_assert_cmpuint (u, ==, 42);
  found = g_variant_lookup (params, "great", "&s", &s);
  g_assert (found);
  g_assert_cmpstr (s, ==, "expectations");

  g_variant_unref (params);
}

static void
test_properties (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  GVariant *props;
  gchar *icon_name, *nickname;
  TpConnectionPresenceType presence_type;
  gchar *presence_status, *presence_message;
  gboolean enabled, connect_automatically;
  gchar **supersedes;
  GArray *avatar;
  gchar *mime_type;
  gboolean found;
  const gchar *s;
  gboolean b;
  GVariant *v;
  gchar *service, *storage_provider;

  test->ar = tp_account_request_new (test->account_manager,
      "gabble", "jabber", "Walter Jr.");

  g_object_get (test->ar,
      "properties", &props,
      NULL);

  g_assert_cmpuint (g_variant_n_children (props), ==, 0);

  g_variant_unref (props);

  /* now set an icon and try again */
  tp_account_request_set_icon_name (test->ar, "user32.dll");

  g_object_get (test->ar,
      "properties", &props,
      "icon-name", &icon_name,
      NULL);

  g_assert_cmpuint (g_variant_n_children (props), ==, 1);
  found = g_variant_lookup (props, TP_PROP_ACCOUNT_ICON, "&s", &s);
  g_assert (found);
  g_assert_cmpstr (s, ==, "user32.dll");
  g_assert_cmpstr (icon_name, ==, "user32.dll");

  g_variant_unref (props);
  g_free (icon_name);

  /* now set the nickname and try again */
  tp_account_request_set_nickname (test->ar, "Walter Jr.");

  g_object_get (test->ar,
      "properties", &props,
      "nickname", &nickname,
      NULL);

  g_assert_cmpuint (g_variant_n_children (props), ==, 2);
  found = g_variant_lookup (props, TP_PROP_ACCOUNT_ICON, "&s", &s);
  g_assert (found);
  g_assert_cmpstr (s, ==, "user32.dll");
  found = g_variant_lookup (props, TP_PROP_ACCOUNT_NICKNAME, "&s", &s);
  g_assert (found);
  g_assert_cmpstr (s, ==, "Walter Jr.");
  g_assert_cmpstr (nickname, ==, "Walter Jr.");

  g_variant_unref (props);
  g_free (nickname);

  /* next is requested presence */
  tp_account_request_set_requested_presence (test->ar,
      TP_CONNECTION_PRESENCE_TYPE_AVAILABLE, "available",
      "come at me, bro!");

  g_object_get (test->ar,
      "requested-presence-type", &presence_type,
      "requested-status", &presence_status,
      "requested-status-message", &presence_message,
      NULL);

  g_assert_cmpuint (presence_type, ==, TP_CONNECTION_PRESENCE_TYPE_AVAILABLE);
  g_assert_cmpstr (presence_status, ==, "available");
  g_assert_cmpstr (presence_message, ==, "come at me, bro!");

  g_free (presence_status);
  g_free (presence_message);

  /* and automatic presence */
  tp_account_request_set_automatic_presence (test->ar,
      TP_CONNECTION_PRESENCE_TYPE_BUSY, "busy",
      "come at me later, actually!");

  g_object_get (test->ar,
      "automatic-presence-type", &presence_type,
      "automatic-status", &presence_status,
      "automatic-status-message", &presence_message,
      NULL);

  g_assert_cmpuint (presence_type, ==, TP_CONNECTION_PRESENCE_TYPE_BUSY);
  g_assert_cmpstr (presence_status, ==, "busy");
  g_assert_cmpstr (presence_message, ==, "come at me later, actually!");

  g_free (presence_status);
  g_free (presence_message);

  /* now enabled and connect automatically */
  tp_account_request_set_enabled (test->ar, FALSE);
  tp_account_request_set_connect_automatically (test->ar, TRUE);

  g_object_get (test->ar,
      "properties", &props,
      "enabled", &enabled,
      "connect-automatically", &connect_automatically,
      NULL);

  g_assert_cmpint (enabled, ==, FALSE);
  g_assert_cmpint (connect_automatically, ==, TRUE);

  found = g_variant_lookup (props, TP_PROP_ACCOUNT_ENABLED, "b", &b);
  g_assert (found);
  g_assert_cmpint (b, ==, FALSE);
  found = g_variant_lookup (props, TP_PROP_ACCOUNT_CONNECT_AUTOMATICALLY,
      "b", &b);
  g_assert (found);
  g_assert_cmpint (b, ==, TRUE);

  g_variant_unref (props);

  /* supersedes */
  tp_account_request_add_supersedes (test->ar,
      "/science/yeah/woo");

  g_object_get (test->ar,
      "properties", &props,
      "supersedes", &supersedes,
      NULL);

  g_assert_cmpuint (g_strv_length (supersedes), ==, 1);
  g_assert_cmpstr (supersedes[0], ==,
      "/science/yeah/woo");
  g_assert (supersedes[1] == NULL);

  found = g_variant_lookup (props, TP_PROP_ACCOUNT_SUPERSEDES, "^a&o", NULL);
  g_assert (found);

  g_strfreev (supersedes);
  g_variant_unref (props);

  /* avatar */
  avatar = g_array_new (FALSE, FALSE, sizeof (guchar));
  g_array_append_vals (avatar, "hello world", strlen ("hello world") + 1);
  tp_account_request_set_avatar (test->ar,
      (const guchar *) avatar->data, avatar->len, "image/lolz");
  g_array_unref (avatar);
  avatar = NULL;

  g_object_get (test->ar,
      "properties", &props,
      "avatar", &avatar,
      "avatar-mime-type", &mime_type,
      NULL);

  g_assert_cmpstr (avatar->data, ==, "hello world");
  g_assert_cmpuint (avatar->len, ==, strlen ("hello world") + 1);
  g_assert_cmpstr (mime_type, ==, "image/lolz");

  v = g_variant_lookup_value (props, TP_PROP_ACCOUNT_INTERFACE_AVATAR1_AVATAR,
      NULL);
  g_assert (v != NULL);
  g_variant_unref (v);

  g_variant_unref (props);
  g_array_unref (avatar);
  g_free (mime_type);

  /* service */
  tp_account_request_set_service (test->ar, "Mushroom");

  g_object_get (test->ar,
      "properties", &props,
      "service", &service,
      NULL);

  v = g_variant_lookup_value (props, TP_PROP_ACCOUNT_SERVICE, NULL);
  g_assert (v != NULL);
  g_assert_cmpstr (g_variant_get_string (v, NULL), ==, "Mushroom");
  g_variant_unref (v);

  g_assert_cmpstr (service, ==, "Mushroom");

  g_variant_unref (props);
  g_free (service);

  /* storage provider */
  tp_account_request_set_storage_provider (test->ar, "my.provider");

  g_object_get (test->ar,
      "properties", &props,
      "storage-provider", &storage_provider,
      NULL);

  v = g_variant_lookup_value (props,
      TP_PROP_ACCOUNT_INTERFACE_STORAGE1_STORAGE_PROVIDER, NULL);
  g_assert (v != NULL);
  g_assert_cmpstr (g_variant_get_string (v, NULL), ==, "my.provider");
  g_variant_unref (v);

  g_assert_cmpstr (storage_provider, ==, "my.provider");

  g_variant_unref (props);
  g_free (storage_provider);
}

static void
test_create_succeed (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  TpAccount *account;
  GValueArray *array;
  GPtrArray *supersedes;
  GArray *avatar;

  test->ar = tp_account_request_new (test->account_manager,
      "gabble", "jabber", "Hank Schrader");

  tp_account_request_set_display_name (test->ar, "Walter White");
  tp_account_request_set_icon_name (test->ar, "gasmask");
  tp_account_request_set_nickname (test->ar, "Heisenberg");
  tp_account_request_set_requested_presence (test->ar,
      TP_CONNECTION_PRESENCE_TYPE_AVAILABLE, "available",
      "Better call Saul!");
  tp_account_request_set_automatic_presence (test->ar,
      TP_CONNECTION_PRESENCE_TYPE_BUSY, "busy",
      "Cooking");
  tp_account_request_set_enabled (test->ar, TRUE);
  tp_account_request_set_connect_automatically (test->ar, TRUE);

  tp_account_request_set_parameter_string (test->ar,
      "account", "walter@white.us");
  tp_account_request_set_parameter_string (test->ar,
      "password", "holly");

  tp_account_request_add_supersedes (test->ar,
      "/some/silly/account");

  avatar = g_array_new (FALSE, FALSE, sizeof (guchar));
  g_array_append_vals (avatar, "blue meth", strlen ("blue meth") + 1);
  tp_account_request_set_avatar (test->ar,
      (const guchar *) avatar->data, avatar->len, "image/png");
  g_array_unref (avatar);
  avatar = NULL;

  tp_account_request_create_account_async (test->ar,
      tp_tests_result_ready_cb, &test->result);
  tp_tests_run_until_result (&test->result);

  account = tp_account_request_create_account_finish (test->ar,
      test->result, &test->error);
  g_assert_no_error (test->error);
  g_assert (account != NULL);

  g_assert_cmpstr (test->am->create_cm, ==, "gabble");
  g_assert_cmpstr (test->am->create_protocol, ==, "jabber");
  g_assert_cmpstr (test->am->create_display_name, ==, "Walter White");
  g_assert_cmpuint (g_hash_table_size (test->am->create_parameters), ==, 2);
  g_assert_cmpstr (tp_asv_get_string (test->am->create_parameters, "account"),
      ==, "walter@white.us");
  g_assert_cmpstr (tp_asv_get_string (test->am->create_parameters, "password"),
      ==, "holly");
  g_assert_cmpuint (g_hash_table_size (test->am->create_properties), ==, 8);
  g_assert_cmpstr (tp_asv_get_string (test->am->create_properties,
          TP_PROP_ACCOUNT_ICON),
      ==, "gasmask");
  g_assert_cmpstr (tp_asv_get_string (test->am->create_properties,
          TP_PROP_ACCOUNT_NICKNAME),
      ==, "Heisenberg");
  g_assert_cmpint (tp_asv_get_boolean (test->am->create_properties,
          TP_PROP_ACCOUNT_ENABLED, NULL),
      ==, TRUE);
  g_assert_cmpint (tp_asv_get_boolean (test->am->create_properties,
          TP_PROP_ACCOUNT_CONNECT_AUTOMATICALLY, NULL),
      ==, TRUE);

  array = tp_asv_get_boxed (test->am->create_properties,
      TP_PROP_ACCOUNT_REQUESTED_PRESENCE,
      TP_STRUCT_TYPE_PRESENCE);
  g_assert_cmpuint (g_value_get_uint (array->values), ==,
      TP_CONNECTION_PRESENCE_TYPE_AVAILABLE);
  g_assert_cmpstr (g_value_get_string (array->values + 1), ==,
      "available");
  g_assert_cmpstr (g_value_get_string (array->values + 2), ==,
      "Better call Saul!");

  array = tp_asv_get_boxed (test->am->create_properties,
      TP_PROP_ACCOUNT_AUTOMATIC_PRESENCE,
      TP_STRUCT_TYPE_PRESENCE);
  g_assert_cmpuint (g_value_get_uint (array->values), ==,
      TP_CONNECTION_PRESENCE_TYPE_BUSY);
  g_assert_cmpstr (g_value_get_string (array->values + 1), ==,
      "busy");
  g_assert_cmpstr (g_value_get_string (array->values + 2), ==,
      "Cooking");

  supersedes = tp_asv_get_boxed (test->am->create_properties,
      TP_PROP_ACCOUNT_SUPERSEDES,
      TP_ARRAY_TYPE_OBJECT_PATH_LIST);
  g_assert_cmpuint (supersedes->len, ==, 1);
  g_assert_cmpstr (g_ptr_array_index (supersedes, 0), ==,
      "/some/silly/account");

  array = tp_asv_get_boxed (test->am->create_properties,
      TP_PROP_ACCOUNT_INTERFACE_AVATAR1_AVATAR,
      TP_STRUCT_TYPE_AVATAR);
  avatar = g_value_get_boxed (array->values);
  g_assert_cmpstr (avatar->data, ==, "blue meth");
  g_assert_cmpstr (g_value_get_string (array->values + 1), ==,
      "image/png");

  g_object_unref (account);
}

static void
test_create_fail (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  TpAccount *account;

  test->ar = tp_account_request_new (test->account_manager,
      "gabble", "jabber", "Walter White");

  /* this will make CreateAccount fail */
  tp_account_request_set_parameter_string (test->ar,
      "fail", "yes");

  tp_account_request_create_account_async (test->ar,
      tp_tests_result_ready_cb, &test->result);
  tp_tests_run_until_result (&test->result);

  account = tp_account_request_create_account_finish (test->ar,
      test->result, &test->error);
  g_assert (test->error != NULL);
  g_assert (account == NULL);

  g_clear_error (&test->error);
  tp_tests_assert_last_unref (&test->result);

  /* now let's unset the fail=yes and make sure it works */

  tp_account_request_unset_parameter (test->ar, "fail");

  tp_account_request_create_account_async (test->ar,
      tp_tests_result_ready_cb, &test->result);
  tp_tests_run_until_result (&test->result);

  account = tp_account_request_create_account_finish (test->ar,
      test->result, &test->error);
  g_assert_no_error (test->error);
  g_assert (account != NULL);

  g_object_unref (account);
}

int
main (int argc,
    char **argv)
{
  tp_tests_abort_after (10);
  tp_debug_set_flags ("all");

  g_test_init (&argc, &argv, NULL);
  g_test_bug_base ("http://bugs.freedesktop.org/show_bug.cgi?id=");

  g_test_add ("/account-request/new", Test, NULL, setup, test_new, teardown);
  g_test_add ("/account-request/gobject-properties", Test, NULL, setup,
      test_gobject_properties, teardown);
  g_test_add ("/account-request/parameters", Test, NULL, setup,
      test_parameters, teardown);
  g_test_add ("/account-request/properties", Test, NULL, setup,
      test_properties, teardown);
  g_test_add ("/account-request/create-succeed", Test, NULL, setup,
      test_create_succeed, teardown);
  g_test_add ("/account-request/create-fail", Test, NULL, setup,
      test_create_fail, teardown);

  return tp_tests_run_with_bus ();
}
