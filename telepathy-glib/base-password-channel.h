/*
 * base-password-channel.h - Header for TpBasePasswordChannel
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

#if defined (TP_DISABLE_SINGLE_INCLUDE) && !defined (_TP_IN_META_HEADER) && !defined (_TP_COMPILATION)
#error "Only <telepathy-glib/telepathy-glib.h> and <telepathy-glib/telepathy-glib-dbus.h> can be included directly."
#endif

#ifndef __TP_BASE_PASSWORD_CHANNEL_H__
#define __TP_BASE_PASSWORD_CHANNEL_H__

#include <glib-object.h>

#include <telepathy-glib/base-channel.h>
#include <telepathy-glib/dbus-properties-mixin.h>

G_BEGIN_DECLS

typedef struct _TpBasePasswordChannel TpBasePasswordChannel;
typedef struct _TpBasePasswordChannelPrivate TpBasePasswordChannelPrivate;
typedef struct _TpBasePasswordChannelClass TpBasePasswordChannelClass;

struct _TpBasePasswordChannelClass
{
  TpBaseChannelClass parent_class;

  TpDBusPropertiesMixinClass properties_class;
};

struct _TpBasePasswordChannel
{
  TpBaseChannel parent;

  TpBasePasswordChannelPrivate *priv;
};

GType tp_base_password_channel_get_type (void);

/* TYPE MACROS */
#define TP_TYPE_BASE_PASSWORD_CHANNEL \
  (tp_base_password_channel_get_type ())
#define TP_BASE_PASSWORD_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TP_TYPE_BASE_PASSWORD_CHANNEL,\
                              TpBasePasswordChannel))
#define TP_BASE_PASSWORD_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TP_TYPE_BASE_PASSWORD_CHANNEL,\
                           TpBasePasswordChannelClass))
#define TP_IS_BASE_PASSWORD_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TP_TYPE_BASE_PASSWORD_CHANNEL))
#define TP_IS_BASE_PASSWORD_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TP_TYPE_BASE_PASSWORD_CHANNEL))
#define TP_BASE_PASSWORD_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TP_TYPE_BASE_PASSWORD_CHANNEL, \
                              TpBasePasswordChannelClass))

G_END_DECLS

#endif /* #ifndef __TP_BASE_PASSWORD_CHANNEL_H__*/
