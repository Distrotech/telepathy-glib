/*
 * proxy.h - Base class for Telepathy client proxies
 *
 * Copyright (C) 2007 Collabora Ltd.
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

#ifndef __TP_PROXY_H__
#define __TP_PROXY_H__

#include <dbus/dbus-glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _TpProxy TpProxy;

typedef struct _TpProxyClass TpProxyClass;

struct _TpProxyClass {
    GObjectClass parent_class;

    GQuark interface;

    gboolean must_have_unique_name:1;
    guint _reserved_flags:31;

    GCallback _reserved[4];
    gpointer priv;
};

typedef struct _TpProxyPendingCall TpProxyPendingCall;

struct _TpProxyPendingCall {
    TpProxy *proxy;
    GCallback callback;
    gpointer user_data;
    GDestroyNotify destroy;
    DBusGProxyCall *pending_call;
    gconstpointer priv;
};

typedef struct _TpProxySignalConnection TpProxySignalConnection;

struct _TpProxySignalConnection {
    TpProxy *proxy;
    GQuark interface;
    gchar *member;
    GCallback callback;
    gpointer user_data;
    GDestroyNotify destroy;
    gconstpointer priv;
};

GType tp_proxy_get_type (void);

/* TYPE MACROS */
#define TP_TYPE_PROXY \
  (tp_proxy_get_type ())
#define TP_PROXY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TP_TYPE_PROXY, \
                              TpProxy))
#define TP_PROXY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TP_TYPE_PROXY, \
                           TpProxyClass))
#define TP_IS_PROXY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TP_TYPE_PROXY))
#define TP_IS_PROXY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TP_TYPE_PROXY))
#define TP_PROXY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TP_TYPE_PROXY, \
                              TpProxyClass))

typedef void (*TpProxyInterfaceAddedCb) (TpProxy *self,
    guint quark, DBusGProxy *proxy, gpointer unused);

void tp_proxy_class_hook_on_interface_add (TpProxyClass *klass,
    TpProxyInterfaceAddedCb callback);

G_END_DECLS

#ifndef TP_PROXY_IN_CLI_IMPLEMENTATION
#include <telepathy-glib/_gen/tp-cli-generic.h>
#endif

#endif /* #ifndef __TP_PROXY_H__*/
