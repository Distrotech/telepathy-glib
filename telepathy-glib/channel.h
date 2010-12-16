/*
 * channel.h - proxy for a Telepathy channel
 *
 * Copyright (C) 2007 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2007 Nokia Corporation
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

#include <telepathy-glib/connection.h>
#include <telepathy-glib/defs.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/handle.h>
#include <telepathy-glib/intset.h>
#include <telepathy-glib/proxy.h>

G_BEGIN_DECLS

typedef struct _TpChannel TpChannel;
typedef struct _TpChannelPrivate TpChannelPrivate;
typedef struct _TpChannelClass TpChannelClass;

struct _TpChannelClass {
    TpProxyClass parent_class;
    /*<private>*/
    GCallback _1;
    GCallback _2;
    GCallback _3;
    GCallback _4;
};

struct _TpChannel {
    /*<private>*/
    TpProxy parent;

    TpChannelPrivate *priv;
};

GType tp_channel_get_type (void);

#define TP_ERRORS_REMOVED_FROM_GROUP (tp_errors_removed_from_group_quark ())
GQuark tp_errors_removed_from_group_quark (void);

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

TpChannel *tp_channel_new (TpConnection *conn,
    const gchar *object_path, const gchar *optional_channel_type,
    TpHandleType optional_handle_type, TpHandle optional_handle,
    GError **error) G_GNUC_WARN_UNUSED_RESULT;

TpChannel *tp_channel_new_from_properties (TpConnection *conn,
    const gchar *object_path, const GHashTable *immutable_properties,
    GError **error) G_GNUC_WARN_UNUSED_RESULT;

#ifndef TP_DISABLE_DEPRECATED
gboolean tp_channel_run_until_ready (TpChannel *self, GError **error,
    GMainLoop **loop) _TP_GNUC_DEPRECATED;
#endif

typedef void (*TpChannelWhenReadyCb) (TpChannel *channel, const GError *error,
    gpointer user_data);

void tp_channel_call_when_ready (TpChannel *self,
    TpChannelWhenReadyCb callback, gpointer user_data);

void tp_channel_init_known_interfaces (void);

gboolean tp_channel_is_ready (TpChannel *self);
const gchar *tp_channel_get_channel_type (TpChannel *self);
GQuark tp_channel_get_channel_type_id (TpChannel *self);
TpHandle tp_channel_get_handle (TpChannel *self, TpHandleType *handle_type);
const gchar *tp_channel_get_identifier (TpChannel *self);
TpConnection *tp_channel_borrow_connection (TpChannel *self);
GHashTable *tp_channel_borrow_immutable_properties (TpChannel *self);

TpHandle tp_channel_group_get_self_handle (TpChannel *self);
TpChannelGroupFlags tp_channel_group_get_flags (TpChannel *self);
const TpIntset *tp_channel_group_get_members (TpChannel *self);
const TpIntset *tp_channel_group_get_local_pending (TpChannel *self);
const TpIntset *tp_channel_group_get_remote_pending (TpChannel *self);
gboolean tp_channel_group_get_local_pending_info (TpChannel *self,
    TpHandle local_pending, TpHandle *actor,
    TpChannelGroupChangeReason *reason, const gchar **message);

TpHandle tp_channel_group_get_handle_owner (TpChannel *self, TpHandle handle);

gboolean tp_channel_get_requested (TpChannel *self);

TpHandle tp_channel_get_initiator_handle (TpChannel *self);
const gchar * tp_channel_get_initiator_identifier (TpChannel *self);

#define TP_CHANNEL_FEATURE_CORE \
  tp_channel_get_feature_quark_core ()

GQuark tp_channel_get_feature_quark_core (void) G_GNUC_CONST;

#define TP_CHANNEL_FEATURE_GROUP \
  tp_channel_get_feature_quark_group ()
GQuark tp_channel_get_feature_quark_group (void) G_GNUC_CONST;

#define TP_CHANNEL_FEATURE_CHAT_STATES \
  tp_channel_get_feature_quark_chat_states ()
GQuark tp_channel_get_feature_quark_chat_states (void) G_GNUC_CONST;
TpChannelChatState tp_channel_get_chat_state (TpChannel *self,
    TpHandle contact);

void tp_channel_leave_async (TpChannel *self,
    TpChannelGroupChangeReason reason,
    const gchar *message,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean tp_channel_leave_finish (TpChannel *self,
    GAsyncResult *result,
    GError **error);

void tp_channel_close_async (TpChannel *self,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean tp_channel_close_finish (TpChannel *self,
    GAsyncResult *result,
    GError **error);

G_END_DECLS

#include <telepathy-glib/_gen/tp-cli-channel.h>

#endif
