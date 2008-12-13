/* Basic introspection on a channel (template for further regression tests)
 *
 * Copyright (C) 2007-2008 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2007-2008 Nokia Corporation
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */

#include <telepathy-glib/channel.h>
#include <telepathy-glib/connection.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/debug.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/proxy-subclass.h>

#include "tests/lib/myassert.h"
#include "tests/lib/simple-conn.h"
#include "tests/lib/textchan-null.h"
#include "tests/lib/util.h"

#define IDENTIFIER "them@example.org"

static int fail = 0;
static GError *invalidated = NULL;
static GMainLoop *mainloop;

static void
myassert_failed (void)
{
  fail = 1;
}

static void
channel_ready (TpChannel *channel,
               const GError *error,
               gpointer user_data)
{
  gboolean *set = user_data;

  *set = TRUE;

  if (error == NULL)
    {
      g_message ("channel %p ready", channel);
    }
  else
    {
      g_message ("channel %p invalidated: %s #%u \"%s\"", channel,
          g_quark_to_string (error->domain), error->code, error->message);

      invalidated = g_error_copy (error);
    }

  if (mainloop != NULL)
    g_main_loop_quit (mainloop);
}

static void
assert_chan_sane (TpChannel *chan,
                  TpHandle handle)
{
  GHashTable *asv;
  TpHandleType type;

  MYASSERT (tp_channel_is_ready (chan), "");
  MYASSERT (tp_channel_get_handle (chan, NULL) == handle, "");
  MYASSERT (tp_channel_get_handle (chan, &type) == handle, "");
  MYASSERT (type == TP_HANDLE_TYPE_CONTACT, "%u", type);
  MYASSERT (g_str_equal (tp_channel_get_channel_type (chan),
        TP_IFACE_CHANNEL_TYPE_TEXT), "");
  MYASSERT (tp_channel_get_channel_type_id (chan) ==
        TP_IFACE_QUARK_CHANNEL_TYPE_TEXT, "");
  MYASSERT (TP_IS_CONNECTION (tp_channel_borrow_connection (chan)), "");
  MYASSERT_SAME_STRING (tp_channel_get_identifier (chan), IDENTIFIER);

  asv = tp_channel_borrow_immutable_properties (chan);
  MYASSERT (asv != NULL, "");
  MYASSERT_SAME_STRING (
      tp_asv_get_string (asv, TP_IFACE_CHANNEL ".ChannelType"),
      TP_IFACE_CHANNEL_TYPE_TEXT);
  MYASSERT_SAME_UINT (
      tp_asv_get_uint32 (asv, TP_IFACE_CHANNEL ".TargetHandleType", NULL),
      TP_HANDLE_TYPE_CONTACT);
  MYASSERT_SAME_UINT (
      tp_asv_get_uint32 (asv, TP_IFACE_CHANNEL ".TargetHandle", NULL),
      handle);
  MYASSERT_SAME_STRING (
      tp_asv_get_string (asv, TP_IFACE_CHANNEL ".TargetID"),
      IDENTIFIER);
}

int
main (int argc,
      char **argv)
{
  SimpleConnection *service_conn;
  TpBaseConnection *service_conn_as_base;
  TpHandleRepoIface *contact_repo;
  TestTextChannelNull *service_chan;
  TestTextChannelNull *service_props_chan;
  TpDBusDaemon *dbus;
  TpConnection *conn;
  TpChannel *chan;
  GError *error = NULL;
  gchar *name;
  gchar *conn_path;
  gchar *chan_path;
  gchar *props_chan_path;
  gchar *bad_chan_path;
  TpHandle handle;
  gboolean was_ready;
  GError invalidated_for_test = { TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
      "No channel for you!" };
  GHashTable *asv;
  GValue *value;

  g_type_init ();
  tp_debug_set_flags ("all");

  service_conn = SIMPLE_CONNECTION (g_object_new (SIMPLE_TYPE_CONNECTION,
        "account", "me@example.com",
        "protocol", "simple",
        NULL));
  service_conn_as_base = TP_BASE_CONNECTION (service_conn);
  MYASSERT (service_conn != NULL, "");
  MYASSERT (service_conn_as_base != NULL, "");

  MYASSERT (tp_base_connection_register (service_conn_as_base, "simple",
        &name, &conn_path, &error), "");
  MYASSERT_NO_ERROR (error);

  dbus = tp_dbus_daemon_new (tp_get_bus ());
  conn = tp_connection_new (dbus, name, conn_path, &error);
  MYASSERT (conn != NULL, "");
  MYASSERT_NO_ERROR (error);

  MYASSERT (tp_connection_run_until_ready (conn, TRUE, &error, NULL),
      "");
  MYASSERT_NO_ERROR (error);

  contact_repo = tp_base_connection_get_handles (service_conn_as_base,
      TP_HANDLE_TYPE_CONTACT);
  MYASSERT (contact_repo != NULL, "");

  handle = tp_handle_ensure (contact_repo, IDENTIFIER, NULL, &error);
  MYASSERT_NO_ERROR (error);

  chan_path = g_strdup_printf ("%s/Channel", conn_path);

  service_chan = TEST_TEXT_CHANNEL_NULL (g_object_new (
        TEST_TYPE_TEXT_CHANNEL_NULL,
        "connection", service_conn,
        "object-path", chan_path,
        "handle", handle,
        NULL));

  props_chan_path = g_strdup_printf ("%s/PropertiesChannel", conn_path);

  service_props_chan = TEST_TEXT_CHANNEL_NULL (g_object_new (
        TEST_TYPE_PROPS_TEXT_CHANNEL,
        "connection", service_conn,
        "object-path", props_chan_path,
        "handle", handle,
        NULL));

  mainloop = g_main_loop_new (NULL, FALSE);

  g_message ("Channel becomes invalid while we wait");

  chan = tp_channel_new (conn, chan_path, TP_IFACE_CHANNEL_TYPE_TEXT,
      TP_HANDLE_TYPE_CONTACT, handle, &error);
  MYASSERT_NO_ERROR (error);
  tp_proxy_invalidate ((TpProxy *) chan, &invalidated_for_test);

  MYASSERT (!tp_channel_run_until_ready (chan, &error, NULL), "");
  MYASSERT (error != NULL, "");
  MYASSERT_SAME_ERROR (&invalidated_for_test, error);
  g_error_free (error);
  error = NULL;

  g_object_unref (chan);
  chan = NULL;

  g_message ("Channel becomes invalid and we are called back synchronously");

  chan = tp_channel_new (conn, chan_path, TP_IFACE_CHANNEL_TYPE_TEXT,
      TP_HANDLE_TYPE_CONTACT, handle, &error);
  MYASSERT_NO_ERROR (error);

  was_ready = FALSE;
  tp_channel_call_when_ready (chan, channel_ready, &was_ready);
  tp_proxy_invalidate ((TpProxy *) chan, &invalidated_for_test);
  MYASSERT (was_ready == TRUE, "");
  MYASSERT (invalidated != NULL, "");
  MYASSERT_SAME_ERROR (&invalidated_for_test, invalidated);
  g_error_free (invalidated);
  invalidated = NULL;

  g_object_unref (chan);
  chan = NULL;

  g_message ("Channel becomes ready while we wait");

  test_connection_run_until_dbus_queue_processed (conn);

  service_chan->get_handle_called = 0;
  service_chan->get_interfaces_called = 0;
  service_chan->get_channel_type_called = 0;

  chan = tp_channel_new (conn, chan_path, TP_IFACE_CHANNEL_TYPE_TEXT,
      TP_HANDLE_TYPE_CONTACT, handle, &error);
  MYASSERT_NO_ERROR (error);

  MYASSERT (tp_channel_run_until_ready (chan, &error, NULL), "");
  MYASSERT_NO_ERROR (error);
  MYASSERT_SAME_UINT (service_chan->get_handle_called, 0);
  MYASSERT_SAME_UINT (service_chan->get_interfaces_called, 1);
  MYASSERT_SAME_UINT (service_chan->get_channel_type_called, 0);

  assert_chan_sane (chan, handle);

  g_object_unref (chan);
  chan = NULL;

  g_message ("Channel becomes ready while we wait (the version with "
      "Properties)");

  test_connection_run_until_dbus_queue_processed (conn);

  service_props_chan->get_handle_called = 0;
  service_props_chan->get_interfaces_called = 0;
  service_props_chan->get_channel_type_called = 0;

  chan = tp_channel_new (conn, props_chan_path, NULL,
      TP_UNKNOWN_HANDLE_TYPE, 0, &error);
  MYASSERT_NO_ERROR (error);

  MYASSERT (tp_channel_run_until_ready (chan, &error, NULL), "");
  MYASSERT_NO_ERROR (error);
  MYASSERT_SAME_UINT (service_props_chan->get_handle_called, 0);
  MYASSERT_SAME_UINT (service_props_chan->get_channel_type_called, 0);
  MYASSERT_SAME_UINT (service_props_chan->get_interfaces_called, 0);

  assert_chan_sane (chan, handle);

  g_object_unref (chan);
  chan = NULL;

  g_message ("Channel becomes ready while we wait (preloading immutable "
      "properties)");

  test_connection_run_until_dbus_queue_processed (conn);

  service_chan->get_handle_called = 0;
  service_chan->get_interfaces_called = 0;
  service_chan->get_channel_type_called = 0;

  asv = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TEXT);
  g_hash_table_insert (asv, g_strdup (TP_IFACE_CHANNEL ".ChannelType"),
      tp_g_value_slice_dup (value));

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (asv, g_strdup (TP_IFACE_CHANNEL ".TargetHandleType"),
      tp_g_value_slice_dup (value));

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, handle);
  g_hash_table_insert (asv, g_strdup (TP_IFACE_CHANNEL ".TargetHandle"),
      tp_g_value_slice_dup (value));

  value = tp_g_value_slice_new (G_TYPE_STRV);
  g_value_set_static_boxed (value, NULL);
  g_hash_table_insert (asv, g_strdup (TP_IFACE_CHANNEL ".Interfaces"),
      tp_g_value_slice_dup (value));

  chan = tp_channel_new_from_properties (conn, chan_path, asv, &error);
  MYASSERT_NO_ERROR (error);

  g_hash_table_destroy (asv);
  asv = NULL;

  MYASSERT (tp_channel_run_until_ready (chan, &error, NULL), "");
  MYASSERT_NO_ERROR (error);
  MYASSERT_SAME_UINT (service_chan->get_handle_called, 0);
  MYASSERT_SAME_UINT (service_chan->get_channel_type_called, 0);
  /* FIXME: with an improved fast-path we could avoid this one too maybe? */
  /* MYASSERT_SAME_UINT (service_chan->get_interfaces_called, 0); */

  assert_chan_sane (chan, handle);

  g_object_unref (chan);
  chan = NULL;

  g_message ("Channel becomes ready while we wait (in the case where we "
      "have to discover the channel type)");

  test_connection_run_until_dbus_queue_processed (conn);

  service_chan->get_handle_called = 0;
  service_chan->get_interfaces_called = 0;
  service_chan->get_channel_type_called = 0;

  chan = tp_channel_new (conn, chan_path, NULL,
      TP_HANDLE_TYPE_CONTACT, handle, &error);
  MYASSERT_NO_ERROR (error);

  MYASSERT (tp_channel_run_until_ready (chan, &error, NULL), "");
  MYASSERT_NO_ERROR (error);
  MYASSERT_SAME_UINT (service_chan->get_handle_called, 0);
  MYASSERT_SAME_UINT (service_chan->get_interfaces_called, 1);
  MYASSERT_SAME_UINT (service_chan->get_channel_type_called, 1);

  assert_chan_sane (chan, handle);

  g_object_unref (chan);
  chan = NULL;

  g_message ("Channel becomes ready while we wait (in the case where we "
      "have to discover the handle type)");

  test_connection_run_until_dbus_queue_processed (conn);

  service_chan->get_handle_called = 0;
  service_chan->get_interfaces_called = 0;
  service_chan->get_channel_type_called = 0;

  chan = tp_channel_new (conn, chan_path, TP_IFACE_CHANNEL_TYPE_TEXT,
      TP_UNKNOWN_HANDLE_TYPE, 0, &error);
  MYASSERT_NO_ERROR (error);

  MYASSERT (tp_channel_run_until_ready (chan, &error, NULL), "");
  MYASSERT_NO_ERROR (error);
  MYASSERT_SAME_UINT (service_chan->get_handle_called, 1);
  MYASSERT_SAME_UINT (service_chan->get_interfaces_called, 1);
  MYASSERT_SAME_UINT (service_chan->get_channel_type_called, 0);

  assert_chan_sane (chan, handle);

  g_object_unref (chan);
  chan = NULL;

  g_message ("Channel becomes ready while we wait (in the case where we "
      "have to discover the handle)");

  test_connection_run_until_dbus_queue_processed (conn);

  service_chan->get_handle_called = 0;
  service_chan->get_interfaces_called = 0;
  service_chan->get_channel_type_called = 0;

  chan = tp_channel_new (conn, chan_path, TP_IFACE_CHANNEL_TYPE_TEXT,
      TP_HANDLE_TYPE_CONTACT, 0, &error);
  MYASSERT_NO_ERROR (error);

  MYASSERT (tp_channel_run_until_ready (chan, &error, NULL), "");
  MYASSERT_NO_ERROR (error);
  MYASSERT_SAME_UINT (service_chan->get_handle_called, 1);
  MYASSERT_SAME_UINT (service_chan->get_interfaces_called, 1);
  MYASSERT_SAME_UINT (service_chan->get_channel_type_called, 0);

  assert_chan_sane (chan, handle);

  g_object_unref (chan);
  chan = NULL;

  g_message ("channel does not, in fact, exist (callback)");

  bad_chan_path = g_strdup_printf ("%s/Does/Not/Actually/Exist", conn_path);
  chan = tp_channel_new (conn, bad_chan_path, NULL,
      TP_UNKNOWN_HANDLE_TYPE, 0, &error);
  MYASSERT_NO_ERROR (error);

  was_ready = FALSE;
  tp_channel_call_when_ready (chan, channel_ready, &was_ready);
  g_main_loop_run (mainloop);
  MYASSERT (was_ready == TRUE, "");
  MYASSERT (invalidated != NULL, "");
  MYASSERT (invalidated->domain == DBUS_GERROR,
      "%s", g_quark_to_string (invalidated->domain));
  MYASSERT (invalidated->code == DBUS_GERROR_UNKNOWN_METHOD,
      "%u", invalidated->code);
  g_error_free (invalidated);
  invalidated = NULL;

  g_object_unref (chan);
  chan = NULL;
  g_free (bad_chan_path);
  bad_chan_path = NULL;

  g_message ("channel does not, in fact, exist (run_until_ready)");

  bad_chan_path = g_strdup_printf ("%s/Does/Not/Actually/Exist", conn_path);
  chan = tp_channel_new (conn, bad_chan_path, NULL,
      TP_UNKNOWN_HANDLE_TYPE, 0, &error);
  MYASSERT_NO_ERROR (error);

  MYASSERT (!tp_channel_run_until_ready (chan, &error, NULL), "");
  MYASSERT (error != NULL, "");
  MYASSERT (error->domain == DBUS_GERROR,
      "%s", g_quark_to_string (error->domain));
  MYASSERT (error->code == DBUS_GERROR_UNKNOWN_METHOD,
      "%u", error->code);
  g_error_free (error);
  error = NULL;

  g_object_unref (chan);
  chan = NULL;
  g_free (bad_chan_path);
  bad_chan_path = NULL;

  g_message ("Channel becomes ready and we are called back");

  test_connection_run_until_dbus_queue_processed (conn);

  service_chan->get_handle_called = 0;
  service_chan->get_interfaces_called = 0;
  service_chan->get_channel_type_called = 0;

  chan = tp_channel_new (conn, chan_path, TP_IFACE_CHANNEL_TYPE_TEXT,
      TP_HANDLE_TYPE_CONTACT, handle, &error);
  MYASSERT_NO_ERROR (error);

  was_ready = FALSE;
  tp_channel_call_when_ready (chan, channel_ready, &was_ready);
  g_message ("Entering main loop");
  g_main_loop_run (mainloop);
  g_message ("Leaving main loop");
  MYASSERT (was_ready == TRUE, "");
  MYASSERT_NO_ERROR (invalidated);
  MYASSERT_SAME_UINT (service_chan->get_handle_called, 0);
  MYASSERT_SAME_UINT (service_chan->get_interfaces_called, 1);
  MYASSERT_SAME_UINT (service_chan->get_channel_type_called, 0);

  assert_chan_sane (chan, handle);

  /* ... keep the same channel for the next test */

  g_message ("Channel already ready, so we are called back synchronously");

  was_ready = FALSE;
  tp_channel_call_when_ready (chan, channel_ready, &was_ready);
  MYASSERT (was_ready == TRUE, "");
  MYASSERT_NO_ERROR (invalidated);

  assert_chan_sane (chan, handle);

  /* ... keep the same channel for the next test */

  g_message ("Channel already dead, so we are called back synchronously");

  MYASSERT (tp_cli_connection_run_disconnect (conn, -1, &error, NULL), "");
  MYASSERT_NO_ERROR (error);

  was_ready = FALSE;
  tp_channel_call_when_ready (chan, channel_ready, &was_ready);
  MYASSERT (was_ready == TRUE, "");
  MYASSERT (invalidated != NULL, "");
  MYASSERT (invalidated->domain == TP_ERRORS_DISCONNECTED,
      "%s", g_quark_to_string (invalidated->domain));
  MYASSERT (invalidated->code == TP_CONNECTION_STATUS_REASON_REQUESTED,
      "%u", invalidated->code);
  g_error_free (invalidated);
  invalidated = NULL;

  g_object_unref (chan);
  chan = NULL;

  /* clean up */

  g_assert (chan == NULL);
  g_main_loop_unref (mainloop);
  mainloop = NULL;

  tp_handle_unref (contact_repo, handle);
  g_object_unref (conn);
  g_object_unref (service_chan);
  g_object_unref (service_props_chan);

  service_conn_as_base = NULL;
  g_object_unref (service_conn);
  g_object_unref (dbus);
  g_free (name);
  g_free (conn_path);
  g_free (chan_path);

  return fail;
}
