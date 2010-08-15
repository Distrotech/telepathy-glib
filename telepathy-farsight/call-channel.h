/*
 * call-channel.c - Source for TfCallChannel
 * Copyright (C) 2010 Collabora Ltd.
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

#ifndef __TF_CALL_CHANNEL_H__
#define __TF_CALL_CHANNEL_H__

#include <glib-object.h>

#include <gst/gst.h>
#include <telepathy-glib/channel.h>

G_BEGIN_DECLS

#define TF_TYPE_CALL_CHANNEL tf_call_channel_get_type()

#define TF_CALL_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  TF_TYPE_CALL_CHANNEL, TfCallChannel))

#define TF_CALL_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  TF_TYPE_CALL_CHANNEL, TfCallChannelClass))

#define TF_IS_CALL_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TF_TYPE_CALL_CHANNEL))

#define TF_IS_CALL_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), TF_TYPE_CALL_CHANNEL))

#define TF_CALL_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  TF_TYPE_CALL_CHANNEL, TfCallChannelClass))

typedef struct _TfCallChannelPrivate TfCallChannelPrivate;

/**
 * TfCallChannel:
 *
 * All members of the object are private
 */

typedef struct _TfCallChannel TfCallChannel;

/**
 * TfCallChannelClass:
 * @parent_class: the parent #GObjecClass
 *
 * There are no overridable functions
 */

typedef struct _TfCallChannelClass TfCallChannelClass;

GType tf_call_channel_get_type (void);

TfCallChannel *tf_call_channel_new (
    TpChannel *channel_proxy);

void tf_call_channel_error (TfCallChannel *chan,
  TpMediaStreamError error,
  const gchar *message);

gboolean tf_call_channel_bus_message (TfCallChannel *channel,
    GstMessage *message);

G_END_DECLS

#endif /* __TF_CALL_CHANNEL_H__ */

