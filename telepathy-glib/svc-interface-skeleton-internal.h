/*<private_header>*/
/*
 * Copyright © 2014 Collabora Ltd.
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

#ifndef __TP_SVC_INTERFACE_SKELETON_INTERNAL_H__
#define __TP_SVC_INTERFACE_SKELETON_INTERNAL_H__

#include <gio/gio.h>

#include <telepathy-glib/svc-interface.h>
#include <telepathy-glib/defs.h>

G_BEGIN_DECLS

typedef struct _TpSvcInterfaceSkeleton TpSvcInterfaceSkeleton;
typedef struct _TpSvcInterfaceSkeletonClass TpSvcInterfaceSkeletonClass;
typedef struct _TpSvcInterfaceSkeletonPrivate TpSvcInterfaceSkeletonPrivate;

GType _tp_svc_interface_skeleton_get_type (void);

#define TP_TYPE_SVC_INTERFACE_SKELETON \
  (_tp_svc_interface_skeleton_get_type ())
#define TP_SVC_INTERFACE_SKELETON(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), TP_TYPE_SVC_INTERFACE_SKELETON, \
                               TpSvcInterfaceSkeleton))
#define TP_SVC_INTERFACE_SKELETON_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), TP_TYPE_SVC_INTERFACE_SKELETON, \
                            TpSvcInterfaceSkeletonClass))
#define TP_IS_SVC_INTERFACE_SKELETON(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TP_TYPE_SVC_INTERFACE_SKELETON))
#define TP_IS_SVC_INTERFACE_SKELETON_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), TP_TYPE_SVC_INTERFACE_SKELETON))
#define TP_SVC_INTERFACE_SKELETON_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TP_TYPE_SVC_INTERFACE_SKELETON, \
                              TpSvcInterfaceSkeletonClass))

struct _TpSvcInterfaceSkeletonClass
{
  /*< private >*/
  GDBusInterfaceSkeletonClass parent_class;
};

struct _TpSvcInterfaceSkeleton
{
  /*< private >*/
  GDBusInterfaceSkeleton parent;
  TpSvcInterfaceSkeletonPrivate *priv;
};

G_END_DECLS

#endif
