/*
 * conn.c - an example connection
 *
 * Copyright (C) 2007 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2007 Nokia Corporation
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */

#include "config.h"

#include "echo-conn.h"

#include <dbus/dbus-glib.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/handle-repo-dynamic.h>

#include "echo-im-manager.h"

G_DEFINE_TYPE (TpTestsEchoConnection,
    tp_tests_echo_connection,
    TP_TYPE_BASE_CONNECTION)

/* type definition stuff */

enum
{
  PROP_ACCOUNT = 1,
  N_PROPS
};

struct _TpTestsEchoConnectionPrivate
{
  gchar *account;
};

static void
tp_tests_echo_connection_init (TpTestsEchoConnection *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, TP_TESTS_TYPE_ECHO_CONNECTION,
      TpTestsEchoConnectionPrivate);
}

static void
get_property (GObject *object,
              guint property_id,
              GValue *value,
              GParamSpec *spec)
{
  TpTestsEchoConnection *self = TP_TESTS_ECHO_CONNECTION (object);

  switch (property_id) {
    case PROP_ACCOUNT:
      g_value_set_string (value, self->priv->account);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, spec);
  }
}

static void
set_property (GObject *object,
              guint property_id,
              const GValue *value,
              GParamSpec *spec)
{
  TpTestsEchoConnection *self = TP_TESTS_ECHO_CONNECTION (object);

  switch (property_id) {
    case PROP_ACCOUNT:
      g_free (self->priv->account);
      self->priv->account = g_utf8_strdown (g_value_get_string (value), -1);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, spec);
  }
}

static void
finalize (GObject *object)
{
  TpTestsEchoConnection *self = TP_TESTS_ECHO_CONNECTION (object);

  g_free (self->priv->account);

  G_OBJECT_CLASS (tp_tests_echo_connection_parent_class)->finalize (object);
}

static gchar *
get_unique_connection_name (TpBaseConnection *conn)
{
  TpTestsEchoConnection *self = TP_TESTS_ECHO_CONNECTION (conn);

  return g_strdup (self->priv->account);
}

/* Returns the same id given in but in lowercase. If '#' is present,
 * the normalized contact will be the lhs of it. For example:
 *
 * LOL -> lol
 * Lol#foo -> lol
 */
static gchar *
tp_tests_echo_normalize_contact (TpHandleRepoIface *repo,
                           const gchar *id,
                           gpointer context,
                           GError **error)
{
  gchar *hash;

  if (id[0] == '\0')
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_HANDLE,
          "ID must not be empty");
      return NULL;
    }

  hash = g_utf8_strchr (id, -1, '#');

  return g_utf8_strdown (id, hash != NULL ? (hash - id) : -1);
}

static void
create_handle_repos (TpBaseConnection *conn,
                     TpHandleRepoIface *repos[TP_NUM_HANDLE_TYPES])
{
  repos[TP_HANDLE_TYPE_CONTACT] = tp_dynamic_handle_repo_new
      (TP_HANDLE_TYPE_CONTACT, tp_tests_echo_normalize_contact, NULL);
}

static GPtrArray *
create_channel_managers (TpBaseConnection *conn)
{
  GPtrArray *ret = g_ptr_array_sized_new (1);

  g_ptr_array_add (ret, g_object_new (TP_TESTS_TYPE_ECHO_IM_MANAGER,
        "connection", conn,
        NULL));

  return ret;
}

static gboolean
start_connecting (TpBaseConnection *conn,
                  GError **error)
{
  TpTestsEchoConnection *self = TP_TESTS_ECHO_CONNECTION (conn);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
  TpHandle self_handle;

  /* In a real connection manager we'd ask the underlying implementation to
   * start connecting, then go to state CONNECTED when finished, but here
   * we can do it immediately. */

  self_handle = tp_handle_ensure (contact_repo, self->priv->account,
      NULL, NULL);

  tp_base_connection_set_self_handle (conn, self_handle);
  tp_base_connection_change_status (conn, TP_CONNECTION_STATUS_CONNECTED,
      TP_CONNECTION_STATUS_REASON_REQUESTED);

  return TRUE;
}

static void
shut_down (TpBaseConnection *conn)
{
  /* In a real connection manager we'd ask the underlying implementation to
   * start shutting down, then call this function when finished, but here
   * we can do it immediately. */
  tp_base_connection_finish_shutdown (conn);
}

static GPtrArray *
get_interfaces_always_present (TpBaseConnection *base)
{
  GPtrArray *interfaces;

  interfaces = TP_BASE_CONNECTION_CLASS (
      tp_tests_echo_connection_parent_class)->get_interfaces_always_present (base);

  g_ptr_array_add (interfaces, TP_IFACE_CONNECTION_INTERFACE_REQUESTS);

  return interfaces;
}

static void
tp_tests_echo_connection_class_init (TpTestsEchoConnectionClass *klass)
{
  TpBaseConnectionClass *base_class =
      (TpBaseConnectionClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *param_spec;

  object_class->get_property = get_property;
  object_class->set_property = set_property;
  object_class->finalize = finalize;
  g_type_class_add_private (klass, sizeof (TpTestsEchoConnectionPrivate));

  base_class->create_handle_repos = create_handle_repos;
  base_class->get_unique_connection_name = get_unique_connection_name;
  base_class->create_channel_managers = create_channel_managers;
  base_class->start_connecting = start_connecting;
  base_class->shut_down = shut_down;
  base_class->get_interfaces_always_present = get_interfaces_always_present;

  param_spec = g_param_spec_string ("account", "Account name",
      "The username of this user", NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ACCOUNT, param_spec);
}
