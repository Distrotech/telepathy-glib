/*
 * gabble-connection-manager.h - Header for GabbleConnectionManager
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#ifndef __GABBLE_CONNECTION_MANAGER_H__
#define __GABBLE_CONNECTION_MANAGER_H__

#include <glib-object.h>
#include <telepathy-glib/base-connection-manager.h>

G_BEGIN_DECLS

typedef struct _GabbleConnectionManager GabbleConnectionManager;
typedef struct _GabbleConnectionManagerClass GabbleConnectionManagerClass;

struct _GabbleConnectionManagerClass {
    TpBaseConnectionManagerClass parent_class;
};

struct _GabbleConnectionManager {
    TpBaseConnectionManager parent;

    gpointer priv;
};

extern const TpCMProtocolSpec gabble_protocols[];

GType gabble_connection_manager_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_CONNECTION_MANAGER \
  (gabble_connection_manager_get_type ())
#define GABBLE_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_CONNECTION_MANAGER, \
                              GabbleConnectionManager))
#define GABBLE_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_CONNECTION_MANAGER, \
                           GabbleConnectionManagerClass))
#define GABBLE_IS_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_CONNECTION_MANAGER))
#define GABBLE_IS_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_CONNECTION_MANAGER))
#define GABBLE_CONNECTION_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_CONNECTION_MANAGER, \
                              GabbleConnectionManagerClass))

G_END_DECLS

#endif /* #ifndef __GABBLE_CONNECTION_MANAGER_H__*/
