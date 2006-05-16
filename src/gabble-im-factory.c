/*
 * im-factory.c - Source for GabbleImFactory
 * Copyright (C) 2006 Collabora Ltd.
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

#define DBUS_API_SUBJECT_TO_CHANGE
#define _GNU_SOURCE /* Needed for strptime (_XOPEN_SOURCE can also be used). */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <loudmouth/loudmouth.h>

#include "telepathy-interfaces.h"
#include "tp-channel-factory-iface.h"
#include "gabble-im-factory.h"
#include "gabble-connection.h"
#include "gabble-im-channel.h"
#include "handles.h"

#include "gabble-text-mixin.h"

static void gabble_im_factory_iface_init (gpointer g_iface, gpointer iface_data);
static LmHandlerResult im_factory_message_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleImFactory, gabble_im_factory, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE, gabble_im_factory_iface_init));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

typedef struct _GabbleImFactoryPrivate GabbleImFactoryPrivate;
struct _GabbleImFactoryPrivate
{
  GabbleConnection *conn;
  LmMessageHandler *message_cb;
  GHashTable *channels;

  gboolean dispose_has_run;
};

#define GABBLE_IM_FACTORY_GET_PRIVATE(o)    (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_IM_FACTORY, GabbleImFactoryPrivate))

static GObject *gabble_im_factory_constructor (GType type, guint n_props, GObjectConstructParam *props);

static void
gabble_im_factory_init (GabbleImFactory *fac)
{
  GabbleImFactoryPrivate *priv = GABBLE_IM_FACTORY_GET_PRIVATE (fac);

  priv->channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                          NULL, g_object_unref);

  priv->message_cb = NULL;

  priv->conn = NULL;
  priv->dispose_has_run = FALSE;
}

static GObject *
gabble_im_factory_constructor (GType type, guint n_props,
                               GObjectConstructParam *props)
{
  GObject *obj;
  GabbleImFactoryPrivate *priv;

  obj = G_OBJECT_CLASS (gabble_im_factory_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_IM_FACTORY_GET_PRIVATE (obj);

  g_assert(priv->conn != NULL);
  g_assert(priv->conn->lmconn != NULL);

  priv->message_cb = lm_message_handler_new (im_factory_message_cb, obj, NULL);
  lm_connection_register_message_handler (priv->conn->lmconn, priv->message_cb,
                                          LM_MESSAGE_TYPE_MESSAGE,
                                          LM_HANDLER_PRIORITY_NORMAL);

  return obj;
}


static void
gabble_im_factory_dispose (GObject *object)
{
  GabbleImFactory *fac = GABBLE_IM_FACTORY (object);
  GabbleImFactoryPrivate *priv = GABBLE_IM_FACTORY_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  g_debug ("%s: dispose called", G_STRFUNC);
  priv->dispose_has_run = TRUE;

  tp_channel_factory_iface_close_all (TP_CHANNEL_FACTORY_IFACE (object));
  g_assert (priv->channels == NULL);

  if (G_OBJECT_CLASS (gabble_im_factory_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_im_factory_parent_class)->dispose (object);
}

static void
gabble_im_factory_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GabbleImFactory *fac = GABBLE_IM_FACTORY (object);
  GabbleImFactoryPrivate *priv = GABBLE_IM_FACTORY_GET_PRIVATE (fac);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_im_factory_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  GabbleImFactory *fac = GABBLE_IM_FACTORY (object);
  GabbleImFactoryPrivate *priv = GABBLE_IM_FACTORY_GET_PRIVATE (fac);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_im_factory_class_init (GabbleImFactoryClass *gabble_im_factory_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_im_factory_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_im_factory_class, sizeof (GabbleImFactoryPrivate));

  object_class->constructor = gabble_im_factory_constructor;
  object_class->dispose = gabble_im_factory_dispose;

  object_class->get_property = gabble_im_factory_get_property;
  object_class->set_property = gabble_im_factory_set_property;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "IM channel factory object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

}

static GabbleIMChannel *new_im_channel (GabbleImFactory *fac, GabbleHandle handle);

static void im_channel_closed_cb (GabbleIMChannel *chan, gpointer user_data);


/**
 * im_factory_message_cb:
 *
 * Called by loudmouth when we get an incoming <message>.
 */
static LmHandlerResult
im_factory_message_cb (LmMessageHandler *handler,
                       LmConnection *lmconn,
                       LmMessage *message,
                       gpointer user_data)
{
  GabbleImFactory *fac = GABBLE_IM_FACTORY (user_data);
  GabbleImFactoryPrivate *priv = GABBLE_IM_FACTORY_GET_PRIVATE (fac);

  const gchar *from, *body, *body_offset;
  time_t stamp;
  TpChannelTextMessageType msgtype;
  GabbleHandle handle;
  GabbleIMChannel *chan;

  if (!gabble_text_mixin_parse_incoming_message (message, &from, &stamp, &msgtype, &body, &body_offset))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  if (body == NULL)
    {
      HANDLER_DEBUG (message->node, "got a message without a body field, ignoring");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  handle = gabble_handle_for_contact (priv->conn->handles, from, FALSE);
  if (handle == 0)
    {
      HANDLER_DEBUG (message->node, "ignoring message node from malformed jid");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  g_debug ("%s: message from %s (handle %u), msgtype %d, body:\n%s",
           G_STRFUNC, from, handle, msgtype, body_offset);

  chan = g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle));

  if (chan == NULL)
    {
      g_debug ("%s: found no IM channel, creating one", G_STRFUNC);

      chan = new_im_channel (fac, handle);
    }

  if (_gabble_im_channel_receive (chan, msgtype, handle,
                                  stamp, body_offset))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

/**
 * im_channel_closed_cb:
 *
 * Signal callback for when an IM channel is closed. Removes the references
 * that #GabbleConnection holds to them.
 */
static void
im_channel_closed_cb (GabbleIMChannel *chan, gpointer user_data)
{
  GabbleImFactory *conn = GABBLE_IM_FACTORY (user_data);
  GabbleImFactoryPrivate *priv = GABBLE_IM_FACTORY_GET_PRIVATE (conn);
  GabbleHandle contact_handle;

  g_object_get (chan, "handle", &contact_handle, NULL);

  g_debug ("%s: removing channel with handle %d", G_STRFUNC, contact_handle);
  g_hash_table_remove (priv->channels, GINT_TO_POINTER (contact_handle));
}

/**
 * new_im_channel
 */
static GabbleIMChannel *
new_im_channel (GabbleImFactory *fac, GabbleHandle handle)
{
  GabbleImFactoryPrivate *priv;
  GabbleIMChannel *chan;
  char *object_path;

  g_assert (GABBLE_IS_IM_FACTORY (fac));

  priv = GABBLE_IM_FACTORY_GET_PRIVATE (fac);

  object_path = g_strdup_printf ("%s/ImChannel%u", priv->conn->object_path, handle);

  chan = g_object_new (GABBLE_TYPE_IM_CHANNEL,
                       "connection", priv->conn,
                       "object-path", object_path,
                       "handle", handle,
                       NULL);

  g_debug ("new_im_channel: object path %s", object_path);

  g_signal_connect (chan, "closed", (GCallback) im_channel_closed_cb, fac);

  g_hash_table_insert (priv->channels, GINT_TO_POINTER (handle), chan);

  g_signal_emit_by_name (fac, "new-channel", chan);

  g_free (object_path);

  return chan;
}

static void
gabble_im_factory_iface_close_all (TpChannelFactoryIface *iface)
{
  GabbleImFactory *fac = GABBLE_IM_FACTORY (iface);
  GabbleImFactoryPrivate *priv = GABBLE_IM_FACTORY_GET_PRIVATE (fac);

  g_debug ("%s: closing channels", G_STRFUNC);

  if (priv->channels)
    {
      g_hash_table_destroy (priv->channels);
      priv->channels = NULL;
    }
}

static void
gabble_im_factory_iface_connected (TpChannelFactoryIface *iface)
{
  /* nothing to do */
}

static void
gabble_im_factory_iface_disconnected (TpChannelFactoryIface *iface)
{
  GabbleImFactory *fac = GABBLE_IM_FACTORY (iface);
  GabbleImFactoryPrivate *priv = GABBLE_IM_FACTORY_GET_PRIVATE (fac);

  g_debug ("%s: removing callbacks", G_STRFUNC);

  if (priv->message_cb)
    {
      lm_connection_unregister_message_handler (priv->conn->lmconn, priv->message_cb,
                                                LM_MESSAGE_TYPE_MESSAGE);
      priv->message_cb = NULL;
    }
}

struct _ForeachData
{
  TpChannelFunc foreach;
  gpointer user_data;
};

static void
_foreach_slave (gpointer key, gpointer value, gpointer user_data)
{
  struct _ForeachData *data = (struct _ForeachData *) user_data;
  TpChannelIface *chan = TP_CHANNEL_IFACE (value);
  
  data->foreach (chan, data->user_data);
}

static void
gabble_im_factory_iface_foreach (TpChannelFactoryIface *iface, TpChannelFunc foreach, gpointer user_data)
{
  GabbleImFactory *fac = GABBLE_IM_FACTORY (iface);
  GabbleImFactoryPrivate *priv = GABBLE_IM_FACTORY_GET_PRIVATE (fac);
  struct _ForeachData data;

  data.user_data = user_data;
  data.foreach = foreach;

  g_hash_table_foreach (priv->channels, _foreach_slave, &data);
}

static TpChannelFactoryRequestStatus
gabble_im_factory_iface_request (TpChannelFactoryIface *iface,
                                 const gchar *chan_type,
                                 TpHandleType handle_type,
                                 guint handle,
                                 TpChannelIface **ret)
{
  GabbleImFactory *fac = GABBLE_IM_FACTORY (iface);
  GabbleImFactoryPrivate *priv = GABBLE_IM_FACTORY_GET_PRIVATE (fac);
  GabbleIMChannel *chan;
  GError *error;

  if (strcmp (chan_type, TP_IFACE_CHANNEL_TYPE_TEXT))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;

  if (handle_type != TP_HANDLE_TYPE_CONTACT)
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;

  if (!gabble_handle_is_valid (priv->conn->handles, handle_type, handle, &error))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;

  chan = g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle));
  if (!chan)
    {
      chan = new_im_channel (fac, handle);
      if (!chan) return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;
    }

  *ret = TP_CHANNEL_IFACE (chan);
  return TP_CHANNEL_FACTORY_REQUEST_STATUS_DONE;
}

static void
gabble_im_factory_iface_init (gpointer g_iface,
                              gpointer iface_data)
{
  TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *) g_iface;

  klass->close_all = gabble_im_factory_iface_close_all;
  klass->connected = gabble_im_factory_iface_connected;
  klass->disconnected = gabble_im_factory_iface_disconnected;
  klass->foreach = gabble_im_factory_iface_foreach;
  klass->request = gabble_im_factory_iface_request;
}

