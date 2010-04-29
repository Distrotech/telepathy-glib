/*
 * Context objects for TpBaseClient calls
 *
 * Copyright © 2010 Collabora Ltd.
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

#ifndef __TP_OBSERVE_CHANNELS_CONTEXT_H__
#define __TP_OBSERVE_CHANNELS_CONTEXT_H__

#include <gio/gio.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _TpObserveChannelsContext TpObserveChannelsContext;
typedef struct _TpObserveChannelsContextClass TpObserveChannelsContextClass;
typedef struct _TpObserveChannelsContextPrivate TpObserveChannelsContextPrivate;

GType tp_observe_channels_context_get_type (void);

typedef enum
{
  TP_OBSERVE_CHANNELS_CONTEXT_STATE_NONE,
  TP_OBSERVE_CHANNELS_CONTEXT_STATE_DONE,
  TP_OBSERVE_CHANNELS_CONTEXT_STATE_FAILED,
  TP_OBSERVE_CHANNELS_CONTEXT_STATE_DELAYED,
} TpObserveChannelsContextState;

#define TP_TYPE_OBSERVE_CHANNELS_CONTEXT \
  (tp_observe_channels_context_get_type ())
#define TP_OBSERVE_CHANNELS_CONTEXT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), TP_TYPE_OBSERVE_CHANNELS_CONTEXT, \
                               TpObserveChannelsContext))
#define TP_OBSERVE_CHANNELS_CONTEXT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), TP_TYPE_OBSERVE_CHANNELS_CONTEXT, \
                            TpObserveChannelsContextClass))
#define TP_IS_OBSERVE_CHANNELS_CONTEXT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TP_TYPE_OBSERVE_CHANNELS_CONTEXT))
#define TP_IS_OBSERVE_CHANNELS_CONTEXT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), TP_TYPE_OBSERVE_CHANNELS_CONTEXT))
#define TP_OBSERVE_CHANNELS_CONTEXT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TP_TYPE_OBSERVE_CHANNELS_CONTEXT, \
                              TpObserveChannelsContextClass))

void tp_observe_channels_context_accept (TpObserveChannelsContext *self);

void tp_observe_channels_context_fail (TpObserveChannelsContext *self,
    const GError *error);

void tp_observe_channels_context_delay (TpObserveChannelsContext *self);

gboolean tp_observe_channels_context_get_recovering (
    TpObserveChannelsContext *self);

void tp_observe_channels_context_prepare_async (TpObserveChannelsContext *self,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean tp_observe_channels_context_prepare_finish (
    TpObserveChannelsContext *self,
    GAsyncResult *result,
    GError **error);

G_END_DECLS

#endif
