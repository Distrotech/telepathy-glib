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

#include <config.h>
#include <telepathy-glib/base-contact-list.h>
#include <telepathy-glib/base-contact-list-internal.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/handle-repo-static.h>
#include <telepathy-glib/interfaces.h>

#include <telepathy-glib/base-connection-internal.h>
#include <telepathy-glib/contact-list-channel-internal.h>
#include <telepathy-glib/handle-repo-internal.h>

/**
 * SECTION:base-contact-list
 * @title: TpBaseContactList
 * @short_description: channel manager for ContactList channels
 *
 * This class represents a connection's contact list (roster, buddy list etc.)
 * inside a connection manager. It can be used to implement the ContactList
 * D-Bus interface on the Connection.
 *
 * In versions of the Telepathy D-Bus Interface Specification prior to
 * 0.19.UNRELEASED, this functionality was provided as a collection of
 * individual ContactList channels. As a result, this object also implements
 * the #TpChannelManager interface, so that it can provide those channels.
 * The channel objects are internal to this object, and not considered to be
 * part of the API.
 */

/**
 * TpBaseContactList:
 *
 * A connection's contact list (roster, buddy list) inside a connection
 * manager. Each #TpBaseConnection may have at most one #TpBaseContactList.
 *
 * This abstract base class provides the Telepathy "view" of the contact list:
 * subclasses must provide access to the "model" by implementing its virtual
 * methods in terms of the protocol's real contact list (e.g. the XMPP roster
 * object in Wocky).
 *
 * The implementation must call tp_base_contact_list_set_list_received()
 * exactly once, when the initial set of contacts has been received (or
 * immediately, if that condition is not meaningful for the protocol).
 *
 * Since: 0.11.UNRELEASED
 */

/**
 * TpBaseContactListClass:
 * @parent_class: the parent class
 * @get_contacts: the implementation of tp_base_contact_list_get_contacts();
 *  every subclass must implement this itself
 * @get_states: the implementation of
 *  tp_base_contact_list_get_states(); every subclass must implement
 *  this itself
 * @get_subscriptions_persist: the implementation of
 *  tp_base_contact_list_get_subscriptions_persist(); if a subclass does not
 *  implement this itself, the default implementation always returns %TRUE,
 *  which is correct for most protocols
 *
 * The class of a #TpBaseContactList.
 *
 * Additional functionality can be added by implementing #GInterface<!-- -->s.
 * Most subclasses should implement %TP_TYPE_MUTABLE_CONTACT_LIST, which allows
 * the contact list to be altered.
 *
 * Subclasses may implement %TP_TYPE_BLOCKABLE_CONTACT_LIST if contacts can
 * be blocked from communicating with the user.
 *
 * Since: 0.11.UNRELEASED
 */

/**
 * TpPresenceState:
 * @TP_PRESENCE_STATE_NO: No subscription exists
 * @TP_PRESENCE_STATE_ASK: No subscription exists but one has been requested
 * @TP_PRESENCE_STATE_YES: A subscription exists
 *
 * The extent of a subscription to presence.
 *
 * (This is temporary, and will be generated from telepathy-spec later.)
 */

/**
 * TpBaseContactListGetContactsFunc:
 * @self: the contact list manager
 *
 * Signature of a virtual method to list contacts with a particular state;
 * the required state is defined by the particular virtual method being
 * implemented.
 *
 * The implementation is expected to have a cache of contacts on the contact
 * list, which is updated based on protocol events.
 *
 * Returns: (transfer full): a set of contacts with the desired state
 */

/**
 * TpBaseContactListGetStatesFunc:
 * @self: the contact list manager
 * @contact: the contact
 * @subscribe: (out): used to return the state of the user's subscription to
 *  @contact's presence
 * @publish: (out): used to return the state of @contact's subscription to
 *  the user's presence
 * @publish_request: (out): if @publish will be set to %TP_PRESENCE_STATE_ASK,
 *  used to return the message that @contact sent when they requested
 *  permission to see the user's presence; otherwise, used to return an empty
 *  string
 *
 * Signature of a virtual method to get contacts' presences. It should return
 * @subscribe = %TP_PRESENCE_STATE_NO, @publish = %TP_PRESENCE_STATE_NO
 * and @publish_request = "", without error, for any contact not on the
 * contact list.
 */

/**
 * TpBaseContactListActOnContactsFunc:
 * @self: the contact list manager
 * @contacts: the contacts on which to act
 *
 * Signature of a virtual method that acts on a set of contacts and needs no
 * additional information, such as removing contacts, approving or cancelling
 * presence publication, cancelling presence subscription, or removing
 * contacts.
 *
 * The virtual method should call tp_base_contact_list_contacts_changed()
 * for any contacts it has changed, before returning.
 */

/**
 * TpBaseContactListRequestSubscriptionFunc:
 * @self: the contact list manager
 * @contacts: the contacts whose subscription is to be requested
 * @message: an optional human-readable message from the user
 *
 * Signature of a virtual method to request permission to see some contacts'
 * presence.
 *
 * The virtual method should call tp_base_contact_list_contacts_changed()
 * for any contacts it has changed, before returning.
 */

#include <telepathy-glib/base-connection.h>

#include <telepathy-glib/handle-repo.h>

#define DEBUG_FLAG TP_DEBUG_CONTACT_LISTS
#include "telepathy-glib/debug-internal.h"

struct _TpBaseContactListPrivate
{
  TpBaseConnection *conn;
  TpHandleRepoIface *contact_repo;

  /* values referenced; 0'th remains NULL */
  TpBaseContactListChannel *lists[NUM_TP_LIST_HANDLES];

  TpHandleRepoIface *group_repo;
  /* handle borrowed from channel => referenced TpContactGroupChannel */
  GHashTable *groups;

  /* FALSE until the contact list has turned up */
  gboolean had_contact_list;
  /* borrowed TpExportableChannel => GSList of gpointer (request tokens) that
   * will be satisfied by that channel when the contact list has been
   * downloaded. The requests are in reverse chronological order.
   *
   * This becomes NULL when the contact list has been downloaded. */
  GHashTable *channel_requests;

  gulong status_changed_id;
};

struct _TpBaseContactListClassPrivate
{
  TpBaseContactListGetGroupsFunc get_groups;
  TpBaseContactListGetContactGroupsFunc get_contact_groups;
  TpBaseContactListBooleanFunc disjoint_groups;
  TpBaseContactListNormalizeFunc normalize_group;
  TpBaseContactListCreateGroupsFunc create_groups;
  TpBaseContactListGroupContactsFunc add_to_group;
  TpBaseContactListGroupContactsFunc remove_from_group;
  TpBaseContactListRemoveGroupFunc remove_group;
};

static void channel_manager_iface_init (TpChannelManagerIface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (TpBaseContactList,
    tp_base_contact_list,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init);
    g_type_add_class_private (g_define_type_id, sizeof (
        TpBaseContactListClassPrivate)))

/**
 * TP_TYPE_MUTABLE_CONTACT_LIST:
 *
 * Interface representing a #TpBaseContactList on which the contact list can
 * potentially be changed.
 */

/**
 * TpMutableContactListInterface:
 * @parent: the parent interface
 * @request_subscription: the implementation of
 *  tp_base_contact_list_request_subscription(); must always be provided
 * @authorize_publication: the implementation of
 *  tp_base_contact_list_authorize_publication(); must always be provided
 * @remove_contacts: the implementation of
 *  tp_base_contact_list_remove_contacts(); must always be provided
 * @unsubscribe: the implementation of
 *  tp_base_contact_list_unsubscribe(); must always be provided
 * @unpublish: the implementation of
 *  tp_base_contact_list_unpublish(); must always be provided
 * @store_contacts: the implementation of
 *  tp_base_contact_list_store_contacts(); if not reimplemented,
 *  the default implementation is %NULL, which is interpreted as "do nothing"
 * @can_change_subscriptions: the implementation of
 *  tp_base_contact_list_can_change_subscriptions(); if not reimplemented,
 *  the default implementation always returns %TRUE
 * @get_request_uses_message: the implementation of
 *  tp_base_contact_list_get_request_uses_message(); if not reimplemented,
 *  the default implementation always returns %TRUE
 *
 * The interface vtable for a %TP_TYPE_MUTABLE_CONTACT_LIST.
 */

G_DEFINE_INTERFACE (TpMutableContactList, tp_mutable_contact_list,
    TP_TYPE_BASE_CONTACT_LIST)

/**
 * TP_TYPE_BLOCKABLE_CONTACT_LIST:
 *
 * Interface representing a #TpBaseContactList on which contacts can
 * be blocked from communicating with the user.
 */

/**
 * TpBlockableContactListInterface:
 * @parent: the parent interface
 * @get_blocked_contacts: the implementation of
 *  tp_base_contact_list_get_blocked_contacts(); must always be provided
 * @block_contacts: the implementation of
 *  tp_base_contact_list_block_contacts(); must always be provided
 * @unblock_contacts: the implementation of
 *  tp_base_contact_list_unblock_contacts(); must always be provided
 * @can_block: the implementation of
 *  tp_base_contact_list_can_block(); if not reimplemented,
 *  the default implementation always returns %TRUE
 *
 * The interface vtable for a %TP_TYPE_BLOCKABLE_CONTACT_LIST.
 */

G_DEFINE_INTERFACE (TpBlockableContactList, tp_blockable_contact_list,
    TP_TYPE_BASE_CONTACT_LIST)

enum {
    PROP_CONNECTION = 1,
    N_PROPS
};

static void
tp_base_contact_list_init (TpBaseContactList *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, TP_TYPE_BASE_CONTACT_LIST,
      TpBaseContactListPrivate);
  self->priv->groups = g_hash_table_new_full (NULL, NULL, NULL,
      g_object_unref);
  self->priv->channel_requests = g_hash_table_new (NULL, NULL);
}

static gboolean
tp_base_contact_list_check_still_usable (TpBaseContactList *self,
    GError **error)
{
  if (self->priv->conn == NULL)
    g_set_error_literal (error, TP_ERRORS, TP_ERROR_DISCONNECTED,
        "Connection is no longer connected");

  return (self->priv->conn != NULL);
}

static void
tp_base_contact_list_free_contents (TpBaseContactList *self)
{
  guint i;

  if (self->priv->channel_requests != NULL)
    {
      GHashTable *tmp = self->priv->channel_requests;
      GHashTableIter iter;
      gpointer key, value;

      self->priv->channel_requests = NULL;
      g_hash_table_iter_init (&iter, tmp);

      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          GSList *requests = value;
          GSList *link;

          requests = g_slist_reverse (requests);

          for (link = requests; link != NULL; link = link->next)
            {
              tp_channel_manager_emit_request_failed (self,
                  link->data, TP_ERRORS, TP_ERROR_DISCONNECTED,
                  "Unable to complete channel request due to disconnection");
            }

          g_slist_free (requests);
          g_hash_table_iter_steal (&iter);
        }

      g_hash_table_destroy (tmp);
    }

  for (i = 0; i < NUM_TP_LIST_HANDLES; i++)
    tp_clear_object (self->priv->lists + i);

  tp_clear_pointer (&self->priv->groups, g_hash_table_unref);
  tp_clear_object (&self->priv->contact_repo);

  if (self->priv->group_repo != NULL)
    {
      /* the normalization data is a borrowed reference to @self, which must
       * be released when @self is no longer usable */
      _tp_dynamic_handle_repo_set_normalization_data (self->priv->group_repo,
          NULL, NULL);
      tp_clear_object (&self->priv->group_repo);
    }

  if (self->priv->conn != NULL)
    {
      if (self->priv->status_changed_id != 0)
        {
          g_signal_handler_disconnect (self->priv->conn,
              self->priv->status_changed_id);
          self->priv->status_changed_id = 0;
        }

      tp_clear_object (&self->priv->conn);
    }
}

static void
tp_base_contact_list_dispose (GObject *object)
{
  TpBaseContactList *self = TP_BASE_CONTACT_LIST (object);
  void (*dispose) (GObject *) =
    G_OBJECT_CLASS (tp_base_contact_list_parent_class)->dispose;

  tp_base_contact_list_free_contents (self);
  g_assert (self->priv->groups == NULL);
  g_assert (self->priv->contact_repo == NULL);
  g_assert (self->priv->group_repo == NULL);
  g_assert (self->priv->lists[TP_LIST_HANDLE_SUBSCRIBE] == NULL);
  g_assert (self->priv->channel_requests == NULL);

  if (dispose != NULL)
    dispose (object);
}

static void
tp_base_contact_list_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  TpBaseContactList *self = TP_BASE_CONTACT_LIST (object);

  switch (property_id)
    {
    case PROP_CONNECTION:
      g_value_set_object (value, self->priv->conn);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
tp_base_contact_list_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  TpBaseContactList *self = TP_BASE_CONTACT_LIST (object);

  switch (property_id)
    {
    case PROP_CONNECTION:
      g_assert (self->priv->conn == NULL);    /* construct-only */
      self->priv->conn = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static gchar *
tp_base_contact_list_normalize_group (TpHandleRepoIface *repo,
    const gchar *id,
    gpointer context,
    GError **error)
{
  TpBaseContactList *self =
    _tp_dynamic_handle_repo_get_normalization_data (repo);
  TpBaseContactListClass *cls;
  gchar *ret;

  if (id == NULL)
    id = "";

  if (self == NULL)
    {
      /* already disconnected or something */
      return g_strdup (id);
    }

  cls = TP_BASE_CONTACT_LIST_GET_CLASS (self);

  if (cls->priv->normalize_group == NULL)
    return g_strdup (id);

  ret = cls->priv->normalize_group (self, id);

  if (ret == NULL)
    g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_HANDLE,
        "Invalid group name '%s'", id);

  return ret;
}

/* elements 0, 1... of this enum must be kept in sync with elements 1, 2...
 * of the enum in the -internal header */
static const gchar * const tp_base_contact_list_contact_lists
  [NUM_TP_LIST_HANDLES + 1] = {
    "subscribe",
    "publish",
    "stored",
    "deny",
    NULL
};

static void
status_changed_cb (TpBaseConnection *conn,
    guint status,
    guint reason,
    TpBaseContactList *self)
{
  if (status == TP_CONNECTION_STATUS_DISCONNECTED)
    tp_base_contact_list_free_contents (self);
}

static void
tp_base_contact_list_constructed (GObject *object)
{
  TpBaseContactList *self = TP_BASE_CONTACT_LIST (object);
  TpBaseContactListClass *cls = TP_BASE_CONTACT_LIST_GET_CLASS (self);
  void (*chain_up) (GObject *) =
    G_OBJECT_CLASS (tp_base_contact_list_parent_class)->constructed;
  TpHandleRepoIface *list_repo;

  if (chain_up != NULL)
    chain_up (object);

  g_assert (self->priv->conn != NULL);

  g_return_if_fail (cls->get_contacts != NULL);
  g_return_if_fail (cls->get_states != NULL);
  g_return_if_fail (cls->get_subscriptions_persist != NULL);

  if (TP_IS_MUTABLE_CONTACT_LIST (self))
    {
      TpMutableContactListInterface *iface =
        TP_MUTABLE_CONTACT_LIST_GET_INTERFACE (self);

      g_return_if_fail (iface->can_change_subscriptions != NULL);
      g_return_if_fail (iface->get_request_uses_message != NULL);
      g_return_if_fail (iface->request_subscription != NULL);
      g_return_if_fail (iface->authorize_publication != NULL);
      /* iface->store_contacts == NULL is OK */
      g_return_if_fail (iface->remove_contacts != NULL);
      g_return_if_fail (iface->unsubscribe != NULL);
      g_return_if_fail (iface->unpublish != NULL);
    }

  if (TP_IS_BLOCKABLE_CONTACT_LIST (self))
    {
      TpBlockableContactListInterface *iface =
        TP_BLOCKABLE_CONTACT_LIST_GET_INTERFACE (self);

      g_return_if_fail (iface->can_block != NULL);
      g_return_if_fail (iface->get_blocked_contacts != NULL);
      g_return_if_fail (iface->block_contacts != NULL);
      g_return_if_fail (iface->unblock_contacts != NULL);
    }

  self->priv->contact_repo = tp_base_connection_get_handles (self->priv->conn,
      TP_HANDLE_TYPE_CONTACT);
  g_object_ref (self->priv->contact_repo);

  list_repo = tp_static_handle_repo_new (TP_HANDLE_TYPE_LIST,
      (const gchar **) tp_base_contact_list_contact_lists);

  if (cls->priv->get_groups != NULL)
    {
      g_assert (cls->priv->get_contact_groups != NULL);

      self->priv->group_repo = tp_dynamic_handle_repo_new (TP_HANDLE_TYPE_GROUP,
          tp_base_contact_list_normalize_group, NULL);

      /* borrowed ref so the handle repo can call our virtual method, released
       * in tp_base_contact_list_free_contents */
      _tp_dynamic_handle_repo_set_normalization_data (self->priv->group_repo,
          self, NULL);

      _tp_base_connection_set_handle_repo (self->priv->conn,
          TP_HANDLE_TYPE_GROUP, self->priv->group_repo);
    }

  _tp_base_connection_set_handle_repo (self->priv->conn, TP_HANDLE_TYPE_LIST,
      list_repo);

  /* set_handle_repo doesn't steal a reference */
  g_object_unref (list_repo);

  self->priv->status_changed_id = g_signal_connect (self->priv->conn,
      "status-changed", (GCallback) status_changed_cb, self);
}

static void
tp_mutable_contact_list_default_init (TpMutableContactListInterface *iface)
{
  iface->can_change_subscriptions = tp_base_contact_list_true_func;
  iface->get_request_uses_message = tp_base_contact_list_true_func;
  /* there's no default for the other virtual methods */
}

static void
tp_blockable_contact_list_default_init (TpBlockableContactListInterface *iface)
{
  iface->can_block = tp_base_contact_list_true_func;
  /* there's no default for the other virtual methods */
}

static void
tp_base_contact_list_class_init (TpBaseContactListClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  g_type_class_add_private (cls, sizeof (TpBaseContactListPrivate));

  cls->priv = G_TYPE_CLASS_GET_PRIVATE (cls, TP_TYPE_BASE_CONTACT_LIST,
      TpBaseContactListClassPrivate);

  /* defaults */
  cls->get_subscriptions_persist = tp_base_contact_list_true_func;

  cls->priv->disjoint_groups = tp_base_contact_list_false_func;

  object_class->get_property = tp_base_contact_list_get_property;
  object_class->set_property = tp_base_contact_list_set_property;
  object_class->constructed = tp_base_contact_list_constructed;
  object_class->dispose = tp_base_contact_list_dispose;

  /**
   * TpBaseContactList:connection:
   *
   * The connection that owns this channel manager.
   * Read-only except during construction.
   *
   * Since: 0.11.UNRELEASED
   */
  g_object_class_install_property (object_class, PROP_CONNECTION,
      g_param_spec_object ("connection", "Connection",
        "The connection that owns this channel manager",
        TP_TYPE_BASE_CONNECTION,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
tp_base_contact_list_foreach_channel (TpChannelManager *manager,
    TpExportableChannelFunc func,
    gpointer user_data)
{
  TpBaseContactList *self = TP_BASE_CONTACT_LIST (manager);
  GHashTableIter iter;
  gpointer handle, channel;
  guint i;

  for (i = 0; i < NUM_TP_LIST_HANDLES; i++)
    {
      if (self->priv->lists[i] != NULL)
        func (TP_EXPORTABLE_CHANNEL (self->priv->lists[i]), user_data);
    }

  g_hash_table_iter_init (&iter, self->priv->groups);

  while (g_hash_table_iter_next (&iter, &handle, &channel))
    {
      func (TP_EXPORTABLE_CHANNEL (channel), user_data);
    }
}

static const gchar * const fixed_properties[] = {
    TP_PROP_CHANNEL_CHANNEL_TYPE,
    TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
    NULL
};

static const gchar * const allowed_properties[] = {
    TP_PROP_CHANNEL_TARGET_HANDLE,
    TP_PROP_CHANNEL_TARGET_ID,
    NULL
};

static void
tp_base_contact_list_foreach_channel_class (TpChannelManager *manager,
    TpChannelManagerChannelClassFunc func,
    gpointer user_data)
{
  TpBaseContactListClass *cls = TP_BASE_CONTACT_LIST_GET_CLASS (manager);
  GHashTable *table = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE,
          G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_LIST,
      NULL);

  func (manager, table, allowed_properties, user_data);

  if (cls->priv->add_to_group != NULL)
    {
      g_hash_table_insert (table, TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
          tp_g_value_slice_new_uint (TP_HANDLE_TYPE_GROUP));
      func (manager, table, allowed_properties, user_data);
    }

  g_hash_table_destroy (table);
}

static void
tp_base_contact_list_new_channel (TpBaseContactList *self,
    TpHandleType handle_type,
    TpHandle handle,
    gpointer request_token)
{
  gpointer chan;
  gchar *object_path;
  GType type;
  GSList *requests = NULL;

  if (handle_type == TP_HANDLE_TYPE_LIST)
    {
      object_path = g_strdup_printf ("%s/ContactList/%s",
          self->priv->conn->object_path,
          tp_base_contact_list_contact_lists[handle - 1]);
      type = TP_TYPE_CONTACT_LIST_CHANNEL;
    }
  else
    {
      g_assert (handle_type == TP_HANDLE_TYPE_GROUP);
      object_path = g_strdup_printf ("%s/Group/%u",
          self->priv->conn->object_path, handle);
      type = TP_TYPE_CONTACT_GROUP_CHANNEL;
    }

  chan = g_object_new (type,
      "connection", self->priv->conn,
      "manager", self,
      "object-path", object_path,
      "handle-type", handle_type,
      "handle", handle,
      NULL);

  g_free (object_path);

  if (handle_type == TP_HANDLE_TYPE_LIST)
    {
      g_assert (self->priv->lists[handle] == NULL);
      self->priv->lists[handle] = chan;
    }
  else
    {
      g_assert (g_hash_table_lookup (self->priv->groups,
            GUINT_TO_POINTER (handle)) == NULL);
      g_hash_table_insert (self->priv->groups, GUINT_TO_POINTER (handle),
          chan);
    }

  if (self->priv->channel_requests == NULL)
    {
      if (request_token != NULL)
        requests = g_slist_prepend (requests, request_token);

      tp_channel_manager_emit_new_channel (self, TP_EXPORTABLE_CHANNEL (chan),
          requests);
      g_slist_free (requests);
    }
  else if (request_token != NULL)
    {
      /* initial contact list not received yet, so we have to wait for it */
      requests = g_hash_table_lookup (self->priv->channel_requests, chan);
      g_hash_table_steal (self->priv->channel_requests, chan);
      requests = g_slist_prepend (requests, request_token);
      g_hash_table_insert (self->priv->channel_requests, chan, requests);
    }
}

static gboolean
tp_base_contact_list_request_helper (TpChannelManager *manager,
    gpointer request_token,
    GHashTable *request_properties,
    gboolean is_create)
{
  TpBaseContactList *self = (TpBaseContactList *) manager;
  TpBaseContactListClass *cls = TP_BASE_CONTACT_LIST_GET_CLASS (self);
  TpHandleType handle_type;
  TpHandle handle;
  TpBaseContactListChannel *chan;
  GError *error = NULL;

  g_return_val_if_fail (TP_IS_BASE_CONTACT_LIST (self), FALSE);

  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_PROP_CHANNEL_CHANNEL_TYPE),
      TP_IFACE_CHANNEL_TYPE_CONTACT_LIST))
    {
      return FALSE;
    }

  handle_type = tp_asv_get_uint32 (request_properties,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, NULL);

  if (handle_type != TP_HANDLE_TYPE_LIST &&
      (handle_type != TP_HANDLE_TYPE_GROUP ||
       cls->priv->add_to_group == NULL))
    {
      return FALSE;
    }

  handle = tp_asv_get_uint32 (request_properties,
      TP_PROP_CHANNEL_TARGET_HANDLE, NULL);
  g_assert (handle != 0);

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
        fixed_properties, allowed_properties, &error) ||
      !tp_base_contact_list_check_still_usable (self, &error))
    {
      goto error;
    }

  if (handle_type == TP_HANDLE_TYPE_LIST)
    {
      /* TpBaseConnection already checked the handle for validity */
      g_assert (handle > 0);
      g_assert (handle < NUM_TP_LIST_HANDLES);

      if (handle == TP_LIST_HANDLE_STORED &&
          !tp_base_contact_list_get_subscriptions_persist (self))
        {
          g_set_error_literal (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
              "Subscriptions do not persist, so this connection lacks the "
              "'stored' channel");
          goto error;
        }

      if (handle == TP_LIST_HANDLE_DENY &&
          !tp_base_contact_list_can_block (self))
        {
          g_set_error_literal (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
              "This connection cannot put people on the 'deny' list");
          goto error;
        }

      chan = self->priv->lists[handle];
    }
  else
    {
      chan = g_hash_table_lookup (self->priv->groups,
          GUINT_TO_POINTER (handle));
    }

  if (chan == NULL)
    {
      if (handle_type == TP_HANDLE_TYPE_LIST)
        {
          /* always create channels for our supported lists */
          tp_base_contact_list_new_channel (self, handle_type, handle,
              request_token);
        }
      else
        {
          /* defer to the subclass to create groups */
          if (cls->priv->create_groups == NULL)
            {
              g_set_error_literal (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
                  "This connection cannot create new groups");
              goto error;
            }
          else
            {
              const gchar *name = tp_handle_inspect (self->priv->group_repo,
                  handle);

              cls->priv->create_groups (self, &name, 1);
              /* hopefully, that resulted in a call to
               * tp_base_contact_list_groups_created, which created the
               * actual channel */
              chan = g_hash_table_lookup (self->priv->groups,
                  GUINT_TO_POINTER (handle));

              if (chan == NULL)
                {
                  g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
                      "Unable to create group '%s'", name);
                  goto error;
                }

              tp_channel_manager_emit_request_already_satisfied (self,
                  request_token, TP_EXPORTABLE_CHANNEL (chan));
            }
        }
    }
  else if (is_create)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "A ContactList channel for type #%u, handle #%u already exists",
          handle_type, handle);
      goto error;
    }
  else
    {
      tp_channel_manager_emit_request_already_satisfied (self,
          request_token, TP_EXPORTABLE_CHANNEL (chan));
    }

  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}

static gboolean
tp_base_contact_list_create_channel (TpChannelManager *manager,
    gpointer request_token,
    GHashTable *request_properties)
{
  return tp_base_contact_list_request_helper (manager, request_token,
      request_properties, TRUE);
}

static gboolean
tp_base_contact_list_ensure_channel (TpChannelManager *manager,
    gpointer request_token,
    GHashTable *request_properties)
{
  return tp_base_contact_list_request_helper (manager, request_token,
      request_properties, FALSE);
}

static void
channel_manager_iface_init (TpChannelManagerIface *iface)
{
  iface->foreach_channel = tp_base_contact_list_foreach_channel;
  iface->foreach_channel_class =
      tp_base_contact_list_foreach_channel_class;
  iface->create_channel = tp_base_contact_list_create_channel;
  iface->ensure_channel = tp_base_contact_list_ensure_channel;
  /* In this channel manager, Request has the same semantics as Ensure */
  iface->request_channel = tp_base_contact_list_ensure_channel;
}

TpChannelGroupFlags
_tp_base_contact_list_get_group_flags (TpBaseContactList *self)
{
  TpBaseContactListClass *cls = TP_BASE_CONTACT_LIST_GET_CLASS (self);
  TpChannelGroupFlags ret = 0;

  if (!tp_base_contact_list_can_change_subscriptions (self))
    return 0;

  if (cls->priv->add_to_group != NULL)
    ret |= TP_CHANNEL_GROUP_FLAG_CAN_ADD;

  if (cls->priv->remove_from_group != NULL)
    ret |= TP_CHANNEL_GROUP_FLAG_CAN_REMOVE;

  return ret;
}

TpChannelGroupFlags
_tp_base_contact_list_get_list_flags (TpBaseContactList *self,
    TpHandle list)
{
  if (!tp_base_contact_list_can_change_subscriptions (self))
    return 0;

  switch (list)
    {
    case TP_LIST_HANDLE_PUBLISH:
      /* We always allow an attempt to stop publishing presence to people,
       * and an attempt to send people our presence (if only as a sort of
       * pre-authorization). */
      return TP_CHANNEL_GROUP_FLAG_CAN_ADD | TP_CHANNEL_GROUP_FLAG_CAN_REMOVE;

    case TP_LIST_HANDLE_SUBSCRIBE:
      /* We can ask people to show us their presence, with a message.
       * We do our best to allow rescinding unreplied requests, and
       * unsubscribing, even if the underlying protocol does not. */
      return
        TP_CHANNEL_GROUP_FLAG_CAN_ADD |
        (tp_base_contact_list_get_request_uses_message (self)
          ? TP_CHANNEL_GROUP_FLAG_MESSAGE_ADD
          : 0) |
        TP_CHANNEL_GROUP_FLAG_CAN_REMOVE |
        TP_CHANNEL_GROUP_FLAG_CAN_RESCIND;

    case TP_LIST_HANDLE_STORED:
      /* We allow attempts to add people to the roster and remove them again,
       * even if the real protocol doesn't. */
      return TP_CHANNEL_GROUP_FLAG_CAN_ADD | TP_CHANNEL_GROUP_FLAG_CAN_REMOVE;

    case TP_LIST_HANDLE_DENY:
      /* A deny list wouldn't be much good if we couldn't actually deny,
       * would it? */
      return TP_CHANNEL_GROUP_FLAG_CAN_ADD | TP_CHANNEL_GROUP_FLAG_CAN_REMOVE;

    default:
      g_return_val_if_reached (0);
    }
}

gboolean
_tp_base_contact_list_add_to_group (TpBaseContactList *self,
    TpHandle group,
    TpHandle contact,
    const gchar *message G_GNUC_UNUSED,
    GError **error)
{
  TpBaseContactListClass *cls = TP_BASE_CONTACT_LIST_GET_CLASS (self);
  TpHandleSet *contacts;
  const gchar *group_name;

  if (!tp_base_contact_list_check_still_usable (self, error))
    return FALSE;

  if (!tp_base_contact_list_can_change_subscriptions (self) ||
      cls->priv->add_to_group == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Cannot add contacts to a group");
      return FALSE;
    }

  contacts = tp_handle_set_new (self->priv->contact_repo);
  tp_handle_set_add (contacts, contact);
  group_name = tp_handle_inspect (self->priv->group_repo, group);

  cls->priv->add_to_group (self, group_name, contacts);

  tp_handle_set_destroy (contacts);
  return TRUE;
}

gboolean
_tp_base_contact_list_remove_from_group (TpBaseContactList *self,
    TpHandle group,
    TpHandle contact,
    const gchar *message G_GNUC_UNUSED,
    GError **error)
{
  TpBaseContactListClass *cls = TP_BASE_CONTACT_LIST_GET_CLASS (self);
  TpHandleSet *contacts;
  const gchar *group_name;

  if (!tp_base_contact_list_check_still_usable (self, error))
    return FALSE;

  if (!tp_base_contact_list_can_change_subscriptions (self) ||
      cls->priv->remove_from_group == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Cannot remove contacts from a group");
      return FALSE;
    }

  contacts = tp_handle_set_new (self->priv->contact_repo);
  tp_handle_set_add (contacts, contact);
  group_name = tp_handle_inspect (self->priv->group_repo, group);

  cls->priv->remove_from_group (self, group_name, contacts);

  tp_handle_set_destroy (contacts);
  return TRUE;
}

gboolean
_tp_base_contact_list_delete_group_by_handle (TpBaseContactList *self,
    TpHandle group,
    GError **error)
{
  TpBaseContactListClass *cls = TP_BASE_CONTACT_LIST_GET_CLASS (self);
  const gchar *group_name;

  if (!tp_base_contact_list_check_still_usable (self, NULL))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_DISCONNECTED, "Disconnected");
      return FALSE;
    }

  if (!tp_base_contact_list_can_change_subscriptions (self) ||
      cls->priv->remove_group == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Cannot remove a group");
      return FALSE;
    }

  group_name = tp_handle_inspect (self->priv->group_repo, group);

  return cls->priv->remove_group (self, group_name, error);
}

gboolean
_tp_base_contact_list_add_to_list (TpBaseContactList *self,
    TpHandle list,
    TpHandle contact,
    const gchar *message,
    GError **error)
{
  TpHandleSet *contacts;

  if (!tp_base_contact_list_check_still_usable (self, error))
    return FALSE;

  if (!tp_base_contact_list_can_change_subscriptions (self))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Cannot change subscriptions");
      return FALSE;
    }

  contacts = tp_handle_set_new (self->priv->contact_repo);
  tp_handle_set_add (contacts, contact);

  switch (list)
    {
    case TP_LIST_HANDLE_SUBSCRIBE:
      tp_base_contact_list_request_subscription (self, contacts, message);
      break;

    case TP_LIST_HANDLE_PUBLISH:
      tp_base_contact_list_authorize_publication (self, contacts);
      break;

    case TP_LIST_HANDLE_STORED:
      tp_base_contact_list_store_contacts (self, contacts);
      break;

    case TP_LIST_HANDLE_DENY:
      tp_base_contact_list_block_contacts (self, contacts);
      break;
    }

  tp_handle_set_destroy (contacts);
  return TRUE;
}

gboolean
_tp_base_contact_list_remove_from_list (TpBaseContactList *self,
    TpHandle list,
    TpHandle contact,
    const gchar *message G_GNUC_UNUSED,
    GError **error)
{
  TpHandleSet *contacts;

  if (!tp_base_contact_list_check_still_usable (self, error))
    return FALSE;

  if (!tp_base_contact_list_can_change_subscriptions (self))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Cannot change subscriptions");
      return FALSE;
    }

  contacts = tp_handle_set_new (self->priv->contact_repo);
  tp_handle_set_add (contacts, contact);

  switch (list)
    {
    case TP_LIST_HANDLE_SUBSCRIBE:
      tp_base_contact_list_unsubscribe (self, contacts);
      break;

    case TP_LIST_HANDLE_PUBLISH:
      tp_base_contact_list_unpublish (self, contacts);
      break;

    case TP_LIST_HANDLE_STORED:
      tp_base_contact_list_remove_contacts (self, contacts);
      break;

    case TP_LIST_HANDLE_DENY:
      tp_base_contact_list_unblock_contacts (self, contacts);
      break;
    }

  tp_handle_set_destroy (contacts);
  return TRUE;
}

static void
satisfy_channel_requests (TpExportableChannel *channel,
    gpointer user_data)
{
  TpBaseContactList *self = user_data;
  GSList *requests = g_hash_table_lookup (self->priv->channel_requests,
      channel);

  /* this is all fine even if requests is NULL */
  g_hash_table_steal (self->priv->channel_requests, channel);
  requests = g_slist_reverse (requests);
  tp_channel_manager_emit_new_channel (self, channel, requests);
  g_slist_free (requests);
}

/**
 * tp_base_contact_list_set_list_received:
 * @self: the contact list manager
 *
 * Record that the initial contact list has been received. This allows the
 * contact list manager to reply to requests for the list of contacts that
 * were previously made, and reply to subsequent requests immediately.
 *
 * This method can be called at most once for a contact list manager.
 *
 * In protocols where there's no good definition of the point at which the
 * initial contact list has been received (such as link-local XMPP), this
 * method may be called immediately.
 *
 * The #TpBaseContactListGetContactsFunc and
 * #TpBaseContactListGetStatesFunc must already give correct
 * results when entering this method.
 *
 * If implemented, tp_base_contact_list_get_blocked_contacts() must also
 * give correct results when entering this method.
 */
void
tp_base_contact_list_set_list_received (TpBaseContactList *self)
{
  TpBaseContactListClass *cls = TP_BASE_CONTACT_LIST_GET_CLASS (self);
  TpHandleSet *contacts;

  g_return_if_fail (TP_IS_BASE_CONTACT_LIST (self));
  g_return_if_fail (!self->priv->had_contact_list);

  if (!tp_base_contact_list_check_still_usable (self, NULL))
    return;

  self->priv->had_contact_list = TRUE;

  if (self->priv->lists[TP_LIST_HANDLE_SUBSCRIBE] == NULL)
    {
      tp_base_contact_list_new_channel (self,
          TP_HANDLE_TYPE_LIST, TP_LIST_HANDLE_SUBSCRIBE, NULL);
    }

  if (self->priv->lists[TP_LIST_HANDLE_PUBLISH] == NULL)
    {
      tp_base_contact_list_new_channel (self,
          TP_HANDLE_TYPE_LIST, TP_LIST_HANDLE_PUBLISH, NULL);
    }

  if (tp_base_contact_list_get_subscriptions_persist (self) &&
      self->priv->lists[TP_LIST_HANDLE_STORED] == NULL)
    {
      tp_base_contact_list_new_channel (self,
          TP_HANDLE_TYPE_LIST, TP_LIST_HANDLE_STORED, NULL);
    }

  contacts = tp_base_contact_list_get_contacts (self);
  g_return_if_fail (contacts != NULL);

  if (DEBUGGING)
    {
      gchar *tmp = tp_intset_dump (tp_handle_set_peek (contacts));

      DEBUG ("Initial contacts: %s", tmp);
      g_free (tmp);
    }

  /* The natural thing to do here would be to iterate over all contacts, and
   * for each contact, emit a signal adding them to their own groups. However,
   * that emits a signal per contact. Here we turn the data model inside out,
   * to emit one signal per group - that's probably fewer (and also means we
   * can put them in batches for legacy Group channels). */
  if (cls->priv->get_groups != NULL)
    {
      GStrv groups = cls->priv->get_groups (self);
      GHashTable *group_members = g_hash_table_new_full (g_str_hash,
          g_str_equal, g_free, (GDestroyNotify) tp_handle_set_destroy);
      TpIntSetFastIter i_iter;
      TpHandle member;
      GHashTableIter h_iter;
      gpointer group, members;

      tp_base_contact_list_groups_created (self,
          (const gchar * const *) groups, -1);

      g_strfreev (groups);

      tp_intset_fast_iter_init (&i_iter, tp_handle_set_peek (contacts));

      while (tp_intset_fast_iter_next (&i_iter, &member))
        {
          groups = cls->priv->get_contact_groups (self, member);

          if (groups != NULL)
            {
              guint i;

              for (i = 0; groups[i] != NULL; i++)
                {
                  members = g_hash_table_lookup (group_members, groups[i]);

                  if (members == NULL)
                    members = tp_handle_set_new (self->priv->contact_repo);
                  else
                    g_hash_table_steal (group_members, groups[i]);

                  tp_handle_set_add (members, member);

                  g_hash_table_insert (group_members, g_strdup (groups[i]),
                      members);
                }

              g_strfreev (groups);
            }
        }

      g_hash_table_iter_init (&h_iter, group_members);

      while (g_hash_table_iter_next (&h_iter, &group, &members))
        {
          const gchar *group_id = group;

          tp_base_contact_list_groups_changed (self, members,
              &group_id, 1, NULL, 0);
        }

      g_hash_table_unref (group_members);
    }

  tp_base_contact_list_contacts_changed (self, contacts, NULL);
  tp_handle_set_destroy (contacts);

  if (tp_base_contact_list_can_block (self))
    {
      if (self->priv->lists[TP_LIST_HANDLE_DENY] == NULL)
        {
          tp_base_contact_list_new_channel (self,
              TP_HANDLE_TYPE_LIST, TP_LIST_HANDLE_DENY, NULL);
        }

      contacts = tp_base_contact_list_get_blocked_contacts (self);

      if (DEBUGGING)
        {
          gchar *tmp = tp_intset_dump (tp_handle_set_peek (contacts));

          DEBUG ("Initially blocked contacts: %s", tmp);
          g_free (tmp);
        }

      tp_base_contact_list_contact_blocking_changed (self, contacts);
      tp_handle_set_destroy (contacts);
    }

  tp_base_contact_list_foreach_channel ((TpChannelManager *) self,
      satisfy_channel_requests, self);

  g_assert (g_hash_table_size (self->priv->channel_requests) == 0);
  g_hash_table_destroy (self->priv->channel_requests);
  self->priv->channel_requests = NULL;
}

#ifdef ENABLE_DEBUG
static char
presence_state_to_letter (TpPresenceState ps)
{
  switch (ps)
    {
    case TP_PRESENCE_STATE_YES:
      return 'Y';

    case TP_PRESENCE_STATE_NO:
      return 'N';

    case TP_PRESENCE_STATE_ASK:
      return 'A';

    default:
      return '!';
    }
}
#endif

/**
 * tp_base_contact_list_contacts_changed:
 * @self: the contact list manager
 * @changed: (allow-none): a set of contacts added to the contact list or with
 *  a changed status
 * @removed: (allow-none): a set of contacts removed from the contact list
 *
 * Emit signals for a change to the contact list.
 *
 * The results of #TpBaseContactListGetContactsFunc and
 * #TpBaseContactListGetStatesFunc must already reflect
 * the contacts' new statuses when entering this method (in practice, this
 * means that implementations must update their own cache of contacts
 * before calling this method).
 */
void
tp_base_contact_list_contacts_changed (TpBaseContactList *self,
    TpHandleSet *changed,
    TpHandleSet *removed)
{
  GHashTable *changes;
  GArray *removals;
  TpIntSetIter iter;
  TpIntSet *pub, *sub, *sub_rp, *unpub, *unsub, *store;
  GObject *sub_chan, *pub_chan, *stored_chan;

  g_return_if_fail (TP_IS_BASE_CONTACT_LIST (self));

  /* don't do anything if we're disconnecting, or if we haven't had the
   * initial contact list yet */
  if (!tp_base_contact_list_check_still_usable (self, NULL) ||
      !self->priv->had_contact_list)
    return;

  sub_chan = (GObject *) self->priv->lists[TP_LIST_HANDLE_SUBSCRIBE];
  pub_chan = (GObject *) self->priv->lists[TP_LIST_HANDLE_PUBLISH];
  stored_chan = (GObject *) self->priv->lists[TP_LIST_HANDLE_STORED];

  g_return_if_fail (G_IS_OBJECT (sub_chan));
  g_return_if_fail (G_IS_OBJECT (pub_chan));
  /* stored_chan can legitimately be NULL, though */

  pub = tp_intset_new ();
  sub = tp_intset_new ();
  unpub = tp_intset_new ();
  unsub = tp_intset_new ();
  sub_rp = tp_intset_new ();
  store = tp_intset_new ();

  changes = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) g_value_array_free);

  if (changed != NULL)
    tp_intset_iter_init (&iter, tp_handle_set_peek (changed));

  while (changed != NULL && tp_intset_iter_next (&iter))
    {
      TpPresenceState subscribe = TP_PRESENCE_STATE_NO;
      TpPresenceState publish = TP_PRESENCE_STATE_NO;
      gchar *publish_request = NULL;

      tp_intset_add (store, iter.element);

      tp_base_contact_list_get_states (self, iter.element,
          &subscribe, &publish, &publish_request);

      if (publish_request == NULL)
        publish_request = g_strdup ("");

      DEBUG ("Contact %s: subscribe=%c publish=%c '%s'",
          tp_handle_inspect (self->priv->contact_repo, iter.element),
          presence_state_to_letter (subscribe),
          presence_state_to_letter (publish), publish_request);

      switch (publish)
        {
        case TP_PRESENCE_STATE_NO:
          tp_intset_add (unpub, iter.element);
          break;

        case TP_PRESENCE_STATE_ASK:
            {
              /* Emit any publication requests as we go along, since they can
               * each have a different message and actor */
              TpIntSet *pub_lp = tp_intset_new_containing (iter.element);

              tp_group_mixin_change_members (pub_chan, publish_request,
                  NULL, NULL, pub_lp, NULL, iter.element,
                  TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
            }
          break;

        case TP_PRESENCE_STATE_YES:
          tp_intset_add (pub, iter.element);
          break;

        default:
          g_assert_not_reached ();
        }

      switch (subscribe)
        {
        case TP_PRESENCE_STATE_NO:
          tp_intset_add (unsub, iter.element);
          break;

        case TP_PRESENCE_STATE_ASK:
          tp_intset_add (sub_rp, iter.element);
          break;

        case TP_PRESENCE_STATE_YES:
          tp_intset_add (sub, iter.element);
          break;

        default:
          g_assert_not_reached ();
        }

      g_hash_table_insert (changes, GUINT_TO_POINTER (iter.element),
          tp_value_array_build (3,
            G_TYPE_UINT, subscribe,
            G_TYPE_UINT, publish,
            G_TYPE_STRING, publish_request,
            G_TYPE_INVALID));
      g_free (publish_request);
    }

  if (removed != NULL)
    {
      TpIntSet *tmp;

      tmp = unsub;
      unsub = tp_intset_union (tmp, tp_handle_set_peek (removed));
      tp_intset_destroy (tmp);

      tmp = unpub;
      unpub = tp_intset_union (tmp, tp_handle_set_peek (removed));
      tp_intset_destroy (tmp);

      removals = tp_handle_set_to_array (removed);
    }
  else
    {
      removals = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), 0);
    }

  /* FIXME: is there a better actor than 0 for these changes? */
  tp_group_mixin_change_members (sub_chan, "",
      sub, unsub, NULL, sub_rp, 0, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
  tp_group_mixin_change_members (pub_chan, "",
      pub, unpub, NULL, NULL, 0, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

  if (stored_chan != NULL)
    {
      tp_group_mixin_change_members (stored_chan, "",
          store,
          removed == NULL ? NULL : tp_handle_set_peek (removed),
          NULL, NULL,
          0, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
    }

  /* FIXME: emit ContactsChanged (changed, removed) when the new D-Bus API
   * is available */

  /* FIXME: the new D-Bus API doesn't allow us to distinguish between
   * added-by-user, added-by-server and added-by-remote, or between
   * removed-by-user, removed-by-server and rejected-by-remote. Do we care? */

  tp_intset_destroy (pub);
  tp_intset_destroy (sub);
  tp_intset_destroy (unpub);
  tp_intset_destroy (unsub);
  tp_intset_destroy (sub_rp);
  tp_intset_destroy (store);

  g_hash_table_unref (changes);
  g_array_unref (removals);
}

/**
 * tp_base_contact_list_contact_blocking_changed:
 * @self: the contact list manager
 * @changed: a set of contacts who were blocked or unblocked
 *
 * Emit signals for a change to the blocked contacts list.
 *
 * tp_base_contact_list_get_blocked_contacts()
 * must already reflect the contacts' new statuses when entering this method
 * (in practice, this means that implementations must update their own cache
 * of contacts before calling this method).
 *
 * It is an error to call this method if tp_base_contact_list_can_block()
 * would return %FALSE.
 */
void
tp_base_contact_list_contact_blocking_changed (TpBaseContactList *self,
    TpHandleSet *changed)
{
  TpHandleSet *now_blocked;
  TpIntSet *blocked, *unblocked;
  TpIntSetFastIter iter;
  GObject *deny_chan;
  TpHandle handle;

  g_return_if_fail (TP_IS_BASE_CONTACT_LIST (self));
  g_return_if_fail (changed != NULL);

  /* don't do anything if we're disconnecting, or if we haven't had the
   * initial contact list yet */
  if (!tp_base_contact_list_check_still_usable (self, NULL) ||
      !self->priv->had_contact_list)
    return;

  g_return_if_fail (tp_base_contact_list_can_block (self));

  deny_chan = (GObject *) self->priv->lists[TP_LIST_HANDLE_DENY];
  g_return_if_fail (G_IS_OBJECT (deny_chan));

  now_blocked = tp_base_contact_list_get_blocked_contacts (self);

  blocked = tp_intset_new ();
  unblocked = tp_intset_new ();

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (changed));

  while (tp_intset_fast_iter_next (&iter, &handle))
    {
      if (tp_handle_set_is_member (now_blocked, handle))
        tp_intset_add (blocked, handle);
      else
        tp_intset_add (unblocked, handle);

      DEBUG ("Contact %s: blocked=%c",
          tp_handle_inspect (self->priv->contact_repo, handle),
          tp_handle_set_is_member (now_blocked, handle) ? 'Y' : 'N');
    }

  tp_group_mixin_change_members (deny_chan, "",
      blocked, unblocked, NULL, NULL,
      tp_base_connection_get_self_handle (self->priv->conn),
      TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

  /* FIXME: emit ContactBlockingChanged (blocked, unblocked) when the new
   * D-Bus API is available */

  tp_intset_destroy (blocked);
  tp_intset_destroy (unblocked);
  tp_handle_set_destroy (now_blocked);
}

/**
 * tp_base_contact_list_get_contacts:
 * @self: a contact list manager
 *
 * Return the contact list. It is incorrect to call this method before
 * tp_base_contact_list_set_list_retrieved() has been called, or after the
 * connection has disconnected.
 *
 * This is a virtual method, implemented using
 * #TpBaseContactListClass.get_contacts. Every subclass of #TpBaseContactList
 * must implement this method.
 *
 * If the contact list implements %TP_TYPE_BLOCKABLE_CONTACT_LIST, blocked
 * contacts should not appear in the result of this method unless they are
 * considered to be on the contact list for some other reason.
 *
 * Returns: (transfer full): a new #TpHandleSet of contact handles
 */
TpHandleSet *
tp_base_contact_list_get_contacts (TpBaseContactList *self)
{
  TpBaseContactListClass *cls = TP_BASE_CONTACT_LIST_GET_CLASS (self);

  g_return_val_if_fail (cls != NULL, NULL);
  g_return_val_if_fail (cls->get_contacts != NULL, NULL);
  g_return_val_if_fail (self->priv->had_contact_list, NULL);
  g_return_val_if_fail (tp_base_contact_list_check_still_usable (self, NULL),
      NULL);

  return cls->get_contacts (self);
}

/**
 * tp_base_contact_list_request_subscription:
 * @self: a contact list manager
 * @contacts: the contacts whose subscription is to be requested
 * @message: an optional human-readable message from the user
 *
 * Request permission to see some contacts' presence.
 *
 * If the #TpBaseContactList subclass does not implement
 * %TP_TYPE_MUTABLE_CONTACT_LIST, this method does nothing.
 *
 * For implementations of %TP_TYPE_MUTABLE_CONTACT_LIST, this is a virtual
 * method which must be implemented, using
 * #TpMutableContactListInterface.request_subscription.
 * The implementation should call tp_base_contact_list_contacts_changed()
 * for any contacts it has changed, before returning.
 *
 * If @message will be ignored,
 * #TpMutableContactListInterface.get_request_uses_message should also be
 * reimplemented to return %FALSE.
 */
void
tp_base_contact_list_request_subscription (TpBaseContactList *self,
    TpHandleSet *contacts,
    const gchar *message)
{
  TpMutableContactListInterface *iface;

  g_return_if_fail (TP_IS_BASE_CONTACT_LIST (self));

  if (!TP_IS_MUTABLE_CONTACT_LIST (self))
    return;

  iface = TP_MUTABLE_CONTACT_LIST_GET_INTERFACE (self);
  g_return_if_fail (iface != NULL);
  g_return_if_fail (iface->request_subscription != NULL);

  iface->request_subscription (self, contacts, message);
}

/**
 * tp_base_contact_list_get_states:
 * @self: a contact list manager
 * @contact: the contact
 * @subscribe: (out): used to return the state of the user's subscription to
 *  @contact's presence
 * @publish: (out): used to return the state of @contact's subscription to
 *  the user's presence
 * @publish_request: (out) (transfer full): if @publish will be set to
 *  %TP_PRESENCE_STATE_ASK, used to return the message that @contact sent when
 *  they requested permission to see the user's presence; otherwise, used to
 *  return an empty string
 *
 * Return the presence subscription state of @contact. It is incorrect to call
 * this method before tp_base_contact_list_set_list_retrieved() has been
 * called, or after the connection has disconnected.
 *
 * This is a virtual method, implemented using
 * #TpBaseContactListClass.get_states. Every subclass of #TpBaseContactList
 * must implement this method.
 */
void
tp_base_contact_list_get_states (TpBaseContactList *self,
    TpHandle contact,
    TpPresenceState *subscribe,
    TpPresenceState *publish,
    gchar **publish_request)
{
  TpBaseContactListClass *cls = TP_BASE_CONTACT_LIST_GET_CLASS (self);

  g_return_if_fail (cls != NULL);
  g_return_if_fail (cls->get_states != NULL);
  g_return_if_fail (self->priv->had_contact_list);
  g_return_if_fail (tp_base_contact_list_check_still_usable (self, NULL));

  cls->get_states (self, contact, subscribe, publish, publish_request);

  if (publish_request != NULL && *publish_request == NULL)
    *publish_request = g_strdup ("");
}

/**
 * tp_base_contact_list_authorize_publication:
 * @self: a contact list manager
 * @contacts: the contacts to whom presence will be published
 *
 * Give permission for some contacts to see the local user's presence.
 *
 * If the #TpBaseContactList subclass does not implement
 * %TP_TYPE_MUTABLE_CONTACT_LIST, this method does nothing.
 *
 * For implementations of %TP_TYPE_MUTABLE_CONTACT_LIST, this is a virtual
 * method which must be implemented, using
 * #TpMutableContactListInterface.authorize_publication.
 * The implementation should call tp_base_contact_list_contacts_changed()
 * for any contacts it has changed, before returning.
 */
void
tp_base_contact_list_authorize_publication (TpBaseContactList *self,
    TpHandleSet *contacts)
{
  TpMutableContactListInterface *iface;

  g_return_if_fail (TP_IS_BASE_CONTACT_LIST (self));

  if (!TP_IS_MUTABLE_CONTACT_LIST (self))
    return;

  iface = TP_MUTABLE_CONTACT_LIST_GET_INTERFACE (self);
  g_return_if_fail (iface != NULL);
  g_return_if_fail (iface->authorize_publication != NULL);

  iface->authorize_publication (self, contacts);
}

/**
 * tp_base_contact_list_store_contacts:
 * @self: a contact list manager
 * @contacts: the contacts to be stored
 *
 * Store @contacts on the contact list, without attempting to subscribe to
 * them or send presence to them. If this is not possible, do nothing.
 *
 * If the #TpBaseContactList subclass does not implement
 * %TP_TYPE_MUTABLE_CONTACT_LIST, or if the implementation
 * of #TpMutableContactListInterface.store_contacts is %NULL (which is the
 * default), this method does nothing.
 *
 * For implementations of %TP_TYPE_MUTABLE_CONTACT_LIST, this is a virtual
 * method, which may be implemented using
 * #TpMutableContactListInterface.store_contacts.
 * The implementation should call tp_base_contact_list_contacts_changed()
 * for any contacts it has changed, before returning.
 */
void
tp_base_contact_list_store_contacts (TpBaseContactList *self,
    TpHandleSet *contacts)
{
  TpMutableContactListInterface *iface;

  g_return_if_fail (TP_IS_BASE_CONTACT_LIST (self));

  if (!TP_IS_MUTABLE_CONTACT_LIST (self))
    return;

  iface = TP_MUTABLE_CONTACT_LIST_GET_INTERFACE (self);
  g_return_if_fail (iface != NULL);

  if (iface->store_contacts == NULL)
    return;

  iface->store_contacts (self, contacts);
}

/**
 * tp_base_contact_list_remove_contacts:
 * @self: a contact list manager
 * @contacts: the contacts to be removed
 *
 * Remove @contacts from the contact list entirely; this includes the
 * effect of both tp_base_contact_list_unsubscribe() and
 * tp_base_contact_list_unpublish(), and also reverses the effect of
 * tp_base_contact_list_store_contacts().
 *
 * If the #TpBaseContactList subclass does not implement
 * %TP_TYPE_MUTABLE_CONTACT_LIST, this method does nothing.
 *
 * For implementations of %TP_TYPE_MUTABLE_CONTACT_LIST, this is a virtual
 * method which must be implemented, using
 * #TpMutableContactListInterface.remove_contacts.
 * The implementation should call tp_base_contact_list_contacts_changed()
 * for any contacts it has changed, before returning.
 */
void
tp_base_contact_list_remove_contacts (TpBaseContactList *self,
    TpHandleSet *contacts)
{
  TpMutableContactListInterface *iface;

  g_return_if_fail (TP_IS_BASE_CONTACT_LIST (self));

  if (!TP_IS_MUTABLE_CONTACT_LIST (self))
    return;

  iface = TP_MUTABLE_CONTACT_LIST_GET_INTERFACE (self);
  g_return_if_fail (iface != NULL);
  g_return_if_fail (iface->remove_contacts != NULL);

  iface->remove_contacts (self, contacts);
}

/**
 * tp_base_contact_list_unsubscribe:
 * @self: a contact list manager
 * @contacts: the contacts whose presence will no longer be received
 *
 * Cancel a pending subscription request to @contacts, or attempt to stop
 * receiving their presence.
 *
 * If the #TpBaseContactList subclass does not implement
 * %TP_TYPE_MUTABLE_CONTACT_LIST, this method does nothing.
 *
 * For implementations of %TP_TYPE_MUTABLE_CONTACT_LIST, this is a virtual
 * method which must be implemented, using
 * #TpMutableContactListInterface.unsubscribe.
 * The implementation should call tp_base_contact_list_contacts_changed()
 * for any contacts it has changed, before returning.
 */
void
tp_base_contact_list_unsubscribe (TpBaseContactList *self,
    TpHandleSet *contacts)
{
  TpMutableContactListInterface *iface;

  g_return_if_fail (TP_IS_BASE_CONTACT_LIST (self));

  if (!TP_IS_MUTABLE_CONTACT_LIST (self))
    return;

  iface = TP_MUTABLE_CONTACT_LIST_GET_INTERFACE (self);
  g_return_if_fail (iface != NULL);
  g_return_if_fail (iface->unsubscribe != NULL);

  iface->unsubscribe (self, contacts);
}

/**
 * tp_base_contact_list_unpublish:
 * @self: a contact list manager
 * @contacts: the contacts to whom presence will no longer be published
 *
 * Reject a pending subscription request from @contacts, or attempt to stop
 * sending presence to them.
 *
 * If the #TpBaseContactList subclass does not implement
 * %TP_TYPE_MUTABLE_CONTACT_LIST, this method does nothing.
 *
 * For implementations of %TP_TYPE_MUTABLE_CONTACT_LIST, this is a virtual
 * method which must be implemented, using
 * #TpMutableContactListInterface.unpublish.
 * The implementation should call tp_base_contact_list_contacts_changed()
 * for any contacts it has changed, before returning.
 */
void
tp_base_contact_list_unpublish (TpBaseContactList *self,
    TpHandleSet *contacts)
{
  TpMutableContactListInterface *iface;

  g_return_if_fail (TP_IS_BASE_CONTACT_LIST (self));

  if (!TP_IS_MUTABLE_CONTACT_LIST (self))
    return;

  iface = TP_MUTABLE_CONTACT_LIST_GET_INTERFACE (self);
  g_return_if_fail (iface != NULL);
  g_return_if_fail (iface->unpublish != NULL);

  iface->unpublish (self, contacts);
}

/**
 * TpBaseContactListBooleanFunc:
 * @self: a contact list manager
 *
 * Signature of a virtual method that returns a boolean result. These are used
 * for feature-discovery.
 *
 * For the simple cases of a constant result, use
 * tp_base_contact_list_true_func() or tp_base_contact_list_false_func().
 *
 * Returns: a boolean result
 */

/**
 * tp_base_contact_list_true_func:
 * @self: ignored
 *
 * An implementation of #TpBaseContactListBooleanFunc that returns %TRUE,
 * for use in simple cases.
 *
 * Returns: %TRUE
 */
gboolean
tp_base_contact_list_true_func (TpBaseContactList *self G_GNUC_UNUSED)
{
  return TRUE;
}

/**
 * tp_base_contact_list_false_func:
 * @self: ignored
 *
 * An implementation of #TpBaseContactListBooleanFunc that returns %FALSE,
 * for use in simple cases.
 *
 * Returns: %FALSE
 */
gboolean
tp_base_contact_list_false_func (TpBaseContactList *self G_GNUC_UNUSED)
{
  return FALSE;
}

/**
 * tp_base_contact_list_can_change_subscriptions:
 * @self: a contact list manager
 *
 * Return whether the contact list can be changed.
 *
 * If the #TpBaseContactList subclass does not implement #TpMutableContactList,
 * this method always returns %FALSE.
 *
 * For implementations of #TpMutableContactList, this is a virtual method,
 * implemented using #TpMutableContactListInterface.can_change_subscriptions.
 * The default implementation always returns %TRUE.
 *
 * In the rare case of a protocol where subscriptions can only sometimes be
 * changed and this is detected while connecting, the #TpBaseContactList
 * subclass should implement #TpMutableContactList, and set
 * #TpMutableContactListInterface.can_change_subscriptions to its own
 * implementation, whose result must remain constant after the
 * #TpBaseConnection has moved to state %TP_CONNECTION_STATUS_CONNECTED.
 *
 * (For instance, this could be useful for XMPP, where subscriptions can
 * normally be altered, but on connections to Facebook Chat servers this is
 * not actually supported.)
 *
 * Returns: %TRUE if the contact list can be changed
 */
gboolean
tp_base_contact_list_can_change_subscriptions (TpBaseContactList *self)
{
  TpMutableContactListInterface *iface;

  g_return_val_if_fail (TP_IS_BASE_CONTACT_LIST (self), FALSE);

  if (!TP_IS_MUTABLE_CONTACT_LIST (self))
    return FALSE;

  iface = TP_MUTABLE_CONTACT_LIST_GET_INTERFACE (self);
  g_return_val_if_fail (iface != NULL, FALSE);
  g_return_val_if_fail (iface->can_change_subscriptions != NULL, FALSE);

  return iface->can_change_subscriptions (self);
}

/**
 * tp_base_contact_list_get_subscriptions_persist:
 * @self: a contact list manager
 *
 * Return whether subscriptions on this protocol persist between sessions
 * (i.e. are stored on the server).
 *
 * This is a virtual method, implemented using
 * #TpBaseContactListClass.get_subscriptions_persist.
 *
 * The default implementation is tp_base_contact_list_true_func(), which is
 * correct for most protocols. Protocols where the contact list isn't stored
 * should use tp_base_contact_list_false_func() as their implementation.
 *
 * In the rare case of a protocol where subscriptions sometimes persist
 * and this is detected while connecting, the subclass can implement another
 * #TpBaseContactListBooleanFunc (whose result must remain constant
 * after the #TpBaseConnection has moved to state
 * %TP_CONNECTION_STATUS_CONNECTED), and use that as the implementation.
 *
 * Returns: %TRUE if subscriptions persist
 */
gboolean
tp_base_contact_list_get_subscriptions_persist (TpBaseContactList *self)
{
  TpBaseContactListClass *cls = TP_BASE_CONTACT_LIST_GET_CLASS (self);

  g_return_val_if_fail (cls != NULL, TRUE);
  g_return_val_if_fail (cls->get_subscriptions_persist != NULL, TRUE);

  return cls->get_subscriptions_persist (self);
}

/**
 * tp_base_contact_list_get_request_uses_message:
 * @self: a contact list manager
 *
 * Return whether the tp_base_contact_list_request_subscription()
 * method's @message argument is actually used.
 *
 * If the #TpBaseContactList subclass does not implement #TpMutableContactList,
 * this method always returns %FALSE.
 *
 * For implementations of #TpMutableContactList, this is a virtual method,
 * implemented using #TpMutableContactListInterface.get_request_uses_message.
 * The default implementation always returns %TRUE, which is correct for most
 * protocols; subclasses may reimplement this method with
 * tp_base_contact_list_false_func() or a custom implementation if desired.
 *
 * Returns: %TRUE if tp_base_contact_list_request_subscription() will not
 *  ignore its @message argument
 */
gboolean
tp_base_contact_list_get_request_uses_message (TpBaseContactList *self)
{
  TpMutableContactListInterface *iface;

  g_return_val_if_fail (TP_IS_BASE_CONTACT_LIST (self), FALSE);

  if (!TP_IS_MUTABLE_CONTACT_LIST (self))
    return FALSE;

  iface = TP_MUTABLE_CONTACT_LIST_GET_INTERFACE (self);
  g_return_val_if_fail (iface != NULL, FALSE);
  g_return_val_if_fail (iface->get_request_uses_message != NULL, FALSE);

  return iface->get_request_uses_message (self);
}

/**
 * tp_base_contact_list_can_block:
 * @self: a contact list manager
 *
 * Return whether this contact list has a list of blocked contacts. If it
 * does, that list is assumed to be modifiable.
 *
 * If the #TpBaseContactList subclass does not implement
 * #TpBlockableContactList, this method always returns %FALSE.
 *
 * For implementations of #TpBlockableContactList, this is a virtual method,
 * implemented using #TpBlockableContactListInterface.can_block.
 * The default implementation always returns %TRUE.
 *
 * In the case of a protocol where blocking may or may not work
 * and this is detected while connecting, the subclass can implement another
 * #TpBaseContactListBooleanFunc (whose result must remain constant
 * after the #TpBaseConnection has moved to state
 * %TP_CONNECTION_STATUS_CONNECTED), and use that as the implementation.
 *
 * (For instance, this could be useful for XMPP, where support for contact
 * blocking is server-dependent: telepathy-gabble 0.8.x implements it for
 * connections to Google Talk servers, but not for any other server.)
 *
 * Returns: %TRUE if communication from contacts can be blocked
 */
gboolean
tp_base_contact_list_can_block (TpBaseContactList *self)
{
  TpBlockableContactListInterface *iface;

  g_return_val_if_fail (TP_IS_BASE_CONTACT_LIST (self), FALSE);

  if (!TP_IS_BLOCKABLE_CONTACT_LIST (self))
    return FALSE;

  iface = TP_BLOCKABLE_CONTACT_LIST_GET_INTERFACE (self);
  g_return_val_if_fail (iface != NULL, FALSE);
  g_return_val_if_fail (iface->can_block != NULL, FALSE);

  return iface->can_block (self);
}

/**
 * tp_base_contact_list_get_blocked_contacts:
 * @self: a contact list manager
 *
 * Return the list of blocked contacts. It is incorrect to call this method
 * before tp_base_contact_list_set_list_retrieved() has been called, after
 * the connection has disconnected, or on a #TpBaseContactList that does
 * not implement %TP_TYPE_BLOCKABLE_CONTACT_LIST.
 *
 * For implementations of #TpBlockableContactList, this is a virtual method,
 * implemented using #TpBlockableContactListInterface.can_block.
 *
 * Returns: (transfer full): a new #TpHandleSet of contact handles
 */
TpHandleSet *
tp_base_contact_list_get_blocked_contacts (TpBaseContactList *self)
{
  TpBlockableContactListInterface *iface =
    TP_BLOCKABLE_CONTACT_LIST_GET_INTERFACE (self);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_blocked_contacts != NULL, NULL);

  return iface->get_blocked_contacts (self);
}

/**
 * tp_base_contact_list_block_contacts:
 * @self: a contact list manager
 * @contacts: contacts whose communications should be blocked
 *
 * Request that the given contacts are prevented from communicating with the
 * user, and that presence is not sent to them even if they have a valid
 * presence subscription, if possible.
 *
 * If the #TpBaseContactList subclass does not implement
 * %TP_TYPE_BLOCKABLE_CONTACT_LIST, this method does nothing.
 *
 * For implementations of %TP_TYPE_BLOCKABLE_CONTACT_LIST, this is a virtual
 * method which must be implemented, using
 * #TpBlockableContactListInterface.block_contacts.
 * The implementation should call
 * tp_base_contact_list_contact_blocking_changed()
 * for any contacts it has changed, before returning.
 */
void
tp_base_contact_list_block_contacts (TpBaseContactList *self,
    TpHandleSet *contacts)
{
  TpBlockableContactListInterface *iface;

  g_return_if_fail (TP_IS_BASE_CONTACT_LIST (self));

  if (!TP_IS_BLOCKABLE_CONTACT_LIST (self))
    return;

  iface = TP_BLOCKABLE_CONTACT_LIST_GET_INTERFACE (self);
  g_return_if_fail (iface != NULL);
  g_return_if_fail (iface->block_contacts != NULL);

  iface->block_contacts (self, contacts);
}

/**
 * tp_base_contact_list_unblock_contacts:
 * @self: a contact list manager
 * @contacts: contacts whose communications should no longer be blocked
 *
 * Reverse the effects of tp_base_contact_list_block_contacts().
 *
 * If the #TpBaseContactList subclass does not implement
 * %TP_TYPE_BLOCKABLE_CONTACT_LIST, this method does nothing.
 *
 * For implementations of %TP_TYPE_BLOCKABLE_CONTACT_LIST, this is a virtual
 * method which must be implemented, using
 * #TpBlockableContactListInterface.unblock_contacts.
 * The implementation should call
 * tp_base_contact_list_contact_blocking_changed()
 * for any contacts it has changed, before returning.
 */
void
tp_base_contact_list_unblock_contacts (TpBaseContactList *self,
    TpHandleSet *contacts)
{
  TpBlockableContactListInterface *iface;

  g_return_if_fail (TP_IS_BASE_CONTACT_LIST (self));

  if (!TP_IS_BLOCKABLE_CONTACT_LIST (self))
    return;

  iface = TP_BLOCKABLE_CONTACT_LIST_GET_INTERFACE (self);
  g_return_if_fail (iface != NULL);
  g_return_if_fail (iface->unblock_contacts != NULL);

  iface->unblock_contacts (self, contacts);
}

/**
 * TpBaseContactListNormalizeFunc:
 * @self: a contact list manager
 * @s: a non-%NULL name to normalize
 *
 * Signature of a virtual method to normalize strings in a contact list
 * manager.
 *
 * Returns: a normalized form of @s, or %NULL on error
 */

/**
 * tp_base_contact_list_class_implement_normalize_group:
 * @cls: a contact list manager subclass
 * @impl: a function that returns a normalized form of the argument @s, or
 *  %NULL on error
 *
 * Set a function that can be used to normalize the name of a group.
 *
 * The default is to use the group's name as-is. Protocols where this default
 * is not suitable (for instance, if group names can only contain XML
 * character data, or a particular Unicode normal form like NFKC) should call
 * this function from #GTypeClass.class_init.
 */
void
tp_base_contact_list_class_implement_normalize_group (
    TpBaseContactListClass *cls,
    TpBaseContactListNormalizeFunc impl)
{
  g_return_if_fail (TP_IS_BASE_CONTACT_LIST_CLASS (cls));
  g_return_if_fail (impl != NULL);
  cls->priv->normalize_group = impl;
}

/**
 * TpBaseContactListCreateGroupsFunc:
 * @self: a contact list manager
 * @normalized_names: (array length=n_names) (element-type utf8): the group
 *  names, which have already been normalized via the
 *  #TpBaseContactListNormalizeFunc if one was provided
 * @n_names: the number of group names in @normalized_names
 *
 * Signature of a virtual method that creates groups.
 *
 * Implementations are expected to send any network messages that are
 * necessary in the underlying protocol, and call
 * tp_base_contact_list_groups_created() to signal success, before returning.
 *
 * If tp_base_contact_list_groups_created() is not called, this will be
 * signalled as a D-Bus error where appropriate.
 */

/**
 * tp_base_contact_list_class_implement_create_groups:
 * @cls: a contact list manager subclass
 * @impl: a function that creates the groups if possible
 *
 * Set a function that can be used to create new groups.
 *
 * The default is to be unable to create new groups. On most protocols this
 * default is not suitable, and the subclass should call this function from
 * #GTypeClass.class_init.
 */
void
tp_base_contact_list_class_implement_create_groups (
    TpBaseContactListClass *cls,
    TpBaseContactListCreateGroupsFunc impl)
{
  g_return_if_fail (TP_IS_BASE_CONTACT_LIST_CLASS (cls));
  g_return_if_fail (impl != NULL);
  cls->priv->create_groups = impl;
}

/**
 * tp_base_contact_list_groups_created:
 * @self: a contact list manager
 * @created: (array length=n_created) (element-type utf8) (allow-none): zero
 *  or more groups that were created
 * @n_created: the number of groups created, or -1 if @created is
 *  %NULL-terminated
 *
 * Called by subclasses when new groups have been created. This will typically
 * be followed by a call to tp_base_contact_list_groups_changed() to add
 * some members to those groups.
 */
void
tp_base_contact_list_groups_created (TpBaseContactList *self,
    const gchar * const *created,
    gssize n_created)
{
  GPtrArray *pa;
  guint i;

  g_return_if_fail (TP_IS_BASE_CONTACT_LIST (self));
  g_return_if_fail (n_created >= -1);
  g_return_if_fail (n_created <= 0 || created != NULL);

  if (n_created == 0 || created == NULL)
    return;

  if (n_created < 0)
    {
      n_created = (gssize) g_strv_length ((GStrv) created);
    }
  else
    {
      for (i = 0; i < n_created; i++)
        g_return_if_fail (created[i] != NULL);
    }

  pa = g_ptr_array_sized_new (n_created + 1);

  for (i = 0; i < n_created; i++)
    {
      TpHandle handle = tp_handle_ensure (self->priv->group_repo, created[i],
          NULL, NULL);

      if (handle != 0)
        {
          gpointer c = g_hash_table_lookup (self->priv->groups,
              GUINT_TO_POINTER (handle));

          if (c == NULL)
            {
              tp_base_contact_list_new_channel (self, TP_HANDLE_TYPE_GROUP,
                  handle, NULL);
              g_ptr_array_add (pa, (gchar *) tp_handle_inspect (
                    self->priv->group_repo, handle));
            }

          tp_handle_unref (self->priv->group_repo, handle);
        }
    }

  if (pa->len > 0)
    {
      g_ptr_array_add (pa, NULL);
      /* FIXME: emit GroupsCreated(pa->pdata) in the new API */
    }

  g_ptr_array_unref (pa);
}

/**
 * tp_base_contact_list_groups_removed:
 * @self: a contact list manager
 * @removed: (array length=n_removed) (element-type utf8) (allow-none): zero
 *  or more groups that were removed
 * @n_removed: the number of groups removed, or -1 if @removed is
 *  %NULL-terminated
 *
 * Called by subclasses when groups have been removed. If the groups had
 * members, the subclass does not also need to call
 * tp_base_contact_list_groups_changed() for them - the group membership
 * change signals will be emitted automatically.
 */
void
tp_base_contact_list_groups_removed (TpBaseContactList *self,
    const gchar * const *removed,
    gssize n_removed)
{
  GPtrArray *pa;
  guint i;

  g_return_if_fail (TP_IS_BASE_CONTACT_LIST (self));
  g_return_if_fail (removed != NULL);
  g_return_if_fail (n_removed >= -1);
  g_return_if_fail (n_removed <= 0 || removed != NULL);

  if (n_removed == 0 || removed == NULL)
    return;

  if (n_removed < 0)
    {
      n_removed = (gssize) g_strv_length ((GStrv) removed);
    }
  else
    {
      for (i = 0; i < n_removed; i++)
        g_return_if_fail (removed[i] != NULL);
    }

  pa = g_ptr_array_sized_new (n_removed + 1);

  for (i = 0; i < n_removed; i++)
    {
      TpHandle handle = tp_handle_lookup (self->priv->group_repo, removed[i],
          NULL, NULL);

      if (handle != 0)
        {
          gpointer c = g_hash_table_lookup (self->priv->groups,
              GUINT_TO_POINTER (handle));

          if (c != NULL)
            {
              TpGroupMixin *mixin = TP_GROUP_MIXIN (c);
              TpIntSet *set;

              g_ptr_array_add (pa, (gchar *) tp_handle_inspect (
                    self->priv->group_repo, handle));

              g_assert (mixin != NULL);
              /* remove members: presumably the self-handle is the actor */
              set = tp_intset_copy (tp_handle_set_peek (mixin->members));
              tp_group_mixin_change_members (c, "", NULL, set, NULL, NULL,
                  self->priv->conn->self_handle,
                  TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
              tp_intset_destroy (set);

              _tp_base_contact_list_channel_close (c);
              g_hash_table_remove (self->priv->groups,
                  GUINT_TO_POINTER (handle));
            }
        }
    }

  if (pa->len > 0)
    {
      g_ptr_array_add (pa, NULL);

      /* FIXME: emit GroupsRemoved(pa->pdata) in the new API */

      /* FIXME: emit GroupsChanged for them, too */
    }

  g_ptr_array_unref (pa);
}

/**
 * tp_base_contact_list_group_renamed:
 * @self: a contact list manager
 * @old_name: the group's old name
 * @new_name: the group's new name
 *
 * Called by subclasses when a group has been renamed. The subclass should not
 * also call tp_base_contact_list_groups_changed() for the group's members -
 * the group membership change signals will be emitted automatically.
 */
void
tp_base_contact_list_group_renamed (TpBaseContactList *self,
    const gchar *old_name,
    const gchar *new_name)
{
  TpHandle old_handle, new_handle;
  gpointer old_chan, new_chan;
  const gchar *old_names[] = { old_name, NULL };
  const gchar *new_names[] = { new_name, NULL };
  TpGroupMixin *mixin;
  TpIntSet *set;

  g_return_if_fail (TP_IS_BASE_CONTACT_LIST (self));

  old_handle = tp_handle_lookup (self->priv->group_repo, old_name, NULL, NULL);

  if (old_handle == 0)
    return;

  old_chan = g_hash_table_lookup (self->priv->groups,
      GUINT_TO_POINTER (old_handle));

  if (old_chan == NULL)
    return;

  mixin = TP_GROUP_MIXIN (old_chan);
  g_assert (mixin != NULL);

  new_handle = tp_handle_ensure (self->priv->group_repo, new_name, NULL, NULL);

  if (new_handle == 0)
    return;

  new_chan = g_hash_table_lookup (self->priv->groups,
      GUINT_TO_POINTER (new_handle));

  if (new_chan == NULL)
    {
      tp_base_contact_list_new_channel (self, TP_HANDLE_TYPE_GROUP,
          new_handle, NULL);

      new_chan = g_hash_table_lookup (self->priv->groups,
          GUINT_TO_POINTER (new_handle));
      g_assert (new_chan != NULL);
    }

  /* move the members - presumably the self-handle is the actor */
  set = tp_intset_copy (tp_handle_set_peek (mixin->members));
  tp_group_mixin_change_members (new_chan, "", set, NULL, NULL, NULL,
      self->priv->conn->self_handle, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
  tp_group_mixin_change_members (old_chan, "", NULL, set, NULL, NULL,
      self->priv->conn->self_handle, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

  /* delete the old channel, but make sure to ref the old handle first,
   * in case the channel's ref was the last */
  tp_handle_ref (self->priv->group_repo, old_handle);
  _tp_base_contact_list_channel_close (old_chan);
  g_hash_table_remove (self->priv->groups, GUINT_TO_POINTER (old_handle));

  /* get normalized forms */
  old_names[0] = tp_handle_inspect (self->priv->group_repo, old_handle);
  new_names[0] = tp_handle_inspect (self->priv->group_repo, new_handle);

  /* FIXME: emit GroupRenamed(old_names[0], new_names[0]) in new API */
  DEBUG ("GroupRenamed('%s', '%s')", old_names[0], new_names[0]);

  /* FIXME: emit GroupsChanged(set, new_names, old_names) in new API */
  DEBUG ("GroupsChanged([...], ['%s'], ['%s'])", new_names[0], old_names[0]);

  tp_intset_destroy (set);
  tp_handle_unref (self->priv->group_repo, new_handle);
  tp_handle_unref (self->priv->group_repo, old_handle);
}

/**
 * tp_base_contact_list_groups_changed:
 * @self: a contact list manager
 * @contacts: a set containing one or more contacts
 * @added: (array length=n_added) (element-type utf8) (allow-none): zero or
 *  more groups to which the @contacts were added, or %NULL (which has the
 *  same meaning as an empty list)
 * @n_added: the number of groups added, or -1 if @added is %NULL-terminated
 * @removed: (array zero-terminated=1) (element-type utf8) (allow-none): zero
 *  or more groups from which the @contacts were removed, or %NULL (which has
 *  the same meaning as an empty list)
 * @n_removed: the number of groups removed, or -1 if @removed is
 *  %NULL-terminated
 *
 * Called by subclasses when groups' membership has been changed.
 *
 * If any of the groups in @added are not already known to exist,
 * this method also signals that they were created, as if
 * tp_base_contact_list_groups_created() had been called first.
 */
void
tp_base_contact_list_groups_changed (TpBaseContactList *self,
    TpHandleSet *contacts,
    const gchar * const *added,
    gssize n_added,
    const gchar * const *removed,
    gssize n_removed)
{
  guint i;

  g_return_if_fail (TP_IS_BASE_CONTACT_LIST (self));
  g_return_if_fail (contacts != NULL);
  g_return_if_fail (n_added >= -1);
  g_return_if_fail (n_removed >= -1);
  g_return_if_fail (n_added <= 0 || added != NULL);
  g_return_if_fail (n_removed <= 0 || removed != NULL);

  if (n_added < 0)
    {
      if (added == NULL)
        n_added = 0;
      else
        n_added = (gssize) g_strv_length ((GStrv) added);

      g_return_if_fail (n_added < 0);
    }
  else
    {
      for (i = 0; i < n_added; i++)
        g_return_if_fail (added[i] != NULL);
    }

  if (n_removed < 0)
    {
      if (added == NULL)
        n_removed = 0;
      else
        n_removed = (gssize) g_strv_length ((GStrv) added);

      g_return_if_fail (n_removed < 0);
    }
  else
    {
      for (i = 0; i < n_removed; i++)
        g_return_if_fail (removed[i] != NULL);
    }

  tp_base_contact_list_groups_created (self, added, n_added);

  for (i = 0; i < n_added; i++)
    {
      TpHandle handle = tp_handle_lookup (self->priv->group_repo, added[i],
          NULL, NULL);
      gpointer c;

      /* it doesn't matter if handle is 0, we'll just get NULL */
      c = g_hash_table_lookup (self->priv->groups,
          GUINT_TO_POINTER (handle));

      if (c == NULL)
        continue;

      tp_group_mixin_change_members (c, "",
          tp_handle_set_peek (contacts), NULL, NULL, NULL,
          self->priv->conn->self_handle, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
    }

  for (i = 0; i < n_removed; i++)
    {
      TpHandle handle = tp_handle_lookup (self->priv->group_repo, removed[i],
          NULL, NULL);
      gpointer c;

      /* it doesn't matter if handle is 0, we'll just get NULL */
      c = g_hash_table_lookup (self->priv->groups,
          GUINT_TO_POINTER (handle));

      if (c == NULL)
        continue;

      tp_group_mixin_change_members (c, "",
          NULL, tp_handle_set_peek (contacts), NULL, NULL,
          self->priv->conn->self_handle, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
    }

  /* FIXME: emit GroupsChanged(contacts, added, removed) in new API */
}

/**
 * tp_base_contact_list_class_implement_disjoint_groups:
 * @cls: a contact list manager subclass
 * @impl: an implementation of the virtual method
 *
 * Fill in an implementation of the @disjoint_groups virtual method,
 * which tells clients whether groups in this protocol are disjoint
 * (i.e. each contact can be in at most one group).
 *
 * This is merely informational: subclasses are responsible for making
 * appropriate calls to tp_base_contact_list_groups_changed(), etc.
 *
 * The default implementation is tp_base_contact_list_false_func();
 * subclasses where groups are disjoint should call this function
 * with @impl = tp_base_contact_list_true_func() during
 * #GTypeClass.class_init.
 *
 * In the unlikely event that a protocol can have disjoint groups, or not,
 * determined at runtime, it can use a custom implementation for @impl.
 */
void
tp_base_contact_list_class_implement_disjoint_groups (
    TpBaseContactListClass *cls,
    TpBaseContactListBooleanFunc impl)
{
  g_return_if_fail (TP_IS_BASE_CONTACT_LIST_CLASS (cls));
  g_return_if_fail (impl != NULL);
  cls->priv->disjoint_groups = impl;
}

/**
 * TpBaseContactListGetGroupsFunc:
 * @self: a contact list manager
 *
 * Signature of a virtual method that lists every group that exists on a
 * connection.
 *
 * Returns: (array zero-terminated=1) (element-type utf8): an array of groups
 */

/**
 * tp_base_contact_list_class_implement_get_groups:
 * @cls: a contact list manager subclass
 * @impl: an implementation of the virtual method
 *
 * Fill in an implementation of the @get_groups virtual method,
 * which is used to list all the groups on a connection. Every subclass
 * that supports contact groups must call this function in its
 * #GTypeClass.class_init.
 */
void
tp_base_contact_list_class_implement_get_groups (
    TpBaseContactListClass *cls,
    TpBaseContactListGetGroupsFunc impl)
{
  g_return_if_fail (TP_IS_BASE_CONTACT_LIST_CLASS (cls));
  g_return_if_fail (impl != NULL);
  cls->priv->get_groups = impl;
}

/**
 * TpBaseContactListGetContactGroupsFunc:
 * @self: a contact list manager
 * @contact: a non-zero contact handle
 *
 * Signature of a virtual method that lists the groups to which @contact
 * belongs.
 *
 * If @contact is not on the contact list, this method must return either
 * %NULL or an empty array, without error.
 *
 * Returns: (array zero-terminated=1) (element-type utf8): an array of groups
 */

/**
 * tp_base_contact_list_class_implement_get_contact_groups:
 * @cls: a contact list manager subclass
 * @impl: an implementation of the virtual method
 *
 * Fill in an implementation of the @get_contact_groups virtual method,
 * which is used to list the groups to which a contact belongs. Every subclass
 * that supports contact groups must call this function in its
 * #GTypeClass.class_init.
 */
void
tp_base_contact_list_class_implement_get_contact_groups (
    TpBaseContactListClass *cls,
    TpBaseContactListGetContactGroupsFunc impl)
{
  g_return_if_fail (TP_IS_BASE_CONTACT_LIST_CLASS (cls));
  g_return_if_fail (impl != NULL);
  cls->priv->get_contact_groups = impl;
}

/**
 * TpBaseContactListGroupContactsFunc:
 * @self: a contact list manager
 * @group: a group
 * @contacts: a set of contact handles
 *
 * Signature of a virtual method that alters a group's members.
 */

/**
 * tp_base_contact_list_class_implement_add_to_group:
 * @cls: a contact list manager subclass
 * @impl: an implementation of the virtual method
 *
 * Fill in an implementation of the @add_to_group virtual method,
 * which adds a contact to one or more groups.
 *
 * Every subclass that supports altering contact groups should call this
 * function in its #GTypeClass.class_init.
 */
void tp_base_contact_list_class_implement_add_to_group (
    TpBaseContactListClass *cls,
    TpBaseContactListGroupContactsFunc impl)
{
  g_return_if_fail (TP_IS_BASE_CONTACT_LIST_CLASS (cls));
  g_return_if_fail (impl != NULL);
  cls->priv->add_to_group = impl;
}

/**
 * tp_base_contact_list_class_implement_remove_from_group:
 * @cls: a contact list manager subclass
 * @impl: an implementation of the virtual method
 *
 * Fill in an implementation of the @remove_from_group virtual method,
 * which removes one or more members from a group.
 *
 * Every subclass that supports altering contact groups should call this
 * function in its #GTypeClass.class_init.
 */
void tp_base_contact_list_class_implement_remove_from_group (
    TpBaseContactListClass *cls,
    TpBaseContactListGroupContactsFunc impl)
{
  g_return_if_fail (TP_IS_BASE_CONTACT_LIST_CLASS (cls));
  g_return_if_fail (impl != NULL);
  cls->priv->remove_from_group = impl;
}

/**
 * TpBaseContactListRemoveGroupFunc:
 * @self: a contact list manager
 * @group: a group
 * @error: used to raise an error if %FALSE is returned
 *
 * Signature of a method that deletes groups.
 *
 * Returns: %TRUE on success
 */

/**
 * tp_base_contact_list_class_implement_remove_group:
 * @cls: a contact list manager subclass
 * @impl: an implementation of the virtual method
 *
 * Fill in an implementation of the @remove_group virtual method,
 * which removes a group entirely, removing any members in the process.
 *
 * Every subclass that supports deleting contact groups should call this
 * function in its #GTypeClass.class_init.
 */
void tp_base_contact_list_class_implement_remove_group (
    TpBaseContactListClass *cls,
    TpBaseContactListRemoveGroupFunc impl)
{
  g_return_if_fail (TP_IS_BASE_CONTACT_LIST_CLASS (cls));
  g_return_if_fail (impl != NULL);
  cls->priv->remove_group = impl;
}
