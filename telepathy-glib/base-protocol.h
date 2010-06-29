/* TpBaseProtocol
 *
 * Copyright © 2007-2010 Collabora Ltd.
 * Copyright © 2007-2009 Nokia Corporation
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

#ifndef TP_BASE_PROTOCOL_H
#define TP_BASE_PROTOCOL_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _TpCMParamSpec TpCMParamSpec;

typedef void (*TpCMParamSetter) (const TpCMParamSpec *paramspec,
    const GValue *value, gpointer params);

typedef gboolean (*TpCMParamFilter) (const TpCMParamSpec *paramspec,
    GValue *value, GError **error);

gboolean tp_cm_param_filter_string_nonempty (const TpCMParamSpec *paramspec,
    GValue *value, GError **error);

gboolean tp_cm_param_filter_uint_nonzero (const TpCMParamSpec *paramspec,
    GValue *value, GError **error);

/* XXX: This should be driven by GTypes, but the GType is insufficiently
 * descriptive: if it's UINT we can't tell whether the D-Bus type is
 * UInt32, UInt16 or possibly even Byte. So we have the D-Bus type too.
 *
 * As it stands at the moment it could be driven by the *D-Bus* type, but
 * in future we may want to have more than one possible GType for a D-Bus
 * type, e.g. converting arrays of string into either a strv or a GPtrArray.
 * So, we keep the redundancy for future expansion.
 */

struct _TpCMParamSpec {
    const gchar *name;
    const gchar *dtype;
    GType gtype;
    guint flags;
    gconstpointer def;
    gsize offset;

    TpCMParamFilter filter;
    gconstpointer filter_data;

    gconstpointer setter_data;

    /*<private>*/
    gpointer _future1;
};

typedef struct _TpBaseProtocol TpBaseProtocol;
typedef struct _TpBaseProtocolClass TpBaseProtocolClass;
typedef struct _TpBaseProtocolPrivate TpBaseProtocolPrivate;
typedef struct _TpBaseProtocolClassPrivate TpBaseProtocolClassPrivate;

GType tp_base_protocol_get_type (void) G_GNUC_CONST;

#define TP_TYPE_BASE_PROTOCOL \
  (tp_base_protocol_get_type ())
#define TP_BASE_PROTOCOL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), TP_TYPE_BASE_PROTOCOL, \
                               TpBaseProtocol))
#define TP_BASE_PROTOCOL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), TP_TYPE_BASE_PROTOCOL, \
                            TpBaseProtocolClass))
#define TP_IS_BASE_PROTOCOL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TP_TYPE_BASE_PROTOCOL))
#define TP_IS_BASE_PROTOCOL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), TP_TYPE_BASE_PROTOCOL))
#define TP_BASE_PROTOCOL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TP_TYPE_BASE_PROTOCOL, \
                              TpBaseProtocolClass))

struct _TpBaseProtocol
{
  /*<private>*/
  GObject parent;
  TpBaseProtocolPrivate *priv;
};

struct _TpBaseProtocolClass
{
  GObjectClass parent_class;

  gboolean is_stub;
  const TpCMParamSpec *(*get_parameters) (TpBaseProtocol *self);

  /*<private>*/
  GCallback padding[8];
  TpBaseProtocolClassPrivate *priv;
};

const TpCMParamSpec *tp_base_protocol_get_parameters (TpBaseProtocol *self);

G_END_DECLS

#endif
