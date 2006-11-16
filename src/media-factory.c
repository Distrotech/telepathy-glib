/*
 * media-factory.c - Source for GabbleMediaFactory
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

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <loudmouth/loudmouth.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "debug.h"
#include "gabble-connection.h"
#include "gabble-media-channel.h"
#include "handles.h"
#include "media-factory.h"
#include "namespaces.h"
#include "telepathy-interfaces.h"
#include "text-mixin.h"
#include "tp-channel-factory-iface.h"
#include "util.h"

static void gabble_media_factory_iface_init (gpointer g_iface, gpointer iface_data);
static LmHandlerResult media_factory_jingle_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleMediaFactory, gabble_media_factory, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE, gabble_media_factory_iface_init));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

typedef struct _GabbleMediaFactoryPrivate GabbleMediaFactoryPrivate;
struct _GabbleMediaFactoryPrivate
{
  GabbleConnection *conn;
  LmMessageHandler *jingle_cb;

  GPtrArray *channels;
  guint channel_index;

  GHashTable *session_chans;

  gboolean dispose_has_run;
};

#define GABBLE_MEDIA_FACTORY_GET_PRIVATE(o)    (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_MEDIA_FACTORY, GabbleMediaFactoryPrivate))

static GObject *gabble_media_factory_constructor (GType type, guint n_props, GObjectConstructParam *props);

static void
gabble_media_factory_init (GabbleMediaFactory *fac)
{
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);

  priv->channels = g_ptr_array_sized_new (1);
  priv->channel_index = 0;

  priv->jingle_cb = NULL;

  priv->conn = NULL;
  priv->dispose_has_run = FALSE;

  priv->session_chans = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               g_free, NULL);
}

static GObject *
gabble_media_factory_constructor (GType type, guint n_props,
                               GObjectConstructParam *props)
{
  GObject *obj;
  GabbleMediaFactoryPrivate *priv;

  obj = G_OBJECT_CLASS (gabble_media_factory_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (obj);

  return obj;
}


static void
gabble_media_factory_dispose (GObject *object)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (object);
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  tp_channel_factory_iface_close_all (TP_CHANNEL_FACTORY_IFACE (object));
  g_assert (priv->channels == NULL);

  if (priv->session_chans)
    {
      g_assert (g_hash_table_size (priv->session_chans) == 0);
      g_hash_table_destroy (priv->session_chans);
      priv->session_chans = NULL;
    }

  if (G_OBJECT_CLASS (gabble_media_factory_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_media_factory_parent_class)->dispose (object);
}

static void
gabble_media_factory_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (object);
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);

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
gabble_media_factory_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (object);
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);

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
gabble_media_factory_class_init (GabbleMediaFactoryClass *gabble_media_factory_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_media_factory_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_media_factory_class, sizeof (GabbleMediaFactoryPrivate));

  object_class->constructor = gabble_media_factory_constructor;
  object_class->dispose = gabble_media_factory_dispose;

  object_class->get_property = gabble_media_factory_get_property;
  object_class->set_property = gabble_media_factory_set_property;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "media channel factory object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

}

static gboolean _gabble_media_factory_sid_in_use (GabbleMediaFactory *fac, const gchar *sid);
static GabbleMediaChannel *new_media_channel (GabbleMediaFactory *fac, GabbleHandle handle);
static void media_channel_closed_cb (GabbleMediaChannel *chan, gpointer user_data);

/**
 * media_factory_jingle_cb
 *
 * Called by loudmouth when we get an incoming <iq>. This handler
 * is concerned only with jingle session queries, and allows other
 * handlers to be called for other queries.
 */
static LmHandlerResult
media_factory_jingle_cb (LmMessageHandler *handler,
                         LmConnection *lmconn,
                         LmMessage *message,
                         gpointer user_data)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (user_data);
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);
  LmMessageNode *iq_node, *session_node;
  const gchar *from, *id, *action, *sid;
  gchar *resource;
  GabbleHandle handle;
  GabbleMediaChannel *chan = NULL;
  gboolean chan_is_new = FALSE;
  GError *error = NULL;

  g_assert (lmconn == priv->conn->lmconn);

  /* all jingle actions are sets */
  if (LM_MESSAGE_SUB_TYPE_SET != lm_message_get_sub_type (message))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  /* is it for us? */
  iq_node = lm_message_get_node (message);
  session_node = lm_message_node_get_child_with_namespace (message->node,
      "jingle", NS_JINGLE);

  if (session_node != NULL)
    {
      action = lm_message_node_get_attribute (session_node, "action");
    }
  else
    {
      session_node = lm_message_node_get_child_with_namespace (iq_node,
          "session", NS_GOOGLE_SESSION);

      if (session_node == NULL)
        return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

      action = lm_message_node_get_attribute (session_node, "type");
    }

  if (action == NULL)
    {
      NODE_DEBUG (iq_node, "session action not found");
      goto BAD_REQUEST;
    }

  from = lm_message_node_get_attribute (iq_node, "from");
  if (from == NULL)
    {
      NODE_DEBUG (iq_node, "'from' attribute not found");
      goto BAD_REQUEST;
    }

  handle = gabble_handle_for_contact (priv->conn->handles, from, FALSE);
  if (handle == 0)
    {
      NODE_DEBUG (iq_node, "unable to get handle for sender");
      goto BAD_REQUEST;
    }

  id = lm_message_node_get_attribute (iq_node, "id");
  if (id == NULL)
    {
      NODE_DEBUG (iq_node, "'id' attribute not found");
      goto BAD_REQUEST;
    }

  /* does the session exist? */
  sid = lm_message_node_get_attribute (session_node, "sid");
  if (sid == NULL)
    sid = lm_message_node_get_attribute (session_node, "id");

  if (sid == NULL)
    {
      NODE_DEBUG (iq_node, "unable to get session id");
      goto BAD_REQUEST;
    }

  if (_gabble_media_factory_sid_in_use (fac, sid))
    {
      /* if it's media session, we should have it in here */
      chan = g_hash_table_lookup (priv->session_chans, sid);
    }

  /* it's a new session */
  if (chan == NULL)
    {
      /* if the session is unknown, the only allowed action is "initiate" */
      if (g_strdiff (action, "initiate") && g_strdiff (action, "session-initiate"))
        {
          NODE_DEBUG (iq_node, "action is not \"initiate\" or \"session-initiate\", rejecting");
          goto BAD_REQUEST;
        }

      DEBUG ("creating media channel");

      chan = new_media_channel (fac, handle);
      chan_is_new = TRUE;
    }

  g_assert (chan != NULL);

  DEBUG ("dispatching to session %s", sid);
  g_object_ref (chan);
  gabble_decode_jid (from, NULL, NULL, &resource);

  if (_gabble_media_channel_dispatch_session_action (chan, handle, resource,
        sid, message, session_node, action, &error))
    {
      if (chan_is_new)
        g_signal_emit_by_name (fac, "new-channel", chan);
    }
  else
    {
      if (chan_is_new)
        gabble_media_channel_close (chan, NULL);

      g_assert (error != NULL);
      _gabble_connection_send_iq_error (priv->conn, message, error->code,
          error->message);
    }

  g_object_unref (chan);
  g_free (resource);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;

BAD_REQUEST:
  _gabble_connection_send_iq_error (
    priv->conn, message, XMPP_ERROR_BAD_REQUEST, NULL);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static const gchar *
_gabble_media_factory_get_unique_sid (GabbleMediaFactory *fac)
{
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);
  guint32 val;
  gchar *sid = NULL;
  gboolean unique = FALSE;

  while (!unique)
    {
      val = g_random_int_range (1000000, G_MAXINT);

      g_free (sid);
      sid = g_strdup_printf ("%u", val);

      unique = !_gabble_media_factory_sid_in_use (fac, sid);
    }

  g_hash_table_insert (priv->session_chans, sid, NULL);

  return sid;
}

static gboolean
_gabble_media_factory_sid_in_use (GabbleMediaFactory *fac, const gchar *sid)
{
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);
  gpointer key, value;

  return g_hash_table_lookup_extended (priv->session_chans, sid, &key, &value);
}

const gchar *
_gabble_media_factory_allocate_sid (GabbleMediaFactory *fac, GabbleMediaChannel *chan)
{
  const gchar *sid = _gabble_media_factory_get_unique_sid (fac);

  g_return_val_if_fail (sid, NULL);

  return _gabble_media_factory_register_sid (fac, sid, chan);
}

const gchar *
_gabble_media_factory_register_sid (GabbleMediaFactory *fac,
                                    const gchar *sid,
                                    GabbleMediaChannel *chan)
{
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);
  gchar *sid_copy = g_strdup (sid);

  g_hash_table_replace (priv->session_chans, sid_copy, chan);

  return sid_copy;
}

void
_gabble_media_factory_free_sid (GabbleMediaFactory *fac, const gchar *sid)
{
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);
  if (g_hash_table_lookup (priv->session_chans, sid))
    {
      g_hash_table_remove (priv->session_chans, sid);
    }
}

static gboolean
_remove_sid_mapping (gpointer key, gpointer value, gpointer user_data)
{
  GabbleMediaChannel *chan = GABBLE_MEDIA_CHANNEL (value);
  GabbleMediaChannel *target_chan = GABBLE_MEDIA_CHANNEL (user_data);

  if (chan == target_chan) return TRUE;
  return FALSE;
}

/**
 * media_channel_closed_cb:
 *
 * Signal callback for when a media channel is closed. Removes the references
 * that #GabbleMediaFactory holds to them. Also removes all the sessions for
 * the closed channel.
 */
static void
media_channel_closed_cb (GabbleMediaChannel *chan, gpointer user_data)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (user_data);
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);

  if (priv->channels)
    {
      DEBUG ("removing media channel %p with ref count %d",
          chan, G_OBJECT (chan)->ref_count);

      g_ptr_array_remove (priv->channels, chan);
      g_object_unref (chan);
    }

  if (priv->session_chans)
    {
      g_hash_table_foreach_remove (priv->session_chans, _remove_sid_mapping, chan);
    }
}

/**
 * new_media_channel
 *
 * Creates a new empty GabbleMediaChannel.
 */
static GabbleMediaChannel *
new_media_channel (GabbleMediaFactory *fac, GabbleHandle creator)
{
  GabbleMediaFactoryPrivate *priv;
  GabbleMediaChannel *chan;
  gchar *object_path;

  g_assert (GABBLE_IS_MEDIA_FACTORY (fac));

  priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);

  object_path = g_strdup_printf ("%s/MediaChannel%u", priv->conn->object_path,
                                 priv->channel_index);
  priv->channel_index += 1;

  chan = g_object_new (GABBLE_TYPE_MEDIA_CHANNEL,
                       "connection", priv->conn,
                       "factory", fac,
                       "object-path", object_path,
                       "creator", creator,
                       NULL);

  DEBUG ("object path %s", object_path);

  g_signal_connect (chan, "closed", (GCallback) media_channel_closed_cb, fac);

  g_ptr_array_add (priv->channels, chan);

  g_free (object_path);

  return chan;
}


static void
gabble_media_factory_iface_close_all (TpChannelFactoryIface *iface)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (iface);
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);

  DEBUG ("closing channels");

  if (priv->channels)
    {
      GPtrArray *tmp = priv->channels;
      priv->channels = NULL;

      guint i;

      for (i = 0; i < tmp->len; i++)
        {
          GabbleMediaChannel *chan = g_ptr_array_index (tmp, i);

          DEBUG ("about to unref channel with ref_count %d",
                   G_OBJECT (chan)->ref_count);

          g_object_unref (chan);
        }

      g_ptr_array_free (tmp, TRUE);
    }

  if (priv->session_chans)
    {
      g_hash_table_destroy (priv->session_chans);
      priv->session_chans = NULL;
    }
}

static void
gabble_media_factory_iface_connecting (TpChannelFactoryIface *iface)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (iface);
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);

  g_assert(priv->conn != NULL);
  g_assert(priv->conn->lmconn != NULL);

  DEBUG ("adding callbacks");

  priv->jingle_cb = lm_message_handler_new (media_factory_jingle_cb, fac, NULL);
  lm_connection_register_message_handler (priv->conn->lmconn, priv->jingle_cb,
                                          LM_MESSAGE_TYPE_IQ,
                                          LM_HANDLER_PRIORITY_NORMAL);
}

static void
gabble_media_factory_iface_connected (TpChannelFactoryIface *iface)
{
  /* nothing to do */
}

static void
gabble_media_factory_iface_disconnected (TpChannelFactoryIface *iface)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (iface);
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);

  g_assert (priv->jingle_cb != NULL);

  DEBUG ("removing callbacks");

  lm_connection_unregister_message_handler (priv->conn->lmconn, priv->jingle_cb,
                                            LM_MESSAGE_TYPE_IQ);
  lm_message_handler_unref (priv->jingle_cb);
  priv->jingle_cb = NULL;
}

static void
gabble_media_factory_iface_foreach (TpChannelFactoryIface *iface, TpChannelFunc foreach, gpointer user_data)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (iface);
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);
  guint i;

  for (i = 0; i < priv->channels->len; i++)
    {
      foreach (TP_CHANNEL_IFACE (g_ptr_array_index (priv->channels, i)), user_data);
    }
}

static TpChannelFactoryRequestStatus
gabble_media_factory_iface_request (TpChannelFactoryIface *iface,
                                    const gchar *chan_type,
                                    TpHandleType handle_type,
                                    guint handle,
                                    TpChannelIface **ret,
                                    GError **error)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (iface);
  GabbleMediaFactoryPrivate *priv = GABBLE_MEDIA_FACTORY_GET_PRIVATE (fac);
  GabbleMediaChannel *chan = NULL;

  if (strcmp (chan_type, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA))
    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;

  if (handle_type == 0)
    {
      /* create an empty channel */
      chan = new_media_channel (fac, priv->conn->self_handle);
    }
  else if (handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      chan = new_media_channel (fac, priv->conn->self_handle);

      if (!_gabble_media_channel_add_member (G_OBJECT (chan), handle, "", error))
        {
          gboolean close_ret;

          close_ret = gabble_media_channel_close (chan, NULL);
          g_assert (close_ret);

          return TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR;
        }
    }
  else
    {
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;
    }

  g_assert (chan != NULL);
  g_signal_emit_by_name (fac, "new-channel", chan);

  *ret = TP_CHANNEL_IFACE (chan);
  return TP_CHANNEL_FACTORY_REQUEST_STATUS_DONE;
}

static void
gabble_media_factory_iface_init (gpointer g_iface,
                              gpointer iface_data)
{
  TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *) g_iface;

  klass->close_all = gabble_media_factory_iface_close_all;
  klass->connected = gabble_media_factory_iface_connected;
  klass->connecting = gabble_media_factory_iface_connecting;
  klass->disconnected = gabble_media_factory_iface_disconnected;
  klass->foreach = gabble_media_factory_iface_foreach;
  klass->request = gabble_media_factory_iface_request;
}

