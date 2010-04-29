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

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/defs.h>
#include <telepathy-glib/dbus-properties-mixin.h>

G_BEGIN_DECLS

typedef struct _TpBaseClient TpBaseClient;
typedef struct _TpBaseClientClass TpBaseClientClass;
typedef struct _TpBaseClientPrivate TpBaseClientPrivate;

struct _TpBaseClientClass {
    /*<private>*/
    GObjectClass parent_class;
    TpDBusPropertiesMixinClass dbus_properties_class;
};

struct _TpBaseClient {
    /*<private>*/
    GObject parent;
    TpBaseClientPrivate *priv;
};

GType tp_base_client_get_type (void);

TpBaseClient *tp_base_client_new (TpDBusDaemon *dbus_daemon,
    const gchar *name,
    gboolean uniquify_name);

/* setup functions which can only be called before register() */

void tp_base_client_add_observer_filter (TpBaseClient *self,
    GHashTable *filter);

void tp_base_client_take_observer_filter (TpBaseClient *self,
    GHashTable *filter);

void tp_base_client_set_observer_recover (TpBaseClient *self,
    gboolean recover);

void tp_base_client_add_approver_filter (TpBaseClient *self,
    GHashTable *filter);
void tp_base_client_take_approver_filter (TpBaseClient *self,
    GHashTable *filter);

void tp_base_client_be_a_handler (TpBaseClient *self);

void tp_base_client_add_handler_filter (TpBaseClient *self,
    GHashTable *filter);
void tp_base_client_take_handler_filter (TpBaseClient *self,
    GHashTable *filter);
void tp_base_client_set_handler_bypass_approval (TpBaseClient *self,
    gboolean bypass_approval);

void tp_base_client_set_handler_request_notification (TpBaseClient *self);

void tp_base_client_add_handler_capability (TpBaseClient *self,
    const gchar *token);
void tp_base_client_add_handler_capabilities (TpBaseClient *self,
    const gchar * const *tokens);
void tp_base_client_add_handler_capabilities_varargs (TpBaseClient *self,
    const gchar *first_token, ...) G_GNUC_NULL_TERMINATED;

/* future, potentially (currently in spec as a draft):
void tp_base_client_set_handler_related_conferences_bypass_approval (
    TpBaseClient *self, gboolean bypass_approval);
    */

void tp_base_client_register (TpBaseClient *self);

/* Normal methods, can be called at any time */

GList *tp_base_client_get_pending_requests (TpBaseClient *self);
GList *tp_base_client_get_handled_channels (TpBaseClient *self);

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
