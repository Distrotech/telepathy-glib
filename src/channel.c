/*
 * channel.c - Source for TpStreamEngineChannel
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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

#include <stdlib.h>

#include <libtelepathy/tp-conn.h>
#include <libtelepathy/tp-constants.h>
#include <libtelepathy/tp-chan.h>
#include <libtelepathy/tp-chan-type-streamed-media-gen.h>
#include <libtelepathy/tp-chan-iface-media-signalling-gen.h>
#include <libtelepathy/tp-media-stream-handler-gen.h>
#include <libtelepathy/tp-helpers.h>
#include <libtelepathy/tp-props-iface.h>

#include "common/telepathy-errors.h"

#include "channel.h"
#include "session.h"
#include "stream.h"
#include "types.h"

G_DEFINE_TYPE (TpStreamEngineChannel, tp_stream_engine_channel, G_TYPE_OBJECT);

#define CHANNEL_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), TP_STREAM_ENGINE_TYPE_CHANNEL, TpStreamEngineChannelPrivate))

typedef struct _TpStreamEngineChannelPrivate TpStreamEngineChannelPrivate;

struct _TpStreamEngineChannelPrivate
{
  TpChan *channel_proxy;
  DBusGProxy *streamed_media_proxy;

  TpStreamEngineStreamProperties props;

  gulong channel_destroy_handler;

  gchar *connection_path;
};

enum
{
  PROP_NAT_TRAVERSAL = 0,
  PROP_STUN_SERVER,
  PROP_STUN_PORT,
  PROP_GTALK_P2P_RELAY_TOKEN,
  NUM_TP_PROPERTIES
};

enum
{
  CLOSED,
  SIGNAL_COUNT
};

static guint signals[SIGNAL_COUNT] = {0};

static void
tp_stream_engine_channel_dispose (GObject *object)
{
  TpStreamEngineChannel *self = TP_STREAM_ENGINE_CHANNEL (object);
  TpStreamEngineChannelPrivate *priv = CHANNEL_PRIVATE (self);

  g_debug (G_STRFUNC);

  if (self->sessions)
    {
      guint i;

      for (i = 0; i < self->sessions->len; i++)
        g_object_unref (g_ptr_array_index (self->sessions, i));

      g_ptr_array_free (self->sessions, TRUE);
      self->sessions = NULL;
    }

  if (self->channel_path)
    {
      g_free (self->channel_path);
      self->channel_path = NULL;
    }

  if (priv->connection_path)
    {
      g_free (priv->connection_path);
      priv->connection_path = NULL;
    }

  if (priv->channel_proxy)
    {
      g_object_unref (priv->channel_proxy);
      priv->channel_proxy = NULL;
    }

  g_free (priv->props.nat_traversal);
  priv->props.nat_traversal = NULL;

  g_free (priv->props.stun_server);
  priv->props.stun_server = NULL;

  g_free (priv->props.relay_token);
  priv->props.relay_token = NULL;

  if (G_OBJECT_CLASS (tp_stream_engine_channel_parent_class)->dispose)
    G_OBJECT_CLASS (tp_stream_engine_channel_parent_class)->dispose (object);
}

static void
tp_stream_engine_channel_class_init (TpStreamEngineChannelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (TpStreamEngineChannelPrivate));

  object_class->dispose = tp_stream_engine_channel_dispose;

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
tp_stream_engine_channel_init (TpStreamEngineChannel *self)
{
/*  TpStreamEngineChannelPrivate *priv = CHANNEL_PRIVATE (self); */

  self->sessions = g_ptr_array_new ();
}

static void
add_session (TpStreamEngineChannel *self,
             const gchar *session_handler_path,
             const gchar *type)
{
  TpStreamEngineChannelPrivate *priv = CHANNEL_PRIVATE (self);
  TpStreamEngineSession *session;
  gchar *bus_name;

  g_debug("adding session handler %s, type %s", session_handler_path,
    type);

  g_object_get (priv->channel_proxy, "name", &bus_name, NULL);

  session = g_object_new (TP_STREAM_ENGINE_TYPE_SESSION, NULL);

  if (!tp_stream_engine_session_go (session, bus_name, priv->connection_path,
      session_handler_path, self->channel_path, type, &(priv->props)))
    {
      g_critical ("couldn't create session");
    }

  g_ptr_array_add (self->sessions, session);

  g_free (bus_name);
}

static void
new_media_session_handler (DBusGProxy *proxy,
                           const char *session_handler_path,
                           const gchar* type,
                           gpointer user_data)
{
  TpStreamEngineChannel *self = TP_STREAM_ENGINE_CHANNEL (user_data);
  add_session (self, session_handler_path, type);
}

static void channel_closed (DBusGProxy *proxy, gpointer user_data);

static void
shutdown_channel (TpStreamEngineChannel *self)
{
  TpStreamEngineChannelPrivate *priv = CHANNEL_PRIVATE (self);

  if (priv->channel_proxy)
    {
      if (priv->channel_destroy_handler)
        {
          g_signal_handler_disconnect (
            priv->channel_proxy, priv->channel_destroy_handler);
          priv->channel_destroy_handler = 0;
        }

      if (priv->streamed_media_proxy)
        {
          DBusGProxy *media_signalling;

          media_signalling = tp_chan_get_interface (priv->channel_proxy,
              TELEPATHY_CHAN_IFACE_MEDIA_SIGNALLING_QUARK);
          g_assert (media_signalling);

          g_debug ("%s: disconnecting signals from media_signalling proxy",
              G_STRFUNC);

          dbus_g_proxy_disconnect_signal (media_signalling,
              "NewSessionHandler", G_CALLBACK (new_media_session_handler),
              self);
        }

      if (priv->channel_proxy)
        {
          g_debug ("%s: disconnecting signals from channel_proxy", G_STRFUNC);

          dbus_g_proxy_disconnect_signal (DBUS_G_PROXY (priv->channel_proxy),
              "Closed", G_CALLBACK (channel_closed), self);
        }
    }

  g_signal_emit (self, signals[CLOSED], 0);
}

static void
channel_closed (DBusGProxy *proxy, gpointer user_data)
{
  TpStreamEngineChannel *self = TP_STREAM_ENGINE_CHANNEL (user_data);

  g_debug ("connection manager channel closed");

  shutdown_channel (self);
}

static void
channel_destroyed (DBusGProxy *proxy, gpointer user_data)
{
  TpStreamEngineChannel *self = TP_STREAM_ENGINE_CHANNEL (user_data);
  TpStreamEngineChannelPrivate *priv = CHANNEL_PRIVATE (self);

  if (priv->channel_proxy)
    {
      TpChan *tmp;

      g_debug ("connection manager channel destroyed");

      /* We shouldn't try to use the channel proxy any more. */
      tmp = priv->channel_proxy;
      priv->channel_proxy = NULL;
      g_object_unref (tmp);

      shutdown_channel (self);
    }
}

void
get_session_handlers_reply (DBusGProxy *proxy,
                            GPtrArray *session_handlers,
                            GError *error,
                            gpointer user_data)
{
  TpStreamEngineChannel *self = TP_STREAM_ENGINE_CHANNEL (user_data);
  guint i;

  if (error)
    g_critical ("Error calling GetSessionHandlers: %s", error->message);

  if (session_handlers->len == 0)
    {
      g_debug ("GetSessionHandlers returned 0 sessions");
    }
  else
    {
      g_debug ("GetSessionHandlers replied: ");

      for (i = 0; i < session_handlers->len; i++)
        {
          GValueArray *session = g_ptr_array_index (session_handlers, i);
          GValue *obj = g_value_array_get_nth (session, 0);
          GValue *type = g_value_array_get_nth (session, 1);

          g_assert (G_VALUE_TYPE (obj) == DBUS_TYPE_G_OBJECT_PATH);
          g_assert (G_VALUE_HOLDS_STRING (type));

          g_debug ("  - session %s", (char *)g_value_get_boxed (obj));
          g_debug ("    type %s", g_value_get_string (type));

          add_session (self,
              g_value_get_boxed (obj), g_value_get_string (type));
        }
    }
}

static const gchar *prop_names[NUM_TP_PROPERTIES] = {
    "nat-traversal",
    "stun-server",
    "stun-port",
    "gtalk-p2p-relay-token"
};

static void
update_prop_str (TpPropsIface *iface,
                 guint prop_id,
                 gchar **value)
{
  GValue tmp = {0, };

  g_free (*value);
  *value = NULL;

  if (tp_props_iface_property_flags (iface, prop_id) & TP_PROPERTY_FLAG_READ)
    {
      g_value_init (&tmp, G_TYPE_STRING);

      if (tp_props_iface_get_value (iface, prop_id, &tmp))
        {
          *value = g_value_dup_string (&tmp);
          g_debug ("got %s = %s", prop_names[prop_id], *value);
        }

      g_value_unset (&tmp);
    }
}

static void
update_prop_uint (TpPropsIface *iface,
                  guint prop_id,
                  guint16 *value)
{
  GValue tmp = {0, };

  *value = 0;

  if (tp_props_iface_property_flags (iface, prop_id) & TP_PROPERTY_FLAG_READ)
    {
      g_value_init (&tmp, G_TYPE_UINT);

      if (tp_props_iface_get_value (iface, prop_id, &tmp))
        {
          *value = g_value_get_uint (&tmp);
          g_debug ("got %s = %u", prop_names[prop_id], *value);
        }

      g_value_unset (&tmp);
    }
}

static void
update_prop (TpPropsIface *iface,
             TpStreamEngineStreamProperties *props,
             guint prop_id)
{
  switch (prop_id)
    {
    case PROP_NAT_TRAVERSAL:
      update_prop_str (iface, PROP_NAT_TRAVERSAL, &(props->nat_traversal));
      break;
    case PROP_STUN_SERVER:
      update_prop_str (iface, PROP_STUN_SERVER, &(props->stun_server));
      break;
    case PROP_STUN_PORT:
      update_prop_uint (iface, PROP_STUN_PORT, &(props->stun_port));
      break;
    case PROP_GTALK_P2P_RELAY_TOKEN:
      update_prop_str (iface, PROP_GTALK_P2P_RELAY_TOKEN,
          &(props->relay_token));
      break;
    default:
      g_debug ("%s: ignoring unknown property id %u", G_STRFUNC, prop_id);
    }
}

static void
cb_property_changed (TpPropsIface *iface,
                     guint prop_id,
                     TpPropsChanged changed,
                     gpointer user_data)
{
  TpStreamEngineChannel *self = TP_STREAM_ENGINE_CHANNEL (user_data);
  TpStreamEngineChannelPrivate *priv = CHANNEL_PRIVATE (self);
  TpStreamEngineStreamProperties *props = &(priv->props);

  update_prop (iface, props, prop_id);
}

static void
cb_properties_ready (TpPropsIface *iface,
                     gpointer user_data)
{
  TpStreamEngineChannel *self = TP_STREAM_ENGINE_CHANNEL (user_data);
  TpStreamEngineChannelPrivate *priv = CHANNEL_PRIVATE (self);
  TpStreamEngineStreamProperties *props = &(priv->props);
  guint i;

  for (i = 0; i < NUM_TP_PROPERTIES; i++)
    update_prop (iface, props, i);

  g_signal_handlers_disconnect_by_func (iface,
      G_CALLBACK (cb_properties_ready), self);

  g_signal_connect (iface, "properties-changed",
      G_CALLBACK (cb_property_changed), self);
}

gboolean
tp_stream_engine_channel_go (
  TpStreamEngineChannel *self,
  const gchar *bus_name,
  const gchar *connection_path,
  const gchar *channel_path,
  guint handle_type,
  guint handle,
  GError **error)
{
  TpStreamEngineChannelPrivate *priv = CHANNEL_PRIVATE (self);
  TpPropsIface *props;
  DBusGProxy *media_signalling;

  g_assert (NULL == priv->channel_proxy);

  self->channel_path = g_strdup (channel_path);

  priv->connection_path = g_strdup (connection_path);

  priv->channel_proxy = tp_chan_new (
    tp_get_bus(),                              /* connection  */
    bus_name,                                  /* bus_name    */
    channel_path,                              /* object_name */
    TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,      /* type        */
    handle_type,                               /* handle_type */
    handle);                                   /* handle      */

  if (!priv->channel_proxy)
    {
      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                            "Unable to bind to channel");
      return FALSE;
    }

  priv->channel_destroy_handler = g_signal_connect (
    priv->channel_proxy, "destroy", G_CALLBACK (channel_destroyed), self);

  dbus_g_proxy_connect_signal (DBUS_G_PROXY (priv->channel_proxy), "Closed",
                               G_CALLBACK (channel_closed),
                               self, NULL);

  props = (TpPropsIface *) tp_chan_get_interface (priv->channel_proxy,
      TELEPATHY_PROPS_IFACE_QUARK);

  /* fail gracefully if there's no properties interface */
  if (props != NULL)
    {
      tp_props_iface_set_mapping (props,
          "nat-traversal", PROP_NAT_TRAVERSAL,
          "stun-server", PROP_STUN_SERVER,
          "stun-port", PROP_STUN_PORT,
          "gtalk-p2p-relay-token", PROP_GTALK_P2P_RELAY_TOKEN,
          NULL);

      g_signal_connect (props, "properties-ready",
          G_CALLBACK (cb_properties_ready), self);
    }

  priv->streamed_media_proxy = tp_chan_get_interface (priv->channel_proxy,
      TELEPATHY_CHAN_IFACE_STREAMED_QUARK);

  if (!priv->streamed_media_proxy)
    {
      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                            "Channel doesn't have StreamedMedia interface");
      return FALSE;
    }

  media_signalling = tp_chan_get_interface (priv->channel_proxy,
    TELEPATHY_CHAN_IFACE_MEDIA_SIGNALLING_QUARK);
  g_assert (media_signalling);

  dbus_g_proxy_connect_signal (media_signalling,
      "NewSessionHandler", G_CALLBACK (new_media_session_handler),
      self, NULL);

  tp_chan_iface_media_signalling_get_session_handlers_async (
        media_signalling, get_session_handlers_reply, self);

  return TRUE;
}

TpStreamEngineChannel*
tp_stream_engine_channel_new (void)
{
  return g_object_new (TP_STREAM_ENGINE_TYPE_CHANNEL, NULL);
}

void tp_stream_engine_channel_error (
  TpStreamEngineChannel *self,
  guint error,
  const gchar *message)
{
  guint i, j;

  for (i = 0; i < self->sessions->len; i++)
    {
      TpStreamEngineSession *session = g_ptr_array_index (self->sessions, i);
      for (j = 0; j < session->streams->len; j++)
        {
          tp_stream_engine_stream_error (
              g_ptr_array_index (session->streams, j),
              TP_MEDIA_STREAM_ERROR_UNKNOWN, message);
        }
    }

  shutdown_channel (self);
}

