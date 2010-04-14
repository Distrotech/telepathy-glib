/*
 * simple-conn.c - a simple connection
 *
 * Copyright (C) 2007-2008 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2007-2008 Nokia Corporation
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */

#include "simple-conn.h"

#include <string.h>

#include <dbus/dbus-glib.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

G_DEFINE_TYPE_WITH_CODE (SimpleConnection,
    simple_connection,
    TP_TYPE_BASE_CONNECTION,
    G_STMT_START { } G_STMT_END)

/* type definition stuff */

enum
{
  PROP_ACCOUNT = 1,
  N_PROPS
};

struct _SimpleConnectionPrivate
{
  gchar *account;
  guint connect_source;
  guint disconnect_source;
};

static void
simple_connection_init (SimpleConnection *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, SIMPLE_TYPE_CONNECTION,
      SimpleConnectionPrivate);
}

static void
get_property (GObject *object,
              guint property_id,
              GValue *value,
              GParamSpec *spec)
{
  SimpleConnection *self = SIMPLE_CONNECTION (object);

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
  SimpleConnection *self = SIMPLE_CONNECTION (object);

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
  SimpleConnection *self = SIMPLE_CONNECTION (object);

  if (self->priv->connect_source != 0)
    {
      g_source_remove (self->priv->connect_source);
    }

  if (self->priv->disconnect_source != 0)
    {
      g_source_remove (self->priv->disconnect_source);
    }

  g_free (self->priv->account);

  G_OBJECT_CLASS (simple_connection_parent_class)->finalize (object);
}

static gchar *
get_unique_connection_name (TpBaseConnection *conn)
{
  SimpleConnection *self = SIMPLE_CONNECTION (conn);

  return g_strdup (self->priv->account);
}

static gchar *
simple_normalize_contact (TpHandleRepoIface *repo,
                           const gchar *id,
                           gpointer context,
                           GError **error)
{
  if (id[0] == '\0')
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_HANDLE,
          "ID must not be empty");
      return NULL;
    }

  if (strchr (id, ' ') != NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_HANDLE,
          "ID must not contain spaces");
      return NULL;
    }

  return g_utf8_strdown (id, -1);
}

static void
create_handle_repos (TpBaseConnection *conn,
                     TpHandleRepoIface *repos[NUM_TP_HANDLE_TYPES])
{
  repos[TP_HANDLE_TYPE_CONTACT] = tp_dynamic_handle_repo_new
      (TP_HANDLE_TYPE_CONTACT, simple_normalize_contact, NULL);
}

static GPtrArray *
create_channel_factories (TpBaseConnection *conn)
{
  return g_ptr_array_sized_new (0);
}

void
simple_connection_inject_disconnect (SimpleConnection *self)
{
  tp_base_connection_change_status ((TpBaseConnection *) self,
      TP_CONNECTION_STATUS_DISCONNECTED,
      TP_CONNECTION_STATUS_REASON_REQUESTED);
}

static gboolean
pretend_connected (gpointer data)
{
  SimpleConnection *self = SIMPLE_CONNECTION (data);
  TpBaseConnection *conn = (TpBaseConnection *) self;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  conn->self_handle = tp_handle_ensure (contact_repo, self->priv->account,
      NULL, NULL);

  if (conn->status == TP_CONNECTION_STATUS_CONNECTING)
    {
      tp_base_connection_change_status (conn, TP_CONNECTION_STATUS_CONNECTED,
          TP_CONNECTION_STATUS_REASON_REQUESTED);
    }

  self->priv->connect_source = 0;
  return FALSE;
}

static gboolean
start_connecting (TpBaseConnection *conn,
                  GError **error)
{
  SimpleConnection *self = SIMPLE_CONNECTION (conn);

  tp_base_connection_change_status (conn, TP_CONNECTION_STATUS_CONNECTING,
      TP_CONNECTION_STATUS_REASON_REQUESTED);

  /* In a real connection manager we'd ask the underlying implementation to
   * start connecting, then go to state CONNECTED when finished. Here there
   * isn't actually a connection, so we'll fake a connection process that
   * takes half a second. */
  self->priv->connect_source = g_timeout_add (500, pretend_connected, self);

  return TRUE;
}

static gboolean
pretend_disconnected (gpointer data)
{
  SimpleConnection *self = SIMPLE_CONNECTION (data);

  tp_base_connection_finish_shutdown (TP_BASE_CONNECTION (data));
  self->priv->disconnect_source = 0;
  return FALSE;
}

static void
shut_down (TpBaseConnection *conn)
{
  SimpleConnection *self = SIMPLE_CONNECTION (conn);

  /* In a real connection manager we'd ask the underlying implementation to
   * start shutting down, then call this function when finished. Here there
   * isn't actually a connection, so we'll fake a disconnection process that
   * takes half a second. */
  self->priv->disconnect_source = g_timeout_add (500, pretend_disconnected,
      conn);
}

static void
simple_connection_class_init (SimpleConnectionClass *klass)
{
  TpBaseConnectionClass *base_class =
      (TpBaseConnectionClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *param_spec;
  static const gchar *interfaces_always_present[] = {
      TP_IFACE_CONNECTION_INTERFACE_REQUESTS, NULL };

  object_class->get_property = get_property;
  object_class->set_property = set_property;
  object_class->finalize = finalize;
  g_type_class_add_private (klass, sizeof (SimpleConnectionPrivate));

  base_class->create_handle_repos = create_handle_repos;
  base_class->get_unique_connection_name = get_unique_connection_name;
  base_class->create_channel_factories = create_channel_factories;
  base_class->start_connecting = start_connecting;
  base_class->shut_down = shut_down;

  base_class->interfaces_always_present = interfaces_always_present;

  param_spec = g_param_spec_string ("account", "Account name",
      "The username of this user", NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ACCOUNT, param_spec);
}

void
simple_connection_set_identifier (SimpleConnection *self,
                                  const gchar *identifier)
{
  TpBaseConnection *conn = (TpBaseConnection *) self;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
  TpHandle handle = tp_handle_ensure (contact_repo, identifier, NULL, NULL);

  /* if this fails then the identifier was bad - caller error */
  g_return_if_fail (handle != 0);

  tp_base_connection_set_self_handle (conn, handle);
  tp_handle_unref (contact_repo, handle);
}
