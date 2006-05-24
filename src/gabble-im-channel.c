/*
 * gabble-im-channel.c - Source for GabbleIMChannel
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#include <dbus/dbus-glib.h>
#include <loudmouth/loudmouth.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "gabble-connection.h"
#include "gabble-presence.h"
#include "gabble-presence-cache.h"
#include "handles.h"
#include "roster.h"
#include "telepathy-constants.h"
#include "telepathy-errors.h"
#include "telepathy-helpers.h"
#include "telepathy-interfaces.h"
#include "tp-channel-iface.h"

#include "gabble-im-channel.h"
#include "gabble-im-channel-glue.h"
#include "gabble-im-channel-signals-marshal.h"

G_DEFINE_TYPE_WITH_CODE (GabbleIMChannel, gabble_im_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

/* signal enum */
enum
{
    CLOSED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CONNECTION,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleIMChannelPrivate GabbleIMChannelPrivate;

struct _GabbleIMChannelPrivate
{
  GabbleConnection *conn;
  char *object_path;
  GabbleHandle handle;

  gchar *peer_jid;

  gboolean closed;
  gboolean dispose_has_run;
};

#define GABBLE_IM_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_IM_CHANNEL, GabbleIMChannelPrivate))

static void
gabble_im_channel_init (GabbleIMChannel *obj)
{
  /* do nothing? */
}

static GObject *
gabble_im_channel_constructor (GType type, guint n_props,
                               GObjectConstructParam *props)
{
  GObject *obj;
  GabbleIMChannelPrivate *priv;
  DBusGConnection *bus;
  gboolean valid;

  obj = G_OBJECT_CLASS (gabble_im_channel_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (GABBLE_IM_CHANNEL (obj));

  valid = gabble_handle_ref (priv->conn->handles, TP_HANDLE_TYPE_CONTACT, priv->handle);
  g_assert (valid);

  priv->peer_jid = g_strdup (gabble_handle_inspect (priv->conn->handles,
        TP_HANDLE_TYPE_CONTACT, priv->handle));

  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  gabble_text_mixin_init (obj, G_STRUCT_OFFSET (GabbleIMChannel, text), priv->conn->handles);

  return obj;
}

static void
gabble_im_channel_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  GabbleIMChannel *chan = GABBLE_IM_CHANNEL (object);
  GabbleIMChannelPrivate *priv = GABBLE_IM_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TEXT);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, priv->handle);
      break;
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_im_channel_set_property (GObject     *object,
                                guint        property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  GabbleIMChannel *chan = GABBLE_IM_CHANNEL (object);
  GabbleIMChannelPrivate *priv = GABBLE_IM_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE:
      priv->handle = g_value_get_uint (value);
      break;
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


static void gabble_im_channel_dispose (GObject *object);
static void gabble_im_channel_finalize (GObject *object);

static void
gabble_im_channel_class_init (GabbleIMChannelClass *gabble_im_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_im_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_im_channel_class, sizeof (GabbleIMChannelPrivate));

  object_class->constructor = gabble_im_channel_constructor;

  object_class->get_property = gabble_im_channel_get_property;
  object_class->set_property = gabble_im_channel_set_property;

  object_class->dispose = gabble_im_channel_dispose;
  object_class->finalize = gabble_im_channel_finalize;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH, "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE, "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE, "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "IM channel object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (gabble_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_im_channel_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  gabble_text_mixin_class_init (object_class, G_STRUCT_OFFSET (GabbleIMChannelClass, text_class));

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_im_channel_class), &dbus_glib_gabble_im_channel_object_info);
}

void
gabble_im_channel_dispose (GObject *object)
{
  GabbleIMChannel *self = GABBLE_IM_CHANNEL (object);
  GabbleIMChannelPrivate *priv = GABBLE_IM_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (!gabble_roster_handle_is_subscribed (priv->conn->roster, priv->handle))
    {
      GabblePresence *presence;

      presence = gabble_presence_cache_get (priv->conn->presence_cache,
          priv->handle);

      if (NULL != presence)
        {
          presence->keep_unavailable = FALSE;
          gabble_presence_cache_maybe_remove (priv->conn->presence_cache,
              priv->handle);
        }
    }

  if (!priv->closed)
    g_signal_emit(self, signals[CLOSED], 0);

  if (G_OBJECT_CLASS (gabble_im_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_im_channel_parent_class)->dispose (object);
}

void
gabble_im_channel_finalize (GObject *object)
{
  GabbleIMChannel *self = GABBLE_IM_CHANNEL (object);
  GabbleIMChannelPrivate *priv = GABBLE_IM_CHANNEL_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  gabble_handle_unref (priv->conn->handles, TP_HANDLE_TYPE_CONTACT, priv->handle);

  g_free (priv->object_path);
  g_free (priv->peer_jid);

  gabble_text_mixin_finalize (object);

  G_OBJECT_CLASS (gabble_im_channel_parent_class)->finalize (object);
}

/**
 * _gabble_im_channel_receive
 *
 */
gboolean _gabble_im_channel_receive (GabbleIMChannel *chan,
                                     TpChannelTextMessageType type,
                                     GabbleHandle sender,
                                     const char *from,
                                     time_t timestamp,
                                     const char *text)
{
  GabbleIMChannelPrivate *priv;

  g_assert (GABBLE_IS_IM_CHANNEL (chan));
  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (chan);

  /* update peer's full JID if it's changed */
  if (0 != strcmp (from, priv->peer_jid))
    {
      g_free (priv->peer_jid);
      priv->peer_jid = g_strdup (from);
    }

  return gabble_text_mixin_receive (G_OBJECT (chan), type, sender, timestamp, text);
}

/**
 * gabble_im_channel_acknowledge_pending_message
 *
 * Implements DBus method AcknowledgePendingMessage
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_im_channel_acknowledge_pending_message (GabbleIMChannel *obj, guint id, GError **error)
{
  g_assert (GABBLE_IS_IM_CHANNEL (obj));

  return gabble_text_mixin_acknowledge_pending_message (G_OBJECT (obj), id, error);
}


/**
 * gabble_im_channel_close
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_im_channel_close (GabbleIMChannel *obj, GError **error)
{
  GabbleIMChannelPrivate *priv;

  g_assert (GABBLE_IS_IM_CHANNEL (obj));

  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (obj);
  priv->closed = TRUE;

  g_debug ("%s called on %p", G_STRFUNC, obj);
  g_signal_emit(obj, signals[CLOSED], 0);

  return TRUE;
}


/**
 * gabble_im_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_im_channel_get_channel_type (GabbleIMChannel *obj, gchar ** ret, GError **error)
{
  *ret = g_strdup (TP_IFACE_CHANNEL_TYPE_TEXT);

  return TRUE;
}


/**
 * gabble_im_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_im_channel_get_handle (GabbleIMChannel *obj, guint* ret, guint* ret1, GError **error)
{
  GabbleIMChannelPrivate *priv;

  g_assert (GABBLE_IS_IM_CHANNEL (obj));

  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (obj);

  *ret = TP_HANDLE_TYPE_CONTACT;
  *ret1 = priv->handle;

  return TRUE;
}


/**
 * gabble_im_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_im_channel_get_interfaces (GabbleIMChannel *obj, gchar *** ret, GError **error)
{
  const char *interfaces[] = { NULL };

  *ret = g_strdupv ((gchar **) interfaces);

  return TRUE;
}


/**
 * gabble_im_channel_list_pending_messages
 *
 * Implements DBus method ListPendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_im_channel_list_pending_messages (GabbleIMChannel *obj, GPtrArray ** ret, GError **error)
{
  g_assert (GABBLE_IS_IM_CHANNEL (obj));

  return gabble_text_mixin_list_pending_messages (G_OBJECT (obj), ret, error);
}


/**
 * gabble_im_channel_send
 *
 * Implements DBus method Send
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_im_channel_send (GabbleIMChannel *obj, guint type, const gchar * text, GError **error)
{
  GabbleIMChannelPrivate *priv;

  g_assert (GABBLE_IS_IM_CHANNEL (obj));
  priv = GABBLE_IM_CHANNEL_GET_PRIVATE (obj);

  return gabble_text_mixin_send (G_OBJECT (obj), type, 0, priv->peer_jid, text,
      priv->conn, error);
}

