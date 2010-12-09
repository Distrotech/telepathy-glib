/*
 * signalled-message.c - Source for TpSignalledMessage
 * Copyright (C) 2006-2010 Collabora Ltd.
 * Copyright (C) 2006-2008 Nokia Corporation
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

/**
 * SECTION:signalled-message
 * @title: TpSignalledMessage
 * @short_description: a message received using the Telepathy message interface
 *
 * #TpSignalledMessage is used within Telepathy clients to represent a message
 * signalled by a connection manager. This can either be a message received from
 * someone else, confirmation that a message has been sent by the local user,
 * or a delivery report indicating that delivery of a message has
 * succeeded or failed.
 *
 * @since 0.13.UNRELEASED
 */

#include "signalled-message.h"
#include "signalled-message-internal.h"
#include "message-internal.h"

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/util.h>

/**
 * TpSignalledMessage:
 *
 * Opaque structure representing a received message using the Telepathy
 * messages interface
 */

enum
{
  PROP_SENDER = 1
};

G_DEFINE_TYPE (TpSignalledMessage, tp_signalled_message, TP_TYPE_MESSAGE)

struct _TpSignalledMessagePrivate
{
  TpContact *sender;
};

static void
tp_signalled_message_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  TpSignalledMessage *self = (TpSignalledMessage *) object;

  switch (property_id)
    {
      case PROP_SENDER:
        g_value_set_object (value, self->priv->sender);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
tp_signalled_message_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  TpSignalledMessage *self = (TpSignalledMessage *) object;

  switch (property_id)
    {
      case PROP_SENDER:
        g_assert (self->priv->sender == NULL); /* construct only */
        self->priv->sender = g_value_dup_object (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
tp_signalled_message_dispose (GObject *object)
{
  TpSignalledMessage *self = TP_SIGNALLED_MESSAGE (object);
  void (*dispose) (GObject *) =
    G_OBJECT_CLASS (tp_signalled_message_parent_class)->dispose;

  tp_clear_object (&self->priv->sender);

  if (dispose != NULL)
    dispose (object);
}

static void
tp_signalled_message_class_init (TpSignalledMessageClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  gobject_class->get_property = tp_signalled_message_get_property;
  gobject_class->set_property = tp_signalled_message_set_property;
  gobject_class->dispose = tp_signalled_message_dispose;

  /**
   * TpSignalledMessage:sender:
   *
   * A #TpContat representing the sender of the message if known,
   * otherwise %NULL.
   *
   * Since: 0.13.UNRELEASED
   */
  param_spec = g_param_spec_object ("sender", "TpContact",
      "The sender of the message",
      TP_TYPE_CONTACT,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (gobject_class, PROP_SENDER,
      param_spec);

  g_type_class_add_private (gobject_class, sizeof (TpSignalledMessagePrivate));
}

static void
tp_signalled_message_init (TpSignalledMessage *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self), TP_TYPE_SIGNALLED_MESSAGE,
      TpSignalledMessagePrivate);

  self->priv->sender = NULL;
}

TpMessage *
_tp_signalled_message_new (const GPtrArray *parts,
    TpContact *sender)
{
  TpMessage *self;
  guint i;
  const GHashTable *header;
  TpHandle sender_handle;

  g_return_val_if_fail (parts != NULL, NULL);
  g_return_val_if_fail (parts->len > 0, NULL);

  /* FIXME: remove message-sender? */
  self = g_object_new (TP_TYPE_SIGNALLED_MESSAGE,
      "sender", sender,
      NULL);

  for (i = 0; i < parts->len; i++)
    {
      /* First part is automatically created */
      if (i != 0)
        tp_message_append_part (self);

      tp_g_hash_table_update (g_ptr_array_index (self->parts, i),
          g_ptr_array_index (parts, i),
          (GBoxedCopyFunc) g_strdup,
          (GBoxedCopyFunc) tp_g_value_slice_dup);
    }

  header = tp_message_peek (self, 0);
  sender_handle = tp_asv_get_uint32 (header, "message-sender", NULL);
  if (sender_handle != 0)
    {
      g_assert (sender != NULL);
      g_assert (tp_contact_get_handle (sender) == sender_handle);

      /* This handle may not be persistent, user should use the TpContact
       * directly */
      g_hash_table_remove ((GHashTable *) header, "message-sender");
    }
  else
    {
      g_assert (sender == NULL);
      g_assert (tp_str_empty (tp_asv_get_string (header, "message-sender-id")));
    }

  _tp_message_set_immutable ((TpMessage *) self);

  return self;
}

/**
 * tp_signalled_message_get_sender:
 * @message: a #TpSignalledMessage
 *
 * Returns a #TpContact representing the sender of @message if known, %NULL
 * otherwise.
 *
 * Returns: (transfer none): the sender of the message
 *
 * @since 0.13.UNRELEASED
 */
TpContact *
tp_signalled_message_get_sender (TpMessage *message)
{
  TpSignalledMessage *self;

  g_return_val_if_fail (TP_IS_SIGNALLED_MESSAGE (message), NULL);

  self = (TpSignalledMessage *) message;

  return self->priv->sender;
}
