/* Tests of TpTextChannel
 *
 * Copyright © 2010 Collabora Ltd. <http://www.collabora.co.uk/>
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */

#include <string.h>

#include <telepathy-glib/telepathy-glib.h>

#include "examples/cm/echo-message-parts/chan.h"
#include "examples/cm/echo-message-parts/conn.h"

#include "tests/lib/util.h"

typedef struct {
    GMainLoop *mainloop;
    TpDBusDaemon *dbus;

    /* Service side objects */
    TpBaseConnection *base_connection;
    ExampleEcho2Channel *chan_service;
    TpHandleRepoIface *contact_repo;

    /* Client side objects */
    TpConnection *connection;
    TpTextChannel *channel;

    TpMessage *received_msg;
    TpMessage *removed_msg;
    TpMessage *sent_msg;
    gchar *token;
    gchar *sent_token;
    TpMessageSendingFlags sending_flags;

    GError *error /* initialized where needed */;
    gint wait;
} Test;

static void
create_contact_chan (Test *test)
{
  gchar *chan_path;
  TpHandle handle, alf_handle;
  GHashTable *props;

  tp_clear_object (&test->chan_service);

  /* Create service-side tube channel object */
  chan_path = g_strdup_printf ("%s/Channel",
      tp_proxy_get_object_path (test->connection));

  test->contact_repo = tp_base_connection_get_handles (test->base_connection,
      TP_HANDLE_TYPE_CONTACT);
  g_assert (test->contact_repo != NULL);

  handle = tp_handle_ensure (test->contact_repo, "bob", NULL, &test->error);

  g_assert_no_error (test->error);

  alf_handle = tp_handle_ensure (test->contact_repo, "alf", NULL, &test->error);
  g_assert_no_error (test->error);

  test->chan_service = g_object_new (
      EXAMPLE_TYPE_ECHO_2_CHANNEL,
      "connection", test->base_connection,
      "handle", handle,
      "object-path", chan_path,
      NULL);

  g_object_get (test->chan_service,
      "channel-properties", &props,
      NULL);

  test->channel = tp_text_channel_new (test->connection, chan_path,
      props, &test->error);
  g_assert_no_error (test->error);

  g_free (chan_path);

  tp_handle_unref (test->contact_repo, handle);
  g_hash_table_unref (props);
}

static void
setup (Test *test,
       gconstpointer data)
{
  test->mainloop = g_main_loop_new (NULL, FALSE);
  test->dbus = tp_tests_dbus_daemon_dup_or_die ();

  test->error = NULL;

  /* Create (service and client sides) connection objects */
  tp_tests_create_and_connect_conn (EXAMPLE_TYPE_ECHO_2_CONNECTION,
      "me@test.com", &test->base_connection, &test->connection);

  create_contact_chan (test);
}

static void
teardown (Test *test,
          gconstpointer data)
{
  g_clear_error (&test->error);

  tp_clear_object (&test->dbus);
  g_main_loop_unref (test->mainloop);
  test->mainloop = NULL;

  tp_clear_object (&test->chan_service);

  tp_cli_connection_run_disconnect (test->connection, -1, &test->error, NULL);
  g_assert_no_error (test->error);

  g_object_unref (test->connection);
  g_object_unref (test->base_connection);

  tp_clear_object (&test->received_msg);
  tp_clear_object (&test->removed_msg);
  tp_clear_object (&test->sent_msg);
  tp_clear_pointer (&test->token, g_free);
  tp_clear_pointer (&test->sent_token, g_free);

  tp_clear_object (&test->channel);
}

static void
test_creation (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  const GError *error = NULL;

  g_assert (TP_IS_TEXT_CHANNEL (test->channel));

  error = tp_proxy_get_invalidated (test->channel);
  g_assert_no_error (error);
}

static void
check_messages_types (GArray *message_types)
{
  TpChannelTextMessageType type;

  g_assert (message_types != NULL);
  g_assert_cmpuint (message_types->len, ==, 3);

  type = g_array_index (message_types, TpChannelTextMessageType, 0);
  g_assert_cmpuint (type, ==, TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL);
  type = g_array_index (message_types, TpChannelTextMessageType, 1);
  g_assert_cmpuint (type, ==, TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION);
  type = g_array_index (message_types, TpChannelTextMessageType, 2);
  g_assert_cmpuint (type, ==, TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE);
}

static void
test_properties (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  GStrv content_types;
  const gchar * const * content_types2;
  TpMessagePartSupportFlags message_part;
  TpDeliveryReportingSupportFlags delivery;
  GArray *message_types;

  g_object_get (test->channel,
      "supported-content-types", &content_types,
      "message-part-support-flags", &message_part,
      "delivery-reporting-support", &delivery,
      "message-types", &message_types,
      NULL);

  /* SupportedContentTypes */
  g_assert_cmpuint (g_strv_length (content_types), ==, 1);
  g_assert_cmpstr (content_types[0], ==, "*/*");
  g_strfreev (content_types);

  content_types2 = tp_text_channel_get_supported_content_types (test->channel);
  g_assert_cmpstr (content_types2[0], ==, "*/*");

  /* MessagePartSupportFlags */
  g_assert_cmpuint (message_part, ==,
      TP_MESSAGE_PART_SUPPORT_FLAG_ONE_ATTACHMENT |
      TP_MESSAGE_PART_SUPPORT_FLAG_MULTIPLE_ATTACHMENTS |
      TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_FAILURES);
  g_assert_cmpuint (message_part, ==,
      tp_text_channel_get_message_part_support_flags (test->channel));

  /* DeliveryReportingSupport */
  g_assert_cmpuint (delivery, ==,
      TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_FAILURES);
  g_assert_cmpuint (delivery, ==,
      tp_text_channel_get_delivery_reporting_support (test->channel));

  /* MessageTypes */
  check_messages_types (message_types);
  g_array_unref (message_types);

  message_types = tp_text_channel_get_message_types (test->channel);
  check_messages_types (message_types);

  g_assert (tp_text_channel_supports_message_type (test->channel,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL));
  g_assert (tp_text_channel_supports_message_type (test->channel,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION));
  g_assert (tp_text_channel_supports_message_type (test->channel,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE));
  g_assert (!tp_text_channel_supports_message_type (test->channel,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_AUTO_REPLY));
  g_assert (!tp_text_channel_supports_message_type (test->channel,
      TP_CHANNEL_TEXT_MESSAGE_TYPE_DELIVERY_REPORT));
}

static void
proxy_prepare_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  Test *test = user_data;

  tp_proxy_prepare_finish (source, result, &test->error);

  test->wait--;
  if (test->wait <= 0)
    g_main_loop_quit (test->mainloop);
}

static void
send_message_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  Test *test = user_data;

  tp_clear_pointer (&test->token, g_free);

  tp_text_channel_send_message_finish (TP_TEXT_CHANNEL (source), result,
      &test->token, &test->error);

  test->wait--;
  if (test->wait <= 0)
    g_main_loop_quit (test->mainloop);
}

static void
on_received (TpChannel *chan,
    guint id,
    guint timestamp,
    guint sender,
    guint type,
    guint flags,
    const gchar *text,
    gpointer user_data,
    GObject *object)
{
  Test *test = user_data;

  test->wait--;
  if (test->wait <= 0)
    g_main_loop_quit (test->mainloop);
}

static void
test_pending_messages (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  GQuark features[] = { TP_TEXT_CHANNEL_FEATURE_INCOMING_MESSAGES, 0 };
  GList *messages;
  TpMessage *msg;
  gchar *text;
  TpContact *sender;

  /* connect on the Received sig to check if the message has been received */
  tp_cli_channel_type_text_connect_to_received (TP_CHANNEL (test->channel),
      on_received, test, NULL, NULL, NULL);

  /* Send a first message */
  msg = tp_client_message_new_text (TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      "Badger");

  tp_text_channel_send_message_async (test->channel, msg, 0,
      send_message_cb, test);

  g_object_unref (msg);

  test->wait = 2;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  /* Send a second message */
  msg = tp_client_message_new_text (TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      "Snake");

  tp_text_channel_send_message_async (test->channel, msg, 0,
      send_message_cb, test);

  g_object_unref (msg);

  test->wait = 2;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  /* We didn't prepare the feature yet so there is no pending msg */
  messages = tp_text_channel_get_pending_messages (test->channel);
  g_assert_cmpuint (g_list_length (messages), ==, 0);
  g_list_free (messages);

  tp_proxy_prepare_async (test->channel, features,
      proxy_prepare_cb, test);

  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  g_assert (tp_proxy_is_prepared (test->channel,
        TP_TEXT_CHANNEL_FEATURE_INCOMING_MESSAGES));

  /* We have the pending messages now */
  messages = tp_text_channel_get_pending_messages (test->channel);
  g_assert_cmpuint (g_list_length (messages), ==, 2);

  /* Check first message */
  msg = messages->data;
  g_assert (TP_IS_SIGNALLED_MESSAGE (msg));

  text = tp_message_to_text (msg, NULL);
  g_assert_cmpstr (text, ==, "Badger");
  g_free (text);
  sender = tp_signalled_message_get_sender (msg);
  g_assert (sender != NULL);
  g_assert_cmpstr (tp_contact_get_identifier (sender), ==, "bob");

  /* Check second message */
  msg = messages->next->data;
  g_assert (TP_IS_SIGNALLED_MESSAGE (msg));

  text = tp_message_to_text (msg, NULL);
  g_assert_cmpstr (text, ==, "Snake");
  g_free (text);
  sender = tp_signalled_message_get_sender (msg);
  g_assert (sender != NULL);
  g_assert_cmpstr (tp_contact_get_identifier (sender), ==, "bob");

  g_list_free (messages);
}

static void
message_received_cb (TpTextChannel *chan,
    TpSignalledMessage *msg,
    Test *test)
{
  tp_clear_object (&test->received_msg);

  test->received_msg = g_object_ref (msg);

  test->wait--;
  if (test->wait <= 0)
    g_main_loop_quit (test->mainloop);
}

static void
test_message_received (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  GQuark features[] = { TP_TEXT_CHANNEL_FEATURE_INCOMING_MESSAGES, 0 };
  TpMessage *msg;
  gchar *text;
  TpContact *sender;

  /* We have to prepare the pending messages feature to be notified about
   * incoming messages */
  tp_proxy_prepare_async (test->channel, features,
      proxy_prepare_cb, test);

  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  g_signal_connect (test->channel, "message-received",
      G_CALLBACK (message_received_cb), test);

  msg = tp_client_message_new_text (TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      "Snake");

  tp_text_channel_send_message_async (test->channel, msg, 0,
      send_message_cb, test);

  test->wait = 2;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  text = tp_message_to_text (test->received_msg, NULL);
  g_assert_cmpstr (text, ==, "Snake");
  g_free (text);

  sender = tp_signalled_message_get_sender (test->received_msg);
  g_assert (sender != NULL);
  g_assert_cmpstr (tp_contact_get_identifier (sender), ==, "bob");

  g_object_unref (msg);
}

static void
messages_acked_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  Test *test = user_data;

  tp_text_channel_ack_messages_finish (TP_TEXT_CHANNEL (source), result,
      &test->error);

  test->wait--;
  if (test->wait <= 0)
    g_main_loop_quit (test->mainloop);
}

static void
test_ack_messages (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  GQuark features[] = { TP_TEXT_CHANNEL_FEATURE_INCOMING_MESSAGES, 0 };
  GList *messages;
  TpMessage *msg;

  /* Send a first message */
  msg = tp_client_message_new_text (TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      "Badger");

  tp_text_channel_send_message_async (test->channel, msg, 0,
      send_message_cb, test);

  g_object_unref (msg);

  /* Send a second message */
  msg = tp_client_message_new_text (TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      "Snake");

  tp_text_channel_send_message_async (test->channel, msg, 0,
      send_message_cb, test);

  g_object_unref (msg);

  test->wait = 2;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  tp_proxy_prepare_async (test->channel, features,
      proxy_prepare_cb, test);

  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  messages = tp_text_channel_get_pending_messages (test->channel);
  g_assert_cmpuint (g_list_length (messages), ==, 2);

  tp_text_channel_ack_messages_async (test->channel, messages,
      messages_acked_cb, test);

  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  g_list_free (messages);

  /* Messages have been acked so there is no pending messages */
  messages = tp_text_channel_get_pending_messages (test->channel);
  g_assert_cmpuint (g_list_length (messages), ==, 0);
}

static void
message_acked_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  Test *test = user_data;

  tp_text_channel_ack_message_finish (TP_TEXT_CHANNEL (source), result,
      &test->error);

  test->wait--;
  if (test->wait <= 0)
    g_main_loop_quit (test->mainloop);
}

static void
pending_message_removed_cb (TpTextChannel *chan,
    TpSignalledMessage *msg,
    Test *test)
{
  tp_clear_object (&test->removed_msg);

  test->removed_msg = g_object_ref (msg);

  test->wait--;
  if (test->wait <= 0)
    g_main_loop_quit (test->mainloop);
}

static void
test_ack_message (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  GQuark features[] = { TP_TEXT_CHANNEL_FEATURE_INCOMING_MESSAGES, 0 };
  GList *messages;
  TpMessage *msg;

  tp_proxy_prepare_async (test->channel, features,
      proxy_prepare_cb, test);

  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  g_signal_connect (test->channel, "message-received",
      G_CALLBACK (message_received_cb), test);

  /* Send message */
  msg = tp_client_message_new_text (TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      "Badger");

  tp_text_channel_send_message_async (test->channel, msg, 0,
      send_message_cb, test);

  g_object_unref (msg);

  test->wait = 2;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  g_assert (TP_IS_SIGNALLED_MESSAGE (test->received_msg));

  g_signal_connect (test->channel, "pending-message-removed",
      G_CALLBACK (pending_message_removed_cb), test);

  tp_text_channel_ack_message_async (test->channel, test->received_msg,
      message_acked_cb, test);

  test->wait = 2;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  g_assert (test->received_msg == test->removed_msg);

  /* Messages has been acked so there is no pending messages */
  messages = tp_text_channel_get_pending_messages (test->channel);
  g_assert_cmpuint (g_list_length (messages), ==, 0);
}

static void
message_sent_cb (TpTextChannel *channel,
    TpSignalledMessage *message,
    TpMessageSendingFlags flags,
    const gchar *token,
    Test *test)
{
  tp_clear_object (&test->sent_msg);
  tp_clear_pointer (&test->sent_token, g_free);

  test->sent_msg = g_object_ref (message);
  test->sending_flags = flags;
  if (token != NULL)
    test->sent_token = g_strdup (token);

  test->wait--;
  if (test->wait <= 0)
    g_main_loop_quit (test->mainloop);
}

static void
test_message_sent (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  TpMessage *msg;
  gchar *text;

  g_signal_connect (test->channel, "message-sent",
      G_CALLBACK (message_sent_cb), test);

  /* Send message */
  msg = tp_client_message_new_text (TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
      "Badger");

  tp_text_channel_send_message_async (test->channel, msg,
      TP_MESSAGE_SENDING_FLAG_REPORT_DELIVERY, send_message_cb, test);

  g_object_unref (msg);

  test->wait = 2;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  g_assert (TP_IS_SIGNALLED_MESSAGE (test->sent_msg));
  text = tp_message_to_text (test->sent_msg, NULL);
  g_assert_cmpstr (text, ==, "Badger");
  g_free (text);

  g_assert_cmpuint (test->sending_flags, ==,
      TP_MESSAGE_SENDING_FLAG_REPORT_DELIVERY);
  g_assert (test->sent_token == NULL);
}

int
main (int argc,
      char **argv)
{
  tp_tests_abort_after (10);
  g_type_init ();
  tp_debug_set_flags ("all");

  g_test_init (&argc, &argv, NULL);
  g_test_bug_base ("http://bugs.freedesktop.org/show_bug.cgi?id=");

  g_test_add ("/text-channel/creation", Test, NULL, setup,
      test_creation, teardown);
  g_test_add ("/text-channel/properties", Test, NULL, setup,
      test_properties, teardown);
  g_test_add ("/text-channel/pending-messages", Test, NULL, setup,
      test_pending_messages, teardown);
  g_test_add ("/text-channel/message-received", Test, NULL, setup,
      test_message_received, teardown);
  g_test_add ("/text-channel/ack-messages", Test, NULL, setup,
      test_ack_messages, teardown);
  g_test_add ("/text-channel/ack-message", Test, NULL, setup,
      test_ack_message, teardown);
  g_test_add ("/text-channel/message-sent", Test, NULL, setup,
      test_message_sent, teardown);

  return g_test_run ();
}
