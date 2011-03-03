/*
 * account-manager.h - proxy for an account in the Telepathy account manager
 *
 * Copyright (C) 2009 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2009 Nokia Corporation
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

#ifndef TP_ACCOUNT_H
#define TP_ACCOUNT_H

#include <gio/gio.h>

#include <telepathy-glib/connection.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/defs.h>
#include <telepathy-glib/proxy.h>

G_BEGIN_DECLS

typedef struct _TpAccount TpAccount;
typedef struct _TpAccountClass TpAccountClass;
typedef struct _TpAccountPrivate TpAccountPrivate;
typedef struct _TpAccountClassPrivate TpAccountClassPrivate;

struct _TpAccount {
    /*<private>*/
    TpProxy parent;
    TpAccountPrivate *priv;
};

struct _TpAccountClass {
    /*<private>*/
    TpProxyClass parent_class;
    GCallback _padding[7];
    TpAccountClassPrivate *priv;
};

GType tp_account_get_type (void);

#define TP_TYPE_ACCOUNT \
  (tp_account_get_type ())
#define TP_ACCOUNT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), TP_TYPE_ACCOUNT, \
                               TpAccount))
#define TP_ACCOUNT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), TP_TYPE_ACCOUNT, \
                            TpAccountClass))
#define TP_IS_ACCOUNT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TP_TYPE_ACCOUNT))
#define TP_IS_ACCOUNT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), TP_TYPE_ACCOUNT))
#define TP_ACCOUNT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TP_TYPE_ACCOUNT, \
                              TpAccountClass))

#define TP_ACCOUNT_FEATURE_CORE \
  tp_account_get_feature_quark_core ()
#define TP_ACCOUNT_FEATURE_STORAGE \
  tp_account_get_feature_quark_storage ()
#define TP_ACCOUNT_FEATURE_ADDRESSING \
  tp_account_get_feature_quark_addressing ()

GQuark tp_account_get_feature_quark_core (void) G_GNUC_CONST;
GQuark tp_account_get_feature_quark_storage (void) G_GNUC_CONST;
GQuark tp_account_get_feature_quark_addressing (void) G_GNUC_CONST;

TpAccount *tp_account_new (TpDBusDaemon *bus_daemon, const gchar *object_path,
    GError **error) G_GNUC_WARN_UNUSED_RESULT;

gboolean tp_account_parse_object_path (const gchar *object_path,
    gchar **cm, gchar **protocol, gchar **account_id, GError **error);
const gchar *tp_account_get_path_suffix (TpAccount *account);

void tp_account_init_known_interfaces (void);

TpConnection *tp_account_get_connection (TpAccount *account);

TpConnection *tp_account_ensure_connection (TpAccount *account,
    const gchar *path);

const gchar *tp_account_get_display_name (TpAccount *account);

const gchar *tp_account_get_connection_manager (TpAccount *account);

const gchar *tp_account_get_protocol (TpAccount *account);
const gchar *tp_account_get_service (TpAccount *self);

const gchar *tp_account_get_icon_name (TpAccount *account);

const gchar *tp_account_get_normalized_name (TpAccount *self);

void tp_account_set_enabled_async (TpAccount *account,
    gboolean enabled, GAsyncReadyCallback callback, gpointer user_data);

gboolean tp_account_set_enabled_finish (TpAccount *account,
    GAsyncResult *result, GError **error);

void tp_account_reconnect_async (TpAccount *account,
    GAsyncReadyCallback callback, gpointer user_data);

gboolean tp_account_reconnect_finish (TpAccount *account,
    GAsyncResult *result, GError **error);

gboolean tp_account_is_enabled (TpAccount *account);

gboolean tp_account_is_valid (TpAccount *account);

void tp_account_update_parameters_async (TpAccount *account,
    GHashTable *parameters, const gchar **unset_parameters,
    GAsyncReadyCallback callback, gpointer user_data);

gboolean tp_account_update_parameters_finish (TpAccount *account,
    GAsyncResult *result, gchar ***reconnect_required, GError **error);

void tp_account_remove_async (TpAccount *account,
    GAsyncReadyCallback callback, gpointer user_data);

gboolean tp_account_remove_finish (TpAccount *account,
    GAsyncResult *result, GError **error);

void tp_account_set_display_name_async (TpAccount *account,
    const gchar *display_name, GAsyncReadyCallback callback,
    gpointer user_data);

gboolean tp_account_set_display_name_finish (TpAccount *account,
    GAsyncResult *result, GError **error);

void tp_account_set_icon_name_async (TpAccount *account,
    const gchar *icon_name, GAsyncReadyCallback callback,
    gpointer user_data);

gboolean tp_account_set_icon_name_finish (TpAccount *account,
    GAsyncResult *result, GError **error);

void tp_account_set_service_async (TpAccount *self,
    const gchar *service, GAsyncReadyCallback callback,
    gpointer user_data);

gboolean tp_account_set_service_finish (TpAccount *self,
    GAsyncResult *result, GError **error);

void tp_account_request_presence_async (TpAccount *account,
    TpConnectionPresenceType type, const gchar *status, const gchar *message,
    GAsyncReadyCallback callback, gpointer user_data);

gboolean tp_account_request_presence_finish (TpAccount *account,
    GAsyncResult *result, GError **error);

void tp_account_set_automatic_presence_async (TpAccount *account,
    TpConnectionPresenceType type, const gchar *status, const gchar *message,
    GAsyncReadyCallback callback, gpointer user_data);

gboolean tp_account_set_automatic_presence_finish (TpAccount *account,
    GAsyncResult *result, GError **error);

gboolean tp_account_get_connect_automatically (TpAccount *account);

void tp_account_set_connect_automatically_async (TpAccount *account,
    gboolean connect_automatically, GAsyncReadyCallback callback,
    gpointer user_data);

gboolean tp_account_set_connect_automatically_finish (TpAccount *account,
    GAsyncResult *result, GError **error);

gboolean tp_account_get_has_been_online (TpAccount *account);

TpConnectionStatus tp_account_get_connection_status (TpAccount *account,
    TpConnectionStatusReason *reason);

const gchar *tp_account_get_detailed_error (TpAccount *self,
    const GHashTable **details);

TpConnectionPresenceType tp_account_get_current_presence (TpAccount *account,
    gchar **status, gchar **status_message);

TpConnectionPresenceType tp_account_get_requested_presence (
    TpAccount *account, gchar **status, gchar **status_message);

TpConnectionPresenceType tp_account_get_automatic_presence (
    TpAccount *self, gchar **status, gchar **status_message);

const GHashTable *tp_account_get_parameters (TpAccount *account);

const gchar *tp_account_get_nickname (TpAccount *account);

void tp_account_set_nickname_async (TpAccount *account,
    const gchar *nickname, GAsyncReadyCallback callback, gpointer user_data);

gboolean tp_account_set_nickname_finish (TpAccount *account,
    GAsyncResult *result, GError **error);

void tp_account_get_avatar_async (TpAccount *account,
    GAsyncReadyCallback callback, gpointer user_data);

const GArray *tp_account_get_avatar_finish (TpAccount *account,
    GAsyncResult *result, GError **error);

gboolean tp_account_is_prepared (TpAccount *account, GQuark feature);

void tp_account_prepare_async (TpAccount *account, const GQuark *features,
    GAsyncReadyCallback callback, gpointer user_data);

gboolean tp_account_prepare_finish (TpAccount *account, GAsyncResult *result,
    GError **error);

void tp_account_set_avatar_async (TpAccount *self,
    const guchar *avatar,
    gsize len,
    const gchar *mime_type,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean tp_account_set_avatar_finish (TpAccount *self,
    GAsyncResult *result,
    GError **error);

gboolean tp_account_get_changing_presence (TpAccount *self);

const gchar *tp_account_get_storage_provider (TpAccount *self);
const GValue *tp_account_get_storage_identifier (TpAccount *self);
TpStorageRestrictionFlags tp_account_get_storage_restrictions (TpAccount *self);
void tp_account_get_storage_specific_information_async (TpAccount *self,
    GAsyncReadyCallback callback, gpointer user_data);
GHashTable *tp_account_get_storage_specific_information_finish (TpAccount *self,
    GAsyncResult *result, GError **error);

const gchar * const *
/* ugh, gtk-doc */
tp_account_get_uri_schemes (TpAccount *self);
gboolean tp_account_associated_with_uri_scheme (TpAccount *self,
    const gchar *scheme);
void tp_account_set_uri_scheme_association_async (TpAccount *self,
    const gchar *scheme, gboolean associate,
    GAsyncReadyCallback callback, gpointer user_data);
gboolean tp_account_set_uri_scheme_association_finish (TpAccount *self,
    GAsyncResult *result, GError **error);

GBinding *tp_account_bind_connection_status_to_property (TpAccount *self,
    gpointer target, const char *target_property, gboolean invert);

G_END_DECLS

#include <telepathy-glib/_gen/tp-cli-account.h>

#endif
