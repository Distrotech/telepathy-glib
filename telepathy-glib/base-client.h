/*
 * Base class for Client implementations
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

#ifndef __TP_BASE_CLIENT_H__
#define __TP_BASE_CLIENT_H__

#include <dbus/dbus-glib.h>
#include <glib-object.h>

#include <telepathy-glib/account.h>
#include <telepathy-glib/observe-channels-context.h>
#include <telepathy-glib/channel-dispatch-operation.h>
#include <telepathy-glib/connection.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/defs.h>
#include <telepathy-glib/dbus-properties-mixin.h>

G_BEGIN_DECLS

typedef struct _TpBaseClient TpBaseClient;
typedef struct _TpBaseClientClass TpBaseClientClass;
typedef struct _TpBaseClientPrivate TpBaseClientPrivate;
typedef struct _TpBaseClientClassPrivate TpBaseClientClassPrivate;

struct _TpBaseClientClass {
    /*<private>*/
    GObjectClass parent_class;
    GCallback _padding[7];
    TpDBusPropertiesMixinClass dbus_properties_class;
    TpBaseClientClassPrivate *priv;
};

struct _TpBaseClient {
    /*<private>*/
    GObject parent;
    TpBaseClientPrivate *priv;
};

GType tp_base_client_get_type (void);

/* Protected methods; should be called only by subclasses */

typedef void (*TpBaseClientClassObserveChannelsImpl) (
    TpBaseClient *client,
    TpAccount *account,
    TpConnection *connection,
    GList *channels,
    TpChannelDispatchOperation *dispatch_operation,
    GList *requests,
    TpObserveChannelsContext *context);

void tp_base_client_implement_observe_channels (TpBaseClientClass *klass,
    TpBaseClientClassObserveChannelsImpl impl);

/* setup functions which can only be called before register() */

void tp_base_client_add_observer_filter (TpBaseClient *self,
    GHashTable *filter);

void tp_base_client_take_observer_filter (TpBaseClient *self,
    GHashTable *filter);

void tp_base_client_set_observer_recover (TpBaseClient *self,
    gboolean recover);

gboolean tp_base_client_register (TpBaseClient *self,
    GError **error);

const gchar *tp_base_client_get_bus_name (TpBaseClient *self);

const gchar *tp_base_client_get_object_path (TpBaseClient *self);

#define TP_TYPE_BASE_CLIENT \
  (tp_base_client_get_type ())
#define TP_BASE_CLIENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), TP_TYPE_BASE_CLIENT, \
                               TpBaseClient))
#define TP_BASE_CLIENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), TP_TYPE_BASE_CLIENT, \
                            TpBaseClientClass))
#define TP_IS_BASE_CLIENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TP_TYPE_BASE_CLIENT))
#define TP_IS_BASE_CLIENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), TP_TYPE_BASE_CLIENT))
#define TP_BASE_CLIENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TP_TYPE_BASE_CLIENT, \
                              TpBaseClientClass))

G_END_DECLS

#endif
