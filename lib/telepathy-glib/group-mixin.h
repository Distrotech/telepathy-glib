/*
 * group-mixin.h - Header for TpGroupMixin
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
 *   @author Robert McQueen <robert.mcqueen@collabora.co.uk>
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

#ifndef __TP_GROUP_MIXIN_H__
#define __TP_GROUP_MIXIN_H__

#include <telepathy-glib/handle-repo.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/util.h>

/* this is a hack to let me change the API back how it was pre-tp-glib,
 * without causing warnings... this will go away in version 0.5.8.
 * FIXME: take this out after 0.5.7 is released */
#ifdef _TP_CM_UPDATED_FOR_0_5_7
#define _TP_GROUP_MIXIN_OBJECT GObject
#define _TP_GROUP_MIXIN_OBJECT_CLASS GObjectClass
#else
#define _TP_GROUP_MIXIN_OBJECT TpSvcChannelInterfaceGroup
#define _TP_GROUP_MIXIN_OBJECT_CLASS TpSvcChannelInterfaceGroupClass
#endif

G_BEGIN_DECLS

typedef struct _TpGroupMixinClass TpGroupMixinClass;
typedef struct _TpGroupMixinClassPrivate TpGroupMixinClassPrivate;

typedef struct _TpGroupMixin TpGroupMixin;
typedef struct _TpGroupMixinPrivate TpGroupMixinPrivate;

/**
 * TpGroupMixinAddMemberFunc:
 * @obj: An object implementing the group interface with this mixin
 * @handle: The handle of the contact to be added
 * @message: A message to be sent if the protocol supports it
 * @error: Used to return a Telepathy D-Bus error if %FALSE is returned
 *
 * Signature of the callback used to add a member to the group.
 * This should perform the necessary operations in the underlying IM protocol
 * to cause the member to be added.
 *
 * Returns: %TRUE on success, %FALSE with @error set on error
 */
typedef gboolean (*TpGroupMixinAddMemberFunc) (_TP_GROUP_MIXIN_OBJECT *obj,
    TpHandle handle, const gchar *message, GError **error);

/**
 * TpGroupMixinRemMemberFunc:
 * @obj: An object implementing the group interface with this mixin
 * @handle: The handle of the contact to be removed
 * @message: A message to be sent if the protocol supports it
 * @error: Used to return a Telepathy D-Bus error if %FALSE is returned
 *
 * Signature of the callback used to remove a member from the group.
 * This should perform the necessary operations in the underlying IM protocol
 * to cause the member to be removed.
 *
 * Returns: %TRUE on success, %FALSE with @error set on error
 */
typedef gboolean (*TpGroupMixinRemMemberFunc) (_TP_GROUP_MIXIN_OBJECT *obj,
    TpHandle handle, const gchar *message, GError **error);

/**
 * TpGroupMixinClass:
 * @add_member: The add-member function that was passed to
 *  tp_group_mixin_class_init()
 * @remove_member: The remove-member function that was passed to
 *  tp_group_mixin_class_init()
 * @priv: Pointer to opaque private data
 *
 * Structure representing the group mixin as used in a particular class.
 * To be placed in the implementation's class structure.
 *
 * Initialize this with tp_group_mixin_class_init().
 *
 * All fields should be considered read-only.
 */
struct _TpGroupMixinClass {
  /*<private>*/
  TpGroupMixinAddMemberFunc add_member;
  TpGroupMixinRemMemberFunc remove_member;
  TpGroupMixinClassPrivate *priv;
};

/**
 * TpGroupMixin:
 * @handle_repo: The connection's contact handle repository
 * @self_handle: The local user's handle within this group, or 0 if none.
 *  Set using (FIXME: how do we do self-renaming?)
 * @group_flags: This group's flags. Set using tp_group_mixin_change_flags().
 * @members: The members of the group. Alter using
 *  tp_group_mixin_change_members().
 * @local_pending: Members awaiting the local user's approval to join the
 *  group. Alter using tp_group_mixin_change_members().
 * @remote_pending: Members awaiting remote (e.g. remote user or server)
 *  approval to join the group. Alter using tp_group_mixin_change_members().
 * @priv: Pointer to opaque private data
 *
 * Structure representing the group mixin as used in a particular class.
 * To be placed in the implementation's instance structure.
 *
 * All fields should be considered read-only.
 */
struct _TpGroupMixin {
  TpHandleRepoIface *handle_repo;
  TpHandle self_handle;

  TpChannelGroupFlags group_flags;

  TpHandleSet *members;
  TpHandleSet *local_pending;
  TpHandleSet *remote_pending;

  TpGroupMixinPrivate *priv;
};

/* TYPE MACROS */
#define TP_GROUP_MIXIN_CLASS_OFFSET_QUARK \
  (tp_group_mixin_class_get_offset_quark())
#define TP_GROUP_MIXIN_CLASS_OFFSET(o) \
  (GPOINTER_TO_UINT (g_type_get_qdata (G_OBJECT_CLASS_TYPE (o), \
                                       TP_GROUP_MIXIN_CLASS_OFFSET_QUARK)))
#define TP_GROUP_MIXIN_CLASS(o) \
  ((TpGroupMixinClass *) tp_mixin_offset_cast (o, \
    TP_GROUP_MIXIN_CLASS_OFFSET (o)))

#define TP_GROUP_MIXIN_OFFSET_QUARK (tp_group_mixin_get_offset_quark())
#define TP_GROUP_MIXIN_OFFSET(o) (GPOINTER_TO_UINT (g_type_get_qdata (\
        G_OBJECT_TYPE (o), TP_GROUP_MIXIN_OFFSET_QUARK)))
#define TP_GROUP_MIXIN(o) ((TpGroupMixin *) tp_mixin_offset_cast (o, \
      TP_GROUP_MIXIN_OFFSET(o)))

GQuark tp_group_mixin_class_get_offset_quark (void);
GQuark tp_group_mixin_get_offset_quark (void);

void tp_group_mixin_class_init (_TP_GROUP_MIXIN_OBJECT_CLASS *obj_cls,
    glong offset, TpGroupMixinAddMemberFunc add_func,
    TpGroupMixinRemMemberFunc rem_func);

void tp_group_mixin_init (_TP_GROUP_MIXIN_OBJECT *obj, glong offset,
    TpHandleRepoIface *handle_repo, TpHandle self_handle);
void tp_group_mixin_finalize (_TP_GROUP_MIXIN_OBJECT *obj);

gboolean tp_group_mixin_get_self_handle (_TP_GROUP_MIXIN_OBJECT *obj,
    guint *ret, GError **error);
gboolean tp_group_mixin_get_group_flags (_TP_GROUP_MIXIN_OBJECT *obj,
    guint *ret, GError **error);

gboolean tp_group_mixin_add_members (_TP_GROUP_MIXIN_OBJECT *obj,
    const GArray *contacts, const gchar *message, GError **error);
gboolean tp_group_mixin_remove_members (_TP_GROUP_MIXIN_OBJECT *obj,
    const GArray *contacts, const gchar *message, GError **error);

gboolean tp_group_mixin_get_members (_TP_GROUP_MIXIN_OBJECT *obj,
    GArray **ret, GError **error);
gboolean tp_group_mixin_get_local_pending_members (_TP_GROUP_MIXIN_OBJECT *obj,
    GArray **ret, GError **error);
gboolean tp_group_mixin_get_local_pending_members_with_info (_TP_GROUP_MIXIN_OBJECT *obj,
    GPtrArray **ret, GError **error);
gboolean tp_group_mixin_get_remote_pending_members (_TP_GROUP_MIXIN_OBJECT *obj,
    GArray **ret, GError **error);
gboolean tp_group_mixin_get_all_members (_TP_GROUP_MIXIN_OBJECT *obj,
    GArray **members, GArray **local_pending, GArray **remote_pending,
    GError **error);

gboolean tp_group_mixin_get_handle_owners (_TP_GROUP_MIXIN_OBJECT *obj,
    const GArray *handles, GArray **ret, GError **error);

void tp_group_mixin_change_flags (_TP_GROUP_MIXIN_OBJECT *obj,
    TpChannelGroupFlags add, TpChannelGroupFlags remove);
gboolean tp_group_mixin_change_members (_TP_GROUP_MIXIN_OBJECT *obj,
    const gchar *message, TpIntSet *add, TpIntSet *remove,
    TpIntSet *add_local_pending, TpIntSet *add_remote_pending, TpHandle actor,
    TpChannelGroupChangeReason reason);

void tp_group_mixin_add_handle_owner (_TP_GROUP_MIXIN_OBJECT *obj,
    TpHandle local_handle, TpHandle owner_handle);

void tp_group_mixin_iface_init (gpointer g_iface, gpointer iface_data);

G_END_DECLS

#endif /* #ifndef __TP_GROUP_MIXIN_H__ */
