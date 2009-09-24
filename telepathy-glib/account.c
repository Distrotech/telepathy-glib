/*
 * account.c - proxy for an account in the Telepathy account manager
 *
 * Copyright (C) 2009 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2009 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <string.h>

#include "telepathy-glib/account.h"

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/defs.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/proxy-subclass.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG TP_DEBUG_ACCOUNTS
#include "telepathy-glib/debug-internal.h"

#include "telepathy-glib/_gen/signals-marshal.h"
#include "telepathy-glib/_gen/tp-cli-account-body.h"

/**
 * SECTION:account
 * @title: TpAccount
 * @short_description: proxy object for an account in the Telepathy account
 *  manager
 * @see_also: #TpAccountManager
 *
 * The Telepathy Account Manager stores the user's configured real-time
 * communication accounts. The #TpAccount object represents a stored account.
 *
 * Since: 0.7.32
 */

/**
 * TpAccount:
 *
 * The Telepathy Account Manager stores the user's configured real-time
 * communication accounts. This object represents a stored account.
 *
 * If this account is deleted from the account manager, the
 * #TpProxy::invalidated signal will be emitted
 * with the domain %TP_DBUS_ERRORS and the error code
 * %TP_DBUS_ERROR_OBJECT_REMOVED.
 *
 * Since: 0.7.32
 */

/**
 * TpAccountClass:
 *
 * The class of a #TpAccount.
 */

struct _TpAccountPrivate {
  gboolean dispose_has_run;

  TpConnection *connection;
  guint connection_invalidated_id;

  TpConnectionStatus connection_status;
  TpConnectionStatusReason reason;

  TpConnectionPresenceType presence;
  gchar *status;
  gchar *message;

  TpConnectionPresenceType requested_presence;
  gchar *requested_status;
  gchar *requested_message;

  TpConnectionPresenceType default_presence;

  gboolean connect_automatically;
  gboolean has_been_online;

  gchar *nickname;

  gboolean enabled;
  gboolean valid;
  gboolean removed;
  /* Timestamp when the connection got connected in seconds since the epoch */
  glong connect_time;

  gchar *cm_name;
  gchar *proto_name;
  gchar *icon_name;

  gchar *display_name;

  GHashTable *parameters;

  /* Features. */
  GList *features;
  GList *callbacks;
  GArray *requested_features;
  GArray *actual_features;
  GArray *missing_features;
};

typedef struct {
  GQuark name;
  gboolean ready;
} TpAccountFeature;

typedef struct {
  GSimpleAsyncResult *result;
  GQuark *features;
} TpAccountFeatureCallback;

G_DEFINE_TYPE (TpAccount, tp_account, TP_TYPE_PROXY);

/* signals */
enum {
  STATUS_CHANGED,
  PRESENCE_CHANGED,
  REMOVED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

/* properties */
enum {
  PROP_ENABLED = 1,
  PROP_PRESENCE,
  PROP_STATUS,
  PROP_STATUS_MESSAGE,
  PROP_CONNECTION_STATUS,
  PROP_CONNECTION_STATUS_REASON,
  PROP_CONNECTION,
  PROP_DISPLAY_NAME,
  PROP_CONNECTION_MANAGER,
  PROP_PROTOCOL,
  PROP_ICON_NAME,
  PROP_CONNECT_AUTOMATICALLY,
  PROP_HAS_BEEN_ONLINE,
  PROP_VALID,
  PROP_REQUESTED_PRESENCE,
  PROP_REQUESTED_STATUS,
  PROP_REQUESTED_STATUS_MESSAGE,
  PROP_NICKNAME,
  PROP_DEFAULT_PRESENCE
};

/**
 * TP_ACCOUNT_FEATURE_CORE:
 *
 * Expands to a call to a function that returns a quark for the "core" feature
 * on a #TpAccount.
 *
 * Since: 0.7.UNRELEASED
 */

/**
 * tp_account_get_feature_quark_core:
 *
 * <!-- -->
 *
 * Returns: the quark used for representing the core feature of a
 *          #TpAccount
 *
 * Since: 0.7.UNRELEASED
 */
GQuark
tp_account_get_feature_quark_core (void)
{
  return g_quark_from_static_string ("tp-account-feature-core");
}

static const GQuark *
_tp_account_get_known_features (void)
{
  static GQuark features[1] = { 0 };

  if (G_UNLIKELY (features[0] == 0))
    {
      features[0] = TP_ACCOUNT_FEATURE_CORE;
    }

  return features;
}

static void
tp_account_init (TpAccount *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, TP_TYPE_ACCOUNT,
      TpAccountPrivate);

  self->priv->connection_status = TP_CONNECTION_STATUS_DISCONNECTED;
}

static TpAccountFeature *
_tp_account_get_feature (TpAccount *self,
    GQuark feature)
{
  TpAccountPrivate *priv = self->priv;
  GList *l;

  for (l = priv->features; l != NULL; l = l->next)
    {
      TpAccountFeature *f = l->data;

      if (f->name == feature)
        return f;
    }

  return NULL;
}

static gboolean
_tp_account_feature_in_array (GQuark feature,
    const GArray *array)
{
  const GQuark *c = (const GQuark *) array->data;

  for (; *c != 0; c++)
    {
      if (*c == feature)
        return TRUE;
    }

  return FALSE;
}

static void
_tp_account_update_feature_arrays (TpAccount *account,
    const GQuark *features)
{
  TpAccountPrivate *priv = account->priv;
  const GQuark *f;

  for (f = features; *f != 0; f++)
    {
      TpAccountFeature *feature;

      feature = _tp_account_get_feature (account, *f);

      if (feature == NULL
          && !_tp_account_feature_in_array (*f, priv->missing_features))
        g_array_append_val (priv->missing_features, feature);

      if (!_tp_account_feature_in_array (*f, priv->requested_features))
        g_array_append_val (priv->requested_features, *f);
    }
}

static gboolean
_tp_account_check_features (TpAccount *self,
    const GQuark *features)
{
  const GQuark *f;

  for (f = features; *f != 0; f++)
    {
      TpAccountFeature *feat;

      feat = _tp_account_get_feature (self, *f);

      /* features which are NULL (ie. don't exist) are always considered as
       * being ready, except in _is_ready when it doesn't make sense to
       * return TRUE. */
      if (feat != NULL && !feat->ready)
        return FALSE;
    }

  return TRUE;
}

static void
_tp_account_become_ready (TpAccount *self,
    GQuark feature)
{
  TpAccountPrivate *priv = self->priv;
  TpAccountFeature *f = NULL;
  GList *l, *remove = NULL;

  f = _tp_account_get_feature (self, feature);

  g_assert (f != NULL);

  if (f->ready)
    return;

  f->ready = TRUE;

  /* Possibly a useless check -- should never get this far with
   * this expression evaluating to false. */
  if (!_tp_account_feature_in_array (feature, priv->missing_features))
    g_array_append_val (priv->actual_features, feature);

  for (l = priv->callbacks; l != NULL; l = l->next)
    {
      TpAccountFeatureCallback *cb = l->data;

      if (_tp_account_check_features (self, cb->features))
        {
          remove = g_list_prepend (remove, l);
          g_simple_async_result_complete (cb->result);
          g_object_unref (cb->result);
        }
    }

  for (l = remove; l != NULL; l = l->next)
    priv->callbacks = g_list_delete_link (priv->callbacks, l->data);

  g_list_free (remove);
}

static void
_tp_account_removed_cb (TpAccount *self,
    gpointer unused G_GNUC_UNUSED,
    GObject *object G_GNUC_UNUSED)
{
  GError e = { TP_DBUS_ERRORS, TP_DBUS_ERROR_OBJECT_REMOVED,
               "Account removed" };

  if (self->priv->removed)
    return;

  self->priv->removed = TRUE;

  tp_proxy_invalidate ((TpProxy *) self, &e);

  g_signal_emit (self, signals[REMOVED], 0);
}

static gchar *
_tp_account_unescape_protocol (const gchar *protocol,
    gssize len)
{
  gchar *result, *escape;
  /* Bad implementation might accidentally use tp_escape_as_identifier,
   * which escapes - in the wrong way... */
  if ((escape = g_strstr_len (protocol, len, "_2d")) != NULL)
    {
      GString *str;
      const gchar *input;

      str = g_string_new ("");
      input = protocol;
      do {
        g_string_append_len (str, input, escape - input);
        g_string_append_c (str, '-');

        len -= escape - input + 3;
        input = escape + 3;
      } while ((escape = g_strstr_len (input, len, "_2d")) != NULL);

      g_string_append_len (str, input, len);

      result = g_string_free (str, FALSE);
    }
  else
    {
      result = g_strndup (protocol, len);
    }

  g_strdelimit (result, "_", '-');

  return result;
}

static gboolean
_tp_account_parse_object_path (const gchar *object_path,
    gchar **protocol,
    gchar **manager)
{
  const gchar *proto, *proto_end;
  const gchar *cm, *cm_end;

  g_return_val_if_fail (
      g_str_has_prefix (object_path, TP_ACCOUNT_OBJECT_PATH_BASE), FALSE);

  cm = object_path + strlen (TP_ACCOUNT_OBJECT_PATH_BASE);

  for (cm_end = cm; *cm_end != '/' && *cm_end != '\0'; cm_end++)
    /* pass */;

  if (*cm_end == '\0')
    return FALSE;

  if (cm_end == '\0')
    return FALSE;

  proto = cm_end + 1;

  for (proto_end = proto; *proto_end != '/' && *proto_end != '\0'; proto_end++)
    /* pass */;

  if (*proto_end == '\0')
    return FALSE;

  if (protocol != NULL)
    *protocol = _tp_account_unescape_protocol (proto, proto_end - proto);

  if (manager != NULL)
    *manager = g_strndup (cm, cm_end - cm);

  return TRUE;
}

static void
_tp_account_free_connection (TpAccount *account)
{
  TpAccountPrivate *priv = account->priv;
  TpConnection *conn;

  if (priv->connection == NULL)
    return;

  conn = priv->connection;
  priv->connection = NULL;

  if (priv->connection_invalidated_id != 0)
    g_signal_handler_disconnect (conn, priv->connection_invalidated_id);
  priv->connection_invalidated_id = 0;

  g_object_unref (conn);
}

static void
_tp_account_connection_invalidated_cb (TpProxy *self,
    guint domain,
    gint code,
    gchar *message,
    gpointer user_data)
{
  TpAccount *account = TP_ACCOUNT (user_data);
  TpAccountPrivate *priv = account->priv;

  if (priv->connection == NULL)
    return;

  DEBUG ("(%s) Connection invalidated",
      tp_account_get_unique_name (account));

  g_assert (priv->connection == TP_CONNECTION (self));

  _tp_account_free_connection (account);

  g_object_notify (G_OBJECT (account), "connection");
}

static void
_tp_account_connection_ready_cb (TpConnection *connection,
    const GError *error,
    gpointer user_data)
{
  TpAccount *account = TP_ACCOUNT (user_data);

  if (error != NULL)
    {
      DEBUG ("(%s) Connection failed to become ready: %s",
          tp_account_get_unique_name (account), error->message);
      _tp_account_free_connection (account);
    }
  else
    {
      DEBUG ("(%s) Connection ready",
          tp_account_get_unique_name (account));
      g_object_notify (G_OBJECT (account), "connection");
    }
}

static void
_tp_account_set_connection (TpAccount *account,
    const gchar *path)
{
  TpAccountPrivate *priv = account->priv;

  if (priv->connection != NULL)
    {
      const gchar *current;

      current = tp_proxy_get_object_path (priv->connection);
      if (!tp_strdiff (current, path))
        return;
    }

  _tp_account_free_connection (account);

  if (tp_strdiff ("/", path))
    {
      GError *error = NULL;
      priv->connection = tp_connection_new (tp_proxy_get_dbus_daemon (account),
          NULL, path, &error);

      if (priv->connection == NULL)
        {
          DEBUG ("Failed to create a new TpConnection: %s",
              error->message);
          g_error_free (error);
        }
      else
        {
          priv->connection_invalidated_id = g_signal_connect (priv->connection,
              "invalidated",
              G_CALLBACK (_tp_account_connection_invalidated_cb), account);

          DEBUG ("Readying connection for %s",
              tp_account_get_unique_name (account));
          /* notify a change in the connection property when it's ready */
          tp_connection_call_when_ready (priv->connection,
              _tp_account_connection_ready_cb, account);
        }
    }

  g_object_notify (G_OBJECT (account), "connection");
}

static void
_tp_account_update (TpAccount *account,
    GHashTable *properties)
{
  TpAccountPrivate *priv = account->priv;
  GValueArray *arr;
  TpConnectionStatus old_s = priv->connection_status;
  gboolean presence_changed = FALSE;

  if (g_hash_table_lookup (properties, "ConnectionStatus") != NULL)
    priv->connection_status =
      tp_asv_get_uint32 (properties, "ConnectionStatus", NULL);


  if (g_hash_table_lookup (properties, "ConnectionStatusReason") != NULL)
    priv->reason = tp_asv_get_int32 (properties,
        "ConnectionStatusReason", NULL);

  if (g_hash_table_lookup (properties, "CurrentPresence") != NULL)
    {
      presence_changed = TRUE;
      arr = tp_asv_get_boxed (properties, "CurrentPresence",
          TP_STRUCT_TYPE_SIMPLE_PRESENCE);
      priv->presence = g_value_get_uint (g_value_array_get_nth (arr, 0));

      g_free (priv->status);
      priv->status = g_value_dup_string (g_value_array_get_nth (arr, 1));

      g_free (priv->message);
      priv->message = g_value_dup_string (g_value_array_get_nth (arr, 2));
    }

  if (g_hash_table_lookup (properties, "RequestedPresence") != NULL)
    {
      arr = tp_asv_get_boxed (properties, "RequestedPresence",
          TP_STRUCT_TYPE_SIMPLE_PRESENCE);
      priv->requested_presence =
        g_value_get_uint (g_value_array_get_nth (arr, 0));

      g_free (priv->requested_status);
      priv->requested_status =
        g_value_dup_string (g_value_array_get_nth (arr, 1));

      g_free (priv->requested_message);
      priv->requested_message =
        g_value_dup_string (g_value_array_get_nth (arr, 2));
    }

  if (g_hash_table_lookup (properties, "DisplayName") != NULL)
    {
      gchar *old = priv->display_name;

      priv->display_name =
        g_strdup (tp_asv_get_string (properties, "DisplayName"));

      if (tp_strdiff (old, priv->display_name))
        g_object_notify (G_OBJECT (account), "display-name");

      g_free (old);
    }

  if (g_hash_table_lookup (properties, "Nickname") != NULL)
    {
      gchar *old = priv->nickname;

      priv->nickname = g_strdup (tp_asv_get_string (properties, "Nickname"));

      if (tp_strdiff (old, priv->nickname))
        g_object_notify (G_OBJECT (account), "nickname");

      g_free (old);
    }

  if (g_hash_table_lookup (properties, "Icon") != NULL)
    {
      const gchar *icon_name;
      gchar *old = priv->icon_name;

      icon_name = tp_asv_get_string (properties, "Icon");

      if (icon_name == NULL || icon_name[0] == '\0')
        priv->icon_name = g_strdup_printf ("im-%s", priv->proto_name);
      else
        priv->icon_name = g_strdup (icon_name);

      if (tp_strdiff (old, priv->icon_name))
        g_object_notify (G_OBJECT (account), "icon-name");

      g_free (old);
    }

  if (g_hash_table_lookup (properties, "Enabled") != NULL)
    {
      gboolean enabled = tp_asv_get_boolean (properties, "Enabled", NULL);
      if (priv->enabled != enabled)
        {
          priv->enabled = enabled;
          g_object_notify (G_OBJECT (account), "enabled");
        }
    }

  if (g_hash_table_lookup (properties, "Valid") != NULL)
    {
      gboolean old = priv->valid;

      priv->valid = tp_asv_get_boolean (properties, "Valid", NULL);

      if (old != priv->valid)
        g_object_notify (G_OBJECT (account), "valid");
    }

  if (g_hash_table_lookup (properties, "Parameters") != NULL)
    {
      GHashTable *parameters;

      parameters = tp_asv_get_boxed (properties, "Parameters",
          TP_HASH_TYPE_STRING_VARIANT_MAP);

      if (priv->parameters != NULL)
        g_hash_table_unref (priv->parameters);

      priv->parameters = g_boxed_copy (TP_HASH_TYPE_STRING_VARIANT_MAP,
          parameters);
    }

  _tp_account_become_ready (account, TP_ACCOUNT_FEATURE_CORE);

  if (priv->connection_status != old_s)
    {
      if (priv->connection_status == TP_CONNECTION_STATUS_CONNECTED)
        {
          GTimeVal val;
          g_get_current_time (&val);

          priv->connect_time = val.tv_sec;
        }

      g_signal_emit (account, signals[STATUS_CHANGED], 0,
          old_s, priv->connection_status, priv->reason);

      g_object_notify (G_OBJECT (account), "connection-status");
      g_object_notify (G_OBJECT (account), "connection-status-reason");
    }

  if (presence_changed)
    {
      g_signal_emit (account, signals[PRESENCE_CHANGED], 0,
          priv->presence, priv->status, priv->message);
      g_object_notify (G_OBJECT (account), "presence");
      g_object_notify (G_OBJECT (account), "status");
      g_object_notify (G_OBJECT (account), "status-message");
    }

  if (g_hash_table_lookup (properties, "Connection") != NULL)
    {
      const gchar *conn_path =
        tp_asv_get_object_path (properties, "Connection");

      _tp_account_set_connection (account, conn_path);
    }

  if (g_hash_table_lookup (properties, "ConnectAutomatically") != NULL)
    {
      gboolean old = priv->connect_automatically;

      priv->connect_automatically =
        tp_asv_get_boolean (properties, "ConnectAutomatically", NULL);

      if (old != priv->connect_automatically)
        g_object_notify (G_OBJECT (account), "connect-automatically");
    }

  if (g_hash_table_lookup (properties, "HasBeenOnline") != NULL)
    {
      gboolean old = priv->has_been_online;

      priv->has_been_online =
        tp_asv_get_boolean (properties, "HasBeenOnline", NULL);

      if (old != priv->has_been_online)
        g_object_notify (G_OBJECT (account), "has-been-online");
    }
}

static void
_tp_account_properties_changed (TpAccount *proxy,
    GHashTable *properties,
    gpointer user_data,
    GObject *weak_object)
{
  TpAccount *self = TP_ACCOUNT (weak_object);

  if (!tp_account_is_ready (self, TP_ACCOUNT_FEATURE_CORE))
    return;

  _tp_account_update (self, properties);
}

static void
_tp_account_constructed (GObject *object)
{
  TpAccount *self = TP_ACCOUNT (object);
  TpAccountPrivate *priv = self->priv;
  void (*chain_up) (GObject *) =
    ((GObjectClass *) tp_account_parent_class)->constructed;
  GError *error = NULL;
  TpProxySignalConnection *sc;
  guint i;
  const GQuark *known_features;

  if (chain_up != NULL)
    chain_up (object);

  g_return_if_fail (tp_proxy_get_dbus_daemon (self) != NULL);

  priv->features = NULL;
  priv->callbacks = NULL;
  priv->requested_features = g_array_new (TRUE, FALSE, sizeof (GQuark));
  priv->actual_features = g_array_new (TRUE, FALSE, sizeof (GQuark));
  priv->missing_features = g_array_new (TRUE, FALSE, sizeof (GQuark));

  known_features = _tp_account_get_known_features ();

  /* Fill features list */
  for (i = 0; i < G_N_ELEMENTS (known_features); i++)
    {
      TpAccountFeature *feature;
      feature = g_slice_new0 (TpAccountFeature);
      feature->name = known_features[i];
      feature->ready = FALSE;
      priv->features = g_list_prepend (priv->features, feature);
    }

  sc = tp_cli_account_connect_to_removed (self, _tp_account_removed_cb,
      NULL, NULL, NULL, &error);

  if (sc == NULL)
    {
      g_critical ("Couldn't connect to Removed: %s", error->message);
      g_error_free (error);
    }

  _tp_account_parse_object_path (tp_account_get_unique_name (self),
      &(priv->proto_name), &(priv->cm_name));

  priv->icon_name = g_strdup_printf ("im-%s", priv->proto_name);

  tp_cli_account_connect_to_account_property_changed (self,
      _tp_account_properties_changed, NULL, NULL, object, NULL);

  tp_account_refresh_properties (self);
}

static void
_tp_account_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  TpAccount *self = TP_ACCOUNT (object);

  switch (prop_id)
    {
    case PROP_ENABLED:
      tp_account_set_enabled_async (self,
          g_value_get_boolean (value), NULL, NULL);
      break;
    case PROP_DEFAULT_PRESENCE:
      self->priv->default_presence = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
_tp_account_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  TpAccount *self = TP_ACCOUNT (object);

  switch (prop_id)
    {
    case PROP_ENABLED:
      g_value_set_boolean (value, self->priv->enabled);
      break;
    case PROP_PRESENCE:
      g_value_set_uint (value, self->priv->presence);
      break;
    case PROP_STATUS:
      g_value_set_string (value, self->priv->status);
      break;
    case PROP_STATUS_MESSAGE:
      g_value_set_string (value, self->priv->message);
      break;
    case PROP_CONNECTION_STATUS:
      g_value_set_uint (value, self->priv->connection_status);
      break;
    case PROP_CONNECTION_STATUS_REASON:
      g_value_set_uint (value, self->priv->reason);
      break;
    case PROP_CONNECTION:
      g_value_set_object (value,
          tp_account_get_connection (self));
      break;
    case PROP_DISPLAY_NAME:
      g_value_set_string (value,
          tp_account_get_display_name (self));
      break;
    case PROP_CONNECTION_MANAGER:
      g_value_set_string (value, self->priv->cm_name);
      break;
    case PROP_PROTOCOL:
      g_value_set_string (value, self->priv->proto_name);
      break;
    case PROP_ICON_NAME:
      g_value_set_string (value, self->priv->icon_name);
      break;
    case PROP_CONNECT_AUTOMATICALLY:
      g_value_set_boolean (value, self->priv->connect_automatically);
      break;
    case PROP_HAS_BEEN_ONLINE:
      g_value_set_boolean (value, self->priv->has_been_online);
      break;
    case PROP_VALID:
      g_value_set_boolean (value, self->priv->valid);
      break;
    case PROP_REQUESTED_PRESENCE:
      g_value_set_uint (value, self->priv->requested_presence);
      break;
    case PROP_REQUESTED_STATUS:
      g_value_set_string (value, self->priv->requested_status);
      break;
    case PROP_REQUESTED_STATUS_MESSAGE:
      g_value_set_string (value, self->priv->requested_message);
      break;
    case PROP_NICKNAME:
      g_value_set_string (value, self->priv->nickname);
      break;
    case PROP_DEFAULT_PRESENCE:
      g_value_set_uint (value, self->priv->default_presence);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
_tp_account_dispose (GObject *object)
{
  TpAccount *self = TP_ACCOUNT (object);
  TpAccountPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  _tp_account_free_connection (self);

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (tp_account_parent_class)->dispose != NULL)
    G_OBJECT_CLASS (tp_account_parent_class)->dispose (object);
}

static void
_tp_account_feature_free (gpointer data,
    gpointer user_data)
{
  g_slice_free (TpAccountFeature, data);
}

static void
_tp_account_feature_callback_free (gpointer data,
    gpointer user_data)
{
  TpAccountFeatureCallback *cb = data;
  GError e = { TP_ERRORS, TP_ERROR_NO_ANSWER,
               "the TpAccount was disposed before the feature(s) became ready" };

  g_simple_async_result_set_from_error (cb->result, &e);
  g_simple_async_result_complete (cb->result);
  g_object_unref (cb->result);

  g_slice_free (TpAccountFeatureCallback, data);
}

static void
_tp_account_finalize (GObject *object)
{
  TpAccount *self = TP_ACCOUNT (object);
  TpAccountPrivate *priv = self->priv;

  g_free (priv->status);
  g_free (priv->message);
  g_free (priv->requested_status);
  g_free (priv->requested_message);

  g_free (priv->nickname);

  g_free (priv->cm_name);
  g_free (priv->proto_name);
  g_free (priv->icon_name);
  g_free (priv->display_name);

  g_list_foreach (priv->features, _tp_account_feature_free, NULL);
  g_list_free (priv->features);
  priv->features = NULL;

  g_list_foreach (priv->callbacks, _tp_account_feature_callback_free, NULL);
  g_list_free (priv->callbacks);
  priv->callbacks = NULL;

  g_array_free (priv->requested_features, TRUE);
  g_array_free (priv->actual_features, TRUE);
  g_array_free (priv->missing_features, TRUE);

  /* free any data held directly by the object here */
  if (G_OBJECT_CLASS (tp_account_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (tp_account_parent_class)->finalize (object);
}

static void
tp_account_class_init (TpAccountClass *klass)
{
  TpProxyClass *proxy_class = (TpProxyClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (TpAccountPrivate));

  object_class->constructed = _tp_account_constructed;
  object_class->get_property = _tp_account_get_property;
  object_class->set_property = _tp_account_set_property;
  object_class->dispose = _tp_account_dispose;
  object_class->finalize = _tp_account_finalize;

  /**
   * TpAccount:enabled:
   *
   * Whether this account is enabled or not.
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_ENABLED,
      g_param_spec_boolean ("enabled",
          "Enabled",
          "Whether this account is enabled or not",
          FALSE,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));

  /**
   * TpAccount:presence:
   *
   * The account connection's presence type.
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_PRESENCE,
      g_param_spec_uint ("presence",
          "Presence",
          "The account connections presence type",
          0,
          NUM_TP_CONNECTION_PRESENCE_TYPES,
          TP_CONNECTION_PRESENCE_TYPE_UNSET,
          G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  /**
   * TpAccount:status:
   *
   * The Status string of the account.
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_STATUS,
      g_param_spec_string ("status",
          "Status",
          "The Status string of the account",
          NULL,
          G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  /**
   * TpAccount: status-message:
   *
   * The status message message of the account.
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_STATUS_MESSAGE,
      g_param_spec_string ("status-message",
          "status-message",
          "The Status message string of the account",
          NULL,
          G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  /**
   * TpAccount:connection-status:
   *
   * The account's connection status type.
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_CONNECTION_STATUS,
      g_param_spec_uint ("connection-status",
          "ConnectionStatus",
          "The accounts connections status type",
          0,
          NUM_TP_CONNECTION_STATUSES,
          TP_CONNECTION_STATUS_DISCONNECTED,
          G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  /**
   * TpAccount:connection-status-reason:
   *
   * The account's connection status reason.
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_CONNECTION_STATUS_REASON,
      g_param_spec_uint ("connection-status-reason",
          "ConnectionStatusReason",
          "The account connections status reason",
          0,
          NUM_TP_CONNECTION_STATUS_REASONS,
          TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED,
          G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  /**
   * TpAccount:connection:
   *
   * The account's connection.
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_CONNECTION,
      g_param_spec_object ("connection",
          "Connection",
          "The accounts connection",
          TP_TYPE_CONNECTION,
          G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  /**
   * TpAccount:display-name:
   *
   * The account's display name.
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_DISPLAY_NAME,
      g_param_spec_string ("display-name",
          "DisplayName",
          "The accounts display name",
          NULL,
          G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  /**
   * TpAccount:connection-manager:
   *
   * The account's connection manager name.
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_CONNECTION_MANAGER,
      g_param_spec_string ("connection-manager",
          "Connection manager",
          "The account's connection manager name",
          NULL,
          G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  /**
   * TpAccount:protocol:
   *
   * The account's protocol name.
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_PROTOCOL,
      g_param_spec_string ("protocol",
          "Protocol",
          "The account's protocol name",
          NULL,
          G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  /**
   * TpAccount:icon-name:
   *
   * The account's icon name. To change this propery, use
   * tp_account_set_icon_name_async().
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_ICON_NAME,
      g_param_spec_string ("icon-name",
          "Icon",
          "The account's icon name",
          NULL,
          G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  /**
   * TpAccount:connect-automatically:
   *
   * Whether the account should connect automatically or not. To change this
   * property, use tp_account_set_connect_automatically_async().
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_CONNECT_AUTOMATICALLY,
      g_param_spec_boolean ("connect-automatically",
          "ConnectAutomatically",
          "Whether this account should connect automatically or not",
          FALSE,
          G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  /**
   * TpAccount:has-been-online:
   *
   * Whether this account has been online or not.
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_HAS_BEEN_ONLINE,
      g_param_spec_boolean ("has-been-online",
          "HasBeenOnline",
          "Whether this account has been online or not",
          FALSE,
          G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  /**
   * TpAccount:valid:
   *
   * Whether this account is valid.
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_VALID,
      g_param_spec_boolean ("valid",
          "Valid",
          "Whether this account is valid",
          FALSE,
          G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  /**
   * TpAccount:requested-presence:
   *
   * The account's requested presence type.
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_REQUESTED_PRESENCE,
      g_param_spec_uint ("requested-presence",
          "RequestedPresence",
          "The account's requested presence type",
          0,
          NUM_TP_CONNECTION_PRESENCE_TYPES,
          TP_CONNECTION_PRESENCE_TYPE_UNSET,
          G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  /**
   * TpAccount:requested-status:
   *
   * The requested Status string of the account.
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_REQUESTED_STATUS,
      g_param_spec_string ("requested-status",
          "RequestedStatus",
          "The account's requested status string",
          NULL,
          G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  /**
   * TpAccount:requested-status-message:
   *
   * The requested status message message of the account.
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_REQUESTED_STATUS_MESSAGE,
      g_param_spec_string ("requested-status-message",
          "RequestedStatusMessage",
          "The requested Status message string of the account",
          NULL,
          G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  /**
   * TpAccount:nickname
   *
   * The account's nickname.
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_NICKNAME,
      g_param_spec_string ("nickname",
          "Nickname",
          "The account's nickname",
          NULL,
          G_PARAM_STATIC_STRINGS | G_PARAM_READABLE));

  /**
   * TpAccount:default-presence:
   *
   * The default presence that should be set on the account when it becomes
   * enabled.
   *
   * Since: 0.7.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_CONNECTION_STATUS,
      g_param_spec_uint ("default-presence",
          "default presence",
          "the default presence that should be set on the account when it becomes enabled",
          0,
          NUM_TP_CONNECTION_PRESENCE_TYPES,
          TP_CONNECTION_PRESENCE_TYPE_AVAILABLE,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));

  /**
   * TpAccount::status-changed:
   * @account: the #TpAccount
   * @old_status: old connection status
   * @new_status: new connection status
   * @reason: the reason for the status change
   *
   * Emitted when the connection status on the account changes.
   *
   * Since: 0.7.UNRELEASED
   */
  signals[STATUS_CHANGED] = g_signal_new ("status-changed",
      G_TYPE_FROM_CLASS (object_class),
      G_SIGNAL_RUN_LAST,
      0, NULL, NULL,
      _tp_marshal_VOID__UINT_UINT_UINT,
      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);

  /**
   * TpAccount::presence-changed:
   * @account: the #TpAccount
   * @presence: the new presence
   * @status: the new presence status
   * @status_message: the new presence status message
   *
   * Emitted when the presence of the account changes.
   *
   * Since: 0.7.UNRELEASED
   */
  signals[PRESENCE_CHANGED] = g_signal_new ("presence-changed",
      G_TYPE_FROM_CLASS (object_class),
      G_SIGNAL_RUN_LAST,
      0, NULL, NULL,
      _tp_marshal_VOID__UINT_STRING_STRING,
      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);

  /**
   * TpAccount::removed:
   * @account: the #TpAccount
   *
   * Emitted when the account is removed.
   *
   * Since: 0.7.UNRELEASED
   */
  signals[REMOVED] = g_signal_new ("removed",
      G_TYPE_FROM_CLASS (object_class),
      G_SIGNAL_RUN_LAST,
      0, NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  proxy_class->interface = TP_IFACE_QUARK_ACCOUNT;
  tp_account_init_known_interfaces ();
}

/**
 * tp_account_init_known_interfaces:
 *
 * Ensure that the known interfaces for TpAccount have been set up.
 * This is done automatically when necessary, but for correct
 * overriding of library interfaces by local extensions, you should
 * call this function before calling
 * tp_proxy_or_subclass_hook_on_interface_add() with first argument
 * %TP_TYPE_ACCOUNT.
 *
 * Since: 0.7.32
 */
void
tp_account_init_known_interfaces (void)
{
  static gsize once = 0;

  if (g_once_init_enter (&once))
    {
      GType tp_type = TP_TYPE_ACCOUNT;

      tp_proxy_init_known_interfaces ();
      tp_proxy_or_subclass_hook_on_interface_add (tp_type,
          tp_cli_account_add_signals);
      tp_proxy_subclass_add_error_mapping (tp_type,
          TP_ERROR_PREFIX, TP_ERRORS, TP_TYPE_ERROR);

      g_once_init_leave (&once, 1);
    }
}

/**
 * tp_account_new:
 * @bus_daemon: Proxy for the D-Bus daemon
 * @object_path: The non-NULL object path of this account
 * @error: Used to raise an error if @object_path is not valid
 *
 * Convenience function to create a new account proxy.
 *
 * Returns: a new reference to an account proxy, or %NULL if @object_path is
 *    not valid
 */
TpAccount *
tp_account_new (TpDBusDaemon *bus_daemon,
    const gchar *object_path,
    GError **error)
{
  TpAccount *self;

  g_return_val_if_fail (bus_daemon != NULL, NULL);
  g_return_val_if_fail (object_path != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (!tp_account_parse_object_path (object_path, NULL, NULL, NULL, error))
    return NULL;

  self = TP_ACCOUNT (g_object_new (TP_TYPE_ACCOUNT,
          "dbus-daemon", bus_daemon,
          "dbus-connection", ((TpProxy *) bus_daemon)->dbus_connection,
          "bus-name", TP_ACCOUNT_MANAGER_BUS_NAME,
          "object-path", object_path,
          NULL));

  return self;
}

static gchar *
unescape_protocol (gchar *protocol)
{
  if (strstr (protocol, "_2d") != NULL)
    {
      /* Work around MC5 bug where it escapes with tp_escape_as_identifier
       * rather than doing it properly. MC5 saves the object path in your
       * config, so if you've ever used a buggy MC5, the path will be wrong
       * forever.
       */
      gchar **chunks = g_strsplit (protocol, "_2d", 0);
      gchar *new = g_strjoinv ("-", chunks);

      g_strfreev (chunks);
      g_free (protocol);
      protocol = new;
    }

  g_strdelimit (protocol, "_", '-');

  return protocol;
}

static void
_tp_account_got_all_cb (TpProxy *proxy,
    GHashTable *properties,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  TpAccount *self = TP_ACCOUNT (weak_object);

  DEBUG ("Got whole set of properties for %s",
      tp_account_get_unique_name (self));

  if (error != NULL)
    {
      DEBUG ("Failed to get the initial set of account properties: %s",
          error->message);
      return;
    }

  _tp_account_update (self, properties);
}

/**
 * tp_account_is_just_connected:
 * @account: a #TpAccount
 *
 * Returns whether @account has connected in the last ten seconds. This
 * is useful for determining whether the account has only just come online, or
 * whether its status has simply changed.
 *
 * Returns: whether @account has only just connected
 *
 * Since: 0.7.UNRELEASED
 */
gboolean
tp_account_is_just_connected (TpAccount *account)
{
  TpAccountPrivate *priv = account->priv;
  GTimeVal val;

  if (priv->connection_status != TP_CONNECTION_STATUS_CONNECTED)
    return FALSE;

  g_get_current_time (&val);

  return (val.tv_sec - priv->connect_time) < 10;
}

/**
 * tp_account_get_connection:
 * @account: a #TpAccount
 *
 * Get the connection of the account, or NULL if account is offline or the
 * connection is not yet ready. This function does not return a new ref.
 *
 * Returns: the connection of the account.
 *
 * Since: 0.7.UNRELEASED
 **/
TpConnection *
tp_account_get_connection (TpAccount *account)
{
  TpAccountPrivate *priv = account->priv;

  if (priv->connection != NULL &&
      tp_connection_is_ready (priv->connection))
    return priv->connection;

  return NULL;
}

/**
 * tp_account_ensure_connection:
 * @account: a #TpAccount
 * @path: the path to connection object for #TpAccount
 *
 * Set the connection of the account by specifying the connection object path.
 * This function does not return a new ref and it is not guaranteed that the
 * returned #TpConnection object is ready
 *
 * Returns: the connection of the account, or %NULL if either the object path
 *   @path is invalid or it is the null-value "/"
 *
 * Since: 0.7.UNRELEASED
 **/
TpConnection *
tp_account_ensure_connection (TpAccount *account,
    const gchar *path)
{
  TpAccountPrivate *priv = account->priv;

  /* double-check that the object path is valid */
  if (!tp_dbus_check_valid_object_path (path, NULL))
    return NULL;

  /* Should be a full object path, not the special "/" value */
  if (strlen (path) == 1)
    return NULL;

  _tp_account_set_connection (account, path);

  return priv->connection;
}

/**
 * tp_account_get_display_name:
 * @account: a #TpAccount
 *
 * <!-- -->
 *
 * Returns: the display name of @account
 *
 * Since: 0.7.UNRELEASED
 **/
const gchar *
tp_account_get_display_name (TpAccount *account)
{
  TpAccountPrivate *priv = account->priv;

  return priv->display_name;
}

/**
 * tp_account_is_valid:
 * @account: a #TpAccount
 *
 * <!-- -->
 *
 * Returns: whether @account is valid
 *
 * Since: 0.7.UNRELEASED
 */
gboolean
tp_account_is_valid (TpAccount *account)
{
  TpAccountPrivate *priv = account->priv;

  return priv->valid;
}

/**
 * tp_account_get_connection_manager:
 * @account: a #TpAccount
 *
 * <!-- -->
 *
 * Returns: the name of the connection manager @account uses
 *
 * Since: 0.7.UNRELEASED
 */
const gchar *
tp_account_get_connection_manager (TpAccount *account)
{
  TpAccountPrivate *priv = account->priv;

  return priv->cm_name;
}

/**
 * tp_account_get_protocol:
 * @account: a #TpAccount
 *
 * <!-- -->
 *
 * Returns: the protocol name @account uses
 *
 * Since: 0.7.UNRELEASED
 */
const gchar *
tp_account_get_protocol (TpAccount *account)
{
  TpAccountPrivate *priv = account->priv;

  return priv->proto_name;
}

/**
 * tp_account_get_icon_name:
 * @account: a #TpAccount
 *
 * <!-- -->
 *
 * Returns: the Icon property on @account
 *
 * Since: 0.7.UNRELEASED
 */
const gchar *
tp_account_get_icon_name (TpAccount *account)
{
  TpAccountPrivate *priv = account->priv;

  return priv->icon_name;
}

/**
 * tp_account_get_parameters:
 * @account: a #TpAccount
 *
 * <!-- -->
 *
 * Returns: the hash table of parameters on @account
 *
 * Since: 0.7.UNRELEASED
 */
const GHashTable *
tp_account_get_parameters (TpAccount *account)
{
  TpAccountPrivate *priv = account->priv;

  return priv->parameters;
}

/**
 * tp_account_is_enabled:
 * @account: a #TpAccount
 *
 * <!-- -->
 *
 * Returns: the Enabled property on @account
 *
 * Since: 0.7.UNRELEASED
 */
gboolean
tp_account_is_enabled (TpAccount *account)
{
  TpAccountPrivate *priv = account->priv;

  return priv->enabled;
}

static void
_tp_account_property_set_cb (TpProxy *proxy,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *result = user_data;

  if (error != NULL)
    {
      DEBUG ("Failed to set property: %s", error->message);
      g_simple_async_result_set_from_error (result, (GError *) error);
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

/**
 * tp_account_set_enabled_finish:
 * @account: a #TpAccount
 * @result: a #GAsyncResult
 * @error: a #GError to fill
 *
 * Finishes an async set of the Enabled property.
 *
 * Returns: %TRUE if the set was successful, otherwise %FALSE
 *
 * Since: 0.7.UNRELEASED
 */
gboolean
tp_account_set_enabled_finish (TpAccount *account,
    GAsyncResult *result,
    GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error) ||
      !g_simple_async_result_is_valid (result, G_OBJECT (account),
          tp_account_set_enabled_finish))
    return FALSE;

  return TRUE;
}

static void
_tp_account_set_presence_from_default (TpAccount *account)
{
  TpAccountPrivate *priv = account->priv;
  const gchar *status;

  switch (priv->default_presence)
    {
    case TP_CONNECTION_PRESENCE_TYPE_AVAILABLE:
      status = "available";
      break;
    case TP_CONNECTION_PRESENCE_TYPE_AWAY:
      status = "away";
      break;
    case TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY:
      status = "xa";
      break;
    case TP_CONNECTION_PRESENCE_TYPE_HIDDEN:
      status = "hidden";
      break;
    case TP_CONNECTION_PRESENCE_TYPE_BUSY:
      status = "busy";
    default:
      status = NULL;
      break;
    }

  if (status == NULL)
    return;

  tp_account_request_presence_async (account, priv->default_presence, status,
      NULL, NULL, NULL);
}

/**
 * tp_account_set_enabled_async:
 * @account: a #TpAccount
 * @enabled: the new enabled value of @account
 * @callback: a callback to call when the request is satisfied
 * @user_data: data to pass to @callback
 *
 * Requests an asynchronous set of the Enabled property of @account. When the
 * operation is finished, @callback will be called. You can then call
 * tp_account_set_enabled_finish() to get the result of the operation.
 *
 * Since: 0.7.UNRELEASED
 */
void
tp_account_set_enabled_async (TpAccount *account,
    gboolean enabled,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  TpAccountPrivate *priv = account->priv;
  GValue value = {0, };
  GSimpleAsyncResult *result;

  result = g_simple_async_result_new (G_OBJECT (account),
      callback, user_data, tp_account_set_enabled_finish);

  if (priv->enabled == enabled)
    {
      g_simple_async_result_complete_in_idle (result);
      return;
    }

  if (enabled)
    _tp_account_set_presence_from_default (account);

  g_value_init (&value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&value, enabled);

  tp_cli_dbus_properties_call_set (TP_PROXY (account),
      -1, TP_IFACE_ACCOUNT, "Enabled", &value,
      _tp_account_property_set_cb, result, NULL, G_OBJECT (account));
}

static void
_tp_account_reconnected_cb (TpAccount *proxy,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *result = user_data;

  if (error != NULL)
    g_simple_async_result_set_from_error (result, (GError *) error);

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

/**
 * tp_account_reconnect_finish:
 * @account: a #TpAccount
 * @result: a #GAsyncResult
 * @error: a #GError to be filled
 *
 * Finishes an async reconnect of @account.
 *
 * Returns: %TRUE if the reconnect call was successful, otherwise %FALSE
 *
 * Since: 0.7.UNRELEASED
 */
gboolean
tp_account_reconnect_finish (TpAccount *account,
    GAsyncResult *result,
    GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error) ||
      !g_simple_async_result_is_valid (result, G_OBJECT (account),
          tp_account_reconnect_finish))
    return FALSE;

  return TRUE;
}

/**
 * tp_account_reconnect_async:
 * @account: a #TpAccount
 * @callback: a callback to call when the request is satisfied
 * @user_data: data to pass to @callback
 *
 * Requests an asynchronous reconnect of @account. When the operation is
 * finished, @callback will be called. You can then call
 * tp_account_reconnect_finish() to get the result of the operation.
 *
 * Since: 0.7.UNRELEASED
 */
void
tp_account_reconnect_async (TpAccount *account,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result;

  result = g_simple_async_result_new (G_OBJECT (account),
      callback, user_data, tp_account_reconnect_finish);

  tp_cli_account_call_reconnect (account, -1, _tp_account_reconnected_cb,
      result, NULL, G_OBJECT (account));
}

/**
 * tp_account_request_presence_finish:
 * @account: a #TpAccount
 * @result: a #GAsyncResult
 * @error: a #GError to fill
 *
 * Finishes an async presence change request on @account.
 *
 * Returns: %TRUE if the operation was successful, otherwise %FALSE
 *
 * Since: 0.7.UNRELEASED
 */
gboolean
tp_account_request_presence_finish (TpAccount *account,
    GAsyncResult *result,
    GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error) ||
      !g_simple_async_result_is_valid (result, G_OBJECT (account),
          tp_account_request_presence_finish))
    return FALSE;

  return TRUE;
}

/**
 * tp_account_request_presence_async:
 * @account: a #TpAccount
 * @type: the requested presence
 * @status: a status message to set, or %NULL
 * @message: a message for the change, or %NULL
 * @callback: a callback to call when the request is satisfied
 * @user_data: data to pass to @callback
 *
 * Requests an asynchronous change of presence on @account. When the
 * operation is finished, @callback will be called. You can then call
 * tp_account_request_presence_finish() to get the result of the operation.
 *
 * Since: 0.7.UNRELEASED
 */
void
tp_account_request_presence_async (TpAccount *account,
    TpConnectionPresenceType type,
    const gchar *status,
    const gchar *message,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GValue value = {0, };
  GValueArray *arr;
  GSimpleAsyncResult *result;

  result = g_simple_async_result_new (G_OBJECT (account),
      callback, user_data, tp_account_request_presence_finish);

  g_value_init (&value, TP_STRUCT_TYPE_SIMPLE_PRESENCE);
  g_value_take_boxed (&value, dbus_g_type_specialized_construct (
          TP_STRUCT_TYPE_SIMPLE_PRESENCE));
  arr = (GValueArray *) g_value_get_boxed (&value);

  g_value_set_uint (arr->values, type);
  g_value_set_static_string (arr->values + 1, status);
  g_value_set_static_string (arr->values + 2, message);

  tp_cli_dbus_properties_call_set (TP_PROXY (account), -1,
      TP_IFACE_ACCOUNT, "RequestedPresence", &value,
      _tp_account_property_set_cb, result, NULL, G_OBJECT (account));

  g_value_unset (&value);
}

static void
_tp_account_updated_cb (TpAccount *proxy,
    const gchar **reconnect_required,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);

  if (error != NULL)
    g_simple_async_result_set_from_error (result, (GError *) error);

  g_simple_async_result_complete (result);
  g_object_unref (G_OBJECT (result));
}

/**
 * tp_account_update_parameters_async:
 * @account: a #TpAccount
 * @parameters: new parameters to set on @account
 * @unset_parameters: list of parameters to unset on @account
 * @callback: a callback to call when the request is satisfied
 * @user_data: data to pass to @callback
 *
 * Requests an asynchronous update of parameters of @account. When the
 * operation is finished, @callback will be called. You can then call
 * tp_account_update_parameters_finish() to get the result of the operation.
 *
 * Since: 0.7.UNRELEASED
 */
void
tp_account_update_parameters_async (TpAccount *account,
    GHashTable *parameters,
    const gchar **unset_parameters,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result;

  result = g_simple_async_result_new (G_OBJECT (account),
      callback, user_data, tp_account_update_parameters_finish);

  tp_cli_account_call_update_parameters (account, -1, parameters,
      unset_parameters, _tp_account_updated_cb, result,
      NULL, G_OBJECT (account));
}

/**
 * tp_account_update_parameters_finish:
 * @account: a #TpAccount
 * @result: a #GAsyncResult
 * @error: a #GError to fill
 *
 * Finishes an async update of the parameters on @account.
 *
 * Returns: %TRUE if the request succeeded, otherwise %FALSE
 *
 * Since: 0.7.UNRELEASED
 */
gboolean
tp_account_update_parameters_finish (TpAccount *account,
    GAsyncResult *result,
    GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
      error))
    return FALSE;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
    G_OBJECT (account), tp_account_update_parameters_finish), FALSE);

  return TRUE;
}

/**
 * tp_account_set_display_name_async:
 * @account: a #TpAccount
 * @display_name: a new display name to set on @account
 * @callback: a callback to call when the request is satisfied
 * @user_data: data to pass to @callback
 *
 * Requests an asynchronous set of the DisplayName property of @account. When
 * the operation is finished, @callback will be called. You can then call
 * tp_account_set_display_name_finish() to get the result of the operation.
 *
 * Since: 0.7.UNRELEASED
 */
void
tp_account_set_display_name_async (TpAccount *account,
    const char *display_name,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result;
  GValue value = {0, };

  if (display_name == NULL)
    {
      g_simple_async_report_error_in_idle (G_OBJECT (account),
          callback, user_data, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
          "Can't set an empty display name");
      return;
    }

  result = g_simple_async_result_new (G_OBJECT (account), callback,
      user_data, tp_account_set_display_name_finish);

  g_value_init (&value, G_TYPE_STRING);
  g_value_set_string (&value, display_name);

  tp_cli_dbus_properties_call_set (account, -1, TP_IFACE_ACCOUNT,
      "DisplayName", &value, _tp_account_property_set_cb, result, NULL,
      G_OBJECT (account));
}

/**
 * tp_account_set_display_name_finish:
 * @account: a #TpAccount
 * @result: a #GAsyncResult
 * @error: a #GError to fill
 *
 * Finishes an async set of the DisplayName property.
 *
 * Returns: %TRUE if the call was successful, otherwise %FALSE
 *
 * Since: 0.7.UNRELEASED
 */
gboolean
tp_account_set_display_name_finish (TpAccount *account,
    GAsyncResult *result,
    GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error) ||
      !g_simple_async_result_is_valid (result, G_OBJECT (account),
          tp_account_set_display_name_finish))
    return FALSE;

  return TRUE;
}

/**
 * tp_account_set_icon_name_async:
 * @account: a #TpAccount
 * @icon_name: a new icon name
 * @callback: a callback to call when the request is satisfied
 * @user_data: data to pass to @callback
 *
 * Requests an asynchronous set of the Icon property of @account. When
 * the operation is finished, @callback will be called. You can then call
 * tp_account_set_icon_name_finish() to get the result of the operation.
 *
 * Since: 0.7.UNRELEASED
 */
void
tp_account_set_icon_name_async (TpAccount *account,
    const char *icon_name,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result;
  GValue value = {0, };
  const char *icon_name_set;

  if (icon_name == NULL)
    /* settings an empty icon name is allowed */
    icon_name_set = "";
  else
    icon_name_set = icon_name;

  result = g_simple_async_result_new (G_OBJECT (account), callback,
      user_data, tp_account_set_icon_name_finish);

  g_value_init (&value, G_TYPE_STRING);
  g_value_set_string (&value, icon_name_set);

  tp_cli_dbus_properties_call_set (account, -1, TP_IFACE_ACCOUNT,
      "Icon", &value, _tp_account_property_set_cb, result, NULL,
      G_OBJECT (account));
}

/**
 * tp_account_set_icon_name_finish:
 * @account: a #TpAccount
 * @result: a #GAsyncResult
 * @error: a #GError to fill
 *
 * Finishes an async set of the Icon parameter.
 *
 * Returns: %TRUE if the operation was successful, otherwise %FALSE
 *
 * Since: 0.7.UNRELEASED
 */
gboolean
tp_account_set_icon_name_finish (TpAccount *account,
    GAsyncResult *result,
    GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error) ||
      !g_simple_async_result_is_valid (result, G_OBJECT (account),
          tp_account_set_icon_name_finish))
    return FALSE;

  return TRUE;
}

static void
_tp_account_remove_cb (TpAccount *proxy,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);

  if (error != NULL)
    g_simple_async_result_set_from_error (result, (GError *) error);

  g_simple_async_result_complete (result);
  g_object_unref (G_OBJECT (result));
}

/**
 * tp_account_remove_async:
 * @account: a #TpAccount
 * @callback: a callback to call when the request is satisfied
 * @user_data: data to pass to @callback
 *
 * Requests an asynchronous removal of @account. When the operation is
 * finished, @callback will be called. You can then call
 * tp_account_remove_finish() to get the result of the operation.
 *
 * Since: 0.7.UNRELEASED
 */
void
tp_account_remove_async (TpAccount *account,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (account),
      callback, user_data, tp_account_remove_finish);

  tp_cli_account_call_remove (account, -1, _tp_account_remove_cb, result, NULL,
      G_OBJECT (account));
}

/**
 * tp_account_remove_finish:
 * @account: a #TpAccount
 * @result: a #GAsyncResult
 * @error: a #GError to fill
 *
 * Finishes an async removal of @account.
 *
 * Returns: %TRUE if the operation was successful, otherwise %FALSE
 *
 * Since: 0.7.UNRELEASED
 */
gboolean
tp_account_remove_finish (TpAccount *account,
    GAsyncResult *result,
    GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error))
    return FALSE;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
          G_OBJECT (account), tp_account_remove_finish), FALSE);

  return TRUE;
}

/**
 * tp_account_refresh_properties:
 * @account: a #TpAccount
 *
 * Refreshes @account's hashtable of properties with what actually exists on
 * the account manager.
 *
 * Since: 0.7.UNRELEASED
 */
void
tp_account_refresh_properties (TpAccount *account)
{
  tp_cli_dbus_properties_call_get_all (account, -1, TP_IFACE_ACCOUNT,
      _tp_account_got_all_cb, NULL, NULL, G_OBJECT (account));
}

/**
 * tp_account_get_connect_automatically:
 * @account: a #TpAccount
 *
 * Gets the ConnectAutomatically parameter on @account.
 *
 * Returns: the value of the ConnectAutomatically parameter on @account
 *
 * Since: 0.7.UNRELEASED
 */
gboolean
tp_account_get_connect_automatically (TpAccount *account)
{
  return account->priv->connect_automatically;
}

/**
 * tp_account_set_connect_automatically_async:
 * @account: a #TpAccount
 * @connect_automatically: new value for the parameter
 * @callback: a callback to call when the request is satisfied
 * @user_data: data to pass to @callback
 *
 * Requests an asynchronous set of the ConnectAutomatically property of
 * @account. When the operation is finished, @callback will be called. You can
 * then call tp_account_set_display_name_finish() to get the result of the
 * operation.
 *
 * Since: 0.7.UNRELEASED
 */
void
tp_account_set_connect_automatically_async (TpAccount *account,
    gboolean connect_automatically,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result;
  GValue value = {0, };

  result = g_simple_async_result_new (G_OBJECT (account), callback,
      user_data, tp_account_set_connect_automatically_finish);

  g_value_init (&value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&value, connect_automatically);

  tp_cli_dbus_properties_call_set (account, -1, TP_IFACE_ACCOUNT,
      "ConnectAutomatically", &value, _tp_account_property_set_cb, result,
      NULL, G_OBJECT (account));
}

/**
 * tp_account_set_connect_automatically_finish:
 * @account: a #TpAccount
 * @result: a #GAsyncResult
 * @error: a #GError to fill
 *
 * Finishes an async set of the ConnectAutomatically property.
 *
 * Returns: %TRUE if the call was successful, otherwise %FALSE
 *
 * Since: 0.7.UNRELEASED
 */
gboolean
tp_account_set_connect_automatically_finish (TpAccount *account,
    GAsyncResult *result,
    GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error) ||
      !g_simple_async_result_is_valid (result, G_OBJECT (account),
          tp_account_set_connect_automatically_finish))
    return FALSE;

  return TRUE;
}

/**
 * tp_account_get_has_been_online:
 * @account: a #TpAccount
 *
 * Gets the HasBeenOnline parameter on @account.
 *
 * Returns: the value of the HasBeenOnline parameter on @account
 *
 * Since: 0.7.UNRELEASED
 */
gboolean
tp_account_get_has_been_online (TpAccount *account)
{
  return account->priv->has_been_online;
}

/**
 * tp_account_get_connection_status:
 * @account: a #TpAccount
 *
 * Gets the ConnectionStatus parameter on @account.
 *
 * Returns: the value of the ConnectionStatus parameter on @account
 *
 * Since: 0.7.UNRELEASED
 */
TpConnectionStatus
tp_account_get_connection_status (TpAccount *account)
{
  return account->priv->connection_status;
}

/**
 * tp_account_get_connection_status_reason:
 * @account: a #TpAccount
 *
 * Gets the ConnectionStatusReason parameter on @account.
 *
 * Returns: the value of the ConnectionStatusReason parameter on @account
 *
 * Since: 0.7.UNRELEASED
 */
TpConnectionStatusReason
tp_account_get_connection_status_reason (TpAccount *account)
{
  return account->priv->reason;
}

/**
 * tp_account_get_presence:
 * @account: a #TpAccount
 *
 * Gets the type from the CurrentPresence parameter on @account.
 *
 * Returns: the type from the CurrentPresence parameter on @account
 *
 * Since: 0.7.UNRELEASED
 */
TpConnectionPresenceType
tp_account_get_presence (TpAccount *account)
{
  return account->priv->presence;
}

/**
 * tp_account_get_status:
 * @account: a #TpAccount
 *
 * Gets the status from the CurrentPresence parameter on @account.
 *
 * Returns: the status from the CurrentPresence parameter on @account
 *
 * Since: 0.7.UNRELEASED
 */
const gchar *
tp_account_get_status (TpAccount *account)
{
  return account->priv->status;
}

/**
 * tp_account_get_status_message:
 * @account: a #TpAccount
 *
 * Gets the message from the CurrentPresence parameter on @account.
 *
 * Returns: the message from the CurrentPresence parameter on @account
 *
 * Since: 0.7.UNRELEASED
 */
const gchar *
tp_account_get_status_message (TpAccount *account)
{
  return account->priv->message;
}

/**
 * tp_account_get_requested_presence:
 * @account: a #TpAccount
 *
 * Gets the presence from the RequestedPresence parameter on @account.
 *
 * Returns: the presence from the RequestedPresence parameter on @account
 *
 * Since: 0.7.UNRELEASED
 */
TpConnectionPresenceType
tp_account_get_requested_presence (TpAccount *account)
{
  return account->priv->requested_presence;
}

/**
 * tp_account_get_requested_status:
 * @account: a #TpAccount
 *
 * Gets the status from the RequestedPresence parameter on @account.
 *
 * Returns: the status from the RequestedPresence parameter on @account
 *
 * Since: 0.7.UNRELEASED
 */
const gchar *
tp_account_get_requested_status (TpAccount *account)
{
  return account->priv->requested_status;
}

/**
 * tp_account_get_requested_status_message:
 * @account: a #TpAccount
 *
 * Gets the message from the RequestedPresence parameter on @account.
 *
 * Returns: the message from the RequestedPresence parameter on @account
 *
 * Since: 0.7.UNRELEASED
 */
const gchar *
tp_account_get_requested_status_message (TpAccount *account)
{
  return account->priv->requested_message;
}

/**
 * tp_account_get_nickname:
 * @account: a #TpAccount
 *
 * Gets the value of the Nickname parameter on @account.
 *
 * Returns: the value of the Nickname parameter on @account
 *
 * Since: 0.7.UNRELEASED
 */
const gchar *
tp_account_get_nickname (TpAccount *account)
{
  return account->priv->nickname;
}

/**
 * tp_account_set_nickname_finish:
 * @account: a #TpAccount
 * @result: a #GAsyncResult
 * @error: a #GError to fill
 *
 * Finishes an async nickname change request on @account.
 *
 * Returns: %TRUE if the operation was successful, otherwise %FALSE
 *
 * Since: 0.7.UNRELEASED
 */
gboolean
tp_account_set_nickname_finish (TpAccount *account,
    GAsyncResult *result,
    GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error) ||
      !g_simple_async_result_is_valid (result, G_OBJECT (account),
          tp_account_set_nickname_finish))
    return FALSE;

  return TRUE;
}

/**
 * tp_account_set_nickname_async:
 * @account: a #TpAccount
 * @nickname: a new nickname to set
 * @callback: a callback to call when the request is satisfied
 * @user_data: data to pass to @callback
 *
 * Requests an asynchronous change of the Nickname parameter on @account. When
 * the operation is finished, @callback will be called. You can then call
 * tp_account_set_nickname_finish() to get the result of the operation.
 *
 * Since: 0.7.UNRELEASED
 */
void
tp_account_set_nickname_async (TpAccount *account,
    const gchar *nickname,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GValue value = {0, };
  GSimpleAsyncResult *result;

  result = g_simple_async_result_new (G_OBJECT (account),
      callback, user_data, tp_account_request_presence_finish);

  if (nickname == NULL)
    {
      g_simple_async_report_error_in_idle (G_OBJECT (account),
          callback, user_data, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
          "Can't set an empty nickname");
      return;
    }

  g_value_init (&value, G_TYPE_STRING);
  g_value_set_string (&value, nickname);

  tp_cli_dbus_properties_call_set (TP_PROXY (account), -1,
      TP_IFACE_ACCOUNT, "Nickname", &value,
      _tp_account_property_set_cb, result, NULL, G_OBJECT (account));

  g_value_unset (&value);
}

/**
 * tp_account_get_unique_name:
 * @account: a #TpAccount
 *
 * <!-- -->
 *
 * Returns: the unique name of @account
 *
 * Since: 0.7.UNRELEASED
 */
const gchar *
tp_account_get_unique_name (TpAccount *account)
{
  return tp_proxy_get_object_path (account);
}

static void
_tp_account_got_avatar_cb (TpProxy *proxy,
    const GValue *out_Value,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);
  GValueArray *avatar;
  GArray *res;

  if (error != NULL)
    {
      DEBUG ("Failed to get avatar: %s", error->message);
      g_simple_async_result_set_from_error (result, (GError *) error);
    }
  else
    {
      avatar = g_value_get_boxed (out_Value);
      res = g_value_get_boxed (g_value_array_get_nth (avatar, 0));
      g_simple_async_result_set_op_res_gpointer (result, res, NULL);
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

/**
 * tp_account_get_avatar_async:
 * @account: a #TpAccount
 * @callback: a callback to call when the request is satisfied
 * @user_data: data to pass to @callback
 *
 * Requests an asynchronous get of @account's avatar. When
 * the operation is finished, @callback will be called. You can then call
 * tp_account_get_avatar_finish() to get the result of the operation.
 *
 * Since: 0.7.UNRELEASED
 */
void
tp_account_get_avatar_async (TpAccount *account,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result;

  result = g_simple_async_result_new (G_OBJECT (account),
      callback, user_data, tp_account_get_avatar_finish);

  tp_cli_dbus_properties_call_get (account, -1,
      TP_IFACE_ACCOUNT_INTERFACE_AVATAR, "Avatar", _tp_account_got_avatar_cb,
      result, NULL, G_OBJECT (account));
}

/**
 * tp_account_get_avatar_finish:
 * @account: a #TpAccount
 * @result: a #GAsyncResult
 * @error: a #GError to fill
 *
 * Finishes an async get operation of @account's avatar.
 *
 * Returns: a #GArray of the account's avatar, or %NULL on failure
 *
 * Since: 0.7.UNRELEASED
 */
const GArray *
tp_account_get_avatar_finish (TpAccount *account,
    GAsyncResult *result,
    GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error) ||
      !g_simple_async_result_is_valid (result, G_OBJECT (account),
          tp_account_get_avatar_finish))
    return NULL;

  return g_simple_async_result_get_op_res_gpointer (
      G_SIMPLE_ASYNC_RESULT (result));
}

/**
 * tp_account_is_ready:
 * @account: a #TpAccount
 * @feature: a feature which is required
 *
 * <!-- -->
 *
 * Returns: %TRUE whether @feature is ready on @account, otherwise %FALSE
 *
 * Since: 0.7.UNRELEASED
 */
gboolean
tp_account_is_ready (TpAccount *account,
    GQuark feature)
{
  TpAccountFeature *f;

  if (tp_proxy_get_invalidated (account) != NULL)
    return FALSE;

  f = _tp_account_get_feature (account, feature);

  if (f == NULL)
    return FALSE;

  return f->ready;
}

/**
 * tp_account_prepare_async:
 * @account: a #TpAccount
 * @features: a 0-terminated list of features
 * @callback: a callback to call when the request is satisfied
 * @user_data: data to pass to @callback
 *
 * Requests an asynchronous preparation of @account with the features specified
 * by @features. When the operation is finished, @callback will be called. You
 * can then call tp_account_prepare_finish() to get the result of the
 * operation.
 *
 * If %NULL is given to @callback, then no callback will be called when the
 * operation is finished. Instead, it will simply set @features on @manager.
 * Note that if @callback is %NULL, then @user_data must also be %NULL.
 *
 * Since: 0.7.UNRELEASED
 */
void
tp_account_prepare_async (TpAccount *account,
    GQuark* features,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  TpAccountPrivate *priv = account->priv;
  GSimpleAsyncResult *result;

  /* In this object, there are no features which are activatable (core is
   * forced on you). They'd be activated here though. */

  _tp_account_update_feature_arrays (account, features);

  if (callback == NULL)
    return;

  result = g_simple_async_result_new (G_OBJECT (account),
      callback, user_data, tp_account_prepare_finish);

  if (_tp_account_check_features (account, features))
    {
      g_simple_async_result_complete (result);
      g_object_unref (result);
    }
  else
    {
      TpAccountFeatureCallback *cb;

      cb = g_slice_new0 (TpAccountFeatureCallback);
      cb->result = result;
      cb->features = features;
      priv->callbacks = g_list_prepend (priv->callbacks, cb);
    }
}

/**
 * tp_account_prepare_finish:
 * @account: a #TpAccount
 * @result: a #GAsyncResult
 * @error: a #GError to fill
 *
 * Finishes an async preparation of the account @account.
 *
 * Returns: %TRUE if the preparation was successful, otherwise %FALSE
 *
 * Since: 0.7.UNRELEASED
 */
gboolean
tp_account_prepare_finish (TpAccount *account,
    GAsyncResult *result,
    GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error) ||
      !g_simple_async_result_is_valid (result, G_OBJECT (account),
          tp_account_prepare_finish))
    return FALSE;

  return TRUE;
}

/**
 * tp_account_get_requested_features:
 * @account: a #TpAccount
 *
 * <!-- -->
 *
 * Returns: a 0-terminated list of features requested on @account
 *
 * Since: 0.7.UNRELEASED
 */
const GQuark *
tp_account_get_requested_features (TpAccount *account)
{
  return (const GQuark *) account->priv->requested_features->data;
}

/**
 * tp_account_get_actual_features:
 * @account: a #TpAccount
 *
 * <!-- -->
 *
 * Returns: a 0-terminated list of actual features on @account
 *
 * Since: 0.7.UNRELEASED
 */
const GQuark *
tp_account_get_actual_features (TpAccount *account)
{
  return (const GQuark *) account->priv->actual_features->data;
}

/**
 * tp_account_get_missing_features:
 * @account: a #TpAccount
 *
 * <!-- -->
 *
 * Returns: a 0-terminated list of missing features from @account
 *          that have been requested
 *
 * Since: 0.7.UNRELEASED
 */
const GQuark *
tp_account_get_missing_features (TpAccount *account)
{
  return (const GQuark *) account->priv->missing_features->data;
}

static void
set_or_free (gchar **target,
    gchar *source)
{
  if (target != NULL)
    *target = source;
  else
    g_free (source);
}

/**
 * tp_account_parse_object_path:
 * @object_path: a Telepathy Account's object path
 * @cm: location at which to store the account's connection manager's name
 * @protocol: location at which to store the account's protocol
 * @account_id: location at which to store the account's unique identifier
 * @error: location at which to return an error
 *
 * Validates and parses a Telepathy Account's object path, extracting the
 * connection manager's name, the protocol, and the account's unique identifier
 * from the path. This includes replacing underscores with hyphens in the
 * protocol name, as defined in the Account specification.
 *
 * Any of the out parameters may be %NULL if not needed.
 *
 * Returns: %TRUE if @object_path was successfully parsed; %FALSE and sets
 *          @error otherwise.
 *
 * Since: 0.7.UNRELEASED
 */
gboolean
tp_account_parse_object_path (const gchar *object_path,
    gchar **cm,
    gchar **protocol,
    gchar **account_id,
    GError **error)
{
  const gchar *suffix;
  gchar **segments;

  if (!tp_dbus_check_valid_object_path (object_path, error))
    return FALSE;

  if (!g_str_has_prefix (object_path, TP_ACCOUNT_OBJECT_PATH_BASE))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Account path does not start with the right prefix: %s",
          object_path);
      return FALSE;
    }

  suffix = object_path + strlen (TP_ACCOUNT_OBJECT_PATH_BASE);

  segments = g_strsplit (suffix, "/", 0);

  if (g_strv_length (segments) != 3)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Account path '%s' is malformed: should have 3 trailing components, "
          "not %u", object_path, g_strv_length (segments));
      goto free_segments_and_fail;
    }

  if (!g_ascii_isalpha (segments[0][0]))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Account path '%s' is malformed: CM name should start with a letter",
          object_path);
      goto free_segments_and_fail;
    }

  if (!g_ascii_isalpha (segments[1][0]))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Account path '%s' is malformed: "
          "protocol name should start with a letter",
          object_path);
      goto free_segments_and_fail;
    }

  if (!g_ascii_isalpha (segments[2][0]) && segments[2][0] != '_')
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Account path '%s' is malformed: "
          "account ID should start with a letter or underscore",
          object_path);
      goto free_segments_and_fail;
    }

  set_or_free (cm, segments[0]);
  set_or_free (protocol, unescape_protocol (segments[1]));
  set_or_free (account_id, segments[2]);

  /* Not g_strfreev because we stole or freed the individual strings */
  g_free (segments);
  return TRUE;

free_segments_and_fail:
  g_strfreev (segments);
  return FALSE;
}
