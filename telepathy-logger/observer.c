/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Collabora Ltd.
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
 *
 * Authors: Cosimo Alfarano <cosimo.alfarano@collabora.co.uk>
 */

#include "config.h"
#include "observer-internal.h"

#include <glib.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/svc-client.h>
#include <telepathy-glib/account-manager.h>

#include <telepathy-logger/conf-internal.h>
#include <telepathy-logger/channel-internal.h>
#include <telepathy-logger/channel-factory-internal.h>
#include <telepathy-logger/log-manager.h>

#define DEBUG_FLAG TPL_DEBUG_OBSERVER
#include <telepathy-logger/action-chain-internal.h>
#include <telepathy-logger/debug-internal.h>
#include <telepathy-logger/util-internal.h>

/**
 * SECTION:observer
 * @title: TplObserver
 * @short_description: TPL Observer main class, used to handle received
 * signals
 * @see_also: #TpSvcClientObserver
 *
 * The Telepathy Logger's Observer implements
 * org.freedesktop.Telepathy.Client.Observer DBus interface and is called by
 * the Channel Dispatcher when a new channel is created, in order to log
 * received signals.
 *
 * Since: 0.1
 */

/**
 * TplObserver:
 *
 * The Telepathy Logger's Observer implements
 * org.freedesktop.Telepathy.Client.Observer DBus interface and is called by
 * the Channel Dispatcher when a new channel is created, in order to log
 * received signals using its #LogManager.
 *
 * This object is a signleton, any call to tpl_observer_new will return the
 * same object with an incremented reference counter. One has to
 * unreference the object properly when the used reference is not used
 * anymore.
 *
 * This object will register to it's DBus interface when
 * tpl_observer_register_dbus is called, ensuring that the registration will
 * happen only once per singleton instance.
 *
 * Since: 0.1
 */

/**
 * TplObserverClass:
 *
 * The class of a #TplObserver.
 */

static void tpl_observer_dispose (GObject * obj);
static void got_tpl_channel_text_ready_cb (GObject *obj, GAsyncResult *result,
    gpointer user_data);
static TplChannelFactory tpl_observer_get_channel_factory (TplObserver *self);
static GHashTable *tpl_observer_get_channel_map (TplObserver *self);

struct _TplObserverPriv
{
    /* mapping channel_path->TplChannel instances */
    GHashTable *channel_map;
    TplLogManager *logmanager;
    gboolean  dbus_registered;
    TplChannelFactory channel_factory;
};

typedef struct
{
  TplObserver *self;
  guint chan_n;
  TpObserveChannelsContext *ctx;
} ObservingContext;

static TplObserver *observer_singleton = NULL;

enum
{
  PROP_0,
  PROP_REGISTERED_CHANNELS
};

G_DEFINE_TYPE (TplObserver, tpl_observer, TP_TYPE_BASE_CLIENT)

static void
tpl_observer_observe_channels (TpBaseClient *client,
    TpAccount *account,
    TpConnection *connection,
    GList *channels,
    TpChannelDispatchOperation *dispatch_operation,
    GList *requests,
    TpObserveChannelsContext *context)
{
  TplObserver *self = TPL_OBSERVER (client);
  TplChannelFactory chan_factory;
  TplConf *conf;
  GError *error = NULL;
  ObservingContext *observing_ctx = NULL;
  const gchar *chan_type;
  GList *l;

  chan_factory = tpl_observer_get_channel_factory (self);

  /* Check if logging if enabled globally and for the given account_path,
   * return imemdiatly if it's not */
  conf = tpl_conf_dup ();
  if (!tpl_conf_is_globally_enabled (conf, &error))
    {
      if (error != NULL)
        DEBUG ("%s", error->message);
      else
        DEBUG ("Logging is globally disabled. Skipping channel logging.");

      goto error;
    }
  if (tpl_conf_is_account_ignored (conf, tp_proxy_get_object_path (account),
        &error))
    {
      DEBUG ("Logging is disabled for account %s. "
          "Channel associated to this account. "
          "Skipping this channel logging.", tp_proxy_get_object_path (account));

      goto error;
    }
  g_object_unref (conf);

  /* Parallelize TplChannel preparations, when the last one will be ready, the
   * counter will be 0 and tp_svc_client_observer_return_from_observe_channels
   * can be called */
  observing_ctx = g_slice_new0 (ObservingContext);
  observing_ctx->self = TPL_OBSERVER (self);
  observing_ctx->chan_n = g_list_length (channels);
  observing_ctx->ctx = g_object_ref (context);

  for (l = channels; l != NULL; l = g_list_next (l))
    {
      TpChannel *channel = l->data;
      TplChannel *tpl_chan;
      GHashTable *prop_map;
      const gchar *path;

      path = tp_proxy_get_object_path (channel);
      /* d.bus.propertyName.str/gvalue hash */
      prop_map = tp_channel_borrow_immutable_properties (channel);
      chan_type = tp_channel_get_channel_type (channel);

      tpl_chan = chan_factory (chan_type, connection, path, prop_map, account,
          &error);
      if (tpl_chan == NULL)
        {
          DEBUG ("%s", error->message);
          g_clear_error (&error);
          continue;
        }
      PATH_DEBUG (tpl_chan, "Starting preparation for TplChannel instance %p",
          tpl_chan);
      tpl_channel_call_when_ready (tpl_chan, got_tpl_channel_text_ready_cb,
          observing_ctx);
    }

  tp_observe_channels_context_delay (context);
  return;

error:
  g_clear_error (&error);

  DEBUG ("Returning from observe channels on error condition. "
      "Unable to log the channel");

  tp_observe_channels_context_accept (context);
}


static void
got_tpl_channel_text_ready_cb (GObject *obj,
    GAsyncResult *result,
    gpointer user_data)
{
  ObservingContext *observing_ctx = user_data;
  gboolean success = _tpl_action_chain_new_finish (result);

  if (success)
    {
      PATH_DEBUG (obj, "prepared channel");

      tpl_observer_register_channel (observing_ctx->self, TPL_CHANNEL (obj));
    }
  else
    {
      PATH_DEBUG (obj, "failed to prepare");
    }

  /* drop our ref */
  g_object_unref (obj);

  observing_ctx->chan_n -= 1;
  if (observing_ctx->chan_n == 0)
    {
      DEBUG ("Returning from observe channels");
      tp_observe_channels_context_accept (observing_ctx->ctx);
      g_object_unref (observing_ctx->ctx);
      g_slice_free (ObservingContext, observing_ctx);
    }
}


static void
tpl_observer_get_property (GObject *self,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  switch (property_id)
    {
      case PROP_REGISTERED_CHANNELS:
        {
          GPtrArray *array = g_ptr_array_new ();
          GList *keys, *ptr;

          keys = g_hash_table_get_keys (tpl_observer_get_channel_map (
                TPL_OBSERVER (self)));

          for (ptr = keys; ptr != NULL; ptr = ptr->next)
            {
              g_ptr_array_add (array, ptr->data);
            }

          g_value_set_boxed (value, array);

          g_ptr_array_free (array, TRUE);
          g_list_free (keys);

          break;
        }

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (self, property_id, pspec);
        break;
    }
}

/* Singleton Constructor */
static GObject *
tpl_observer_constructor (GType type,
    guint n_props,
    GObjectConstructParam *props)
{
  GObject *retval;

  if (observer_singleton)
    retval = g_object_ref (observer_singleton);
  else
    {
      retval = G_OBJECT_CLASS (tpl_observer_parent_class)->constructor (type,
          n_props, props);

      observer_singleton = TPL_OBSERVER (retval);
      g_object_add_weak_pointer (retval, (gpointer *) & observer_singleton);
    }

  return retval;
}


static void
tpl_observer_class_init (TplObserverClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  TpBaseClientClass *base_clt_cls = TP_BASE_CLIENT_CLASS (klass);

  object_class->constructor = tpl_observer_constructor;
  object_class->dispose = tpl_observer_dispose;
  object_class->get_property = tpl_observer_get_property;

  /**
   * TplObserver:registered-channels:
   *
   * A list of channel's paths currently registered to this object.
   *
   * One can receive change notifications on this property by connecting
   * to the #GObject::notify signal and using this property as the signal
   * detail.
   */
  g_object_class_install_property (object_class, PROP_REGISTERED_CHANNELS,
      g_param_spec_boxed ("registered-channels",
        "Registered Channels",
        "open TpChannels which the TplObserver is logging",
        TP_ARRAY_TYPE_OBJECT_PATH_LIST,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (object_class, sizeof (TplObserverPriv));

  tp_base_client_implement_observe_channels (base_clt_cls,
      tpl_observer_observe_channels);
}

static void
tpl_observer_init (TplObserver *self)
{
  TplObserverPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      TPL_TYPE_OBSERVER, TplObserverPriv);
  self->priv = priv;

  priv->channel_map = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, g_object_unref);
  priv->logmanager = tpl_log_manager_dup_singleton ();

  /* Observe contact text channels */
  tp_base_client_take_observer_filter (TP_BASE_CLIENT (self),
      tp_asv_new (
       TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
           TP_IFACE_CHANNEL_TYPE_TEXT,
       TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
           TP_HANDLE_TYPE_CONTACT,
      NULL));

  /* Observe room text channels */
  tp_base_client_take_observer_filter (TP_BASE_CLIENT (self),
      tp_asv_new (
       TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
           TP_IFACE_CHANNEL_TYPE_TEXT,
       TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
           TP_HANDLE_TYPE_ROOM,
      NULL));

  tp_base_client_set_observer_recover (TP_BASE_CLIENT (self), TRUE);
}


/**
 * tpl_observer_register_dbus:
 * @self: #TplObserver instance, cannot be %NULL.
 * @error: Used to raise an error if DBus registration fails
 *
 * Registers the object using #TPL_OBSERVER_WELL_KNOWN_BUS_NAME well known
 * name.
 *
 * Returns: %TRUE if the registration is successful, %FALSE with @error set if
 * it fails.
 */
gboolean
tpl_observer_register_dbus (TplObserver *self,
    GError **error)
{
  return tp_base_client_register (TP_BASE_CLIENT (self), error);
}


static void
tpl_observer_dispose (GObject *obj)
{
  TplObserverPriv *priv = TPL_OBSERVER (obj)->priv;

  if (priv->channel_map != NULL)
    {
      g_hash_table_unref (priv->channel_map);
      priv->channel_map = NULL;
    }
  if (priv->logmanager != NULL)
    {
      g_object_unref (priv->logmanager);
      priv->logmanager = NULL;
    }

  G_OBJECT_CLASS (tpl_observer_parent_class)->dispose (obj);
}


TplObserver *
tpl_observer_new (void)
{
  TpDBusDaemon *dbus;
  TplObserver *result;

  dbus = tp_dbus_daemon_dup (NULL);
  g_return_val_if_fail (dbus != NULL, NULL);

  result = g_object_new (TPL_TYPE_OBSERVER,
      "dbus-daemon", dbus,
      "name", "Logger",
      "uniquify-name", FALSE,
      NULL);

  g_object_unref (dbus);
  return result;
}


static GHashTable *
tpl_observer_get_channel_map (TplObserver *self)
{
  g_return_val_if_fail (TPL_IS_OBSERVER (self), NULL);

  return self->priv->channel_map;
}


gboolean
tpl_observer_register_channel (TplObserver *self,
    TplChannel *channel)
{
  GHashTable *glob_map = tpl_observer_get_channel_map (self);
  gchar *key;

  g_return_val_if_fail (TPL_IS_OBSERVER (self), FALSE);
  g_return_val_if_fail (TPL_IS_CHANNEL (channel), FALSE);
  g_return_val_if_fail (glob_map != NULL, FALSE);

  key = (char *) tp_proxy_get_object_path (G_OBJECT (channel));

  DEBUG ("Registering channel %s", key);

  g_hash_table_insert (glob_map, key, g_object_ref (channel));
  g_object_notify (G_OBJECT (self), "registered-channels");

  return TRUE;
}


/**
 * tpl_observer_unregister_channel:
 * @self: #TplObserver instance, cannot be %NULL.
 * @channel: a #TplChannel cast of a TplChannel subclass instance
 *
 * Un-registers a TplChannel subclass instance, i.e. TplChannelText instance,
 * as TplChannel instance.
 * It is supposed to be called when the Closed signal for a channel is
 * emitted or when an un-recoverable error during the life or a TplChannel
 * happens.
 *
 * Every time that a channel is registered or unregistered, a notification is
 * sent for the 'registered-channels' property.
 *
 * Returns: %TRUE if @channel is registered and can thus be un-registered or
 * %FALSE if the @channel is not currently among registered channels and thus
 * cannot be un-registered.
 */
gboolean
tpl_observer_unregister_channel (TplObserver *self,
    TplChannel *channel)
{
  GHashTable *glob_map = tpl_observer_get_channel_map (self);
  gboolean retval;
  gchar *key;

  g_return_val_if_fail (TPL_IS_OBSERVER (self), FALSE);
  g_return_val_if_fail (TPL_IS_CHANNEL (channel), FALSE);
  g_return_val_if_fail (glob_map != NULL, FALSE);

  key = (char *) tp_proxy_get_object_path (TP_PROXY (channel));

  DEBUG ("Unregistering channel path %s", key);

  /* this will destroy the associated value object: at this point
     the hash table reference should be the only one for the
     value's object
   */
  retval = g_hash_table_remove (glob_map, key);

  if (retval)
    g_object_notify (G_OBJECT (self), "registered-channels");

  return retval;
}


static TplChannelFactory
tpl_observer_get_channel_factory (TplObserver *self)
{
  g_return_val_if_fail (TPL_IS_OBSERVER (self), NULL);

  return self->priv->channel_factory;
}


void
tpl_observer_set_channel_factory (TplObserver *self,
    TplChannelFactory factory)
{
  g_return_if_fail (TPL_IS_OBSERVER (self));
  g_return_if_fail (factory != NULL);
  g_return_if_fail (self->priv->channel_factory == NULL);

  self->priv->channel_factory = factory;
}