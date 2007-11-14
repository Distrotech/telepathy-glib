/*
 * channel.h - proxy for a Telepathy channel
 *
 * Copyright (C) 2007 Collabora Ltd.
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

#ifndef __TP_CHANNEL_H__
#define __TP_CHANNEL_H__

#include <telepathy-glib/proxy.h>

G_BEGIN_DECLS

typedef struct _TpChannel TpChannel;
typedef struct _TpChannelClass TpChannelClass;

GType tp_channel_get_type (void);

/* TYPE MACROS */
#define TP_TYPE_CHANNEL \
  (tp_channel_get_type ())
#define TP_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TP_TYPE_CHANNEL, \
                              TpChannel))
#define TP_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TP_TYPE_CHANNEL, \
                           TpChannelClass))
#define TP_IS_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TP_TYPE_CHANNEL))
#define TP_IS_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TP_TYPE_CHANNEL))
#define TP_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TP_TYPE_CHANNEL, \
                              TpChannelClass))

#include <telepathy-glib/_gen/tp-cli-channel-interfaces.h>

G_END_DECLS

#endif
