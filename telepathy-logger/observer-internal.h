/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Collabora Ltd.
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
 *
 * Authors: Cosimo Alfarano <cosimo.alfarano@collabora.co.uk>
 */

#ifndef __TPL_OBSERVER_H__
#define __TPL_OBSERVER_H__

#include <glib-object.h>
#include <telepathy-glib/dbus-properties-mixin.h>

#include <telepathy-logger/channel-internal.h>
#include <telepathy-logger/channel-factory-internal.h>

#define TPL_OBSERVER_WELL_KNOWN_BUS_NAME \
  "org.freedesktop.Telepathy.Client.Logger"
#define TPL_OBSERVER_OBJECT_PATH \
  "/org/freedesktop/Telepathy/Client/Logger"


G_BEGIN_DECLS
#define TPL_TYPE_OBSERVER (tpl_observer_get_type ())
#define TPL_OBSERVER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), TPL_TYPE_OBSERVER, TplObserver))
#define TPL_OBSERVER_CLASS(obj) (G_TYPE_CHECK_CLASS_CAST ((obj), TPL_TYPE_OBSERVER, TplObserverClass))
#define TPL_IS_OBSERVER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TPL_TYPE_OBSERVER))
#define TPL_IS_OBSERVER_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE ((obj), TPL_TYPE_OBSERVER))
#define TPL_OBSERVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), TPL_TYPE_OBSERVER, TplObserverClass))
typedef struct _TplObserverPriv TplObserverPriv;
typedef struct
{
  GObject parent;

  /* private */
  TplObserverPriv *priv;
} TplObserver;

typedef struct
{
  GObjectClass parent_class;
  TpDBusPropertiesMixinClass dbus_props_class;
} TplObserverClass;

GType tpl_observer_get_type (void);

TplObserver *tpl_observer_new (void);

gboolean tpl_observer_register_channel (TplObserver *self,
    TplChannel *channel);
gboolean tpl_observer_unregister_channel (TplObserver *self,
    TplChannel *channel);
gboolean tpl_observer_register_dbus (TplObserver *self, GError **error);
void tpl_observer_set_channel_factory (TplObserver *self,
    TplChannelFactory factory);


G_END_DECLS
#endif // __TPL_OBSERVER_H__
