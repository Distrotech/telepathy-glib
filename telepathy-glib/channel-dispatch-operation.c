/*
 * channel-dispatch-operation.c - proxy for incoming channels seeking approval
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

#include "telepathy-glib/channel-dispatch-operation.h"

#include <telepathy-glib/channel.h>
#include <telepathy-glib/defs.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/proxy-internal.h>
#include <telepathy-glib/proxy-subclass.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG TP_DEBUG_DISPATCHER
#include "telepathy-glib/dbus-internal.h"
#include "telepathy-glib/debug-internal.h"
#include "telepathy-glib/_gen/signals-marshal.h"

#include "telepathy-glib/_gen/tp-cli-channel-dispatch-operation-body.h"

/**
 * SECTION:channel-dispatch-operation
 * @title: TpChannelDispatchOperation
 * @short_description: proxy object for a to the Telepathy channel
 *  dispatcher
 * @see_also: #TpChannelDispatcher
 *
 * One of the channel dispatcher's functions is to offer incoming channels to
 * Approver clients for approval. Approvers respond to the channel dispatcher
 * via a #TpChannelDispatchOperation object.
 */

/**
 * TpChannelDispatchOperation:
 *
 * One of the channel dispatcher's functions is to offer incoming channels to
 * Approver clients for approval. An approver should generally ask the user
 * whether they want to participate in the requested communication channels
 * (join the chat or chatroom, answer the call, accept the file transfer, or
 * whatever is appropriate). A collection of channels offered in this way
 * is represented by a ChannelDispatchOperation object.
 *
 * If the user wishes to accept the communication channels, the approver
 * should call tp_cli_channel_dispatch_operation_call_handle_with() to
 * indicate the user's or approver's preferred handler for the channels (the
 * empty string indicates no particular preference, and will cause any
 * suitable handler to be used).
 *
 * If the user wishes to reject the communication channels, or if the user
 * accepts the channels and the approver will handle them itself, the approver
 * should call tp_cli_channel_dispatch_operation_call_claim(). If this method
 * succeeds, the approver immediately has control over the channels as their
 * primary handler, and may do anything with them (in particular, it may close
 * them in whatever way seems most appropriate).
 *
 * There are various situations in which the channel dispatch operation will
 * be closed, causing the #TpProxy::invalidated signal to be emitted. If this
 * happens, the approver should stop prompting the user.
 *
 * Because all approvers are launched simultaneously, the user might respond
 * to another approver; if this happens, the invalidated signal will be
 * emitted with the domain %TP_DBUS_ERRORS and the error code
 * %TP_DBUS_ERROR_OBJECT_REMOVED.
 *
 * If a channel closes, the #TpChannelDispatchOperation::channel-lost signal
 * is emitted. If all channels
 * close, there is nothing more to dispatch, so the invalidated signal will be
 * emitted with the domain %TP_DBUS_ERRORS and the error code
 * %TP_DBUS_ERROR_OBJECT_REMOVED.
 *
 * If the channel dispatcher crashes or exits, the invalidated
 * signal will be emitted with the domain %TP_DBUS_ERRORS and the error code
 * %TP_DBUS_ERROR_NAME_OWNER_LOST. In a high-quality implementation, the
 * dispatcher should be restarted, at which point it will create new
 * channel dispatch operations for any undispatched channels, and the approver
 * will be notified again.
 *
 * This proxy is usable but incomplete: accessors for the D-Bus properties will
 * be added in a later version of telepathy-glib, along with a mechanism
 * similar to tp_connection_call_when_ready().
 *
 * Since: 0.7.32
 */

/**
 * TpChannelDispatchOperationClass:
 *
 * The class of a #TpChannelDispatchOperation.
 */

struct _TpChannelDispatchOperationPrivate {
  TpConnection *connection;
  TpAccount *account;
  GPtrArray *channels;
  GStrv possible_handlers;
  GHashTable *immutable_properties;

  gboolean preparing_core;
};

enum
{
  PROP_CONNECTION = 1,
  PROP_ACCOUNT,
  PROP_CHANNELS,
  PROP_POSSIBLE_HANDLERS,
  PROP_CHANNEL_DISPATCH_OPERATION_PROPERTIES,
  N_PROPS
};

enum {
  SIGNAL_CHANNEL_LOST,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

G_DEFINE_TYPE (TpChannelDispatchOperation, tp_channel_dispatch_operation,
    TP_TYPE_PROXY);

static void
tp_channel_dispatch_operation_init (TpChannelDispatchOperation *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      TP_TYPE_CHANNEL_DISPATCH_OPERATION, TpChannelDispatchOperationPrivate);

  self->priv->immutable_properties = g_hash_table_new_full (g_str_hash,
      g_str_equal, g_free, (GDestroyNotify) tp_g_value_slice_free);
}

static void
tp_channel_dispatch_operation_finished_cb (TpChannelDispatchOperation *self,
    gpointer unused G_GNUC_UNUSED,
    GObject *object G_GNUC_UNUSED)
{
  GError e = { TP_DBUS_ERRORS, TP_DBUS_ERROR_OBJECT_REMOVED,
      "ChannelDispatchOperation finished and was removed" };

  tp_proxy_invalidate ((TpProxy *) self, &e);
}

static void
tp_channel_dispatch_operation_channel_lost_cb (TpChannelDispatchOperation *self,
    const gchar *path,
    const gchar *dbus_error,
    const gchar *message,
    gpointer unused G_GNUC_UNUSED,
    GObject *object G_GNUC_UNUSED)
{
  guint i;

  if (self->priv->channels == NULL)
    /* We didn't fetch channels yet */
    return;

  for (i = 0; i < self->priv->channels->len; i++)
    {
      TpChannel *channel = g_ptr_array_index (self->priv->channels, i);

      if (!tp_strdiff (tp_proxy_get_object_path (channel), path))
        {
          GError *error = NULL;

          /* Removing the channel from the array will unref it, add an extra
           * ref as we'll need it to fire the signal */
          g_object_ref (channel);

          g_ptr_array_remove (self->priv->channels, channel);

          tp_proxy_dbus_error_to_gerror (self, dbus_error, message, &error);

          g_signal_emit (self, signals[SIGNAL_CHANNEL_LOST], 0, channel,
              error->domain, error->code, error->message);

          g_object_notify ((GObject *) self, "channels");

          g_object_unref (channel);
          g_error_free (error);
          return;
        }
    }

  DEBUG ("Don't know this channel: %s", path);
}

static void
tp_channel_dispatch_operation_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  TpChannelDispatchOperation *self = TP_CHANNEL_DISPATCH_OPERATION (object);

  switch (property_id)
    {
    case PROP_CONNECTION:
      g_value_set_object (value, self->priv->connection);
      break;

    case PROP_ACCOUNT:
      g_value_set_object (value, self->priv->account);
      break;

    case PROP_CHANNELS:
      g_value_set_boxed (value, self->priv->channels);
      break;

    case PROP_POSSIBLE_HANDLERS:
      g_value_set_boxed (value, self->priv->possible_handlers);
      break;

    case PROP_CHANNEL_DISPATCH_OPERATION_PROPERTIES:
      g_value_set_boxed (value, self->priv->immutable_properties);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
maybe_set_connection (TpChannelDispatchOperation *self,
    const gchar *path)
{
  TpDBusDaemon *dbus;
  GError *error = NULL;

  if (self->priv->connection != NULL)
    return;

  if (path == NULL)
    return;

  dbus = tp_proxy_get_dbus_daemon (self);

  self->priv->connection = tp_connection_new (dbus, NULL, path, &error);
  if (self->priv->connection == NULL)
    {
      DEBUG ("Failed to create connection %s: %s", path, error->message);
      g_error_free (error);
      return;
    }

  g_object_notify ((GObject *) self, "connection");

  if (g_hash_table_lookup (self->priv->immutable_properties,
        TP_PROP_CHANNEL_DISPATCH_OPERATION_CONNECTION) != NULL)
    return;

  g_hash_table_insert (self->priv->immutable_properties,
      g_strdup (TP_PROP_CHANNEL_DISPATCH_OPERATION_CONNECTION),
      tp_g_value_slice_new_boxed (DBUS_TYPE_G_OBJECT_PATH, path));
}

static void
maybe_set_account (TpChannelDispatchOperation *self,
    const gchar *path)
{
  TpDBusDaemon *dbus;
  GError *error = NULL;

  if (self->priv->account != NULL)
    return;

  if (path == NULL)
    return;

  dbus = tp_proxy_get_dbus_daemon (self);

  self->priv->account = tp_account_new (dbus, path, &error);
  if (self->priv->account == NULL)
    {
      DEBUG ("Failed to create account %s: %s", path, error->message);
      g_error_free (error);
      return;
    }

  g_object_notify ((GObject *) self, "account");

  if (g_hash_table_lookup (self->priv->immutable_properties,
        TP_PROP_CHANNEL_DISPATCH_OPERATION_ACCOUNT) != NULL)
    return;

  g_hash_table_insert (self->priv->immutable_properties,
      g_strdup (TP_PROP_CHANNEL_DISPATCH_OPERATION_ACCOUNT),
      tp_g_value_slice_new_boxed (DBUS_TYPE_G_OBJECT_PATH, path));
}

static void
maybe_set_possible_handlers (TpChannelDispatchOperation *self,
    GStrv handlers)
{
  if (self->priv->possible_handlers != NULL)
    return;

  if (handlers == NULL)
    return;

  self->priv->possible_handlers = g_strdupv (handlers);

  g_object_notify ((GObject *) self, "possible-handlers");

  if (g_hash_table_lookup (self->priv->immutable_properties,
        TP_PROP_CHANNEL_DISPATCH_OPERATION_POSSIBLE_HANDLERS) != NULL)
    return;

  g_hash_table_insert (self->priv->immutable_properties,
      g_strdup (TP_PROP_CHANNEL_DISPATCH_OPERATION_POSSIBLE_HANDLERS),
      tp_g_value_slice_new_boxed (G_TYPE_STRV, handlers));
}

static void
maybe_set_interfaces (TpChannelDispatchOperation *self,
    const gchar **interfaces)
{
  const gchar **iter;

  if (interfaces == NULL)
    return;

  for (iter = interfaces; *iter != NULL; iter++)
    {
      DEBUG ("- %s", *iter);

      if (tp_dbus_check_valid_interface_name (*iter, NULL))
        {
          GQuark q = g_quark_from_string (*iter);
          tp_proxy_add_interface_by_id ((TpProxy *) self, q);
        }
      else
        {
          DEBUG ("\tInterface %s not valid, ignoring it", *iter);
        }
    }

  g_hash_table_insert (self->priv->immutable_properties,
      g_strdup (TP_PROP_CHANNEL_DISPATCH_OPERATION_INTERFACES),
      tp_g_value_slice_new_boxed (G_TYPE_STRV, interfaces));
}

static void
tp_channel_dispatch_operation_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  TpChannelDispatchOperation *self = TP_CHANNEL_DISPATCH_OPERATION (object);

  switch (property_id)
    {
      case PROP_CHANNEL_DISPATCH_OPERATION_PROPERTIES:
        {
          GHashTable *asv = g_value_get_boxed (value);

          if (asv == NULL)
            return;

          tp_g_hash_table_update (self->priv->immutable_properties,
              asv, (GBoxedCopyFunc) g_strdup,
              (GBoxedCopyFunc) tp_g_value_slice_dup);

          maybe_set_connection (self, tp_asv_get_boxed (asv,
                TP_PROP_CHANNEL_DISPATCH_OPERATION_CONNECTION,
                DBUS_TYPE_G_OBJECT_PATH));

          maybe_set_account (self, tp_asv_get_boxed (asv,
                TP_PROP_CHANNEL_DISPATCH_OPERATION_ACCOUNT,
                DBUS_TYPE_G_OBJECT_PATH));

          maybe_set_possible_handlers (self, tp_asv_get_boxed (asv,
                TP_PROP_CHANNEL_DISPATCH_OPERATION_POSSIBLE_HANDLERS,
                G_TYPE_STRV));

          maybe_set_interfaces (self, tp_asv_get_boxed (asv,
                TP_PROP_CHANNEL_DISPATCH_OPERATION_INTERFACES,
                G_TYPE_STRV));
        }
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
  }
}

static void
tp_channel_dispatch_operation_constructed (GObject *object)
{
  TpChannelDispatchOperation *self = TP_CHANNEL_DISPATCH_OPERATION (object);
  void (*chain_up) (GObject *) =
    ((GObjectClass *) tp_channel_dispatch_operation_parent_class)->constructed;
  GError *error = NULL;
  TpProxySignalConnection *sc;

  if (chain_up != NULL)
    chain_up (object);

  g_return_if_fail (tp_proxy_get_dbus_daemon (self) != NULL);

  sc = tp_cli_channel_dispatch_operation_connect_to_finished (self,
      tp_channel_dispatch_operation_finished_cb, NULL, NULL, NULL, &error);

  if (sc == NULL)
    {
      CRITICAL ("Couldn't connect to Finished: %s", error->message);
      g_error_free (error);
      g_assert_not_reached ();
      return;
    }

  sc = tp_cli_channel_dispatch_operation_connect_to_channel_lost (self,
      tp_channel_dispatch_operation_channel_lost_cb, NULL, NULL, NULL, &error);

  if (sc == NULL)
    {
      g_critical ("Couldn't connect to ChannelLost: %s", error->message);
      g_error_free (error);
      g_assert_not_reached ();
      return;
    }
}

static void
tp_channel_dispatch_operation_dispose (GObject *object)
{
  TpChannelDispatchOperation *self = TP_CHANNEL_DISPATCH_OPERATION (object);
  void (*dispose) (GObject *) =
    G_OBJECT_CLASS (tp_channel_dispatch_operation_parent_class)->dispose;

  if (self->priv->connection != NULL)
    {
      g_object_unref (self->priv->connection);
      self->priv->connection = NULL;
    }

  if (self->priv->account != NULL)
    {
      g_object_unref (self->priv->account);
      self->priv->account = NULL;
    }

  if (self->priv->channels != NULL)
    {
      /* channels array has 'g_object_unref' has free_func */
      g_ptr_array_free (self->priv->channels, TRUE);
      self->priv->channels = NULL;
    }

  g_strfreev (self->priv->possible_handlers);
  self->priv->possible_handlers = NULL;

  if (self->priv->immutable_properties != NULL)
    {
      g_hash_table_unref (self->priv->immutable_properties);
      self->priv->immutable_properties = NULL;
    }

  if (dispose != NULL)
    dispose (object);
}

static void
get_dispatch_operation_prop_cb (TpProxy *proxy,
    GHashTable *props,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  TpChannelDispatchOperation *self = (TpChannelDispatchOperation *) proxy;
  gboolean prepared = TRUE;
  GPtrArray *channels;
  guint i;
  GError *e = NULL;

  self->priv->preparing_core = FALSE;

  if (error != NULL)
    {
      DEBUG ("Failed to fetch ChannelDispatchOperation properties: %s",
          error->message);

      prepared = FALSE;
      e = g_error_copy (error);
      goto out;
    }

  /* Connection */
  maybe_set_connection (self, tp_asv_get_boxed (props, "Connection",
        DBUS_TYPE_G_OBJECT_PATH));

  if (self->priv->connection == NULL)
    {
      e = g_error_new_literal (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Mandatory 'Connection' property is missing");
      DEBUG ("%s", e->message);

      prepared = FALSE;
      goto out;
    }

  /* Account */
  maybe_set_account (self, tp_asv_get_boxed (props, "Account",
        DBUS_TYPE_G_OBJECT_PATH));

  if (self->priv->account == NULL)
    {
      e = g_error_new_literal (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Mandatory 'Account' property is missing");
      DEBUG ("%s", e->message);

      prepared = FALSE;
      goto out;
    }

  /* PossibleHandlers */
  maybe_set_possible_handlers (self, tp_asv_get_boxed (props,
        "PossibleHandlers", G_TYPE_STRV));

  if (self->priv->possible_handlers == NULL)
    {
      e = g_error_new_literal (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Mandatory 'PossibleHandlers' property is missing");
      DEBUG ("%s", e->message);

      prepared = FALSE;
      goto out;
    }

  maybe_set_interfaces (self, tp_asv_get_boxed (props,
        "Interfaces", G_TYPE_STRV));

  /* set channels (not an immutable property) */
  channels = tp_asv_get_boxed (props, "Channels",
      TP_ARRAY_TYPE_CHANNEL_DETAILS_LIST);
  if (channels == NULL)
    {
      e = g_error_new_literal (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Mandatory 'Channels' property is missing");
      DEBUG ("%s", e->message);

      prepared = FALSE;
      goto out;
    }

  self->priv->channels = g_ptr_array_sized_new (channels->len);
  g_ptr_array_set_free_func (self->priv->channels,
      (GDestroyNotify) g_object_unref);

  for (i = 0; i < channels->len; i++)
    {
      const gchar *path;
      GHashTable *chan_props;
      TpChannel *channel;
      GError *err = NULL;

      tp_value_array_unpack (g_ptr_array_index (channels, i), 2,
            &path, &chan_props);

      channel = tp_channel_new_from_properties (self->priv->connection,
          path, chan_props, &err);

      if (channel == NULL)
        {
          DEBUG ("Failed to create channel %s: %s", path, err->message);
          g_error_free (err);
          continue;
        }

      g_ptr_array_add (self->priv->channels, channel);
    }

  g_object_notify ((GObject *) self, "channels");
  g_object_notify ((GObject *) self, "channel-dispatch-operation-properties");

out:
  _tp_proxy_set_feature_prepared (proxy,
      TP_CHANNEL_DISPATCH_OPERATION_FEATURE_CORE, prepared);

  if (!prepared)
    {
      tp_proxy_invalidate ((TpProxy *) self, e);
      g_error_free (e);
    }
}

static void
maybe_prepare_core (TpProxy *proxy)
{
  TpChannelDispatchOperation *self = (TpChannelDispatchOperation *) proxy;

  if (self->priv->channels != NULL)
    return;   /* already done */

  if (self->priv->preparing_core)
    return;   /* already running */

  if (!_tp_proxy_is_preparing (proxy,
        TP_CHANNEL_DISPATCH_OPERATION_FEATURE_CORE))
    return;   /* not interested right now */

  tp_cli_dbus_properties_call_get_all (self, -1,
      TP_IFACE_CHANNEL_DISPATCH_OPERATION,
      get_dispatch_operation_prop_cb,
      NULL, NULL, NULL);
}

enum {
    FEAT_CORE,
    N_FEAT
};

static const TpProxyFeature *
tp_channel_dispatch_operation_list_features (TpProxyClass *cls G_GNUC_UNUSED)
{
  static TpProxyFeature features[N_FEAT + 1] = { { 0 } };

  if (G_LIKELY (features[0].name != 0))
    return features;

  features[FEAT_CORE].name = TP_CHANNEL_DISPATCH_OPERATION_FEATURE_CORE;
  features[FEAT_CORE].core = TRUE;
  features[FEAT_CORE].start_preparing = maybe_prepare_core;

  /* assert that the terminator at the end is there */
  g_assert (features[N_FEAT].name == 0);

  return features;
}

static void
tp_channel_dispatch_operation_class_init (TpChannelDispatchOperationClass *klass)
{
  TpProxyClass *proxy_class = (TpProxyClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (TpChannelDispatchOperationPrivate));

  object_class->get_property = tp_channel_dispatch_operation_get_property;
  object_class->set_property = tp_channel_dispatch_operation_set_property;
  object_class->constructed = tp_channel_dispatch_operation_constructed;
  object_class->dispose = tp_channel_dispatch_operation_dispose;

  /**
   * TpChannelDispatchOperation:connection:
   *
   * The #TpConnection with which the channels are associated.
   *
   * Read-only except during construction.
   *
   * This is not guaranteed to be set until tp_proxy_prepare_async() has
   * finished preparing %TP_CHANNEL_DISPATCH_OPERATION_FEATURE_CORE.
   *
   * Since: 0.11.UNRELEASED
   */
  param_spec = g_param_spec_object ("connection", "TpConnection",
      "The TpConnection of this channel dispatch operation",
      TP_TYPE_CONNECTION,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION,
      param_spec);

  /**
   * TpChannelDispatchOperation:account:
   *
   * The #TpAccount with which the connection and channels are associated.
   *
   * Read-only except during construction.
   *
   * This is not guaranteed to be set until tp_proxy_prepare_async() has
   * finished preparing %TP_CHANNEL_DISPATCH_OPERATION_FEATURE_CORE.
   *
   * Since: 0.11.UNRELEASED
   */
  param_spec = g_param_spec_object ("account", "TpAccount",
      "The TpAccount of this channel dispatch operation",
      TP_TYPE_ACCOUNT,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ACCOUNT,
      param_spec);

  /**
   * TpChannelDispatchOperation:channels:
   *
   * A #GPtrArray containing the #TpChannel to be dispatched.
   *
   * Read-only.
   *
   * This is not guaranteed to be set until tp_proxy_prepare_async() has
   * finished preparing %TP_CHANNEL_DISPATCH_OPERATION_FEATURE_CORE.
   *
   * Since: 0.11.UNRELEASED
   */
  param_spec = g_param_spec_boxed ("channels", "GPtrArray of TpChannel",
      "The TpChannel to be dispatched",
      G_TYPE_PTR_ARRAY,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CHANNELS,
      param_spec);

  /**
   * TpChannelDispatchOperation:possible-handlers:
   *
   * A #GStrv containing the well known bus names (starting
   * with TP_CLIENT_BUS_NAME_BASE) of the possible Handlers for
   * the channels
   *
   * Read-only except during construction.
   *
   * This is not guaranteed to be set until tp_proxy_prepare_async() has
   * finished preparing %TP_CHANNEL_DISPATCH_OPERATION_FEATURE_CORE.
   *
   * Since: 0.11.UNRELEASED
   */
  param_spec = g_param_spec_boxed ("possible-handlers", "Possible handlers",
      "Possible handlers for the channels",
      G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_POSSIBLE_HANDLERS,
      param_spec);

  /**
   * TpChannelDispatchOperation:channel-dispatch-operation-properties:
   *
   * The immutable D-Bus properties of this ChannelDispatchOperation,
   * represented by a #GHashTable where the keys are D-Bus
   * interface name + "." + property name, and the values are #GValue instances.
   *
   * Read-only except during construction. If this is not provided
   * during construction, it is not guaranteed to be set until
   * tp_proxy_prepare_async() has finished preparing
   * %TP_CHANNEL_DISPATCH_OPERATION_FEATURE_CORE.
   *
   * Since: 0.11.UNRELEASED
   */
  param_spec = g_param_spec_boxed ("channel-dispatch-operation-properties",
      "Immutable D-Bus properties",
      "A map D-Bus interface + \".\" + property name => GValue",
      TP_HASH_TYPE_QUALIFIED_PROPERTY_VALUE_MAP,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class,
      PROP_CHANNEL_DISPATCH_OPERATION_PROPERTIES, param_spec);

 /**
   * TpChannelDispatchOperation::channel-lost: (skip)
   * @self: a #TpChannelDispatchOperation
   * @channel: the #TpChannel that closed
   * @domain: domain of a #GError indicating why the channel has been closed
   * @code: error code of a #GError indicating why the channel has been closed
   * @message: a message associated with the error
   *
   * Emitted when a channel has closed before it could be claimed or handled.
   *
   * Since: 0.11.UNRELEASED
   */
  signals[SIGNAL_CHANNEL_LOST] = g_signal_new (
      "channel-lost", G_OBJECT_CLASS_TYPE (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0,
      NULL, NULL,
      _tp_marshal_VOID__OBJECT_UINT_INT_STRING,
      G_TYPE_NONE, 4,
      TP_TYPE_CHANNEL, G_TYPE_UINT, G_TYPE_INT, G_TYPE_STRING);

  proxy_class->interface = TP_IFACE_QUARK_CHANNEL_DISPATCH_OPERATION;
  proxy_class->must_have_unique_name = TRUE;
  proxy_class->list_features = tp_channel_dispatch_operation_list_features;

  tp_channel_dispatch_operation_init_known_interfaces ();
}

/**
 * tp_channel_dispatch_operation_init_known_interfaces:
 *
 * Ensure that the known interfaces for TpChannelDispatchOperation have been
 * set up. This is done automatically when necessary, but for correct
 * overriding of library interfaces by local extensions, you should
 * call this function before calling
 * tp_proxy_or_subclass_hook_on_interface_add() with first argument
 * %TP_TYPE_CHANNEL_DISPATCH_OPERATION.
 *
 * Since: 0.7.32
 */
void
tp_channel_dispatch_operation_init_known_interfaces (void)
{
  static gsize once = 0;

  if (g_once_init_enter (&once))
    {
      GType tp_type = TP_TYPE_CHANNEL_DISPATCH_OPERATION;

      tp_proxy_init_known_interfaces ();
      tp_proxy_or_subclass_hook_on_interface_add (tp_type,
          tp_cli_channel_dispatch_operation_add_signals);
      tp_proxy_subclass_add_error_mapping (tp_type,
          TP_ERROR_PREFIX, TP_ERRORS, TP_TYPE_ERROR);

      g_once_init_leave (&once, 1);
    }
}

/**
 * tp_channel_dispatch_operation_new:
 * @bus_daemon: Proxy for the D-Bus daemon
 * @object_path: The non-NULL object path of this channel dispatch operation
 * @immutable_properties: As many as are known of the immutable D-Bus
 *  properties of this channel dispatch operation, or %NULL if none are known
 * @error: Used to raise an error if %NULL is returned
 *
 * Convenience function to create a new channel dispatch operation proxy.
 *
 * The @immutable_properties argument is not yet used.
 *
 * Returns: a new reference to an channel dispatch operation proxy, or %NULL if
 *    @object_path is not syntactically valid or the channel dispatcher is not
 *    running
 */
TpChannelDispatchOperation *
tp_channel_dispatch_operation_new (TpDBusDaemon *bus_daemon,
    const gchar *object_path,
    GHashTable *immutable_properties,
    GError **error)
{
  TpChannelDispatchOperation *self;
  gchar *unique_name;

  g_return_val_if_fail (bus_daemon != NULL, NULL);
  g_return_val_if_fail (object_path != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (!tp_dbus_check_valid_object_path (object_path, error))
    return NULL;

  if (!_tp_dbus_daemon_get_name_owner (bus_daemon, -1,
      TP_CHANNEL_DISPATCHER_BUS_NAME, &unique_name, error))
    return NULL;

  self = TP_CHANNEL_DISPATCH_OPERATION (g_object_new (
        TP_TYPE_CHANNEL_DISPATCH_OPERATION,
        "dbus-daemon", bus_daemon,
        "dbus-connection", ((TpProxy *) bus_daemon)->dbus_connection,
        "bus-name", unique_name,
        "object-path", object_path,
        "channel-dispatch-operation-properties", immutable_properties,
        NULL));

  g_free (unique_name);

  return self;
}

/**
 * TP_CHANNEL_DISPATCH_OPERATION_FEATURE_CORE:
 *
 * Expands to a call to a function that returns a quark for the "core" feature
 * on a #TpChannelDispatchOperation.
 *
 * When this feature is prepared, the basic properties of the
 * ChannelDispatchOperation have been retrieved and are available for use.
 *
 * Specifically, this implies that:
 *
 * - #TpChannelDispatchOperation:connection is set (but
 *   TP_CONNECTION_FEATURE_CORE is not necessarily prepared)
 * - #TpChannelDispatchOperation:account is set (but
 *   TP_ACCOUNT_FEATURE_CORE is not necessarily prepared)
 * - #TpChannelDispatchOperation:channels is set (but
 *   TP_CHANNEL_FEATURE_CORE is not necessarily prepared)
 * - #TpChannelDispatchOperation:possible-handlers is set
 * - any extra interfaces will have been set up in TpProxy (i.e.
 *   #TpProxy:interfaces contains at least all extra ChannelDispatchOperation
 *   interfaces)
 *
 * One can ask for a feature to be prepared using the
 * tp_proxy_prepare_async() function, and waiting for it to callback.
 *
 * Since: 0.11.UNRELEASED
 */
GQuark
tp_channel_dispatch_operation_get_feature_quark_core (void)
{
  return g_quark_from_static_string (
      "tp-channel-dispatch-operation-feature-core");
}

/**
 * tp_channel_dispatch_operation_borrow_connection:
 * @self: a #TpChannelDispatchOperation
 *
 * Returns the #TpConnection of this ChannelDispatchOperation.
 * The returned pointer is only valid while @self is valid - reference
 * it with g_object_ref() if needed.
 *
 * Returns: (transfer none): the value of #TpChannelDispatchOperation:connection
 *
 * Since: 0.11.UNRELEASED
 */
TpConnection *
tp_channel_dispatch_operation_borrow_connection (
    TpChannelDispatchOperation *self)
{
  return self->priv->connection;
}

/**
 * tp_channel_dispatch_operation_borrow_account:
 * @self: a #TpChannelDispatchOperation
 *
 * Returns the #TpAccount of this ChannelDispatchOperation.
 * The returned pointer is only valid while @self is valid - reference
 * it with g_object_ref() if needed.
 *
 * Returns: (transfer none): the value of #TpChannelDispatchOperation:account
 *
 * Since: 0.11.UNRELEASED
 */
TpAccount *
tp_channel_dispatch_operation_borrow_account (
    TpChannelDispatchOperation *self)
{
  return self->priv->account;
}

/**
 * tp_channel_dispatch_operation_borrow_channels:
 * @self: a #TpChannelDispatchOperation
 *
 * Returns a #GPtrArray containing the #TpChannel of this
 * ChannelDispatchOperation.
 * The returned array and its #TpChannel are only valid while @self is
 * valid - copy array and reference channels with g_object_ref() if needed.
 *
 * Returns: (transfer none): the value of #TpChannelDispatchOperation:channels
 *
 * Since: 0.11.UNRELEASED
 */
GPtrArray *
tp_channel_dispatch_operation_borrow_channels (
    TpChannelDispatchOperation *self)
{
  return self->priv->channels;
}

/**
 * tp_channel_dispatch_operation_borrow_possible_handlers:
 * @self: a #TpChannelDispatchOperation
 *
 * Returns a #GStrv containing the possible handlers of this
 * ChannelDispatchOperation.
 * The returned array and its strings are only valid while @self is
 * valid - copy it with g_strdupv if needed.
 *
 * Returns: (transfer none): the value of
 * #TpChannelDispatchOperation:possible-handlers
 *
 * Since: 0.11.UNRELEASED
 */
GStrv
tp_channel_dispatch_operation_borrow_possible_handlers (
    TpChannelDispatchOperation *self)
{
  return self->priv->possible_handlers;
}

/**
 * tp_channel_dispatch_operation_borrow_immutable_properties:
 * @self: a #TpChannelDispatchOperation
 *
 * Returns the immutable D-Bus properties of this channel.
 * The returned hash table is only valid while @self is valid - reference
 * it with g_hash_table_ref() if needed.
 *
 * Returns: (transfer none): the value of
 * #TpChannelDispatchOperation:channel-dispatch-operation-properties
 *
 * Since: 0.11.UNRELEASED
 */
GHashTable *
tp_channel_dispatch_operation_borrow_immutable_properties (
    TpChannelDispatchOperation *self)
{
  return self->priv->immutable_properties;
}

static void
handle_with_cb (TpChannelDispatchOperation *self,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *result = user_data;

  if (error != NULL)
    {
      DEBUG ("HandleWith failed: %s", error->message);
      g_simple_async_result_set_from_error (result, error);
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

/**
 * tp_channel_dispatch_operation_handle_with_async:
 * @self: a #TpChannelDispatchOperation
 * @handler: The well-known bus name (starting with #TP_CLIENT_BUS_NAME_BASE)
 * of the channel handler that should handle the channel, or %NULL
 * if the client has no preferred channel handler
 * @callback: a callback to call when the call returns
 * @user_data: data to pass to @callback
 *
 * Called by an approver to accept a channel bundle and request that the
 * given handler be used to handle it.
 *
 * If successful, this method will cause the #TpProxy::invalidated signal
 * to be emitted.
 *
 * However, this method may fail because the dispatch has already been
 * completed and the object has already gone. If this occurs, it indicates
 * that another approver has asked for the bundle to be handled by a
 * particular handler. The approver MUST NOT attempt to interact with
 * the channels further in this case, unless it is separately
 * invoked as the handler.
 *
 * Approvers which are also channel handlers SHOULD use
 * tp_channel_dispatch_operation_claim_async() instead
 * of tp_channel_dispatch_operation_handle_with_async() to request
 * that they can handle a channel bundle themselves.
 *
 * Since: 0.11.UNRELEASED
 */
void
tp_channel_dispatch_operation_handle_with_async (
    TpChannelDispatchOperation *self,
    const gchar *handler,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result;

  g_return_if_fail (TP_IS_CHANNEL_DISPATCH_OPERATION (self));

  result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, tp_channel_dispatch_operation_handle_with_async);

  tp_cli_channel_dispatch_operation_call_handle_with (self, -1,
      handler != NULL ? handler: "",
      handle_with_cb, result, NULL, G_OBJECT (self));
}

/**
 * tp_channel_dispatch_operation_handle_with_finish:
 * @self: a #TpChannelDispatchOperation
 * @result: a #GAsyncResult
 * @error: a #GError to fill
 *
 * Finishes an async call to HandleWith().
 *
 * Returns: %TRUE if the HandleWith() call was successful, otherwise %FALSE
 *
 * Since: 0.11.UNRELEASED
 */
gboolean
tp_channel_dispatch_operation_handle_with_finish (
    TpChannelDispatchOperation *self,
    GAsyncResult *result,
    GError **error)
{
  GSimpleAsyncResult *simple;

  g_return_val_if_fail (TP_IS_CHANNEL_DISPATCH_OPERATION (self), FALSE);
  g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (result), FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
        G_OBJECT (self), tp_channel_dispatch_operation_handle_with_async),
      FALSE);

  simple = G_SIMPLE_ASYNC_RESULT (result);

  if (g_simple_async_result_propagate_error (simple, error))
    return FALSE;

  return TRUE;
}

static void
claim_cb (TpChannelDispatchOperation *self,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *result = user_data;

  if (error != NULL)
    {
      DEBUG ("Claim failed: %s", error->message);
      g_simple_async_result_set_from_error (result, error);
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

/**
 * tp_channel_dispatch_operation_claim_async:
 * @self: a #TpChannelDispatchOperation
 * @callback: a callback to call when the call returns
 * @user_data: data to pass to @callback
 *
 * Called by an approver to claim channels for handling internally.
 * If this method is called successfully, the process calling this
 * method becomes the handler for the channel.
 *
 * If successful, this method will cause the #TpProxy::invalidated signal
 * to be emitted, in the same wayas for
 * tp_channel_dispatch_operation_handle_with_async().
 *
 * This method may fail because the dispatch operation has already
 * been completed. Again, see tp_channel_dispatch_operation_claim_async()
 * for more details. The approver MUST NOT attempt to interact with
 * the channels further in this case.
 *
 * Since: 0.11.UNRELEASED
 */
void
tp_channel_dispatch_operation_claim_async (
    TpChannelDispatchOperation *self,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result;

  g_return_if_fail (TP_IS_CHANNEL_DISPATCH_OPERATION (self));

  result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, tp_channel_dispatch_operation_claim_async);

  tp_cli_channel_dispatch_operation_call_claim (self, -1,
      claim_cb, result, NULL, G_OBJECT (self));
}

/**
 * tp_channel_dispatch_operation_claim_finish:
 * @self: a #TpChannelDispatchOperation
 * @result: a #GAsyncResult
 * @error: a #GError to fill
 *
 * Finishes an async call to Claim().
 *
 * Returns: %TRUE if the Claim() call was successful, otherwise %FALSE
 *
 * Since: 0.11.UNRELEASED
 */
gboolean
tp_channel_dispatch_operation_claim_finish (
    TpChannelDispatchOperation *self,
    GAsyncResult *result,
    GError **error)
{
  GSimpleAsyncResult *simple;

  g_return_val_if_fail (TP_IS_CHANNEL_DISPATCH_OPERATION (self), FALSE);
  g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (result), FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
        G_OBJECT (self), tp_channel_dispatch_operation_claim_async),
      FALSE);

  simple = G_SIMPLE_ASYNC_RESULT (result);

  if (g_simple_async_result_propagate_error (simple, error))
    return FALSE;

  return TRUE;
}
