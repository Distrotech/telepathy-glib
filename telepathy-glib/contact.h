/* Object representing a Telepathy contact
 *
 * Copyright (C) 2008 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2008 Nokia Corporation
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

#ifndef __TP_CONTACT_H__
#define __TP_CONTACT_H__

#include <glib-object.h>
#include <gio/gio.h>

#include <telepathy-glib/capabilities.h>
#include <telepathy-glib/connection.h>
#include <telepathy-glib/handle.h>

#include <telepathy-glib/_gen/genums.h>

G_BEGIN_DECLS

/* TpContact is forward-declared in connection.h */
typedef struct _TpContactClass TpContactClass;
typedef struct _TpContactPrivate TpContactPrivate;

GType tp_contact_get_type (void) G_GNUC_CONST;

#define TP_TYPE_CONTACT \
  (tp_contact_get_type ())
#define TP_CONTACT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), TP_TYPE_CONTACT, \
                               TpContact))
#define TP_CONTACT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), TP_TYPE_CONTACT, \
                            TpContactClass))
#define TP_IS_CONTACT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TP_TYPE_CONTACT))
#define TP_IS_CONTACT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), TP_TYPE_CONTACT))
#define TP_CONTACT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TP_TYPE_CONTACT, \
                              TpContactClass))

typedef enum {
    TP_CONTACT_FEATURE_ALIAS,
    TP_CONTACT_FEATURE_AVATAR_TOKEN,
    TP_CONTACT_FEATURE_PRESENCE,
    TP_CONTACT_FEATURE_LOCATION,
    TP_CONTACT_FEATURE_CAPABILITIES,
    TP_CONTACT_FEATURE_AVATAR_DATA,
    TP_CONTACT_FEATURE_CONTACT_INFO,
    TP_CONTACT_FEATURE_CLIENT_TYPES,
    TP_CONTACT_FEATURE_SUBSCRIPTION_STATES,
    TP_CONTACT_FEATURE_CONTACT_GROUPS,
} TpContactFeature;
#define NUM_TP_CONTACT_FEATURES (TP_CONTACT_FEATURE_CONTACT_GROUPS + 1)

/* Basic functionality, always available */
TpConnection *tp_contact_get_connection (TpContact *self);
TpHandle tp_contact_get_handle (TpContact *self);
const gchar *tp_contact_get_identifier (TpContact *self);
gboolean tp_contact_has_feature (TpContact *self, TpContactFeature feature);

/* TP_CONTACT_FEATURE_ALIAS */
const gchar *tp_contact_get_alias (TpContact *self);

/* TP_CONTACT_FEATURE_AVATAR_TOKEN */
const gchar *tp_contact_get_avatar_token (TpContact *self);

/* TP_CONTACT_FEATURE_PRESENCE */
TpConnectionPresenceType tp_contact_get_presence_type (TpContact *self);
const gchar *tp_contact_get_presence_status (TpContact *self);
const gchar *tp_contact_get_presence_message (TpContact *self);

/* TP_CONTACT_FEATURE_LOCATION */
GHashTable *tp_contact_get_location (TpContact *self);

/* TP_CONTACT_FEATURE_CAPABILITIES */
TpCapabilities *tp_contact_get_capabilities (TpContact *self);

/* TP_CONTACT_FEATURE_AVATAR_DATA */
GFile *tp_contact_get_avatar_file (TpContact *self);
const gchar *tp_contact_get_avatar_mime_type (TpContact *self);

/* TP_CONTACT_FEATURE_INFO */
GList *tp_contact_get_contact_info (TpContact *self);

void tp_contact_request_contact_info_async (TpContact *self,
    GCancellable *cancellable, GAsyncReadyCallback callback,
    gpointer user_data);

gboolean tp_contact_request_contact_info_finish (TpContact *self,
    GAsyncResult *result, GError **error);

void tp_connection_refresh_contact_info (TpConnection *self,
    guint n_contacts, TpContact * const *contacts);

/* TP_CONTACT_FEATURE_CLIENT_TYPES */
const gchar * const *
/* this comment stops gtkdoc denying that this function exists */
tp_contact_get_client_types (TpContact *self);

/* TP_CONTACT_FEATURE_SUBSCRIPTION_STATES */
TpSubscriptionState tp_contact_get_subscribe_state (TpContact *self);
TpSubscriptionState tp_contact_get_publish_state (TpContact *self);
const gchar *tp_contact_get_publish_request (TpContact *self);

/* TP_CONTACT_FEATURE_CONTACT_GROUPS */
const gchar * const *
/* this comment stops gtkdoc denying that this function exists */
tp_contact_get_contact_groups (TpContact *self);
void tp_contact_set_contact_groups_async (TpContact *self,
    gint n_groups, const gchar * const *groups, GAsyncReadyCallback callback,
    gpointer user_data);
gboolean tp_contact_set_contact_groups_finish (TpContact *self,
    GAsyncResult *result, GError **error);

typedef void (*TpConnectionContactsByHandleCb) (TpConnection *connection,
    guint n_contacts, TpContact * const *contacts,
    guint n_failed, const TpHandle *failed,
    const GError *error, gpointer user_data, GObject *weak_object);

void tp_connection_get_contacts_by_handle (TpConnection *self,
    guint n_handles, const TpHandle *handles,
    guint n_features, const TpContactFeature *features,
    TpConnectionContactsByHandleCb callback,
    gpointer user_data, GDestroyNotify destroy, GObject *weak_object);

typedef void (*TpConnectionUpgradeContactsCb) (TpConnection *connection,
    guint n_contacts, TpContact * const *contacts,
    const GError *error, gpointer user_data, GObject *weak_object);

void tp_connection_upgrade_contacts (TpConnection *self,
    guint n_contacts, TpContact * const *contacts,
    guint n_features, const TpContactFeature *features,
    TpConnectionUpgradeContactsCb callback,
    gpointer user_data, GDestroyNotify destroy, GObject *weak_object);

typedef void (*TpConnectionContactsByIdCb) (TpConnection *connection,
    guint n_contacts, TpContact * const *contacts,
    const gchar * const *requested_ids, GHashTable *failed_id_errors,
    const GError *error, gpointer user_data, GObject *weak_object);

void tp_connection_get_contacts_by_id (TpConnection *self,
    guint n_ids, const gchar * const *ids,
    guint n_features, const TpContactFeature *features,
    TpConnectionContactsByIdCb callback,
    gpointer user_data, GDestroyNotify destroy, GObject *weak_object);

TpContact *tp_connection_dup_contact_if_possible (TpConnection *connection,
    TpHandle handle, const gchar *identifier);

G_END_DECLS

#endif
