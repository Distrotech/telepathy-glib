/*
 * channel-request.c - proxy for a request to the Telepathy channel dispatcher
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

#include "telepathy-glib/channel-request.h"

#include <telepathy-glib/automatic-proxy-factory.h>
#include <telepathy-glib/channel.h>
#include <telepathy-glib/connection.h>
#include <telepathy-glib/defs.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/proxy-subclass.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG TP_DEBUG_DISPATCHER
#include "telepathy-glib/dbus-internal.h"
#include "telepathy-glib/debug-internal.h"

#include "telepathy-glib/_gen/tp-cli-channel-request-body.h"

#include "_gen/signals-marshal.h"

/**
 * SECTION:channel-request
 * @title: TpChannelRequest
 * @short_description: proxy object for a request to the Telepathy channel
 *  dispatcher
 * @see_also: #TpChannelDispatcher
 *
 * Requesting a channel from the channel dispatcher can take some time, so an
 * object is created in the channel dispatcher to represent each request.
 * Objects of the #TpChannelRequest class provide access to one of those
 * objects.
 */

/**
 * TpChannelRequest:
 *
 * Requesting a channel from the channel dispatcher can take some time, so an
 * object is created in the channel dispatcher to represent each request. This
 * proxy represents one of those objects.
 *
 * Any client can call tp_cli_channel_request_call_cancel() at any time to
 * attempt to cancel the request.
 *
 * On success, the #TpChannelRequest::succeeded signal will be emitted.
 * Immediately after that, the #TpProxy::invalidated signal will be emitted,
 * with the domain %TP_DBUS_ERRORS and the error code
 * %TP_DBUS_ERROR_OBJECT_REMOVED (this is not an error condition, it merely
 * indicates that the channel request no longer exists).
 *
 * On failure, the #TpProxy::invalidated signal will be emitted with some
 * other suitable error, usually from the %TP_ERRORS domain.
 *
 * If the channel dispatcher crashes or exits, the #TpProxy::invalidated
 * signal will be emitted with the domain %TP_DBUS_ERRORS and the error code
 * %TP_DBUS_ERROR_NAME_OWNER_LOST.
 *
 * This proxy is usable but incomplete: accessors for the Account,
 * UserActionTime, PreferredHandler, Requests and Interfaces properties will
 * be added in a later version of telepathy-glib, along with a mechanism
 * similar to tp_connection_call_when_ready().
 *
 * Until suitable convenience methods are implemented, the generic
 * tp_cli_dbus_properties_call_get_all() method can be used to get those
 * properties.
 *
 * Since: 0.7.32
 */

/**
 * TpChannelRequestClass:
 *
 * The class of a #TpChannelRequest.
 */

enum {
  SIGNAL_SUCCEEDED,
  SIGNAL_SUCCEEDED_WITH_CHANNEL,
  N_SIGNALS
};

enum {
  PROP_CHANNEL_FACTORY = 1
};

static guint signals[N_SIGNALS] = { 0 };

struct _TpChannelRequestPrivate {
    TpClientChannelFactory *channel_factory;
};

G_DEFINE_TYPE (TpChannelRequest, tp_channel_request, TP_TYPE_PROXY)

static void
tp_channel_request_init (TpChannelRequest *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, TP_TYPE_CHANNEL_REQUEST,
      TpChannelRequestPrivate);
}

static void
tp_channel_request_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  TpChannelRequest *self = TP_CHANNEL_REQUEST (object);

  switch (property_id)
    {
      case PROP_CHANNEL_FACTORY:
        tp_clear_object (&self->priv->channel_factory);
        self->priv->channel_factory = g_value_dup_object (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
  }
}

static void
tp_channel_request_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  TpChannelRequest *self = TP_CHANNEL_REQUEST (object);

  switch (property_id)
    {
      case PROP_CHANNEL_FACTORY:
        g_value_set_object (value, self->priv->channel_factory);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
  }
}

static void
tp_channel_request_failed_cb (TpChannelRequest *self,
    const gchar *error_name,
    const gchar *message,
    gpointer unused G_GNUC_UNUSED,
    GObject *object G_GNUC_UNUSED)
{
  GError *error = NULL;

  tp_proxy_dbus_error_to_gerror (self, error_name, message, &error);
  tp_proxy_invalidate ((TpProxy *) self, error);
  g_error_free (error);
}

static void
tp_channel_request_succeeded_cb (TpChannelRequest *self,
    gpointer unused G_GNUC_UNUSED,
    GObject *object G_GNUC_UNUSED)
{
  GError e = { TP_DBUS_ERRORS, TP_DBUS_ERROR_OBJECT_REMOVED,
      "ChannelRequest succeeded and was removed" };

  g_signal_emit (self, signals[SIGNAL_SUCCEEDED], 0);
  tp_proxy_invalidate ((TpProxy *) self, &e);
}

static void
tp_channel_request_succeeded_with_channel_cb (TpChannelRequest *self,
    const gchar *conn_path,
    GHashTable *conn_props,
    const gchar *chan_path,
    GHashTable *chan_props,
    gpointer unused G_GNUC_UNUSED,
    GObject *object G_GNUC_UNUSED)
{
  TpDBusDaemon *dbus;
  TpConnection *connection;
  TpChannel *channel;
  GError *error = NULL;

  dbus = tp_proxy_get_dbus_daemon (self);

  connection = tp_connection_new (dbus, NULL, conn_path, &error);
  if (connection == NULL)
    {
      DEBUG ("Failed to create TpConnection: %s", error->message);
      g_error_free (error);
      return;
    }

  channel = tp_client_channel_factory_create_channel (
      self->priv->channel_factory, connection, chan_path, chan_props, &error);
  if (channel == NULL)
    {
      DEBUG ("Failed to create TpChannel: %s", error->message);
      g_error_free (error);
      g_object_unref (channel);
      return;
    }

  g_signal_emit (self, signals[SIGNAL_SUCCEEDED_WITH_CHANNEL], 0,
      connection, channel);

  g_object_unref (connection);
  g_object_unref (channel);
}

static void
tp_channel_request_constructed (GObject *object)
{
  TpChannelRequest *self = TP_CHANNEL_REQUEST (object);
  void (*chain_up) (GObject *) =
    ((GObjectClass *) tp_channel_request_parent_class)->constructed;
  GError *error = NULL;
  TpProxySignalConnection *sc;

  if (chain_up != NULL)
    chain_up (object);

  g_return_if_fail (tp_proxy_get_dbus_daemon (self) != NULL);

  if (self->priv->channel_factory == NULL)
    {
      self->priv->channel_factory = TP_CLIENT_CHANNEL_FACTORY (
          tp_automatic_proxy_factory_dup ());
    }

  sc = tp_cli_channel_request_connect_to_failed (self,
      tp_channel_request_failed_cb, NULL, NULL, NULL, &error);

  if (sc == NULL)
    {
      CRITICAL ("Couldn't connect to Failed: %s", error->message);
      g_error_free (error);
      g_assert_not_reached ();
      return;
    }

  sc = tp_cli_channel_request_connect_to_succeeded (self,
      tp_channel_request_succeeded_cb, NULL, NULL, NULL, &error);

  if (sc == NULL)
    {
      CRITICAL ("Couldn't connect to Succeeded: %s", error->message);
      g_error_free (error);
      g_assert_not_reached ();
      return;
    }

  sc = tp_cli_channel_request_connect_to_succeeded_with_channel (self,
      tp_channel_request_succeeded_with_channel_cb, NULL, NULL, NULL, &error);

  if (sc == NULL)
    {
      DEBUG ("Couldn't connect to SucceededWithChannel: %s", error->message);
      g_error_free (error);
      return;
    }
}

static void
tp_channel_request_dispose (GObject *object)
{
  TpChannelRequest *self = TP_CHANNEL_REQUEST (object);
  void (*dispose) (GObject *) =
    G_OBJECT_CLASS (tp_channel_request_parent_class)->dispose;

  tp_clear_object (&self->priv->channel_factory);

  if (dispose != NULL)
    dispose (object);
}

static void
tp_channel_request_class_init (TpChannelRequestClass *klass)
{
  TpProxyClass *proxy_class = (TpProxyClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (TpChannelRequestPrivate));

  object_class->set_property = tp_channel_request_set_property;
  object_class->get_property = tp_channel_request_get_property;
  object_class->constructed = tp_channel_request_constructed;
  object_class->dispose = tp_channel_request_dispose;

  proxy_class->interface = TP_IFACE_QUARK_CHANNEL_REQUEST;
  tp_channel_request_init_known_interfaces ();
  proxy_class->must_have_unique_name = TRUE;

  /**
   * TpChannelRequest:channel-factory:
   *
   * The object implementing the #TpClientChannelFactoryInterface interface
   * that will be used to create channel proxies when the
   * #TpChannelRequest::succeeded-with-channel signal is fired.
   * This property can be changed using
   * tp_channel_request_set_channel_factory().
   *
   * If no channel factory is specified then #TpAutomaticProxyFactory is used.
   *
   * Since: 0.13.UNRELEASED
   */
  param_spec = g_param_spec_object ("channel-factory", "Channel factory",
      "Object implementing TpClientChannelFactoryInterface",
      G_TYPE_OBJECT,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CHANNEL_FACTORY,
      param_spec);

  /**
   * TpChannelRequest::succeeded:
   * @self: the channel request proxy
   *
   * Emitted when the channel request succeeds.
   */
  signals[SIGNAL_SUCCEEDED] = g_signal_new ("succeeded",
      G_OBJECT_CLASS_TYPE (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  /**
   * TpChannelRequest::succeeded-with-channel:
   * @self: the channel request proxy
   * @connection: the #TpConnection of @channel
   * @channel: the #TpChannel created
   *
   * Variant of the #TpChannelRequest::succeeded signal allowing to get
   * the channel which has been created.
   *
   * Note that this signal can not be fired if your telepathy-mission-control
   * is too old.
   *
   * The #TpChannel is created using #TpChannelRequest:channel-factory but
   * the features of the factory are NOT prepared. It's up to the user to
   * prepare the features returned by
   * tp_client_channel_factory_dup_channel_features() himself.
   *
   * Since: 0.13.UNRELEASED
   */
  signals[SIGNAL_SUCCEEDED_WITH_CHANNEL] = g_signal_new (
      "succeeded-with-channel",
      G_OBJECT_CLASS_TYPE (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0,
      NULL, NULL,
      _tp_marshal_VOID__OBJECT_OBJECT,
      G_TYPE_NONE, 2, TP_TYPE_CONNECTION, TP_TYPE_CHANNEL);
}

/**
 * tp_channel_request_init_known_interfaces:
 *
 * Ensure that the known interfaces for TpChannelRequest have been set up.
 * This is done automatically when necessary, but for correct
 * overriding of library interfaces by local extensions, you should
 * call this function before calling
 * tp_proxy_or_subclass_hook_on_interface_add() with first argument
 * %TP_TYPE_CHANNEL_REQUEST.
 *
 * Since: 0.7.32
 */
void
tp_channel_request_init_known_interfaces (void)
{
  static gsize once = 0;

  if (g_once_init_enter (&once))
    {
      GType tp_type = TP_TYPE_CHANNEL_REQUEST;

      tp_proxy_init_known_interfaces ();
      tp_proxy_or_subclass_hook_on_interface_add (tp_type,
          tp_cli_channel_request_add_signals);
      tp_proxy_subclass_add_error_mapping (tp_type,
          TP_ERROR_PREFIX, TP_ERRORS, TP_TYPE_ERROR);

      g_once_init_leave (&once, 1);
    }
}

/**
 * tp_channel_request_new:
 * @bus_daemon: Proxy for the D-Bus daemon
 * @object_path: The non-NULL object path of this channel request
 * @immutable_properties: As many as are known of the immutable D-Bus
 *  properties of this channel request, or %NULL if none are known
 * @error: Used to raise an error if %NULL is returned
 *
 * Convenience function to create a new channel request proxy.
 *
 * If the channel request was newly created, the client making the request
 * is responsible for calling tp_cli_channel_request_call_proceed() when it
 * is ready for the channel request to proceed.
 *
 * The @immutable_properties argument is not yet used.
 *
 * Returns: a new reference to an channel request proxy, or %NULL if
 *    @object_path is not syntactically valid or the channel dispatcher is
 *    not running
 */
TpChannelRequest *
tp_channel_request_new (TpDBusDaemon *bus_daemon,
    const gchar *object_path,
    GHashTable *immutable_properties G_GNUC_UNUSED,
    GError **error)
{
  TpChannelRequest *self;
  gchar *unique_name;

  g_return_val_if_fail (bus_daemon != NULL, NULL);
  g_return_val_if_fail (object_path != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (!tp_dbus_check_valid_object_path (object_path, error))
    return NULL;

  if (!_tp_dbus_daemon_get_name_owner (bus_daemon, -1,
      TP_CHANNEL_DISPATCHER_BUS_NAME, &unique_name, error))
    return NULL;

  self = TP_CHANNEL_REQUEST (g_object_new (TP_TYPE_CHANNEL_REQUEST,
        "dbus-daemon", bus_daemon,
        "dbus-connection", ((TpProxy *) bus_daemon)->dbus_connection,
        "bus-name", unique_name,
        "object-path", object_path,
        NULL));

  g_free (unique_name);

  return self;
}

/**
 * tp_channel_request_set_channel_factory:
 * @self: a #TpChannelRequest
 * @factory: an object implementing the #TpClientChannelFactoryInterface
 * interface
 *
 * Change the value of the #TpChannelRequest:channel-factory property.
 *
 * Since: 0.13.UNRELEASED
 */
void
tp_channel_request_set_channel_factory (TpChannelRequest *self,
    TpClientChannelFactory *factory)
{
  tp_clear_object (&self->priv->channel_factory);
  self->priv->channel_factory = g_object_ref (factory);
  g_object_notify (G_OBJECT (self), "channel-factory");
}
