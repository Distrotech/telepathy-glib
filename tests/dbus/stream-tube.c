/* Tests of TpStreamTubeChannel
 *
 * Copyright © 2010 Collabora Ltd. <http://www.collabora.co.uk/>
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */

#include <string.h>

#include <telepathy-glib/stream-tube-channel.h>
#include <telepathy-glib/debug.h>
#include <telepathy-glib/defs.h>
#include <telepathy-glib/dbus.h>

#include "tests/lib/util.h"
#include "tests/lib/simple-conn.h"
#include "tests/lib/stream-tube-chan.h"

#define BUFFER_SIZE 128

typedef struct {
    gboolean contact;

    TpSocketAddressType address_type;
    TpSocketAccessControl access_control;
} TestContext;

TestContext contexts[] = {
  { FALSE, TP_SOCKET_ADDRESS_TYPE_UNIX, TP_SOCKET_ACCESS_CONTROL_LOCALHOST },
  { FALSE, TP_SOCKET_ADDRESS_TYPE_IPV4, TP_SOCKET_ACCESS_CONTROL_LOCALHOST },
  { FALSE, TP_SOCKET_ADDRESS_TYPE_IPV6, TP_SOCKET_ACCESS_CONTROL_LOCALHOST },
  { FALSE, TP_SOCKET_ADDRESS_TYPE_UNIX, TP_SOCKET_ACCESS_CONTROL_CREDENTIALS },
  { FALSE, TP_SOCKET_ADDRESS_TYPE_IPV4, TP_SOCKET_ACCESS_CONTROL_PORT },

  { TRUE, TP_SOCKET_ADDRESS_TYPE_UNIX, TP_SOCKET_ACCESS_CONTROL_LOCALHOST },
  { TRUE, TP_SOCKET_ADDRESS_TYPE_IPV4, TP_SOCKET_ACCESS_CONTROL_LOCALHOST },
  { TRUE, TP_SOCKET_ADDRESS_TYPE_IPV6, TP_SOCKET_ACCESS_CONTROL_LOCALHOST },
  { TRUE, TP_SOCKET_ADDRESS_TYPE_UNIX, TP_SOCKET_ACCESS_CONTROL_CREDENTIALS },
  { TRUE, TP_SOCKET_ADDRESS_TYPE_IPV4, TP_SOCKET_ACCESS_CONTROL_PORT },

  { FALSE, NUM_TP_SOCKET_ADDRESS_TYPES, NUM_TP_SOCKET_ACCESS_CONTROLS }
};

typedef struct {
    GMainLoop *mainloop;
    TpDBusDaemon *dbus;

    /* Service side objects */
    TpBaseConnection *base_connection;
    TpTestsStreamTubeChannel *tube_chan_service;
    TpHandleRepoIface *contact_repo;
    TpHandleRepoIface *room_repo;

    /* Client side objects */
    TpConnection *connection;
    TpStreamTubeChannel *tube;

    TpStreamTubeConnection *tube_conn;
    GIOStream *cm_stream;

    GError *error /* initialized where needed */;
    gint wait;
} Test;

static void
setup (Test *test,
       gconstpointer data)
{
  test->mainloop = g_main_loop_new (NULL, FALSE);
  test->dbus = tp_tests_dbus_daemon_dup_or_die ();

  test->error = NULL;

  /* Create (service and client sides) connection objects */
  tp_tests_create_and_connect_conn (TP_TESTS_TYPE_SIMPLE_CONNECTION,
      "me@test.com", &test->base_connection, &test->connection);
}

static void
teardown (Test *test,
          gconstpointer data)
{
  g_clear_error (&test->error);

  tp_clear_object (&test->dbus);
  g_main_loop_unref (test->mainloop);
  test->mainloop = NULL;

  tp_clear_object (&test->tube_chan_service);
  tp_clear_object (&test->tube);
  tp_clear_object (&test->tube_conn);
  tp_clear_object (&test->cm_stream);

  tp_cli_connection_run_disconnect (test->connection, -1, &test->error, NULL);
  g_assert_no_error (test->error);

  g_object_unref (test->connection);
  g_object_unref (test->base_connection);
}

static void
destroy_socket_control_list (gpointer data)
{
  GArray *tab = data;
  g_array_free (tab, TRUE);
}

static GHashTable *
create_supported_socket_types_hash (TpSocketAddressType address_type,
    TpSocketAccessControl access_control)
{
  GHashTable *ret;
  GArray *tab;

  ret = g_hash_table_new_full (NULL, NULL, NULL, destroy_socket_control_list);

  tab = g_array_sized_new (FALSE, FALSE, sizeof (TpSocketAccessControl),
      1);
  g_array_append_val (tab, access_control);

  g_hash_table_insert (ret, GUINT_TO_POINTER (address_type), tab);

  return ret;
}

static void
create_tube_service (Test *test,
    gboolean requested,
    TpSocketAddressType address_type,
    TpSocketAccessControl access_control,
    gboolean contact)
{
  gchar *chan_path;
  TpHandle handle, alf_handle;
  GHashTable *props;
  GHashTable *sockets;
  GType type;

  tp_clear_object (&test->tube_chan_service);
  tp_clear_object (&test->tube);

  /* Create service-side tube channel object */
  chan_path = g_strdup_printf ("%s/Channel",
      tp_proxy_get_object_path (test->connection));

  test->contact_repo = tp_base_connection_get_handles (test->base_connection,
      TP_HANDLE_TYPE_CONTACT);
  g_assert (test->contact_repo != NULL);

  test->room_repo = tp_base_connection_get_handles (test->base_connection,
      TP_HANDLE_TYPE_ROOM);
  g_assert (test->room_repo != NULL);

  if (contact)
    {
      handle = tp_handle_ensure (test->contact_repo, "bob", NULL, &test->error);
      type = TP_TESTS_TYPE_CONTACT_STREAM_TUBE_CHANNEL;
    }
  else
    {
      handle = tp_handle_ensure (test->room_repo, "#test", NULL, &test->error);
      type = TP_TESTS_TYPE_ROOM_STREAM_TUBE_CHANNEL;
    }

  g_assert_no_error (test->error);

  alf_handle = tp_handle_ensure (test->contact_repo, "alf", NULL, &test->error);
  g_assert_no_error (test->error);

  sockets = create_supported_socket_types_hash (address_type, access_control);

  test->tube_chan_service = g_object_new (
      type,
      "connection", test->base_connection,
      "handle", handle,
      "requested", requested,
      "object-path", chan_path,
      "supported-socket-types", sockets,
      "initiator-handle", alf_handle,
      NULL);

  /* Create client-side tube channel object */
  g_object_get (test->tube_chan_service, "channel-properties", &props, NULL);

  test->tube = tp_stream_tube_channel_new (test->connection,
      chan_path, props, &test->error);

  g_assert_no_error (test->error);

  g_free (chan_path);
  g_hash_table_unref (props);
  g_hash_table_unref (sockets);

  if (contact)
    tp_handle_unref (test->contact_repo, handle);
  else
    tp_handle_unref (test->room_repo, handle);
}

/* Test Basis */

static void
test_creation (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  create_tube_service (test, TRUE, TP_SOCKET_ADDRESS_TYPE_UNIX,
      TP_SOCKET_ACCESS_CONTROL_LOCALHOST, TRUE);

  g_assert (TP_IS_STREAM_TUBE_CHANNEL (test->tube));
  g_assert (TP_IS_CHANNEL (test->tube));

  create_tube_service (test, FALSE, TP_SOCKET_ADDRESS_TYPE_UNIX,
      TP_SOCKET_ACCESS_CONTROL_LOCALHOST, FALSE);

  g_assert (TP_IS_STREAM_TUBE_CHANNEL (test->tube));
  g_assert (TP_IS_CHANNEL (test->tube));
}

static void
check_parameters (GHashTable *parameters)
{
  g_assert (parameters != NULL);

  g_assert_cmpuint (g_hash_table_size (parameters), ==, 1);
  g_assert_cmpuint (tp_asv_get_uint32 (parameters, "badger", NULL), ==, 42);
}

static void
test_properties (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  gchar *service;
  GHashTable *parameters;

  /* Outgoing tube */
  create_tube_service (test, TRUE, TP_SOCKET_ADDRESS_TYPE_UNIX,
      TP_SOCKET_ACCESS_CONTROL_LOCALHOST, TRUE);

  /* Service */
  g_assert_cmpstr (tp_stream_tube_channel_get_service (test->tube), ==,
      "test-service");
  g_object_get (test->tube, "service", &service, NULL);
  g_assert_cmpstr (service, ==, "test-service");
  g_free (service);

  /* Parameters */
  parameters = tp_stream_tube_channel_get_parameters (test->tube);
  /* NULL as the tube has not be offered yet */
  g_assert (parameters == NULL);
  g_object_get (test->tube, "parameters", &parameters, NULL);
  g_assert (parameters == NULL);

  /* Incoming tube */
  create_tube_service (test, FALSE, TP_SOCKET_ADDRESS_TYPE_UNIX,
      TP_SOCKET_ACCESS_CONTROL_LOCALHOST, FALSE);

  /* Parameters */
  parameters = tp_stream_tube_channel_get_parameters (test->tube);
  check_parameters (parameters);
  g_object_get (test->tube, "parameters", &parameters, NULL);
  check_parameters (parameters);
  g_hash_table_unref (parameters);
}

static void
tube_accept_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  Test *test = user_data;

  test->tube_conn = tp_stream_tube_channel_accept_finish (
      TP_STREAM_TUBE_CHANNEL (source), result, &test->error);

  test->wait--;
  if (test->wait <= 0)
    g_main_loop_quit (test->mainloop);
}

static void
write_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  Test *test = user_data;

  g_output_stream_write_finish (G_OUTPUT_STREAM (source), result, &test->error);

  test->wait--;
  if (test->wait <= 0)
    g_main_loop_quit (test->mainloop);
}

static void
read_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  Test *test = user_data;
  gssize size;

  size = g_input_stream_read_finish (G_INPUT_STREAM (source), result,
      &test->error);

  g_assert_no_error (test->error);
  g_assert_cmpuint (size, !=, 0);

  test->wait--;
  if (test->wait <= 0)
    g_main_loop_quit (test->mainloop);
}

static void
chan_incoming_connection_cb (TpTestsStreamTubeChannel *chan,
    GIOStream *stream,
    Test *test)
{
  test->cm_stream = g_object_ref (stream);

  test->wait--;
  if (test->wait <= 0)
    g_main_loop_quit (test->mainloop);
}

static void
use_tube_with_streams (Test *test,
    GIOStream *stream,
    GIOStream *cm_stream)
{
  GOutputStream *out;
  GInputStream *in;
  gchar buffer[BUFFER_SIZE];
  gchar cm_buffer[BUFFER_SIZE];

  g_assert (stream != NULL);
  g_assert (cm_stream != NULL);

  /* User sends something through the tube */
  out = g_io_stream_get_output_stream (stream);

  strcpy (buffer, "badger");

  g_output_stream_write_async (out, buffer, BUFFER_SIZE, G_PRIORITY_DEFAULT,
      NULL, write_cb, test);

  /* ...CM reads them */
  in = g_io_stream_get_input_stream (cm_stream);

  g_input_stream_read_async (in, cm_buffer, BUFFER_SIZE,
      G_PRIORITY_DEFAULT, NULL, read_cb, test);

  test->wait = 2;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  /* CM received the right data */
  g_assert_cmpstr (buffer, ==, cm_buffer);

  /* Now the CM writes some data to the tube */
  out = g_io_stream_get_output_stream (cm_stream);

  strcpy (cm_buffer, "mushroom");

  g_output_stream_write_async (out, cm_buffer, BUFFER_SIZE, G_PRIORITY_DEFAULT,
      NULL, write_cb, test);

  /* ...users reads them */
  in = g_io_stream_get_input_stream (stream);

  g_input_stream_read_async (in, buffer, BUFFER_SIZE,
      G_PRIORITY_DEFAULT, NULL, read_cb, test);

  test->wait = 2;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  /* client reads the right data */
  g_assert_cmpstr (buffer, ==, cm_buffer);
}

static void
use_tube (Test *test)
{
  GSocketConnection *conn;

  conn = tp_stream_tube_connection_get_socket_connection (test->tube_conn);

  use_tube_with_streams (test, G_IO_STREAM (conn), test->cm_stream);
}

static void
tube_conn_closed_cb (TpStreamTubeConnection *conn,
    GError *error,
    Test *test)
{
  test->error = g_error_copy (error);

  test->wait--;
  if (test->wait <= 0)
    g_main_loop_quit (test->mainloop);
}

static void
test_accept_success (Test *test,
    gconstpointer data)
{
  guint i = GPOINTER_TO_UINT (data);
  TpContact *contact;

  create_tube_service (test, FALSE, contexts[i].address_type,
      contexts[i].access_control, contexts[i].contact);

  g_signal_connect (test->tube_chan_service, "incoming-connection",
      G_CALLBACK (chan_incoming_connection_cb), test);

  tp_stream_tube_channel_accept_async (test->tube, tube_accept_cb, test);

  test->wait = 2;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  g_assert (test->tube_conn != NULL);

  contact = tp_stream_tube_connection_get_contact (test->tube_conn);
  g_assert (contact != NULL);
  g_assert_cmpstr (tp_contact_get_identifier (contact), ==, "alf");

  use_tube (test);

  /* Connection is closed */
  g_signal_connect (test->tube_conn, "closed",
      G_CALLBACK (tube_conn_closed_cb), test);

  tp_tests_stream_tube_channel_last_connection_disconnected (
      test->tube_chan_service, TP_ERROR_STR_DISCONNECTED);

  test->wait = 1;
  g_main_loop_run (test->mainloop);

  g_assert_error (test->error, TP_ERROR, TP_ERROR_DISCONNECTED);
}

static void
tube_offer_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  Test *test = user_data;

  tp_stream_tube_channel_offer_finish (TP_STREAM_TUBE_CHANNEL (source), result,
      &test->error);

  test->wait--;
  if (test->wait <= 0)
    g_main_loop_quit (test->mainloop);
}

static void
tube_incoming_cb (TpStreamTubeChannel *tube,
    TpStreamTubeConnection *tube_conn,
    Test *test)
{
  tp_clear_object (&test->tube_conn);
  test->tube_conn = g_object_ref (tube_conn);

  test->wait--;
  if (test->wait <= 0)
    g_main_loop_quit (test->mainloop);
}

static void
socket_connected (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  Test *test = user_data;

  tp_clear_object (&test->cm_stream);

  test->cm_stream = G_IO_STREAM (g_socket_client_connect_finish (
        G_SOCKET_CLIENT (source), result, &test->error));

  test->wait--;
  if (test->wait <= 0)
    g_main_loop_quit (test->mainloop);
}

static gboolean
can_identify_contacts (gboolean contact,
    TpSocketAccessControl access_control)
{
  if (contact)
    return TRUE;

  return access_control == TP_SOCKET_ACCESS_CONTROL_CREDENTIALS ||
    access_control == TP_SOCKET_ACCESS_CONTROL_PORT;
}

static void
test_offer_success (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  guint i = GPOINTER_TO_UINT (data);
  GHashTable *params;
  GSocketAddress *address;
  GSocketClient *client;
  TpHandle bob_handle;
  TpContact *contact;

  create_tube_service (test, TRUE, contexts[i].address_type,
      contexts[i].access_control, contexts[i].contact);

  params = tp_asv_new ("badger", G_TYPE_UINT, 42, NULL);

  g_assert (tp_stream_tube_channel_get_parameters (test->tube) == NULL);

  tp_stream_tube_channel_offer_async (test->tube, params, tube_offer_cb, test);
  g_hash_table_unref (params);

  check_parameters (tp_stream_tube_channel_get_parameters (test->tube));

  test->wait = 1;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  /* A client connects to the tube */
  address = tp_tests_stream_tube_channel_get_server_address (
      test->tube_chan_service);
  g_assert (address != NULL);

  client = g_socket_client_new ();

  g_socket_client_connect_async (client, G_SOCKET_CONNECTABLE (address),
      NULL, socket_connected, test);

  g_object_unref (client);
  g_object_unref (address);

  test->wait = 1;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);
  g_assert (test->cm_stream != NULL);

  /* The connection is announced on TpStreamTubeChannel */
  g_signal_connect (test->tube, "incoming",
      G_CALLBACK (tube_incoming_cb), test);

  bob_handle = tp_handle_ensure (test->contact_repo, "bob", NULL, NULL);

  tp_tests_stream_tube_channel_peer_connected (test->tube_chan_service,
      test->cm_stream, bob_handle);

  test->wait = 1;
  g_main_loop_run (test->mainloop);
  g_assert (test->tube_conn != NULL);

  contact = tp_stream_tube_connection_get_contact (test->tube_conn);

  if (can_identify_contacts (contexts[i].contact, contexts[i].access_control))
    {
      g_assert (contact != NULL);
      g_assert_cmpstr (tp_contact_get_identifier (contact), ==, "bob");
    }
  else
    {
      g_assert (contact == NULL);
    }

  use_tube (test);

  /* Connection is closed */
  g_signal_connect (test->tube_conn, "closed",
      G_CALLBACK (tube_conn_closed_cb), test);

  tp_tests_stream_tube_channel_last_connection_disconnected (
      test->tube_chan_service, TP_ERROR_STR_DISCONNECTED);

  test->wait = 1;
  g_main_loop_run (test->mainloop);

  g_assert_error (test->error, TP_ERROR, TP_ERROR_DISCONNECTED);

  tp_handle_unref (test->contact_repo, bob_handle);
}

static void
test_accept_twice (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  create_tube_service (test, FALSE, TP_SOCKET_ADDRESS_TYPE_UNIX,
      TP_SOCKET_ACCESS_CONTROL_LOCALHOST, FALSE);

  tp_stream_tube_channel_accept_async (test->tube, tube_accept_cb, test);

  test->wait = 1;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  /* Try to re-accept the tube */
  tp_stream_tube_channel_accept_async (test->tube, tube_accept_cb, test);
  test->wait = 1;
  g_main_loop_run (test->mainloop);
  g_assert_error (test->error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT);
}

static void
test_accept_outgoing (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  /* Try to accept an outgoing channel */
  create_tube_service (test, TRUE, TP_SOCKET_ADDRESS_TYPE_UNIX,
      TP_SOCKET_ACCESS_CONTROL_LOCALHOST, FALSE);

  tp_stream_tube_channel_accept_async (test->tube, tube_accept_cb, test);

  test->wait = 1;
  g_main_loop_run (test->mainloop);
  g_assert_error (test->error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT);
}

typedef void (*TestFunc) (Test *, gconstpointer);

static gchar *
test_context_to_str (TestContext *ctx,
    const gchar *base)
{
  const gchar *type, *socket, *access_control;

  if (ctx->contact)
    type = "contact";
  else
    type = "room";

  switch (ctx->address_type)
    {
      case TP_SOCKET_ADDRESS_TYPE_UNIX:
        socket = "unix";
        break;
      case TP_SOCKET_ADDRESS_TYPE_IPV4:
        socket = "ipv4";
        break;
      case TP_SOCKET_ADDRESS_TYPE_IPV6:
        socket = "ipv6";
        break;
      default:
        g_assert_not_reached ();
    }

  switch (ctx->access_control)
    {
      case TP_SOCKET_ACCESS_CONTROL_LOCALHOST:
        access_control = "localhost";
        break;
      case TP_SOCKET_ACCESS_CONTROL_PORT:
        access_control = "port";
        break;
      case TP_SOCKET_ACCESS_CONTROL_CREDENTIALS:
        access_control = "credentials";
        break;
      default:
        g_assert_not_reached ();
    }

  return g_strdup_printf ("%s/%s/%s/%s", base, type, socket, access_control);
}

/* Run a test with each TestContext defined in contexts */
static void
run_tube_test (const char *test_path,
    TestFunc ftest)
{
  guint i;

  for (i = 0; contexts[i].address_type != NUM_TP_SOCKET_ADDRESS_TYPES; i++)
    {
      gchar *path = test_context_to_str (&contexts[i], test_path);

      g_test_add (path, Test, GUINT_TO_POINTER (i), setup, ftest, teardown);

      g_free (path);
    }
}

static void
test_offer_race (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  /* Two clients connect to the tube we offered but they are announced in a
   * racy way. */
  guint i = GPOINTER_TO_UINT (data);
  GSocketAddress *address;
  GSocketClient *client;
  TpHandle alice_handle, bob_handle;
  GIOStream *alice_cm_stream, *bob_cm_stream;
  GIOStream *alice_stream, *bob_stream;
  GSocketConnection *conn;
  TpContact *contact;

  /* The race only appear in room stream tubes */
  if (contexts[i].contact)
    return;

  /* We can't break the race with other access controles :( */
  if (contexts[i].access_control != TP_SOCKET_ACCESS_CONTROL_PORT &&
      contexts[i].access_control != TP_SOCKET_ACCESS_CONTROL_CREDENTIALS)
    return;

  create_tube_service (test, TRUE, contexts[i].address_type,
      contexts[i].access_control, contexts[i].contact);

  tp_stream_tube_channel_offer_async (test->tube, NULL, tube_offer_cb, test);

  test->wait = 1;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  g_signal_connect (test->tube, "incoming",
      G_CALLBACK (tube_incoming_cb), test);

  alice_handle = tp_handle_ensure (test->contact_repo, "alice", NULL, NULL);
  bob_handle = tp_handle_ensure (test->contact_repo, "bob", NULL, NULL);

  /* Alice connects to the tube */
  address = tp_tests_stream_tube_channel_get_server_address (
      test->tube_chan_service);
  g_assert (address != NULL);

  client = g_socket_client_new ();

  g_socket_client_connect_async (client, G_SOCKET_CONNECTABLE (address),
      NULL, socket_connected, test);

  g_object_unref (client);

  test->wait = 1;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);
  g_assert (test->cm_stream != NULL);
  alice_cm_stream = g_object_ref (test->cm_stream);

  /* Now Bob connects to the tube */
  client = g_socket_client_new ();

  g_socket_client_connect_async (client, G_SOCKET_CONNECTABLE (address),
      NULL, socket_connected, test);

  g_object_unref (client);

  test->wait = 1;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);
  g_assert (test->cm_stream != NULL);
  bob_cm_stream = g_object_ref (test->cm_stream);

  /* The CM detects Bob's connection first */
  tp_tests_stream_tube_channel_peer_connected (test->tube_chan_service,
      bob_cm_stream, bob_handle);

  /* ...and then detects Alice's connection */
  tp_tests_stream_tube_channel_peer_connected (test->tube_chan_service,
      alice_cm_stream, alice_handle);

  /* Bob connection is identified */
  test->wait = 1;
  g_main_loop_run (test->mainloop);
  g_assert (test->tube_conn != NULL);

  conn = tp_stream_tube_connection_get_socket_connection (test->tube_conn);
  bob_stream = g_object_ref (conn);
  contact = tp_stream_tube_connection_get_contact (test->tube_conn);

  g_assert_cmpstr (tp_contact_get_identifier (contact), ==, "bob");

  /* Alice connection is identified */
  test->wait = 1;
  g_main_loop_run (test->mainloop);
  g_assert (test->tube_conn != NULL);

  conn = tp_stream_tube_connection_get_socket_connection (test->tube_conn);
  alice_stream = g_object_ref (conn);
  contact = tp_stream_tube_connection_get_contact (test->tube_conn);

  g_assert_cmpstr (tp_contact_get_identifier (contact), ==, "alice");

  /* Check that the streams have been mapped to the right contact */
  use_tube_with_streams (test, alice_stream, alice_cm_stream);
  use_tube_with_streams (test, bob_stream, bob_cm_stream);

  tp_handle_unref (test->contact_repo, alice_handle);
  g_object_unref (address);
  g_object_unref (alice_cm_stream);
  g_object_unref (bob_cm_stream);
}

static void
read_eof_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  Test *test = user_data;
  gssize size;

  size = g_input_stream_read_finish (G_INPUT_STREAM (source), result,
      &test->error);

  g_assert_no_error (test->error);
  g_assert_cmpuint (size, ==, 0);

  test->wait--;
  if (test->wait <= 0)
    g_main_loop_quit (test->mainloop);
}

/* We offer a contact stream tube to bob. The CM is bugged and claim that
 * another contact has connected to the tube. Tp-glib ignores it. */
static void
test_offer_bad_connection_conn_first (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  GSocketAddress *address;
  GSocketClient *client;
  TpHandle alice_handle;
  GInputStream *in;
  gchar cm_buffer[BUFFER_SIZE];

  /* Offer a tube to Bob */
  create_tube_service (test, TRUE, TP_SOCKET_ADDRESS_TYPE_UNIX,
      TP_SOCKET_ACCESS_CONTROL_LOCALHOST, TRUE);

  tp_stream_tube_channel_offer_async (test->tube, NULL, tube_offer_cb, test);

  test->wait = 1;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  /* Alice client connects to the tube */
  address = tp_tests_stream_tube_channel_get_server_address (
      test->tube_chan_service);
  g_assert (address != NULL);

  client = g_socket_client_new ();

  g_socket_client_connect_async (client, G_SOCKET_CONNECTABLE (address),
      NULL, socket_connected, test);

  g_object_unref (client);
  g_object_unref (address);

  test->wait = 1;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);
  g_assert (test->cm_stream != NULL);

  /* The connection is *not* announced on TpStreamTubeChannel */
  g_signal_connect (test->tube, "incoming",
      G_CALLBACK (tube_incoming_cb), test);

  /* Try to read on the stream to get EOF when it's closed */
  in = g_io_stream_get_input_stream (test->cm_stream);

  g_input_stream_read_async (in, cm_buffer, BUFFER_SIZE,
      G_PRIORITY_DEFAULT, NULL, read_eof_cb, test);

  alice_handle = tp_handle_ensure (test->contact_repo, "alice", NULL, NULL);

  tp_tests_stream_tube_channel_peer_connected (test->tube_chan_service,
      test->cm_stream, alice_handle);

  test->wait = 1;
  g_main_loop_run (test->mainloop);

  /* "incoming" has not be fired */
  g_assert (test->tube_conn == NULL);
}

/* Same test but now NewRemoteConnection is fired before the socket
 * connects */
static void
test_offer_bad_connection_sig_first (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  GSocketAddress *address;
  GSocketClient *client;
  TpHandle alice_handle;
  GInputStream *in;
  gchar cm_buffer[BUFFER_SIZE];

  /* Offer a tube to Bob */
  create_tube_service (test, TRUE, TP_SOCKET_ADDRESS_TYPE_UNIX,
      TP_SOCKET_ACCESS_CONTROL_LOCALHOST, TRUE);

  tp_stream_tube_channel_offer_async (test->tube, NULL, tube_offer_cb, test);

  test->wait = 1;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  /* CM announces that Alice is connected */
  alice_handle = tp_handle_ensure (test->contact_repo, "alice", NULL, NULL);

  tp_tests_stream_tube_channel_peer_connected (test->tube_chan_service,
      test->cm_stream, alice_handle);

  /* Alice client connects to the tube */
  address = tp_tests_stream_tube_channel_get_server_address (
      test->tube_chan_service);
  g_assert (address != NULL);

  client = g_socket_client_new ();

  g_socket_client_connect_async (client, G_SOCKET_CONNECTABLE (address),
      NULL, socket_connected, test);

  g_object_unref (client);
  g_object_unref (address);

  test->wait = 1;
  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);
  g_assert (test->cm_stream != NULL);

  /* The connection is *not* announced on TpStreamTubeChannel */
  g_signal_connect (test->tube, "incoming",
      G_CALLBACK (tube_incoming_cb), test);

  /* Try to read on the stream to get EOF when it's closed */
  in = g_io_stream_get_input_stream (test->cm_stream);

  g_input_stream_read_async (in, cm_buffer, BUFFER_SIZE,
      G_PRIORITY_DEFAULT, NULL, read_eof_cb, test);

  test->wait = 1;
  g_main_loop_run (test->mainloop);

  /* "incoming" has not be fired */
  g_assert (test->tube_conn == NULL);
}

int
main (int argc,
      char **argv)
{
  g_type_init ();
  tp_debug_set_flags ("all");

  g_test_init (&argc, &argv, NULL);
  g_test_bug_base ("http://bugs.freedesktop.org/show_bug.cgi?id=");

  g_test_add ("/stream-tube/creation", Test, NULL, setup, test_creation,
      teardown);
  g_test_add ("/stream-tube/properties", Test, NULL, setup, test_properties,
      teardown);
  g_test_add ("/stream-tube/accept/twice", Test, NULL, setup,
      test_accept_twice, teardown);
  g_test_add ("/stream-tube/accept/outgoing", Test, NULL, setup,
      test_accept_outgoing, teardown);

  run_tube_test ("/stream-tube/accept/success", test_accept_success);
  run_tube_test ("/stream-tube/offer/success", test_offer_success);
  run_tube_test ("/stream-tube/offer/race", test_offer_race);

  g_test_add ("/stream-tube/offer/bad-connection/conn-first", Test, NULL, setup,
      test_offer_bad_connection_conn_first, teardown);
  g_test_add ("/stream-tube/offer/bad-connection/sig-first", Test, NULL, setup,
      test_offer_bad_connection_sig_first, teardown);

  return g_test_run ();
}