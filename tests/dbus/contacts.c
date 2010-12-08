/* Feature test for TpContact creation.
 *
 * Code missing coverage in contact.c:
 * - connection becoming invalid
 * - fatal error on the connection
 * - inconsistent CM
 * - having to fall back to RequestAliases
 * - get_contacts_by_id with features (but it's trivial)
 *
 * Copyright (C) 2008 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2008 Nokia Corporation
 * Copyright (C) 2007 Will Thompson
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */
#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>

#include <telepathy-glib/connection.h>
#include <telepathy-glib/contact.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/debug.h>

#include "tests/lib/contacts-conn.h"
#include "tests/lib/debug.h"
#include "tests/lib/myassert.h"
#include "tests/lib/util.h"

typedef struct {
  TpBaseConnection *base_connection;
  TpBaseConnection *legacy_base_connection;
  TpBaseConnection *no_requests_base_connection;
  TpTestsContactsConnection *service_conn;
  TpConnection *client_conn;
  TpConnection *legacy_client_conn;
  TpConnection *no_requests_client_conn;
} Fixture;

typedef struct {
    GMainLoop *loop;
    GError *error /* initialized to 0 */;
    GPtrArray *contacts;
    GArray *invalid;
    gchar **good_ids;
    GHashTable *bad_ids;
} Result;

static void
by_handle_cb (TpConnection *connection,
              guint n_contacts,
              TpContact * const *contacts,
              guint n_invalid,
              const TpHandle *invalid,
              const GError *error,
              gpointer user_data,
              GObject *weak_object)
{
  Result *result = user_data;

  g_assert (result->invalid == NULL);
  g_assert (result->contacts == NULL);
  g_assert (result->error == NULL);
  g_assert (result->good_ids == NULL);
  g_assert (result->bad_ids == NULL);

  if (error == NULL)
    {
      guint i;

      DEBUG ("got %u contacts and %u invalid", n_contacts, n_invalid);

      result->invalid = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle),
          n_invalid);
      g_array_append_vals (result->invalid, invalid, n_invalid);

      result->contacts = g_ptr_array_sized_new (n_contacts);

      for (i = 0; i < n_contacts; i++)
        {
          TpContact *contact = contacts[i];
          GFile *avatar_file;
          gchar *avatar_uri = NULL;

          avatar_file = tp_contact_get_avatar_file (contact);
          if (avatar_file != NULL)
            avatar_uri = g_file_get_uri (avatar_file);

          DEBUG ("contact #%u: %p", i, contact);
          DEBUG ("contact #%u alias: %s", i, tp_contact_get_alias (contact));
          DEBUG ("contact #%u avatar token: %s", i,
              tp_contact_get_avatar_token (contact));
          DEBUG ("contact #%u avatar MIME type: %s", i,
              tp_contact_get_avatar_mime_type (contact));
          DEBUG ("contact #%u avatar file: %s", i,
              avatar_uri);
          DEBUG ("contact #%u presence type: %u", i,
              tp_contact_get_presence_type (contact));
          DEBUG ("contact #%u presence status: %s", i,
              tp_contact_get_presence_status (contact));
          DEBUG ("contact #%u presence message: %s", i,
              tp_contact_get_presence_message (contact));
          g_ptr_array_add (result->contacts, g_object_ref (contact));

          g_free (avatar_uri);
        }
    }
  else
    {
      DEBUG ("got an error: %s %u: %s", g_quark_to_string (error->domain),
          error->code, error->message);
      result->error = g_error_copy (error);
    }
}

static void
finish (gpointer r)
{
  Result *result = r;

  g_main_loop_quit (result->loop);
}

static void
reset_result (Result *result)
{
  g_clear_error (&(result->error));

  if (result->contacts != NULL)
    {
      g_ptr_array_foreach (result->contacts, (GFunc) g_object_unref, NULL);
      g_ptr_array_unref (result->contacts);
      result->contacts = NULL;
    }

  if (result->invalid)
    {
      g_array_unref (result->invalid);
      result->invalid = NULL;
    }

  if (result->good_ids)
    {
      g_strfreev (result->good_ids);
      result->good_ids = NULL;
    }

  if (result->bad_ids)
    {
      g_hash_table_unref (result->bad_ids);
      result->bad_ids = NULL;
    }
}

static void
contact_info_verify (TpContact *contact)
{
  GList *info;
  TpContactInfoField *field;

  g_assert (tp_contact_has_feature (contact, TP_CONTACT_FEATURE_CONTACT_INFO));

  info = tp_contact_get_contact_info (contact);
  g_assert (info != NULL);
  g_assert (info->data != NULL);
  g_assert (info->next == NULL);

  field = info->data;
  g_assert_cmpstr (field->field_name, ==, "n");
  g_assert (field->parameters != NULL);
  g_assert (field->parameters[0] == NULL);
  g_assert (field->field_value != NULL);
  g_assert_cmpstr (field->field_value[0], ==, "Foo");
  g_assert (field->field_value[1] == NULL);

  g_list_free (info);
}

static void
contact_info_notify_cb (TpContact *contact,
    GParamSpec *pspec,
    Result *result)
{
  contact_info_verify (contact);
  finish (result);
}

static void
contact_info_prepare_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpConnection *connection = TP_CONNECTION (object);
  Result *result = user_data;

  if (tp_proxy_prepare_finish (connection, res, &result->error))
    {
      TpContactInfoFlags flags;
      GList *specs;
      TpContactInfoFieldSpec *spec;

      flags = tp_connection_get_contact_info_flags (connection);
      g_assert_cmpint (flags, ==, TP_CONTACT_INFO_FLAG_PUSH |
          TP_CONTACT_INFO_FLAG_CAN_SET);

      specs = tp_connection_get_contact_info_supported_fields (connection);
      g_assert (specs != NULL);
      g_assert (specs->data != NULL);
      g_assert (specs->next == NULL);

      spec = specs->data;
      g_assert_cmpstr (spec->name, ==, "n");
      g_assert (spec->parameters != NULL);
      g_assert (spec->parameters[0] == NULL);
      g_assert_cmpint (spec->flags, ==, 0);
      g_assert_cmpint (spec->max, ==, 0);

      g_list_free (specs);
    }

  finish (result);
}

static void
contact_info_set_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpConnection *connection = TP_CONNECTION (object);
  Result *result = user_data;

  tp_connection_set_contact_info_finish (connection, res, &result->error);
  finish (result);
}

static void
contact_info_request_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpContact *contact = TP_CONTACT (object);
  Result *result = user_data;

  contact_info_verify (contact);

  tp_contact_request_contact_info_finish (contact, res, &result->error);
  finish (result);
}

static void
contact_info_request_cancelled_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpContact *contact = TP_CONTACT (object);
  Result *result = user_data;
  GError *error = NULL;

  tp_contact_request_contact_info_finish (contact, res, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_clear_error (&error);

  finish (result);
}

static gboolean
contact_info_request_cancel (gpointer cancellable)
{
  g_cancellable_cancel (cancellable);
  return FALSE;
}

static void
test_contact_info (TpTestsContactsConnection *service_conn,
    TpConnection *client_conn)
{
  Result result = { g_main_loop_new (NULL, FALSE), NULL, NULL, NULL };
  TpHandleRepoIface *service_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) service_conn, TP_HANDLE_TYPE_CONTACT);
  TpContactFeature features[] = { TP_CONTACT_FEATURE_CONTACT_INFO };
  TpContact *contact;
  TpHandle handle;
  const gchar *field_value[] = { "Foo", NULL };
  GPtrArray *info;
  GList *info_list = NULL;
  GQuark conn_features[] = { TP_CONNECTION_FEATURE_CONTACT_INFO, 0 };
  GCancellable *cancellable;

  /* Create fake info fields */
  info = g_ptr_array_new_with_free_func ((GDestroyNotify) g_value_array_free);
  g_ptr_array_add (info, tp_value_array_build (3,
      G_TYPE_STRING, "n",
      G_TYPE_STRV, NULL,
      G_TYPE_STRV, field_value,
      G_TYPE_INVALID));

  info_list = g_list_prepend (info_list,
      tp_contact_info_field_new ("n", NULL, (GStrv) field_value));

  tp_tests_contacts_connection_set_default_contact_info (service_conn, info);

  /* TEST1: Verify ContactInfo properties are correctly introspected on
   * TpConnection */
  tp_proxy_prepare_async (client_conn, conn_features, contact_info_prepare_cb,
      &result);
  g_main_loop_run (result.loop);
  g_assert_no_error (result.error);

  /* TEST2: Set contact info on the connection, then get the self TpContact.
   * This tests the set operation works correctly and also test TpContact
   * correctly introspects the ContactInfo when the feature is requested. */
  tp_connection_set_contact_info_async (client_conn, info_list,
    contact_info_set_cb, &result);
  g_main_loop_run (result.loop);
  g_assert_no_error (result.error);

  handle = tp_connection_get_self_handle (client_conn);
  tp_connection_get_contacts_by_handle (client_conn,
      1, &handle,
      G_N_ELEMENTS (features), features,
      by_handle_cb,
      &result, finish, NULL);
  g_main_loop_run (result.loop);
  g_assert_no_error (result.error);

  contact = g_ptr_array_index (result.contacts, 0);
  contact_info_verify (contact);

  reset_result (&result);

  /* TEST3: Create a TpContact with the INFO feature. Then change its info in
   * the CM. That should emit "notify::info" signal on the TpContact. */
  handle = tp_handle_ensure (service_repo, "info-test-3", NULL, NULL);
  tp_connection_get_contacts_by_handle (client_conn,
      1, &handle,
      G_N_ELEMENTS (features), features,
      by_handle_cb,
      &result, finish, NULL);
  g_main_loop_run (result.loop);
  g_assert_no_error (result.error);

  contact = g_ptr_array_index (result.contacts, 0);
  g_signal_connect (contact, "notify::contact-info",
      G_CALLBACK (contact_info_notify_cb), &result);

  tp_tests_contacts_connection_change_contact_info (service_conn, handle,
      info);
  g_main_loop_run (result.loop);
  g_assert_no_error (result.error);

  reset_result (&result);
  tp_handle_unref (service_repo, handle);

  /* TEST 4: First set the info in the CM for an handle, then create a TpContact
   * without INFO feature, and finally refresh the contact's info. */
  handle = tp_handle_ensure (service_repo, "info-test-4", NULL, NULL);
  tp_tests_contacts_connection_change_contact_info (service_conn, handle,
      info);

  tp_connection_get_contacts_by_handle (client_conn,
      1, &handle,
      0, NULL,
      by_handle_cb,
      &result, finish, NULL);
  g_main_loop_run (result.loop);
  g_assert_no_error (result.error);

  contact = g_ptr_array_index (result.contacts, 0);
  g_assert (tp_contact_get_contact_info (contact) == NULL);

  g_signal_connect (contact, "notify::contact-info",
      G_CALLBACK (contact_info_notify_cb), &result);
  tp_connection_refresh_contact_info (client_conn, 1, &contact);
  g_main_loop_run (result.loop);
  g_assert_no_error (result.error);

  reset_result (&result);
  tp_handle_unref (service_repo, handle);

  /* TEST5: Create a TpContact without INFO feature, then request the contact's
   * info. */
  handle = tp_handle_ensure (service_repo, "info-test-5", NULL, NULL);
  tp_connection_get_contacts_by_handle (client_conn,
      1, &handle,
      0, NULL,
      by_handle_cb,
      &result, finish, NULL);
  g_main_loop_run (result.loop);
  g_assert_no_error (result.error);

  contact = g_ptr_array_index (result.contacts, 0);
  g_assert (tp_contact_get_contact_info (contact) == NULL);

  tp_contact_request_contact_info_async (contact, NULL, contact_info_request_cb,
      &result);
  g_main_loop_run (result.loop);
  g_assert_no_error (result.error);

  reset_result (&result);
  tp_handle_unref (service_repo, handle);

  /* TEST6: Create a TpContact without INFO feature, then request the contact's
   * info, and cancel the request. */
  handle = tp_handle_ensure (service_repo, "info-test-6", NULL, NULL);
  tp_connection_get_contacts_by_handle (client_conn,
      1, &handle,
      0, NULL,
      by_handle_cb,
      &result, finish, NULL);
  g_main_loop_run (result.loop);
  g_assert_no_error (result.error);

  contact = g_ptr_array_index (result.contacts, 0);
  g_assert (tp_contact_get_contact_info (contact) == NULL);

  cancellable = g_cancellable_new ();
  tp_contact_request_contact_info_async (contact, cancellable,
      contact_info_request_cancelled_cb, &result);

  g_idle_add_full (G_PRIORITY_HIGH, contact_info_request_cancel,
      cancellable, g_object_unref);

  g_main_loop_run (result.loop);
  g_assert_no_error (result.error);

  reset_result (&result);
  tp_handle_unref (service_repo, handle);

  /* Cleanup */
  g_main_loop_unref (result.loop);
  g_ptr_array_unref (info);
  tp_contact_info_list_free (info_list);
}

static void
prepare_avatar_requirements_cb (GObject *object,
    GAsyncResult *res,
    gpointer user_data)
{
  TpConnection *connection = TP_CONNECTION (object);
  Result *result = user_data;

  if (tp_proxy_prepare_finish (connection, res, &result->error))
    {
      TpAvatarRequirements *req;

      req = tp_connection_get_avatar_requirements (connection);
      g_assert (req != NULL);
      g_assert (req->supported_mime_types != NULL);
      g_assert_cmpstr (req->supported_mime_types[0], ==, "image/png");
      g_assert (req->supported_mime_types[1] == NULL);
      g_assert_cmpuint (req->minimum_width, ==, 1);
      g_assert_cmpuint (req->minimum_height, ==, 2);
      g_assert_cmpuint (req->recommended_width, ==, 3);
      g_assert_cmpuint (req->recommended_height, ==, 4);
      g_assert_cmpuint (req->maximum_width, ==, 5);
      g_assert_cmpuint (req->maximum_height, ==, 6);
      g_assert_cmpuint (req->maximum_bytes, ==, 7);
    }

  finish (result);
}

static void
test_avatar_requirements (TpConnection *client_conn)
{
  Result result = { g_main_loop_new (NULL, FALSE), NULL, NULL, NULL };
  GQuark features[] = { TP_CONNECTION_FEATURE_AVATAR_REQUIREMENTS, 0 };

  g_message (G_STRFUNC);

  tp_proxy_prepare_async (TP_PROXY (client_conn), features,
      prepare_avatar_requirements_cb, &result);
  g_main_loop_run (result.loop);

  g_assert_no_error (result.error);
  g_main_loop_unref (result.loop);
}

static GFile *
create_contact_with_fake_avatar (TpTestsContactsConnection *service_conn,
    TpConnection *client_conn,
    const gchar *id)
{
  Result result = { g_main_loop_new (NULL, FALSE), NULL, NULL, NULL };
  TpHandleRepoIface *service_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) service_conn, TP_HANDLE_TYPE_CONTACT);
  TpContactFeature features[] = { TP_CONTACT_FEATURE_AVATAR_DATA };
  const gchar avatar_data[] = "fake-avatar-data";
  const gchar avatar_token[] = "fake-avatar-token";
  const gchar avatar_mime_type[] = "fake-avatar-mime-type";
  TpContact *contact;
  TpHandle handle;
  GArray *array;
  GFile *avatar_file;
  gchar *content = NULL;

  handle = tp_handle_ensure (service_repo, id, NULL, NULL);
  array = g_array_new (FALSE, FALSE, sizeof (gchar));
  g_array_append_vals (array, avatar_data, strlen (avatar_data) + 1);

  tp_tests_contacts_connection_change_avatar_data (service_conn, handle, array,
      avatar_mime_type, avatar_token);

  tp_connection_get_contacts_by_handle (client_conn,
      1, &handle,
      G_N_ELEMENTS (features), features,
      by_handle_cb,
      &result, finish, NULL);
  g_main_loop_run (result.loop);
  g_assert_no_error (result.error);

  contact = g_ptr_array_index (result.contacts, 0);
  if (tp_contact_get_avatar_file (contact) == NULL)
    {
      g_signal_connect_swapped (contact, "notify::avatar-file",
          G_CALLBACK (finish), &result);
      g_main_loop_run (result.loop);
    }

  g_assert_cmpstr (tp_contact_get_avatar_mime_type (contact), ==,
      avatar_mime_type);
  g_assert_cmpstr (tp_contact_get_avatar_token (contact), ==, avatar_token);

  avatar_file = tp_contact_get_avatar_file (contact);
  g_assert (avatar_file != NULL);
  g_file_load_contents (avatar_file, NULL, &content, NULL, NULL, &result.error);
  g_assert_no_error (result.error);
  g_assert_cmpstr (content, ==, avatar_data);
  g_free (content);

  /* Keep avatar_file alive after contact destruction */
  g_object_ref (avatar_file);

  reset_result (&result);
  g_main_loop_unref (result.loop);

  tp_handle_unref (service_repo, handle);
  g_array_unref (array);

  return avatar_file;
}

static void
avatar_retrieved_cb (TpConnection *connection,
    guint handle,
    const gchar *token,
    const GArray *avatar,
    const gchar *mime_type,
    gpointer user_data,
    GObject *weak_object)
{
  gboolean *called = user_data;

  *called = TRUE;
}

/* From telepathy-haze, with permission */
static gboolean
haze_remove_directory (const gchar *path)
{
  const gchar *child_path;
  GDir *dir = g_dir_open (path, 0, NULL);
  gboolean ret = TRUE;

  if (!dir)
    return FALSE;

  while (ret && (child_path = g_dir_read_name (dir)))
    {
      gchar *child_full_path = g_build_filename (path, child_path, NULL);

      if (g_file_test (child_full_path, G_FILE_TEST_IS_DIR))
        {
          if (!haze_remove_directory (child_full_path))
            ret = FALSE;
        }
      else
        {
          DEBUG ("deleting %s", child_full_path);

          if (g_unlink (child_full_path))
            ret = FALSE;
        }

      g_free (child_full_path);
    }

  g_dir_close (dir);

  if (ret)
    {
      DEBUG ("deleting %s", path);
      ret = !g_rmdir (path);
    }

  return ret;
}

#define RAND_STR_LEN 6

static void
test_avatar_data (TpTestsContactsConnection *service_conn,
    TpConnection *client_conn)
{
  static const gchar letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  gchar rand_str[RAND_STR_LEN + 1];
  gchar *dir;
  guint i;
  gboolean avatar_retrieved_called;
  GError *error = NULL;
  GFile *file1, *file2;
  TpProxySignalConnection *signal_id;

  g_message (G_STRFUNC);

  /* Make sure g_get_user_cache_dir() returns a tmp directory, to not mess up
   * user's cache dir.
   * FIXME: Replace this with g_mkdtemp once it gets added to GLib.
   * See GNOME bug #118563 */
  for (i = 0; i < RAND_STR_LEN; i++)
    rand_str[i] = letters[g_random_int_range (0, strlen (letters))];
  rand_str[RAND_STR_LEN] = '\0';
  dir = g_build_filename (g_get_tmp_dir (), rand_str, NULL);
  g_assert (g_mkdir (dir, 0700) == 0);
  g_setenv ("XDG_CACHE_HOME", dir, TRUE);
  g_assert_cmpstr (g_get_user_cache_dir (), ==, dir);

  /* Check if AvatarRetrieved gets called */
  signal_id = tp_cli_connection_interface_avatars_connect_to_avatar_retrieved (
      client_conn, avatar_retrieved_cb, &avatar_retrieved_called, NULL, NULL,
      &error);
  g_assert_no_error (error);

  /* First time we create a contact, avatar should not be in cache, so
   * AvatarRetrived should be called */
  avatar_retrieved_called = FALSE;
  file1 = create_contact_with_fake_avatar (service_conn, client_conn,
      "fake-id1");
  g_assert (avatar_retrieved_called);

  /* Second time we create a contact, avatar should be in cache now, so
   * AvatarRetrived should NOT be called */
  avatar_retrieved_called = FALSE;
  file2 = create_contact_with_fake_avatar (service_conn, client_conn,
      "fake-id2");
  g_assert (!avatar_retrieved_called);

  g_assert (g_file_equal (file1, file2));
  g_assert (haze_remove_directory (dir));

  tp_proxy_signal_connection_disconnect (signal_id);
  g_object_unref (file1);
  g_object_unref (file2);
  g_free (dir);
}

static void
test_by_handle (TpTestsContactsConnection *service_conn,
                TpConnection *client_conn)
{
  Result result = { g_main_loop_new (NULL, FALSE), NULL, NULL, NULL };
  TpHandle handles[5] = { 0, 0, 0, 0, 0 };
  TpHandleRepoIface *service_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) service_conn, TP_HANDLE_TYPE_CONTACT);
  TpContact *contacts[4];
  gpointer weak_pointers[4];
  guint i;

  g_message (G_STRFUNC);

  /* arrange for some handles to exist */
  handles[0] = tp_handle_ensure (service_repo, "alice", NULL, NULL);
  MYASSERT (handles[0] != 0, "");
  handles[1] = tp_handle_ensure (service_repo, "bob", NULL, NULL);
  MYASSERT (handles[1] != 0, "");
  /* randomly guess at a handle that shouldn't exist */
  handles[2] = 31337;
  MYASSERT (!tp_handle_is_valid (service_repo, 31337, NULL), "");
  /* another valid handle */
  handles[3] = tp_handle_ensure (service_repo, "chris", NULL, NULL);
  MYASSERT (handles[3] != 0, "");
  /* another invalid handle */
  handles[4] = 12345;
  MYASSERT (!tp_handle_is_valid (service_repo, 12345, NULL), "");

  /* Make a request for the following 5 contacts:
   * - alice
   * - bob
   * - invalid handle 31337
   * - chris
   * - invalid handle 12345
   */
  tp_connection_get_contacts_by_handle (client_conn,
      5, handles,
      0, NULL,
      by_handle_cb,
      &result, finish, NULL);

  g_main_loop_run (result.loop);

  MYASSERT (result.contacts->len == 3, ": %u", result.contacts->len);
  MYASSERT (result.invalid->len == 2, ": %u", result.invalid->len);
  g_assert_no_error (result.error);

  MYASSERT (g_ptr_array_index (result.contacts, 0) != NULL, "");
  MYASSERT (g_ptr_array_index (result.contacts, 1) != NULL, "");
  MYASSERT (g_ptr_array_index (result.contacts, 2) != NULL, "");
  contacts[0] = g_object_ref (g_ptr_array_index (result.contacts, 0));
  g_assert_cmpuint (tp_contact_get_handle (contacts[0]), ==, handles[0]);
  g_assert_cmpstr (tp_contact_get_identifier (contacts[0]), ==, "alice");
  contacts[1] = g_object_ref (g_ptr_array_index (result.contacts, 1));
  g_assert_cmpuint (tp_contact_get_handle (contacts[1]), ==, handles[1]);
  g_assert_cmpstr (tp_contact_get_identifier (contacts[1]), ==, "bob");
  contacts[3] = g_object_ref (g_ptr_array_index (result.contacts, 2));
  g_assert_cmpuint (tp_contact_get_handle (contacts[3]), ==, handles[3]);
  g_assert_cmpstr (tp_contact_get_identifier (contacts[3]), ==, "chris");

  /* clean up before doing the second request */
  reset_result (&result);
  g_assert (result.error == NULL);

  /* Replace one of the invalid handles with a valid one */
  handles[2] = tp_handle_ensure (service_repo, "dora", NULL, NULL);
  MYASSERT (handles[2] != 0, "");

  /* Make a request for the following 4 contacts:
   * - alice (TpContact exists)
   * - bob (TpContact exists)
   * - dora (TpContact needs to be created)
   * - chris (TpContact exists)
   */
  tp_connection_get_contacts_by_handle (client_conn,
      4, handles,
      0, NULL,
      by_handle_cb,
      &result, finish, NULL);

  g_main_loop_run (result.loop);

  /* assert that we got the same contacts back */

  MYASSERT (result.contacts->len == 4, ": %u", result.contacts->len);
  MYASSERT (result.invalid->len == 0, ": %u", result.invalid->len);
  g_assert_no_error (result.error);

  /* 0, 1 and 3 we already have a reference to */
  MYASSERT (g_ptr_array_index (result.contacts, 0) == contacts[0], "");
  g_object_unref (g_ptr_array_index (result.contacts, 0));
  MYASSERT (g_ptr_array_index (result.contacts, 1) == contacts[1], "");
  g_object_unref (g_ptr_array_index (result.contacts, 1));
  MYASSERT (g_ptr_array_index (result.contacts, 3) == contacts[3], "");
  g_object_unref (g_ptr_array_index (result.contacts, 3));

  /* 2 we don't */
  contacts[2] = g_ptr_array_index (result.contacts, 2);
  g_assert_cmpuint (tp_contact_get_handle (contacts[2]), ==, handles[2]);
  g_assert_cmpstr (tp_contact_get_identifier (contacts[2]), ==, "dora");

  g_ptr_array_unref (result.contacts);
  result.contacts = NULL;

  /* clean up refs to contacts and assert that they aren't leaked */

  for (i = 0; i < 4; i++)
    {
      weak_pointers[i] = contacts[i];
      g_object_add_weak_pointer ((GObject *) contacts[i],weak_pointers +i);
    }

  for (i = 0; i < 4; i++)
    {
      g_object_unref (contacts[i]);
      MYASSERT (weak_pointers[i] == NULL, ": %u", i);
    }

  /* wait for ReleaseHandles to run */
  tp_tests_proxy_run_until_dbus_queue_processed (client_conn);

  /* unref all the handles we created service-side */
  tp_handle_unref (service_repo, handles[0]);
  MYASSERT (!tp_handle_is_valid (service_repo, handles[0], NULL), "");
  tp_handle_unref (service_repo, handles[1]);
  MYASSERT (!tp_handle_is_valid (service_repo, handles[1], NULL), "");
  tp_handle_unref (service_repo, handles[2]);
  MYASSERT (!tp_handle_is_valid (service_repo, handles[2], NULL), "");
  tp_handle_unref (service_repo, handles[3]);
  MYASSERT (!tp_handle_is_valid (service_repo, handles[3], NULL), "");

  /* remaining cleanup */
  g_assert (result.error == NULL);
  reset_result (&result);
  g_main_loop_unref (result.loop);
}

static void
test_no_features (TpTestsContactsConnection *service_conn,
                  TpConnection *client_conn)
{
  Result result = { g_main_loop_new (NULL, FALSE), NULL, NULL, NULL };
  const gchar * const ids[] = { "alice", "bob", "chris" };
  TpHandle handles[3] = { 0, 0, 0 };
  TpHandleRepoIface *service_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) service_conn, TP_HANDLE_TYPE_CONTACT);
  TpContact *contacts[3];
  guint i;

  g_message (G_STRFUNC);

  for (i = 0; i < 3; i++)
    handles[i] = tp_handle_ensure (service_repo, ids[i], NULL, NULL);

  tp_connection_get_contacts_by_handle (client_conn,
      3, handles,
      0, NULL,
      by_handle_cb,
      &result, finish, NULL);

  g_main_loop_run (result.loop);

  MYASSERT (result.contacts->len == 3, ": %u", result.contacts->len);
  MYASSERT (result.invalid->len == 0, ": %u", result.invalid->len);
  g_assert_no_error (result.error);

  MYASSERT (g_ptr_array_index (result.contacts, 0) != NULL, "");
  MYASSERT (g_ptr_array_index (result.contacts, 1) != NULL, "");
  MYASSERT (g_ptr_array_index (result.contacts, 2) != NULL, "");

  for (i = 0; i < 3; i++)
    contacts[i] = g_object_ref (g_ptr_array_index (result.contacts, i));

  g_assert (result.error == NULL);
  reset_result (&result);

  for (i = 0; i < 3; i++)
    {
      MYASSERT (tp_contact_get_connection (contacts[i]) == client_conn, "");
      g_assert_cmpuint (tp_contact_get_handle (contacts[i]), ==, handles[i]);
      g_assert_cmpstr (tp_contact_get_identifier (contacts[i]), ==,
          ids[i]);
      g_assert_cmpstr (tp_contact_get_alias (contacts[i]), ==,
          tp_contact_get_identifier (contacts[i]));
      MYASSERT (tp_contact_get_avatar_token (contacts[i]) == NULL,
          ": %s", tp_contact_get_avatar_token (contacts[i]));
      g_assert_cmpuint (tp_contact_get_presence_type (contacts[i]), ==,
          TP_CONNECTION_PRESENCE_TYPE_UNSET);
      g_assert_cmpstr (tp_contact_get_presence_status (contacts[i]), ==,
          "");
      g_assert_cmpstr (tp_contact_get_presence_message (contacts[i]), ==,
          "");
      MYASSERT (!tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_ALIAS), "");
      MYASSERT (!tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_AVATAR_TOKEN), "");
      MYASSERT (!tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_PRESENCE), "");
      MYASSERT (!tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_LOCATION), "");
      MYASSERT (!tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_CAPABILITIES), "");
    }

  for (i = 0; i < 3; i++)
    {
      g_object_unref (contacts[i]);
      tp_tests_proxy_run_until_dbus_queue_processed (client_conn);
      tp_handle_unref (service_repo, handles[i]);
      MYASSERT (!tp_handle_is_valid (service_repo, handles[i], NULL), "");
    }

  /* remaining cleanup */
  g_main_loop_unref (result.loop);
}

static void
upgrade_cb (TpConnection *connection,
            guint n_contacts,
            TpContact * const *contacts,
            const GError *error,
            gpointer user_data,
            GObject *weak_object)
{
  Result *result = user_data;

  g_assert (result->invalid == NULL);
  g_assert (result->contacts == NULL);
  g_assert (result->error == NULL);
  g_assert (result->good_ids == NULL);
  g_assert (result->bad_ids == NULL);

  if (error == NULL)
    {
      guint i;

      DEBUG ("got %u contacts", n_contacts);

      result->contacts = g_ptr_array_sized_new (n_contacts);

      for (i = 0; i < n_contacts; i++)
        {
          TpContact *contact = contacts[i];

          DEBUG ("contact #%u: %p", i, contact);
          DEBUG ("contact #%u alias: %s", i, tp_contact_get_alias (contact));
          DEBUG ("contact #%u avatar token: %s", i,
              tp_contact_get_avatar_token (contact));
          DEBUG ("contact #%u presence type: %u", i,
              tp_contact_get_presence_type (contact));
          DEBUG ("contact #%u presence status: %s", i,
              tp_contact_get_presence_status (contact));
          DEBUG ("contact #%u presence message: %s", i,
              tp_contact_get_presence_message (contact));
          g_ptr_array_add (result->contacts, g_object_ref (contact));
        }
    }
  else
    {
      DEBUG ("got an error: %s %u: %s", g_quark_to_string (error->domain),
          error->code, error->message);
      result->error = g_error_copy (error);
    }
}

/* Just put a country in locations for easier comparaisons.
 * FIXME: Ideally we should have a MYASSERT_SAME_ASV */
#define ASSERT_SAME_LOCATION(left, right)\
  G_STMT_START {\
    g_assert_cmpuint (g_hash_table_size (left), ==, \
        g_hash_table_size (right));\
    g_assert_cmpstr (g_hash_table_lookup (left, "country"), ==,\
        g_hash_table_lookup (right, "country"));\
  } G_STMT_END

static void
free_rcc_list (GPtrArray *rccs)
{
  g_boxed_free (TP_ARRAY_TYPE_REQUESTABLE_CHANNEL_CLASS_LIST, rccs);
}

static void
add_text_chat_class (GPtrArray *classes,
    TpHandleType handle_type)
{
  GHashTable *fixed;
  const gchar * const allowed[] = { NULL };
  GValueArray *arr;

  fixed = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TP_IFACE_CHANNEL_TYPE_TEXT,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
          handle_type,
      NULL);

  arr = tp_value_array_build (2,
      TP_HASH_TYPE_STRING_VARIANT_MAP, fixed,
      G_TYPE_STRV, allowed,
      G_TYPE_INVALID);

  g_hash_table_unref (fixed);

  g_ptr_array_add (classes, arr);
}

static GHashTable *
create_contact_caps (TpHandle *handles)
{
  GHashTable *capabilities;
  GPtrArray *caps1, *caps2, *caps3;

  capabilities = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) free_rcc_list);

  /* Support private text chats */
  caps1 = g_ptr_array_sized_new (2);
  add_text_chat_class (caps1, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (capabilities, GUINT_TO_POINTER (handles[0]), caps1);

  /* Support text chatrooms */
  caps2 = g_ptr_array_sized_new (1);
  add_text_chat_class (caps2, TP_HANDLE_TYPE_ROOM);
  g_hash_table_insert (capabilities, GUINT_TO_POINTER (handles[1]), caps2);

  /* Don't support anything */
  caps3 = g_ptr_array_sized_new (0);
  g_hash_table_insert (capabilities, GUINT_TO_POINTER (handles[2]), caps3);

  return capabilities;
}

static void
test_upgrade (TpTestsContactsConnection *service_conn,
              TpConnection *client_conn)
{
  Result result = { g_main_loop_new (NULL, FALSE), NULL, NULL, NULL };
  TpHandle handles[] = { 0, 0, 0 };
  static const gchar * const ids[] = { "alice", "bob", "chris" };
  static const gchar * const aliases[] = { "Alice in Wonderland",
      "Bob the Builder", "Christopher Robin" };
  static const gchar * const tokens[] = { "aaaaa", "bbbbb", "ccccc" };
  static TpTestsContactsConnectionPresenceStatusIndex statuses[] = {
      TP_TESTS_CONTACTS_CONNECTION_STATUS_AVAILABLE,
      TP_TESTS_CONTACTS_CONNECTION_STATUS_BUSY,
      TP_TESTS_CONTACTS_CONNECTION_STATUS_AWAY };
  static const gchar * const messages[] = { "", "Fixing it",
      "GON OUT BACKSON" };
  GHashTable *location_1 = tp_asv_new (
      "country",  G_TYPE_STRING, "United-kingdoms", NULL);
  GHashTable *location_2 = tp_asv_new (
      "country",  G_TYPE_STRING, "Atlantis", NULL);
  GHashTable *location_3 = tp_asv_new (
      "country",  G_TYPE_STRING, "Belgium", NULL);
  GHashTable *locations[] = { location_1, location_2, location_3 };
  GHashTable *capabilities;
  TpHandleRepoIface *service_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) service_conn, TP_HANDLE_TYPE_CONTACT);
  TpContact *contacts[3];
  TpContactFeature features[] = { TP_CONTACT_FEATURE_ALIAS,
      TP_CONTACT_FEATURE_AVATAR_TOKEN, TP_CONTACT_FEATURE_PRESENCE,
      TP_CONTACT_FEATURE_LOCATION, TP_CONTACT_FEATURE_CAPABILITIES };
  guint i;

  g_message (G_STRFUNC);

  for (i = 0; i < 3; i++)
    handles[i] = tp_handle_ensure (service_repo, ids[i], NULL, NULL);

  tp_tests_contacts_connection_change_aliases (service_conn, 3, handles,
      aliases);
  tp_tests_contacts_connection_change_presences (service_conn, 3, handles,
      statuses, messages);
  tp_tests_contacts_connection_change_avatar_tokens (service_conn, 3, handles,
      tokens);
  tp_tests_contacts_connection_change_locations (service_conn, 3, handles,
      locations);

  capabilities = create_contact_caps (handles);
  tp_tests_contacts_connection_change_capabilities (service_conn, capabilities);
  g_hash_table_unref (capabilities);

  tp_connection_get_contacts_by_handle (client_conn,
      3, handles,
      0, NULL,
      by_handle_cb,
      &result, finish, NULL);

  g_main_loop_run (result.loop);

  MYASSERT (result.contacts->len == 3, ": %u", result.contacts->len);
  MYASSERT (result.invalid->len == 0, ": %u", result.invalid->len);
  g_assert_no_error (result.error);

  MYASSERT (g_ptr_array_index (result.contacts, 0) != NULL, "");
  MYASSERT (g_ptr_array_index (result.contacts, 1) != NULL, "");
  MYASSERT (g_ptr_array_index (result.contacts, 2) != NULL, "");

  for (i = 0; i < 3; i++)
    contacts[i] = g_object_ref (g_ptr_array_index (result.contacts, i));

  for (i = 0; i < 3; i++)
    {
      MYASSERT (tp_contact_get_connection (contacts[i]) == client_conn, "");
      g_assert_cmpuint (tp_contact_get_handle (contacts[i]), ==, handles[i]);
      g_assert_cmpstr (tp_contact_get_identifier (contacts[i]), ==,
          ids[i]);
      g_assert_cmpstr (tp_contact_get_alias (contacts[i]), ==,
          tp_contact_get_identifier (contacts[i]));
      MYASSERT (tp_contact_get_avatar_token (contacts[i]) == NULL,
          ": %s", tp_contact_get_avatar_token (contacts[i]));
      g_assert_cmpuint (tp_contact_get_presence_type (contacts[i]), ==,
          TP_CONNECTION_PRESENCE_TYPE_UNSET);
      g_assert_cmpstr (tp_contact_get_presence_status (contacts[i]), ==,
          "");
      g_assert_cmpstr (tp_contact_get_presence_message (contacts[i]), ==,
          "");
      MYASSERT (!tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_ALIAS), "");
      MYASSERT (!tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_AVATAR_TOKEN), "");
      MYASSERT (!tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_PRESENCE), "");
      MYASSERT (!tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_LOCATION), "");
      MYASSERT (!tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_CAPABILITIES), "");
    }

  /* clean up before doing the second request */
  g_assert (result.error == NULL);
  reset_result (&result);

  tp_connection_upgrade_contacts (client_conn,
      3, contacts,
      sizeof (features) / sizeof (features[0]), features,
      upgrade_cb,
      &result, finish, NULL);

  g_main_loop_run (result.loop);

  MYASSERT (result.contacts->len == 3, ": %u", result.contacts->len);
  MYASSERT (result.invalid == NULL, "");
  g_assert_no_error (result.error);

  for (i = 0; i < 3; i++)
    {
      MYASSERT (g_ptr_array_index (result.contacts, 0) == contacts[0], "");
    }

  g_assert (result.invalid == NULL);
  g_assert (result.error == NULL);
  reset_result (&result);

  for (i = 0; i < 3; i++)
    {
      g_assert_cmpuint (tp_contact_get_handle (contacts[i]), ==, handles[i]);
      g_assert_cmpstr (tp_contact_get_identifier (contacts[i]), ==,
          ids[i]);

      MYASSERT (tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_ALIAS), "");
      g_assert_cmpstr (tp_contact_get_alias (contacts[i]), ==,
          aliases[i]);

      MYASSERT (tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_AVATAR_TOKEN), "");
      g_assert_cmpstr (tp_contact_get_avatar_token (contacts[i]), ==,
          tokens[i]);

      MYASSERT (tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_PRESENCE), "");
      g_assert_cmpstr (tp_contact_get_presence_message (contacts[i]), ==,
          messages[i]);

      MYASSERT (tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_LOCATION), "");
      ASSERT_SAME_LOCATION (tp_contact_get_location (contacts[i]),
          locations[i]);

      MYASSERT (tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_CAPABILITIES), "");
      MYASSERT (tp_contact_get_capabilities (contacts[i]) != NULL, "");
    }

  g_assert_cmpuint (tp_contact_get_presence_type (contacts[0]), ==,
      TP_CONNECTION_PRESENCE_TYPE_AVAILABLE);
  g_assert_cmpstr (tp_contact_get_presence_status (contacts[0]), ==,
      "available");
  g_assert_cmpuint (tp_contact_get_presence_type (contacts[1]), ==,
      TP_CONNECTION_PRESENCE_TYPE_BUSY);
  g_assert_cmpstr (tp_contact_get_presence_status (contacts[1]), ==,
      "busy");
  g_assert_cmpuint (tp_contact_get_presence_type (contacts[2]), ==,
      TP_CONNECTION_PRESENCE_TYPE_AWAY);
  g_assert_cmpstr (tp_contact_get_presence_status (contacts[2]), ==,
      "away");

  for (i = 0; i < 3; i++)
    {
      g_object_unref (contacts[i]);
      tp_tests_proxy_run_until_dbus_queue_processed (client_conn);
      tp_handle_unref (service_repo, handles[i]);
      MYASSERT (!tp_handle_is_valid (service_repo, handles[i], NULL), "");
    }

  /* remaining cleanup */
  g_hash_table_unref (location_1);
  g_hash_table_unref (location_2);
  g_hash_table_unref (location_3);
  g_main_loop_unref (result.loop);
}

typedef struct
{
  gboolean alias_changed;
  gboolean avatar_token_changed;
  gboolean presence_type_changed;
  gboolean presence_status_changed;
  gboolean presence_msg_changed;
  gboolean location_changed;
  gboolean capabilities_changed;
} notify_ctx;

static void
notify_ctx_init (notify_ctx *ctx)
{
  ctx->alias_changed = FALSE;
  ctx->avatar_token_changed = FALSE;
  ctx->presence_type_changed = FALSE;
  ctx->presence_status_changed = FALSE;
  ctx->presence_msg_changed = FALSE;
  ctx->location_changed = FALSE;
  ctx->capabilities_changed = FALSE;
}

static gboolean
notify_ctx_is_fully_changed (notify_ctx *ctx)
{
  return ctx->alias_changed && ctx->avatar_token_changed &&
    ctx->presence_type_changed && ctx->presence_status_changed &&
    ctx->presence_msg_changed && ctx->location_changed &&
    ctx->capabilities_changed;
}

static gboolean
notify_ctx_is_changed (notify_ctx *ctx)
{
  return ctx->alias_changed || ctx->avatar_token_changed ||
    ctx->presence_type_changed || ctx->presence_status_changed ||
    ctx->presence_msg_changed || ctx->location_changed ||
    ctx->capabilities_changed;
}

static void
contact_notify_cb (TpContact *contact,
    GParamSpec *param,
    notify_ctx *ctx)
{
  if (!tp_strdiff (param->name, "alias"))
    ctx->alias_changed = TRUE;
  else if (!tp_strdiff (param->name, "avatar-token"))
    ctx->avatar_token_changed = TRUE;
  else if (!tp_strdiff (param->name, "presence-type"))
    ctx->presence_type_changed = TRUE;
  else if (!tp_strdiff (param->name, "presence-status"))
    ctx->presence_status_changed = TRUE;
  else if (!tp_strdiff (param->name, "presence-message"))
    ctx->presence_msg_changed = TRUE;
  else if (!tp_strdiff (param->name, "location"))
    ctx->location_changed = TRUE;
  else if (!tp_strdiff (param->name, "capabilities"))
    ctx->capabilities_changed = TRUE;
}

static GHashTable *
create_new_contact_caps (TpHandle *handles)
{
  GHashTable *capabilities;
  GPtrArray *caps1, *caps2;

  capabilities = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) free_rcc_list);

  /* Support private text chats and chatrooms */
  caps1 = g_ptr_array_sized_new (2);
  add_text_chat_class (caps1, TP_HANDLE_TYPE_CONTACT);
  add_text_chat_class (caps1, TP_HANDLE_TYPE_ROOM);
  g_hash_table_insert (capabilities, GUINT_TO_POINTER (handles[0]), caps1);

  /* Don't support anything */
  caps2 = g_ptr_array_sized_new (0);
  g_hash_table_insert (capabilities, GUINT_TO_POINTER (handles[1]), caps2);

  return capabilities;
}

static void
test_features (TpTestsContactsConnection *service_conn,
               TpConnection *client_conn)
{
  Result result = { g_main_loop_new (NULL, FALSE), NULL, NULL, NULL };
  TpHandle handles[] = { 0, 0, 0 };
  static const gchar * const ids[] = { "alice", "bob", "chris" };
  static const gchar * const aliases[] = { "Alice in Wonderland",
      "Bob the Builder", "Christopher Robin" };
  static const gchar * const tokens[] = { "aaaaa", "bbbbb", "ccccc" };
  static TpTestsContactsConnectionPresenceStatusIndex statuses[] = {
      TP_TESTS_CONTACTS_CONNECTION_STATUS_AVAILABLE,
      TP_TESTS_CONTACTS_CONNECTION_STATUS_BUSY,
      TP_TESTS_CONTACTS_CONNECTION_STATUS_AWAY };
  static const gchar * const messages[] = { "", "Fixing it",
      "GON OUT BACKSON" };
  static const gchar * const new_aliases[] = { "Alice [at a tea party]",
      "Bob the Plumber" };
  static const gchar * const new_tokens[] = { "AAAA", "BBBB" };
  static TpTestsContactsConnectionPresenceStatusIndex new_statuses[] = {
      TP_TESTS_CONTACTS_CONNECTION_STATUS_AWAY,
      TP_TESTS_CONTACTS_CONNECTION_STATUS_AVAILABLE };
  static const gchar * const new_messages[] = { "At the Mad Hatter's",
      "It'll cost you" };
  GHashTable *location_1 = tp_asv_new (
      "country",  G_TYPE_STRING, "United-kingdoms", NULL);
  GHashTable *location_2 = tp_asv_new (
      "country",  G_TYPE_STRING, "Atlantis", NULL);
  GHashTable *location_3 = tp_asv_new (
      "country",  G_TYPE_STRING, "Belgium", NULL);
  GHashTable *locations[] = { location_1, location_2, location_3 };
  GHashTable *location_4 = tp_asv_new (
      "country",  G_TYPE_STRING, "France", NULL);
  GHashTable *location_5 = tp_asv_new (
      "country",  G_TYPE_STRING, "Irland", NULL);
  GHashTable *new_locations[] = { location_4, location_5 };
  GHashTable *capabilities, *new_capabilities;
  gboolean support_text_chats[] = { TRUE, FALSE, FALSE };
  gboolean support_text_chatrooms[] = { FALSE, TRUE, FALSE };
  gboolean new_support_text_chats[] = { TRUE, FALSE };
  gboolean new_support_text_chatrooms[] = { TRUE, FALSE };
  TpHandleRepoIface *service_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) service_conn, TP_HANDLE_TYPE_CONTACT);
  TpContact *contacts[3];
  TpContactFeature features[] = { TP_CONTACT_FEATURE_ALIAS,
      TP_CONTACT_FEATURE_AVATAR_TOKEN, TP_CONTACT_FEATURE_PRESENCE,
      TP_CONTACT_FEATURE_LOCATION, TP_CONTACT_FEATURE_CAPABILITIES };
  guint i;
  struct {
      TpConnection *connection;
      TpHandle handle;
      gchar *identifier;
      gchar *alias;
      gchar *avatar_token;
      TpConnectionPresenceType presence_type;
      gchar *presence_status;
      gchar *presence_message;
      GHashTable *location;
      TpCapabilities *capabilities;
  } from_gobject;
  notify_ctx notify_ctx_alice, notify_ctx_chris;

  g_message (G_STRFUNC);

  for (i = 0; i < 3; i++)
    handles[i] = tp_handle_ensure (service_repo, ids[i], NULL, NULL);

  tp_tests_contacts_connection_change_aliases (service_conn, 3, handles,
      aliases);
  tp_tests_contacts_connection_change_presences (service_conn, 3, handles,
      statuses, messages);
  tp_tests_contacts_connection_change_avatar_tokens (service_conn, 3, handles,
      tokens);
  tp_tests_contacts_connection_change_locations (service_conn, 3, handles,
      locations);

  /* contact capabilities */
  capabilities = create_contact_caps (handles);
  tp_tests_contacts_connection_change_capabilities (service_conn,
      capabilities);
  g_hash_table_unref (capabilities);

  tp_connection_get_contacts_by_handle (client_conn,
      3, handles,
      sizeof (features) / sizeof (features[0]), features,
      by_handle_cb,
      &result, finish, NULL);

  g_main_loop_run (result.loop);

  MYASSERT (result.contacts->len == 3, ": %u", result.contacts->len);
  MYASSERT (result.invalid->len == 0, ": %u", result.invalid->len);
  g_assert_no_error (result.error);

  MYASSERT (g_ptr_array_index (result.contacts, 0) != NULL, "");
  MYASSERT (g_ptr_array_index (result.contacts, 1) != NULL, "");
  MYASSERT (g_ptr_array_index (result.contacts, 2) != NULL, "");

  for (i = 0; i < 3; i++)
    contacts[i] = g_object_ref (g_ptr_array_index (result.contacts, i));

  g_assert (result.error == NULL);
  reset_result (&result);

  for (i = 0; i < 3; i++)
    {
      TpCapabilities *caps;

      g_assert_cmpuint (tp_contact_get_handle (contacts[i]), ==, handles[i]);
      g_assert_cmpstr (tp_contact_get_identifier (contacts[i]), ==,
          ids[i]);

      MYASSERT (tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_ALIAS), "");
      g_assert_cmpstr (tp_contact_get_alias (contacts[i]), ==,
          aliases[i]);

      MYASSERT (tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_AVATAR_TOKEN), "");
      g_assert_cmpstr (tp_contact_get_avatar_token (contacts[i]), ==,
          tokens[i]);

      MYASSERT (tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_PRESENCE), "");
      g_assert_cmpstr (tp_contact_get_presence_message (contacts[i]), ==,
          messages[i]);

      MYASSERT (tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_LOCATION), "");
      ASSERT_SAME_LOCATION (tp_contact_get_location (contacts[i]),
          locations[i]);

      MYASSERT (tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_CAPABILITIES), "");

      caps = tp_contact_get_capabilities (contacts[i]);
      MYASSERT (caps != NULL, "");
      MYASSERT (tp_capabilities_is_specific_to_contact (caps), "");
      MYASSERT (tp_capabilities_supports_text_chats (caps) ==
          support_text_chats[i], " contact %u", i);
      MYASSERT (tp_capabilities_supports_text_chatrooms (caps) ==
          support_text_chatrooms[i], " contact %u", i);
    }

  g_assert_cmpuint (tp_contact_get_presence_type (contacts[0]), ==,
      TP_CONNECTION_PRESENCE_TYPE_AVAILABLE);
  g_assert_cmpstr (tp_contact_get_presence_status (contacts[0]), ==,
      "available");
  g_assert_cmpuint (tp_contact_get_presence_type (contacts[1]), ==,

      TP_CONNECTION_PRESENCE_TYPE_BUSY);
  g_assert_cmpstr (tp_contact_get_presence_status (contacts[1]), ==,
      "busy");
  g_assert_cmpuint (tp_contact_get_presence_type (contacts[2]), ==,
      TP_CONNECTION_PRESENCE_TYPE_AWAY);
  g_assert_cmpstr (tp_contact_get_presence_status (contacts[2]), ==,
      "away");

  /* exercise GObject properties in a basic way */
  g_object_get (contacts[0],
      "connection", &from_gobject.connection,
      "handle", &from_gobject.handle,
      "identifier", &from_gobject.identifier,
      "alias", &from_gobject.alias,
      "avatar-token", &from_gobject.avatar_token,
      "presence-type", &from_gobject.presence_type,
      "presence-status", &from_gobject.presence_status,
      "presence-message", &from_gobject.presence_message,
      "location", &from_gobject.location,
      "capabilities", &from_gobject.capabilities,
      NULL);
  MYASSERT (from_gobject.connection == client_conn, "");
  g_assert_cmpuint (from_gobject.handle, ==, handles[0]);
  g_assert_cmpstr (from_gobject.identifier, ==, "alice");
  g_assert_cmpstr (from_gobject.alias, ==, "Alice in Wonderland");
  g_assert_cmpstr (from_gobject.avatar_token, ==, "aaaaa");
  g_assert_cmpuint (from_gobject.presence_type, ==,
      TP_CONNECTION_PRESENCE_TYPE_AVAILABLE);
  g_assert_cmpstr (from_gobject.presence_status, ==, "available");
  g_assert_cmpstr (from_gobject.presence_message, ==, "");
  ASSERT_SAME_LOCATION (from_gobject.location, locations[0]);
  MYASSERT (tp_capabilities_is_specific_to_contact (from_gobject.capabilities),
      "");
  MYASSERT (tp_capabilities_supports_text_chats (from_gobject.capabilities)
      == support_text_chats[0], "");
  MYASSERT (tp_capabilities_supports_text_chatrooms (from_gobject.capabilities)
      == support_text_chatrooms[0], "");
  g_object_unref (from_gobject.connection);
  g_free (from_gobject.identifier);
  g_free (from_gobject.alias);
  g_free (from_gobject.avatar_token);
  g_free (from_gobject.presence_status);
  g_free (from_gobject.presence_message);
  g_hash_table_unref (from_gobject.location);
  g_object_unref (from_gobject.capabilities);

  notify_ctx_init (&notify_ctx_alice);
  g_signal_connect (contacts[0], "notify",
      G_CALLBACK (contact_notify_cb), &notify_ctx_alice);

  notify_ctx_init (&notify_ctx_chris);
  g_signal_connect (contacts[2], "notify",
      G_CALLBACK (contact_notify_cb), &notify_ctx_chris);

  /* Change Alice and Bob's contact info, leave Chris as-is */
  tp_tests_contacts_connection_change_aliases (service_conn, 2, handles,
      new_aliases);
  tp_tests_contacts_connection_change_presences (service_conn, 2, handles,
      new_statuses, new_messages);
  tp_tests_contacts_connection_change_avatar_tokens (service_conn, 2, handles,
      new_tokens);
  tp_tests_contacts_connection_change_locations (service_conn, 2, handles,
      new_locations);

  new_capabilities = create_new_contact_caps (handles);
  tp_tests_contacts_connection_change_capabilities (service_conn,
      new_capabilities);
  g_hash_table_unref (new_capabilities);

  tp_tests_proxy_run_until_dbus_queue_processed (client_conn);

  g_assert (notify_ctx_is_fully_changed (&notify_ctx_alice));
  g_assert (!notify_ctx_is_changed (&notify_ctx_chris));

  for (i = 0; i < 2; i++)
    {
      TpCapabilities *caps;

      g_assert_cmpuint (tp_contact_get_handle (contacts[i]), ==, handles[i]);
      g_assert_cmpstr (tp_contact_get_identifier (contacts[i]), ==,
          ids[i]);

      MYASSERT (tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_ALIAS), "");
      g_assert_cmpstr (tp_contact_get_alias (contacts[i]), ==,
          new_aliases[i]);

      MYASSERT (tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_AVATAR_TOKEN), "");
      g_assert_cmpstr (tp_contact_get_avatar_token (contacts[i]), ==,
          new_tokens[i]);

      MYASSERT (tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_PRESENCE), "");
      g_assert_cmpstr (tp_contact_get_presence_message (contacts[i]), ==,
          new_messages[i]);

      MYASSERT (tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_LOCATION), "");
      ASSERT_SAME_LOCATION (tp_contact_get_location (contacts[i]),
          new_locations[i]);

      caps = tp_contact_get_capabilities (contacts[i]);
      MYASSERT (caps != NULL, "");
      MYASSERT (tp_capabilities_is_specific_to_contact (caps), "");
      MYASSERT (tp_capabilities_supports_text_chats (caps) ==
          new_support_text_chats[i], " contact %u", i);
      MYASSERT (tp_capabilities_supports_text_chatrooms (caps) ==
          new_support_text_chatrooms[i], " contact %u", i);
    }

  g_assert_cmpuint (tp_contact_get_presence_type (contacts[0]), ==,
      TP_CONNECTION_PRESENCE_TYPE_AWAY);
  g_assert_cmpstr (tp_contact_get_presence_status (contacts[0]), ==,
      "away");
  g_assert_cmpuint (tp_contact_get_presence_type (contacts[1]), ==,
      TP_CONNECTION_PRESENCE_TYPE_AVAILABLE);
  g_assert_cmpstr (tp_contact_get_presence_status (contacts[1]), ==,
      "available");

  for (i = 0; i < 3; i++)
    {
      g_object_unref (contacts[i]);
      tp_tests_proxy_run_until_dbus_queue_processed (client_conn);
      tp_handle_unref (service_repo, handles[i]);
      MYASSERT (!tp_handle_is_valid (service_repo, handles[i], NULL), "");
    }

  /* remaining cleanup */
  g_main_loop_unref (result.loop);
  g_hash_table_unref (location_1);
  g_hash_table_unref (location_2);
  g_hash_table_unref (location_3);
  g_hash_table_unref (location_4);
  g_hash_table_unref (location_5);
}

static void
by_id_cb (TpConnection *connection,
          guint n_contacts,
          TpContact * const *contacts,
          const gchar * const *good_ids,
          GHashTable *bad_ids,
          const GError *error,
          gpointer user_data,
          GObject *weak_object)
{
  Result *result = user_data;

  g_assert (result->invalid == NULL);
  g_assert (result->contacts == NULL);
  g_assert (result->error == NULL);
  g_assert (result->good_ids == NULL);
  g_assert (result->bad_ids == NULL);

  if (error == NULL)
    {
      GHashTableIter iter;
      gpointer key, value;
      guint i;

      DEBUG ("got %u contacts and %u bad IDs", n_contacts,
          g_hash_table_size (bad_ids));

      result->bad_ids = g_hash_table_new_full (g_str_hash, g_str_equal,
          g_free, (GDestroyNotify) g_error_free);
      tp_g_hash_table_update (result->bad_ids, bad_ids,
          (GBoxedCopyFunc) g_strdup, (GBoxedCopyFunc) g_error_copy);

      g_hash_table_iter_init (&iter, result->bad_ids);

      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          gchar *id = key;
          GError *e = value;

          DEBUG ("bad ID %s: %s %u: %s", id, g_quark_to_string (e->domain),
              e->code, e->message);
        }

      result->good_ids = g_strdupv ((GStrv) good_ids);

      result->contacts = g_ptr_array_sized_new (n_contacts);

      for (i = 0; i < n_contacts; i++)
        {
          TpContact *contact = contacts[i];

          DEBUG ("contact #%u: %p", i, contact);
          DEBUG ("contact #%u we asked for ID %s", i, good_ids[i]);
          DEBUG ("contact #%u we got ID %s", i,
              tp_contact_get_identifier (contact));
          DEBUG ("contact #%u alias: %s", i, tp_contact_get_alias (contact));
          DEBUG ("contact #%u avatar token: %s", i,
              tp_contact_get_avatar_token (contact));
          DEBUG ("contact #%u presence type: %u", i,
              tp_contact_get_presence_type (contact));
          DEBUG ("contact #%u presence status: %s", i,
              tp_contact_get_presence_status (contact));
          DEBUG ("contact #%u presence message: %s", i,
              tp_contact_get_presence_message (contact));
          g_ptr_array_add (result->contacts, g_object_ref (contact));
        }
    }
  else
    {
      DEBUG ("got an error: %s %u: %s", g_quark_to_string (error->domain),
          error->code, error->message);
      result->error = g_error_copy (error);
    }
}

static void
test_by_id (TpConnection *client_conn)
{
  Result result = { g_main_loop_new (NULL, FALSE) };
  static const gchar * const ids[] = { "Alice", "Bob", "Not valid", "Chris",
      "not valid either", NULL };
  TpContact *contacts[3];
  GError *e /* no initialization needed */;

  g_message ("%s: all bad (fd.o #19688)", G_STRFUNC);

  tp_connection_get_contacts_by_id (client_conn,
      1, ids + 2,
      0, NULL,
      by_id_cb,
      &result, finish, NULL);

  g_main_loop_run (result.loop);

  MYASSERT (result.contacts->len == 0, ": %u", result.contacts->len);
  MYASSERT (g_hash_table_size (result.bad_ids) == 1, ": %u",
      g_hash_table_size (result.bad_ids));
  g_assert_no_error (result.error);

  e = g_hash_table_lookup (result.bad_ids, "Not valid");
  MYASSERT (e != NULL, "");

  reset_result (&result);

  g_message ("%s: all good", G_STRFUNC);

  tp_connection_get_contacts_by_id (client_conn,
      2, ids,
      0, NULL,
      by_id_cb,
      &result, finish, NULL);

  g_main_loop_run (result.loop);

  MYASSERT (result.contacts->len == 2, ": %u", result.contacts->len);
  MYASSERT (g_hash_table_size (result.bad_ids) == 0, ": %u",
      g_hash_table_size (result.bad_ids));
  g_assert_no_error (result.error);

  MYASSERT (g_ptr_array_index (result.contacts, 0) != NULL, "");
  MYASSERT (g_ptr_array_index (result.contacts, 1) != NULL, "");
  contacts[0] = g_ptr_array_index (result.contacts, 0);
  g_assert_cmpstr (result.good_ids[0], ==, "Alice");
  g_assert_cmpstr (tp_contact_get_identifier (contacts[0]), ==, "alice");
  contacts[1] = g_ptr_array_index (result.contacts, 1);
  g_assert_cmpstr (result.good_ids[1], ==, "Bob");
  g_assert_cmpstr (tp_contact_get_identifier (contacts[1]), ==, "bob");

  reset_result (&result);

  g_message ("%s: not all good", G_STRFUNC);

  tp_connection_get_contacts_by_id (client_conn,
      5, ids,
      0, NULL,
      by_id_cb,
      &result, finish, NULL);

  g_main_loop_run (result.loop);

  MYASSERT (result.contacts->len == 3, ": %u", result.contacts->len);
  MYASSERT (g_hash_table_size (result.bad_ids) == 2, ": %u",
      g_hash_table_size (result.bad_ids));
  g_assert_no_error (result.error);

  e = g_hash_table_lookup (result.bad_ids, "Not valid");
  MYASSERT (e != NULL, "");

  e = g_hash_table_lookup (result.bad_ids, "not valid either");
  MYASSERT (e != NULL, "");

  MYASSERT (g_ptr_array_index (result.contacts, 0) != NULL, "");
  MYASSERT (g_ptr_array_index (result.contacts, 1) != NULL, "");
  MYASSERT (g_ptr_array_index (result.contacts, 2) != NULL, "");
  contacts[0] = g_ptr_array_index (result.contacts, 0);
  g_assert_cmpstr (result.good_ids[0], ==, "Alice");
  g_assert_cmpstr (tp_contact_get_identifier (contacts[0]), ==, "alice");
  contacts[1] = g_ptr_array_index (result.contacts, 1);
  g_assert_cmpstr (result.good_ids[1], ==, "Bob");
  g_assert_cmpstr (tp_contact_get_identifier (contacts[1]), ==, "bob");
  contacts[2] = g_ptr_array_index (result.contacts, 2);
  g_assert_cmpstr (result.good_ids[2], ==, "Chris");
  g_assert_cmpstr (tp_contact_get_identifier (contacts[2]), ==, "chris");

  /* wait for ReleaseHandles to run */
  tp_tests_proxy_run_until_dbus_queue_processed (client_conn);

  /* remaining cleanup */
  reset_result (&result);
  g_main_loop_unref (result.loop);
}

static void
test_capabilities_without_contact_caps (
    TpTestsContactsConnection *service_conn,
    TpConnection *client_conn)
{
  Result result = { g_main_loop_new (NULL, FALSE), NULL, NULL, NULL };
  TpHandle handles[] = { 0, 0, 0 };
  static const gchar * const ids[] = { "alice", "bob", "chris" };
  TpHandleRepoIface *service_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) service_conn, TP_HANDLE_TYPE_CONTACT);
  TpContact *contacts[3];
  guint i;
  TpContactFeature features[] = { TP_CONTACT_FEATURE_CAPABILITIES };

  g_message (G_STRFUNC);

  for (i = 0; i < 3; i++)
    handles[i] = tp_handle_ensure (service_repo, ids[i], NULL, NULL);

  tp_connection_get_contacts_by_handle (client_conn,
      3, handles,
      sizeof (features) / sizeof (features[0]), features,
      by_handle_cb,
      &result, finish, NULL);

  g_main_loop_run (result.loop);

  MYASSERT (result.contacts->len == 3, ": %u", result.contacts->len);
  MYASSERT (result.invalid->len == 0, ": %u", result.invalid->len);
  g_assert_no_error (result.error);

  MYASSERT (g_ptr_array_index (result.contacts, 0) != NULL, "");
  MYASSERT (g_ptr_array_index (result.contacts, 1) != NULL, "");
  MYASSERT (g_ptr_array_index (result.contacts, 2) != NULL, "");

  for (i = 0; i < 3; i++)
    contacts[i] = g_ptr_array_index (result.contacts, i);

  for (i = 0; i < 3; i++)
    {
      TpCapabilities *caps;

      g_assert_cmpuint (tp_contact_get_handle (contacts[i]), ==, handles[i]);
      g_assert_cmpstr (tp_contact_get_identifier (contacts[i]), ==,
          ids[i]);

      MYASSERT (tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_CAPABILITIES), "");

      caps = tp_contact_get_capabilities (contacts[i]);
      MYASSERT (caps != NULL, "");
      MYASSERT (!tp_capabilities_is_specific_to_contact (caps), "");
      MYASSERT (!tp_capabilities_supports_text_chats (caps), " contact %u", i);
      MYASSERT (!tp_capabilities_supports_text_chatrooms (caps),
          " contact %u", i);
    }

  g_assert (result.error == NULL);
  reset_result (&result);
  g_main_loop_unref (result.loop);
}

static void
test_prepare_contact_caps_without_request (
    TpTestsContactsConnection *service_conn,
    TpConnection *client_conn)
{
  Result result = { g_main_loop_new (NULL, FALSE), NULL, NULL, NULL };
  TpHandle handles[] = { 0, 0, 0 };
  static const gchar * const ids[] = { "alice", "bob", "chris" };
  TpHandleRepoIface *service_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) service_conn, TP_HANDLE_TYPE_CONTACT);
  TpContact *contacts[3];
  guint i;
  TpContactFeature features[] = { TP_CONTACT_FEATURE_CAPABILITIES };

  g_message (G_STRFUNC);

  for (i = 0; i < 3; i++)
    handles[i] = tp_handle_ensure (service_repo, ids[i], NULL, NULL);

  tp_connection_get_contacts_by_handle (client_conn,
      3, handles,
      sizeof (features) / sizeof (features[0]), features,
      by_handle_cb,
      &result, finish, NULL);

  g_main_loop_run (result.loop);

  MYASSERT (result.contacts->len == 3, ": %u", result.contacts->len);
  MYASSERT (result.invalid->len == 0, ": %u", result.invalid->len);
  g_assert_no_error (result.error);

  MYASSERT (g_ptr_array_index (result.contacts, 0) != NULL, "");
  MYASSERT (g_ptr_array_index (result.contacts, 1) != NULL, "");
  MYASSERT (g_ptr_array_index (result.contacts, 2) != NULL, "");

  for (i = 0; i < 3; i++)
    contacts[i] = g_ptr_array_index (result.contacts, i);

  for (i = 0; i < 3; i++)
    {
      TpCapabilities *caps;
      GPtrArray *classes;

      g_assert_cmpuint (tp_contact_get_handle (contacts[i]), ==, handles[i]);
      g_assert_cmpstr (tp_contact_get_identifier (contacts[i]), ==,
          ids[i]);

      MYASSERT (tp_contact_has_feature (contacts[i],
            TP_CONTACT_FEATURE_CAPABILITIES), "");

      caps = tp_contact_get_capabilities (contacts[i]);
      MYASSERT (caps != NULL, "");
      MYASSERT (!tp_capabilities_is_specific_to_contact (caps), "");
      classes = tp_capabilities_get_channel_classes (caps);
      g_assert_cmpuint (classes->len, ==, 0);
    }

  g_assert (result.error == NULL);
  reset_result (&result);
  g_main_loop_unref (result.loop);
}

static void
setup (Fixture *f,
    gconstpointer unused G_GNUC_UNUSED)
{
  tp_tests_create_and_connect_conn (TP_TESTS_TYPE_CONTACTS_CONNECTION,
      "me@test.com", &f->base_connection, &f->client_conn);

  f->service_conn = TP_TESTS_CONTACTS_CONNECTION (f->base_connection);
  g_object_ref (f->service_conn);

  tp_tests_create_and_connect_conn (TP_TESTS_TYPE_LEGACY_CONTACTS_CONNECTION,
      "me2@test.com", &f->legacy_base_connection, &f->legacy_client_conn);

  tp_tests_create_and_connect_conn (TP_TESTS_TYPE_NO_REQUESTS_CONNECTION,
      "me3@test.com", &f->no_requests_base_connection,
      &f->no_requests_client_conn);
}

static void
teardown (Fixture *f,
    gconstpointer unused G_GNUC_UNUSED)
{
  gboolean ok;
  GError *error = NULL;

  if (f->client_conn != NULL)
    {
      ok = tp_cli_connection_run_disconnect (f->client_conn, -1, &error, NULL);
      g_assert_no_error (error);
      g_assert (ok);
    }

  tp_clear_object (&f->client_conn);
  tp_clear_object (&f->service_conn);
  tp_clear_object (&f->base_connection);

  if (f->legacy_client_conn != NULL)
    {
      ok = tp_cli_connection_run_disconnect (f->legacy_client_conn, -1, &error,
          NULL);
      g_assert_no_error (error);
      g_assert (ok);
    }

  tp_clear_object (&f->legacy_client_conn);
  tp_clear_object (&f->legacy_base_connection);

  if (f->no_requests_client_conn != NULL)
    {
      ok = tp_cli_connection_run_disconnect (f->no_requests_client_conn, -1,
            &error, NULL);
      g_assert_no_error (error);
      g_assert (ok);
    }

  tp_clear_object (&f->no_requests_client_conn);
  tp_clear_object (&f->no_requests_base_connection);
}

int
main (int argc,
      char **argv)
{
  Fixture f = { NULL };

  /* Setup */

  g_type_init ();
  tp_debug_set_flags ("all");

  setup (&f, NULL);
  test_by_handle (f.service_conn, f.client_conn);
  teardown (&f, NULL);

  setup (&f, NULL);
  test_no_features (f.service_conn, f.client_conn);
  teardown (&f, NULL);

  setup (&f, NULL);
  test_features (f.service_conn, f.client_conn);
  teardown (&f, NULL);

  setup (&f, NULL);
  test_upgrade (f.service_conn, f.client_conn);
  teardown (&f, NULL);

  setup (&f, NULL);
  test_by_id (f.client_conn);
  teardown (&f, NULL);

  setup (&f, NULL);
  test_avatar_requirements (f.client_conn);
  teardown (&f, NULL);

  setup (&f, NULL);
  test_avatar_data (f.service_conn, f.client_conn);
  teardown (&f, NULL);

  setup (&f, NULL);
  test_contact_info (f.service_conn, f.client_conn);
  teardown (&f, NULL);

  /* test if TpContact fallbacks to connection's capabilities if
   * ContactCapabilities is not implemented. */
  setup (&f, NULL);
  test_capabilities_without_contact_caps (
      TP_TESTS_CONTACTS_CONNECTION (f.legacy_base_connection),
      f.legacy_client_conn);
  teardown (&f, NULL);

  /* test if TP_CONTACT_FEATURE_CAPABILITIES is prepared but with
   * an empty set of capabilities if the connection doesn't support
   * ContactCapabilities and Requests. */
  setup (&f, NULL);
  test_prepare_contact_caps_without_request (
      TP_TESTS_CONTACTS_CONNECTION (f.no_requests_base_connection),
      f.no_requests_client_conn);
  teardown (&f, NULL);

  return 0;
}
