/* ContactList channel manager
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

#ifndef __TP_BASE_CONTACT_LIST_H__
#define __TP_BASE_CONTACT_LIST_H__

#include <glib-object.h>
#include <gio/gio.h>

#include <telepathy-glib/handle-repo.h>

G_BEGIN_DECLS

typedef struct _TpBaseContactList TpBaseContactList;
typedef struct _TpBaseContactListClass TpBaseContactListClass;
typedef struct _TpBaseContactListPrivate TpBaseContactListPrivate;
typedef struct _TpBaseContactListClassPrivate TpBaseContactListClassPrivate;

struct _TpBaseContactList {
    /*<private>*/
    GObject parent;
    TpBaseContactListPrivate *priv;
};

GType tp_base_contact_list_get_type (void);

#define TP_TYPE_BASE_CONTACT_LIST \
  (tp_base_contact_list_get_type ())
#define TP_BASE_CONTACT_LIST(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), TP_TYPE_BASE_CONTACT_LIST, \
                               TpBaseContactList))
#define TP_BASE_CONTACT_LIST_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), TP_TYPE_BASE_CONTACT_LIST, \
                            TpBaseContactListClass))
#define TP_IS_BASE_CONTACT_LIST(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TP_TYPE_BASE_CONTACT_LIST))
#define TP_IS_BASE_CONTACT_LIST_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), TP_TYPE_BASE_CONTACT_LIST))
#define TP_BASE_CONTACT_LIST_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TP_TYPE_BASE_CONTACT_LIST, \
                              TpBaseContactListClass))

/* ---- Will be in telepathy-spec later (so, no GEnum) ---- */

typedef enum { /*< skip >*/
    TP_PRESENCE_STATE_NO,
    TP_PRESENCE_STATE_ASK,
    TP_PRESENCE_STATE_YES
} TpPresenceState;

/* ---- Called by subclasses for ContactList (or both) ---- */

void tp_base_contact_list_set_list_received (TpBaseContactList *self);

void tp_base_contact_list_contacts_changed (TpBaseContactList *self,
    TpHandleSet *changed,
    TpHandleSet *removed);

/* ---- Implemented by subclasses for ContactList (mandatory read-only
 * things) ---- */

typedef gboolean (*TpBaseContactListBooleanFunc) (
    TpBaseContactList *self);

gboolean tp_base_contact_list_true_func (TpBaseContactList *self);
gboolean tp_base_contact_list_false_func (TpBaseContactList *self);

gboolean tp_base_contact_list_get_subscriptions_persist (
    TpBaseContactList *self);

typedef TpHandleSet *(*TpBaseContactListGetContactsFunc) (
    TpBaseContactList *self);

TpHandleSet *tp_base_contact_list_get_contacts (TpBaseContactList *self);

typedef void (*TpBaseContactListGetStatesFunc) (
    TpBaseContactList *self,
    TpHandle contact,
    TpPresenceState *subscribe,
    TpPresenceState *publish,
    gchar **publish_request);

void tp_base_contact_list_get_states (TpBaseContactList *self,
    TpHandle contact,
    TpPresenceState *subscribe,
    TpPresenceState *publish,
    gchar **publish_request);

struct _TpBaseContactListClass {
    GObjectClass parent_class;

    TpBaseContactListGetContactsFunc get_contacts;
    TpBaseContactListGetStatesFunc get_states;
    TpBaseContactListBooleanFunc get_subscriptions_persist;

    /*<private>*/
    GCallback _padding[7];
    TpBaseContactListClassPrivate *priv;
};

/* ---- Implemented by subclasses for ContactList modification ---- */

#define TP_TYPE_MUTABLE_CONTACT_LIST \
  (tp_mutable_contact_list_get_type ())

#define TP_IS_MUTABLE_CONTACT_LIST(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  TP_TYPE_MUTABLE_CONTACT_LIST))

#define TP_MUTABLE_CONTACT_LIST_GET_INTERFACE(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), \
  TP_TYPE_MUTABLE_CONTACT_LIST, TpMutableContactListInterface))

typedef struct _TpMutableContactListInterface TpMutableContactListInterface;

typedef void (*TpBaseContactListRequestSubscriptionFunc) (
    TpBaseContactList *self,
    TpHandleSet *contacts,
    const gchar *message);

typedef void (*TpBaseContactListActOnContactsFunc) (
    TpBaseContactList *self,
    TpHandleSet *contacts);

struct _TpMutableContactListInterface {
    GTypeInterface parent;
    /* mandatory-to-implement */
    TpBaseContactListRequestSubscriptionFunc request_subscription;
    TpBaseContactListActOnContactsFunc authorize_publication;
    TpBaseContactListActOnContactsFunc remove_contacts;
    TpBaseContactListActOnContactsFunc unsubscribe;
    TpBaseContactListActOnContactsFunc unpublish;
    /* optional-to-implement */
    TpBaseContactListActOnContactsFunc store_contacts;
    TpBaseContactListBooleanFunc can_change_subscriptions;
    TpBaseContactListBooleanFunc get_request_uses_message;
};

GType tp_mutable_contact_list_get_type (void) G_GNUC_CONST;

gboolean tp_base_contact_list_can_change_subscriptions (
    TpBaseContactList *self);

gboolean tp_base_contact_list_get_request_uses_message (
    TpBaseContactList *self);

void tp_base_contact_list_request_subscription (TpBaseContactList *self,
    TpHandleSet *contacts,
    const gchar *message);

void tp_base_contact_list_authorize_publication (TpBaseContactList *self,
    TpHandleSet *contacts);

void tp_base_contact_list_store_contacts (TpBaseContactList *self,
    TpHandleSet *contacts);

void tp_base_contact_list_remove_contacts (TpBaseContactList *self,
    TpHandleSet *contacts);

void tp_base_contact_list_unsubscribe (TpBaseContactList *self,
    TpHandleSet *contacts);

void tp_base_contact_list_unpublish (TpBaseContactList *self,
    TpHandleSet *contacts);

/* ---- contact blocking ---- */

void tp_base_contact_list_contact_blocking_changed (
    TpBaseContactList *self,
    TpHandleSet *changed);

void tp_base_contact_list_class_implement_can_block (
    TpBaseContactListClass *cls,
    TpBaseContactListBooleanFunc check);

void tp_base_contact_list_class_implement_get_blocked_contacts (
    TpBaseContactListClass *cls,
    TpBaseContactListGetContactsFunc impl);

typedef gboolean (*TpBaseContactListContactBooleanFunc) (
    TpBaseContactList *self,
    TpHandle contact);

void tp_base_contact_list_class_implement_get_contact_blocked (
    TpBaseContactListClass *cls,
    TpBaseContactListContactBooleanFunc impl);

void tp_base_contact_list_class_implement_block_contacts (
    TpBaseContactListClass *cls,
    TpBaseContactListActOnContactsFunc impl);

void tp_base_contact_list_class_implement_unblock_contacts (
    TpBaseContactListClass *cls,
    TpBaseContactListActOnContactsFunc impl);

/* ---- Called by subclasses for ContactGroups ---- */

void tp_base_contact_list_groups_created (TpBaseContactList *self,
    const gchar * const *created, gssize n_created);

void tp_base_contact_list_groups_removed (TpBaseContactList *self,
    const gchar * const *removed, gssize n_removed);

void tp_base_contact_list_group_renamed (TpBaseContactList *self,
    const gchar *old_name,
    const gchar *new_name);

void tp_base_contact_list_groups_changed (TpBaseContactList *self,
    TpHandleSet *contacts,
    const gchar * const *added, gssize n_added,
    const gchar * const *removed, gssize n_removed);

/* ---- Implemented by subclasses for ContactGroups ---- */

void tp_base_contact_list_class_implement_disjoint_groups (
    TpBaseContactListClass *cls,
    TpBaseContactListBooleanFunc impl);

typedef GStrv (*TpBaseContactListGetGroupsFunc) (
    TpBaseContactList *self);

void tp_base_contact_list_class_implement_get_groups (
    TpBaseContactListClass *cls,
    TpBaseContactListGetGroupsFunc impl);

typedef GStrv (*TpBaseContactListGetContactGroupsFunc) (
    TpBaseContactList *self,
    TpHandle contact);

void tp_base_contact_list_class_implement_get_contact_groups (
    TpBaseContactListClass *cls,
    TpBaseContactListGetContactGroupsFunc impl);

typedef gchar *(*TpBaseContactListNormalizeFunc) (
    TpBaseContactList *self,
    const gchar *s);

void tp_base_contact_list_class_implement_normalize_group (
    TpBaseContactListClass *cls,
    TpBaseContactListNormalizeFunc impl);

typedef void (*TpBaseContactListCreateGroupsFunc) (
    TpBaseContactList *self,
    const gchar * const *normalized_names,
    gsize n_names);

void tp_base_contact_list_class_implement_create_groups (
    TpBaseContactListClass *cls,
    TpBaseContactListCreateGroupsFunc impl);

typedef void (*TpBaseContactListGroupContactsFunc) (
    TpBaseContactList *self,
    const gchar *group,
    TpHandleSet *contacts);

void tp_base_contact_list_class_implement_add_to_group (
    TpBaseContactListClass *cls,
    TpBaseContactListGroupContactsFunc impl);

void tp_base_contact_list_class_implement_remove_from_group (
    TpBaseContactListClass *cls,
    TpBaseContactListGroupContactsFunc impl);

typedef gboolean (*TpBaseContactListRemoveGroupFunc) (
    TpBaseContactList *self,
    const gchar *group,
    GError **error);

void tp_base_contact_list_class_implement_remove_group (
    TpBaseContactListClass *cls,
    TpBaseContactListRemoveGroupFunc impl);

G_END_DECLS

#endif
