/*
 * media-manager.c - an example channel manager for StreamedMedia calls.
 * This channel manager emulates a protocol like XMPP Jingle, where you can
 * make several simultaneous calls to the same or different contacts.
 *
 * Copyright © 2007-2009 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright © 2007-2009 Nokia Corporation
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

#include "media-manager.h"

#include <dbus/dbus-glib.h>

#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/interfaces.h>

#include "media-channel.h"

static void channel_manager_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (ExampleCallableMediaManager,
    example_callable_media_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init))

/* type definition stuff */

enum
{
  PROP_CONNECTION = 1,
  N_PROPS
};

struct _ExampleCallableMediaManagerPrivate
{
  TpBaseConnection *conn;

  /* List of ExampleCallableMediaChannel */
  GList *channels;

  /* Next channel will be ("MediaChannel%u", next_channel_index) */
  guint next_channel_index;

  gulong status_changed_id;
};

static void
example_callable_media_manager_init (ExampleCallableMediaManager *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EXAMPLE_TYPE_CALLABLE_MEDIA_MANAGER,
      ExampleCallableMediaManagerPrivate);

  self->priv->conn = NULL;
  self->priv->channels = NULL;
  self->priv->status_changed_id = 0;
}

static void
example_callable_media_manager_close_all (ExampleCallableMediaManager *self)
{
  if (self->priv->channels != NULL)
    {
      GList *tmp = self->priv->channels;

      self->priv->channels = NULL;

      g_list_foreach (tmp, (GFunc) g_object_unref, NULL);
      g_list_free (tmp);
    }

  if (self->priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (self->priv->conn,
          self->priv->status_changed_id);
      self->priv->status_changed_id = 0;
    }
}

static void
dispose (GObject *object)
{
  ExampleCallableMediaManager *self = EXAMPLE_CALLABLE_MEDIA_MANAGER (object);

  example_callable_media_manager_close_all (self);
  g_assert (self->priv->channels == NULL);

  ((GObjectClass *) example_callable_media_manager_parent_class)->dispose (
    object);
}

static void
get_property (GObject *object,
              guint property_id,
              GValue *value,
              GParamSpec *pspec)
{
  ExampleCallableMediaManager *self = EXAMPLE_CALLABLE_MEDIA_MANAGER (object);

  switch (property_id)
    {
    case PROP_CONNECTION:
      g_value_set_object (value, self->priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
set_property (GObject *object,
              guint property_id,
              const GValue *value,
              GParamSpec *pspec)
{
  ExampleCallableMediaManager *self = EXAMPLE_CALLABLE_MEDIA_MANAGER (object);

  switch (property_id)
    {
    case PROP_CONNECTION:
      /* We don't ref the connection, because it owns a reference to the
       * channel manager, and it guarantees that the manager's lifetime is
       * less than its lifetime */
      self->priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
status_changed_cb (TpBaseConnection *conn,
                   guint status,
                   guint reason,
                   ExampleCallableMediaManager *self)
{
  switch (status)
    {
    case TP_CONNECTION_STATUS_DISCONNECTED:
        {
          example_callable_media_manager_close_all (self);
        }
      break;

    default:
      break;
    }
}

static void
constructed (GObject *object)
{
  ExampleCallableMediaManager *self = EXAMPLE_CALLABLE_MEDIA_MANAGER (object);
  void (*chain_up) (GObject *) =
      ((GObjectClass *) example_callable_media_manager_parent_class)->constructed;

  if (chain_up != NULL)
    {
      chain_up (object);
    }

  self->priv->status_changed_id = g_signal_connect (self->priv->conn,
      "status-changed", (GCallback) status_changed_cb, self);
}

static void
example_callable_media_manager_class_init (
    ExampleCallableMediaManagerClass *klass)
{
  GParamSpec *param_spec;
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->constructed = constructed;
  object_class->dispose = dispose;
  object_class->get_property = get_property;
  object_class->set_property = set_property;

  param_spec = g_param_spec_object ("connection", "Connection object",
      "The connection that owns this channel manager",
      TP_TYPE_BASE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  g_type_class_add_private (klass,
      sizeof (ExampleCallableMediaManagerPrivate));
}

static void
example_callable_media_manager_foreach_channel (
    TpChannelManager *iface,
    TpExportableChannelFunc callback,
    gpointer user_data)
{
  ExampleCallableMediaManager *self = EXAMPLE_CALLABLE_MEDIA_MANAGER (iface);

  g_list_foreach (self->priv->channels, (GFunc) callback, user_data);
}

static void
channel_closed_cb (ExampleCallableMediaChannel *chan,
                   ExampleCallableMediaManager *self)
{
  tp_channel_manager_emit_channel_closed_for_object (self,
      TP_EXPORTABLE_CHANNEL (chan));

  if (self->priv->channels != NULL)
    {
      self->priv->channels = g_list_remove_all (self->priv->channels, chan);
    }
}

static ExampleCallableMediaChannel *
new_channel (ExampleCallableMediaManager *self,
             TpHandle handle,
             TpHandle initiator,
             gpointer request_token)
{
  ExampleCallableMediaChannel *chan;
  gchar *object_path;
  GSList *requests = NULL;

  /* FIXME: This could potentially wrap around, but only after 4 billion
   * calls, which is probably plenty. */
  object_path = g_strdup_printf ("%s/MediaChannel%u",
      self->priv->conn->object_path, self->priv->next_channel_index++);

  chan = g_object_new (EXAMPLE_TYPE_CALLABLE_MEDIA_CHANNEL,
      "connection", self->priv->conn,
      "object-path", object_path,
      "handle", handle,
      "initiator-handle", initiator,
      NULL);

  g_free (object_path);

  g_signal_connect (chan, "closed", G_CALLBACK (channel_closed_cb), self);

  self->priv->channels = g_list_prepend (self->priv->channels, chan);

  if (request_token != NULL)
    requests = g_slist_prepend (requests, request_token);

  tp_channel_manager_emit_new_channel (self, TP_EXPORTABLE_CHANNEL (chan),
      requests);
  g_slist_free (requests);

  return chan;
}

static const gchar * const fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};

static const gchar * const allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    NULL
};

static void
example_callable_media_manager_foreach_channel_class (
    TpChannelManager *manager,
    TpChannelManagerChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);

  g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType",
      tp_g_value_slice_new_static_string (
        TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA));

  g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandleType",
      tp_g_value_slice_new_uint (TP_HANDLE_TYPE_CONTACT));

  func (manager, table, allowed_properties, user_data);

  g_hash_table_destroy (table);
}

static gboolean
example_callable_media_manager_request (ExampleCallableMediaManager *self,
                                        gpointer request_token,
                                        GHashTable *request_properties,
                                        gboolean require_new)
{
  TpHandle handle;
  GError *error = NULL;

  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"),
      TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA))
    {
      return FALSE;
    }

  if (tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandleType", NULL) != TP_HANDLE_TYPE_CONTACT)
    {
      return FALSE;
    }

  handle = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandle", NULL);
  g_assert (handle != 0);

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
        fixed_properties, allowed_properties, &error))
    {
      goto error;
    }

  if (!require_new)
    {
      /* see if we're already calling that handle */
      const GList *link;

      for (link = self->priv->channels; link != NULL; link = link->next)
        {
          guint its_handle;

          g_object_get (link->data,
              "handle", &its_handle,
              NULL);

          if (its_handle == handle)
            {
              tp_channel_manager_emit_request_already_satisfied (self,
                  request_token, TP_EXPORTABLE_CHANNEL (link->data));
              return TRUE;
            }
        }
    }

  new_channel (self, handle, self->priv->conn->self_handle,
      request_token);
  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}

static gboolean
example_callable_media_manager_create_channel (TpChannelManager *manager,
                                               gpointer request_token,
                                               GHashTable *request_properties)
{
    return example_callable_media_manager_request (
        EXAMPLE_CALLABLE_MEDIA_MANAGER (manager),
        request_token, request_properties, TRUE);
}

static gboolean
example_callable_media_manager_ensure_channel (TpChannelManager *manager,
                                               gpointer request_token,
                                               GHashTable *request_properties)
{
    return example_callable_media_manager_request (
        EXAMPLE_CALLABLE_MEDIA_MANAGER (manager),
        request_token, request_properties, FALSE);
}

static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data G_GNUC_UNUSED)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = example_callable_media_manager_foreach_channel;
  iface->foreach_channel_class =
    example_callable_media_manager_foreach_channel_class;
  iface->create_channel = example_callable_media_manager_create_channel;
  iface->ensure_channel = example_callable_media_manager_ensure_channel;
  /* In this channel manager, Request has the same semantics as Create
   * (this matches telepathy-gabble's behaviour) */
  iface->request_channel = example_callable_media_manager_create_channel;
}
