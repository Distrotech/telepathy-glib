/*
 * gabble-roster-channel.c - Source for GabbleRosterChannel
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
#include <stdio.h>
#include <stdlib.h>

#include "gabble-connection.h"
#include "gintset.h"
#include "handle-set.h"
#include "telepathy-errors.h"
#include "telepathy-helpers.h"
#include "telepathy-interfaces.h"

#include "gabble-group-mixin.h"
#include "gabble-roster-channel.h"
#include "gabble-roster-channel-glue.h"
#include "gabble-roster-channel-signals-marshal.h"

G_DEFINE_TYPE(GabbleRosterChannel, gabble_roster_channel, G_TYPE_OBJECT)

/* signal enum */
enum
{
    CLOSED,
    GROUP_FLAGS_CHANGED,
    MEMBERS_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_OBJECT_PATH,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleRosterChannelPrivate GabbleRosterChannelPrivate;

struct _GabbleRosterChannelPrivate
{
  GabbleConnection *connection;
  char *object_path;
  GabbleHandle handle;

  gboolean dispose_has_run;
};

#define GABBLE_ROSTER_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_ROSTER_CHANNEL, GabbleRosterChannelPrivate))

static void
gabble_roster_channel_init (GabbleRosterChannel *obj)
{
  /* GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (obj); */

  /* allocate any data required by the object here */
}

static GObject *
gabble_roster_channel_constructor (GType type, guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;
  GabbleRosterChannelPrivate *priv;
  DBusGConnection *bus;
  GabbleHandleRepo *handles;
  gboolean valid;
  GabbleHandle self_handle;
  GError *error = NULL;

  obj = G_OBJECT_CLASS (gabble_roster_channel_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (GABBLE_ROSTER_CHANNEL (obj));

  /* register object on the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  /* ref our list handle */
  handles = _gabble_connection_get_handles (priv->connection);
  valid = gabble_handle_ref (handles, TP_HANDLE_TYPE_LIST, priv->handle);
  g_assert (valid);

  /* initialize group mixin */
  valid = gabble_connection_get_self_handle (priv->connection, &self_handle, &error);
  g_assert (valid);
  gabble_group_mixin_init (obj, G_STRUCT_OFFSET (GabbleRosterChannel, group),
                           handles, self_handle);

  if (gabble_handle_for_list_publish (handles) == priv->handle)
    {
      gabble_group_mixin_change_flags (obj,
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
          0);
    }
  else if (gabble_handle_for_list_subscribe (handles) == priv->handle)
    {
      gabble_group_mixin_change_flags (obj,
          TP_CHANNEL_GROUP_FLAG_CAN_ADD |
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE |
          TP_CHANNEL_GROUP_FLAG_CAN_RESCIND,
          0);
    }
  else
    {
      g_assert_not_reached ();
    }

  return obj;
}

static void
gabble_roster_channel_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  GabbleRosterChannel *chan = GABBLE_ROSTER_CHANNEL (object);
  GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->connection);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_string (value, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, TP_HANDLE_TYPE_LIST);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, priv->handle);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_roster_channel_set_property (GObject     *object,
                                    guint        property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  GabbleRosterChannel *chan = GABBLE_ROSTER_CHANNEL (object);
  GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->connection = g_value_get_object (value);
      break;
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE:
      priv->handle = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_roster_channel_dispose (GObject *object);
static void gabble_roster_channel_finalize (GObject *object);

static gboolean _gabble_roster_channel_add_member_cb (GObject *obj, GabbleHandle handle, const gchar *message, GError **error);
static gboolean _gabble_roster_channel_remove_member_cb (GObject *obj, GabbleHandle handle, const gchar *message, GError **error);

static void
gabble_roster_channel_class_init (GabbleRosterChannelClass *gabble_roster_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_roster_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_roster_channel_class, sizeof (GabbleRosterChannelPrivate));

  object_class->constructor = gabble_roster_channel_constructor;

  object_class->get_property = gabble_roster_channel_get_property;
  object_class->set_property = gabble_roster_channel_set_property;

  object_class->dispose = gabble_roster_channel_dispose;
  object_class->finalize = gabble_roster_channel_finalize;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "Roster channel object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
                                    "The D-Bus object path used for this "
                                    "object on the bus.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_string ("channel-type", "Telepathy channel type",
                                    "The D-Bus interface representing the "
                                    "type of this channel.",
                                    NULL,
                                    G_PARAM_READABLE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CHANNEL_TYPE, param_spec);

  param_spec = g_param_spec_uint ("handle-type", "Contact handle type",
                                  "The TpHandleType representing a "
                                  "contact handle.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_READABLE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HANDLE_TYPE, param_spec);

  param_spec = g_param_spec_uint ("handle", "Contact handle",
                                  "The GabbleHandle representing the contact "
                                  "with whom this channel communicates.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HANDLE, param_spec);

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (gabble_roster_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_roster_channel_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  gabble_group_mixin_class_init (object_class,
                                 G_STRUCT_OFFSET (GabbleRosterChannelClass, group_class),
                                 _gabble_roster_channel_add_member_cb,
                                 _gabble_roster_channel_remove_member_cb);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_roster_channel_class), &dbus_glib_gabble_roster_channel_object_info);
}

void
gabble_roster_channel_dispose (GObject *object)
{
  GabbleRosterChannel *self = GABBLE_ROSTER_CHANNEL (object);
  GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_signal_emit(self, signals[CLOSED], 0);

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_roster_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_roster_channel_parent_class)->dispose (object);
}

void
gabble_roster_channel_finalize (GObject *object)
{
  GabbleRosterChannel *self = GABBLE_ROSTER_CHANNEL (object);
  GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (self);
  GabbleHandleRepo *handles;

  /* free any data held directly by the object here */

  g_free (priv->object_path);

  handles = _gabble_connection_get_handles (priv->connection);
  gabble_handle_unref (handles, TP_HANDLE_TYPE_LIST, priv->handle);

  gabble_group_mixin_finalize (object);

  G_OBJECT_CLASS (gabble_roster_channel_parent_class)->finalize (object);
}


/**
 * _gabble_roster_channel_add_member_cb
 *
 * Called by the group mixin to add one member.
 */
static gboolean
_gabble_roster_channel_add_member_cb (GObject *obj,
                                      GabbleHandle handle,
                                      const gchar *message,
                                      GError **error)
{
  GabbleRosterChannelPrivate *priv;
  GabbleHandleRepo *repo;

  g_assert (GABBLE_IS_ROSTER_CHANNEL (obj));

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (obj);
  repo = _gabble_connection_get_handles (priv->connection);

  /* publish list */
  if (gabble_handle_for_list_publish (repo) == priv->handle)
    {
      /* send <presence type="subscribed"> messages */
      LmMessage *message;
      const char *contact;
      gboolean result;

      contact = gabble_handle_inspect (repo, TP_HANDLE_TYPE_CONTACT, handle);

      message = lm_message_new_with_sub_type (contact,
          LM_MESSAGE_TYPE_PRESENCE,
          LM_MESSAGE_SUB_TYPE_SUBSCRIBED);
      result = _gabble_connection_send (priv->connection, message, error);
      lm_message_unref (message);

      if (!result)
        return FALSE;
    }
  /* subscribe list */
  else if (gabble_handle_for_list_subscribe (repo) == priv->handle)
    {
      /* send <presence type="subscribe">, but skip existing members */
      LmMessage *message;
      const char *contact;
      gboolean result;

      contact = gabble_handle_inspect (repo, TP_HANDLE_TYPE_CONTACT, handle);

      message = lm_message_new_with_sub_type (contact,
          LM_MESSAGE_TYPE_PRESENCE,
          LM_MESSAGE_SUB_TYPE_SUBSCRIBE);
      result = _gabble_connection_send (priv->connection, message, error);
      lm_message_unref (message);

      if (!result)
        return FALSE;
    }
  else
    {
      g_assert_not_reached ();
    }

  return TRUE;
}


/**
 * _gabble_roster_channel_remove_member_cb
 *
 * Called by the group mixin to remove one member.
 */
static gboolean
_gabble_roster_channel_remove_member_cb (GObject *obj,
                                         GabbleHandle handle,
                                         const gchar *message,
                                         GError **error)
{
  GabbleRosterChannelPrivate *priv;
  GabbleHandleRepo *repo;

  g_assert (GABBLE_IS_ROSTER_CHANNEL (obj));

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (obj);
  repo = _gabble_connection_get_handles (priv->connection);

  /* publish list */
  if (gabble_handle_for_list_publish (repo) == priv->handle)
    {
      /* send <presence type="unsubscribed"> messages */
      LmMessage *message;
      const char *contact;
      gboolean result;

      contact = gabble_handle_inspect (repo, TP_HANDLE_TYPE_CONTACT, handle);

      message = lm_message_new_with_sub_type (contact,
          LM_MESSAGE_TYPE_PRESENCE,
          LM_MESSAGE_SUB_TYPE_SUBSCRIBED);
      result = _gabble_connection_send (priv->connection, message, error);
      lm_message_unref (message);

      if (!result)
        return FALSE;
    }
  /* subscribe list */
  else if (gabble_handle_for_list_subscribe (repo) == priv->handle)
    {
      /* send <presence type="unsubscribe"> */
      LmMessage *message;
      const char *contact;
      gboolean result;

      contact = gabble_handle_inspect (repo, TP_HANDLE_TYPE_CONTACT, handle);

      message = lm_message_new_with_sub_type (contact,
          LM_MESSAGE_TYPE_PRESENCE,
          LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE);
      result = _gabble_connection_send (priv->connection, message, error);
      lm_message_unref (message);

      if (!result)
        return FALSE;
    }
  else
    {
      g_assert_not_reached ();
    }

  return TRUE;
}


/**
 * gabble_roster_channel_add_members
 *
 * Implements DBus method AddMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_add_members (GabbleRosterChannel *obj, const GArray * contacts, const gchar * message, GError **error)
{
  return gabble_group_mixin_add_members (G_OBJECT (obj), contacts, message, error);
}


/**
 * gabble_roster_channel_close
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
gboolean gabble_roster_channel_close (GabbleRosterChannel *obj, GError **error)
{
  *error = g_error_new (TELEPATHY_ERRORS, NotImplemented,
                        "you may not close contact list channels");

  return FALSE;
}


/**
 * gabble_roster_channel_get_channel_type
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
gboolean gabble_roster_channel_get_channel_type (GabbleRosterChannel *obj, gchar ** ret, GError **error)
{
  *ret = g_strdup (TP_IFACE_CHANNEL_TYPE_CONTACT_LIST);

  return TRUE;
}


/**
 * gabble_roster_channel_get_group_flags
 *
 * Implements DBus method GetGroupFlags
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_group_flags (GabbleRosterChannel *obj, guint* ret, GError **error)
{
  return gabble_group_mixin_get_group_flags (G_OBJECT (obj), ret, error);
}


/**
 * gabble_roster_channel_get_handle
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
gboolean gabble_roster_channel_get_handle (GabbleRosterChannel *obj, guint* ret, guint* ret1, GError **error)
{
  GabbleRosterChannelPrivate *priv;

  g_assert (GABBLE_IS_ROSTER_CHANNEL (obj));

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (obj);

  *ret = TP_HANDLE_TYPE_LIST;
  *ret1 = priv->handle;

  return TRUE;
}


/**
 * gabble_roster_channel_get_interfaces
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
gboolean gabble_roster_channel_get_interfaces (GabbleRosterChannel *obj, gchar *** ret, GError **error)
{
  const char *interfaces[] = { TP_IFACE_CHANNEL_INTERFACE_GROUP, NULL };

  *ret = g_strdupv ((gchar **) interfaces);

  return TRUE;
}


/**
 * gabble_roster_channel_get_local_pending_members
 *
 * Implements DBus method GetLocalPendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_local_pending_members (GabbleRosterChannel *obj, GArray ** ret, GError **error)
{
  return gabble_group_mixin_get_local_pending_members (G_OBJECT (obj), ret, error);
}


/**
 * gabble_roster_channel_get_members
 *
 * Implements DBus method GetMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_members (GabbleRosterChannel *obj, GArray ** ret, GError **error)
{
  return gabble_group_mixin_get_members (G_OBJECT (obj), ret, error);
}


/**
 * gabble_roster_channel_get_remote_pending_members
 *
 * Implements DBus method GetRemotePendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_remote_pending_members (GabbleRosterChannel *obj, GArray ** ret, GError **error)
{
  return gabble_group_mixin_get_remote_pending_members (G_OBJECT (obj), ret, error);
}


/**
 * gabble_roster_channel_get_self_handle
 *
 * Implements DBus method GetSelfHandle
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_self_handle (GabbleRosterChannel *obj, guint* ret, GError **error)
{
  return gabble_group_mixin_get_self_handle (G_OBJECT (obj), ret, error);
}


/**
 * gabble_roster_channel_remove_members
 *
 * Implements DBus method RemoveMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_remove_members (GabbleRosterChannel *obj, const GArray * contacts, const gchar * message, GError **error)
{
  return gabble_group_mixin_remove_members (G_OBJECT (obj), contacts, message, error);
}

