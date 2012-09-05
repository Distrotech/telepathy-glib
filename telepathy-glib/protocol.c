/* TpProtocol
 *
 * Copyright © 2010-2012 Collabora Ltd.
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

/**
 * SECTION:protocol
 * @title: TpProtocol
 * @short_description: proxy for a Telepathy Protocol object
 * @see_also: #TpConnectionManager
 *
 * #TpProtocol objects represent the protocols implemented by Telepathy
 * connection managers. In modern connection managers, each protocol is
 * represented by a D-Bus object; in older connection managers, the protocols
 * are represented by data structures, and this object merely emulates a D-Bus
 * object.
 *
 * Since: 0.11.11
 */

#include "config.h"

#include <telepathy-glib/protocol.h>
#include <telepathy-glib/protocol-internal.h>

#include <telepathy-glib/proxy-subclass.h>
#include <telepathy-glib/telepathy-glib.h>

#define DEBUG_FLAG TP_DEBUG_PARAMS
#include "telepathy-glib/capabilities-internal.h"
#include "telepathy-glib/debug-internal.h"
#include "telepathy-glib/proxy-internal.h"

#include "telepathy-glib/_gen/tp-cli-protocol-body.h"

#include <string.h>

struct _TpProtocolClass
{
  /*<private>*/
  TpProxyClass parent_class;
};

/**
 * TpProtocol:
 *
 * A base class for connection managers' protocols.
 *
 * Since: 0.11.11
 */

/**
 * TpProtocolClass:
 *
 * The class of a #TpProtocol.
 *
 * Since: 0.11.11
 */

G_DEFINE_TYPE(TpProtocol, tp_protocol, TP_TYPE_PROXY)

/**
 * TP_PROTOCOL_FEATURE_PARAMETERS:
 *
 * Expands to a call to a function that returns a quark for the parameters
 * feature of a #TpProtocol.
 *
 * When this feature is prepared, the possible parameters for connections to
 * this protocol have been retrieved and are available for use.
 *
 * Unlike %TP_PROTOCOL_FEATURE_CORE, this feature can even be available on
 * connection managers that don't really have Protocol objects
 * (on these older connection managers, the #TpProtocol uses information from
 * ConnectionManager methods to provide the list of parameters).
 *
 * One can ask for a feature to be prepared using the
 * tp_proxy_prepare_async() function, and waiting for it to callback.
 *
 * Since: 0.11.11
 */

GQuark
tp_protocol_get_feature_quark_parameters (void)
{
  return g_quark_from_static_string ("tp-protocol-feature-parameters");
}

/**
 * TP_PROTOCOL_FEATURE_CORE:
 *
 * Expands to a call to a function that returns a quark for the core
 * feature of a #TpProtocol.
 *
 * When this feature is prepared, at least the following basic information
 * about the protocol is available:
 *
 * <itemizedlist>
 *  <listitem>possible parameters for connections to this protocol</listitem>
 *  <listitem>interfaces expected on connections to this protocol</listitem>
 *  <listitem>classes of channel that could be requested from connections
 *    to this protocol</listitem>
 * </itemizedlist>
 *
 * (This feature implies that %TP_PROTOCOL_FEATURE_PARAMETERS is also
 * available.)
 *
 * Unlike %TP_PROTOCOL_FEATURE_PARAMETERS, this feature can only become
 * available on connection managers that implement Protocol objects.
 *
 * One can ask for a feature to be prepared using the
 * tp_proxy_prepare_async() function, and waiting for it to callback.
 *
 * Since: 0.11.11
 */

GQuark
tp_protocol_get_feature_quark_core (void)
{
  return g_quark_from_static_string ("tp-protocol-feature-core");
}

struct _TpProtocolPrivate
{
  TpConnectionManagerProtocol protocol_struct;
  GHashTable *protocol_properties;
  gchar *vcard_field;
  gchar *english_name;
  gchar *icon_name;
  GStrv authentication_types;
  TpCapabilities *capabilities;
  TpAvatarRequirements *avatar_req;
  gchar *cm_name;
};

enum
{
    PROP_PROTOCOL_NAME = 1,
    PROP_PROTOCOL_PROPERTIES,
    PROP_ENGLISH_NAME,
    PROP_VCARD_FIELD,
    PROP_ICON_NAME,
    PROP_CAPABILITIES,
    PROP_PARAM_NAMES,
    PROP_AUTHENTICATION_TYPES,
    PROP_AVATAR_REQUIREMENTS,
    PROP_CM_NAME,
    N_PROPS
};

/* this is NULL-safe for @parameters, and callers rely on this */
static TpConnectionManagerParam *
tp_protocol_params_from_param_specs (const GPtrArray *parameters,
    const gchar *cm_debug_name,
    const gchar *protocol)
{
  GArray *output;
  guint i;

  DEBUG ("Protocol name: %s", protocol);

  if (parameters == NULL)
    {
      return g_new0 (TpConnectionManagerParam, 1);
    }

  output = g_array_sized_new (TRUE, TRUE,
      sizeof (TpConnectionManagerParam), parameters->len);

  for (i = 0; i < parameters->len; i++)
    {
      GValue structure = { 0 };
      GValue *tmp;
      /* Points to the zeroed entry just after the end of the array
       * - but we're about to extend the array to make it valid */
      TpConnectionManagerParam *param = &g_array_index (output,
          TpConnectionManagerParam, output->len);

      g_value_init (&structure, TP_STRUCT_TYPE_PARAM_SPEC);
      g_value_set_static_boxed (&structure, g_ptr_array_index (parameters, i));

      g_array_set_size (output, output->len + 1);

      if (!dbus_g_type_struct_get (&structure,
            0, &param->name,
            1, &param->flags,
            2, &param->dbus_signature,
            3, &tmp,
            G_MAXUINT))
        {
          DEBUG ("Unparseable parameter #%d for %s, ignoring", i, protocol);
          /* *shrug* that one didn't work, let's skip it */
          g_array_set_size (output, output->len - 1);
          continue;
        }

      g_value_init (&param->default_value,
          G_VALUE_TYPE (tmp));
      g_value_copy (tmp, &param->default_value);
      g_value_unset (tmp);
      g_free (tmp);

      param->priv = NULL;

      DEBUG ("\tParam name: %s", param->name);
      DEBUG ("\tParam flags: 0x%x", param->flags);
      DEBUG ("\tParam sig: %s", param->dbus_signature);

      if ((!tp_strdiff (param->name, "password") ||
          g_str_has_suffix (param->name, "-password")) &&
          (param->flags & TP_CONN_MGR_PARAM_FLAG_SECRET) == 0)
        {
          DEBUG ("\tTreating as secret due to its name (please fix %s)",
              cm_debug_name);
          param->flags |= TP_CONN_MGR_PARAM_FLAG_SECRET;
        }

#ifdef ENABLE_DEBUG
        {
          gchar *repr = g_strdup_value_contents (&(param->default_value));

          DEBUG ("\tParam default value: %s of type %s", repr,
              G_VALUE_TYPE_NAME (&(param->default_value)));
          g_free (repr);
        }
#endif
    }

  return (TpConnectionManagerParam *) g_array_free (output, FALSE);
}

static void
tp_protocol_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  TpProtocol *self = (TpProtocol *) object;

  switch (property_id)
    {
    case PROP_PROTOCOL_NAME:
      g_value_set_string (value, self->priv->protocol_struct.name);
      break;

    case PROP_PROTOCOL_PROPERTIES:
      g_value_set_boxed (value, self->priv->protocol_properties);
      break;

    case PROP_ENGLISH_NAME:
      g_value_set_string (value, tp_protocol_get_english_name (self));
      break;

    case PROP_VCARD_FIELD:
      g_value_set_string (value, tp_protocol_get_vcard_field (self));
      break;

    case PROP_ICON_NAME:
      g_value_set_string (value, tp_protocol_get_icon_name (self));
      break;

    case PROP_CAPABILITIES:
      g_value_set_object (value, tp_protocol_get_capabilities (self));
      break;

    case PROP_PARAM_NAMES:
      g_value_take_boxed (value, tp_protocol_dup_param_names (self));
      break;

    case PROP_AUTHENTICATION_TYPES:
      g_value_set_boxed (value, tp_protocol_get_authentication_types (self));
      break;

    case PROP_AVATAR_REQUIREMENTS:
      g_value_set_pointer (value, tp_protocol_get_avatar_requirements (self));
      break;

    case PROP_CM_NAME:
      g_value_set_string (value, tp_protocol_get_cm_name (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
tp_protocol_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  TpProtocol *self = (TpProtocol *) object;

  switch (property_id)
    {
    case PROP_PROTOCOL_NAME:
      g_assert (self->priv->protocol_struct.name == NULL);
      self->priv->protocol_struct.name = g_value_dup_string (value);
      break;

    case PROP_PROTOCOL_PROPERTIES:
      g_assert (self->priv->protocol_properties == NULL);
      self->priv->protocol_properties = g_value_dup_boxed (value);
      break;

    case PROP_CM_NAME:
      g_assert (self->priv->cm_name == NULL);
      self->priv->cm_name = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

void
_tp_connection_manager_param_free_contents (TpConnectionManagerParam *param)
{
  g_free (param->name);
  g_free (param->dbus_signature);

  if (G_IS_VALUE (&param->default_value))
    g_value_unset (&param->default_value);
}

void
_tp_connection_manager_protocol_free_contents (
    TpConnectionManagerProtocol *proto)
{
  g_free (proto->name);

  if (proto->params != NULL)
    {
      TpConnectionManagerParam *param;

      for (param = proto->params; param->name != NULL; param++)
        _tp_connection_manager_param_free_contents (param);
    }

  g_free (proto->params);
}

static void
tp_protocol_dispose (GObject *object)
{
  TpProtocol *self = TP_PROTOCOL (object);
  GObjectFinalizeFunc dispose =
    ((GObjectClass *) tp_protocol_parent_class)->dispose;

  if (self->priv->capabilities != NULL)
    {
      g_object_unref (self->priv->capabilities);
      self->priv->capabilities = NULL;
    }

  if (self->priv->authentication_types)
    {
      g_strfreev (self->priv->authentication_types);
      self->priv->authentication_types = NULL;
    }

  tp_clear_pointer (&self->priv->avatar_req, tp_avatar_requirements_destroy);

  if (dispose != NULL)
    dispose (object);
}

static void
tp_protocol_finalize (GObject *object)
{
  TpProtocol *self = TP_PROTOCOL (object);
  GObjectFinalizeFunc finalize =
    ((GObjectClass *) tp_protocol_parent_class)->finalize;

  _tp_connection_manager_protocol_free_contents (&self->priv->protocol_struct);
  g_free (self->priv->vcard_field);
  g_free (self->priv->english_name);
  g_free (self->priv->icon_name);
  g_free (self->priv->cm_name);

  if (self->priv->protocol_properties != NULL)
    g_hash_table_unref (self->priv->protocol_properties);

  if (finalize != NULL)
    finalize (object);
}

static gboolean
tp_protocol_check_for_core (TpProtocol *self)
{
  const GHashTable *props = self->priv->protocol_properties;
  const GValue *value;

  /* this one can legitimately be NULL so we need to be more careful */
  value = tp_asv_lookup (props, TP_PROP_PROTOCOL_CONNECTION_INTERFACES);

  if (value == NULL || !G_VALUE_HOLDS (value, G_TYPE_STRV))
    return FALSE;

  if (tp_asv_get_boxed (props, TP_PROP_PROTOCOL_REQUESTABLE_CHANNEL_CLASSES,
        TP_ARRAY_TYPE_REQUESTABLE_CHANNEL_CLASS_LIST) == NULL)
    return FALSE;

  /* Interfaces has a sensible default, the empty list.
   * VCardField, EnglishName and Icon have a sensible default, "". */

  return TRUE;
}

static gchar *
title_case (const gchar *s)
{
  gunichar u;
  /* 6 bytes are enough for any Unicode character, 7th byte remains '\0' */
  gchar buf[7] = { 0 };

  /* if s isn't UTF-8, give up and use it as-is */
  if (!g_utf8_validate (s, -1, NULL))
    return g_strdup (s);

  u = g_utf8_get_char (s);

  if (!g_unichar_islower (u))
    return g_strdup (s);

  u = g_unichar_totitle (u);
  g_unichar_to_utf8 (u, buf);
  g_assert (buf [sizeof (buf) - 1] == '\0');

  return g_strdup_printf ("%s%s", buf, g_utf8_next_char (s));
}

static void
tp_protocol_constructed (GObject *object)
{
  TpProtocol *self = (TpProtocol *) object;
  TpProxy *proxy = (TpProxy *) object;
  void (*chain_up) (GObject *) =
    ((GObjectClass *) tp_protocol_parent_class)->constructed;
  const gchar *s;
  const GPtrArray *rccs;
  gboolean had_immutables = TRUE;
  const gchar * const *auth_types = NULL;
  const gchar * const *interfaces;

  if (chain_up != NULL)
    chain_up (object);

  g_assert (self->priv->protocol_struct.name != NULL);

  if (self->priv->protocol_properties == NULL)
    {
      had_immutables = FALSE;
      self->priv->protocol_properties = g_hash_table_new_full (g_str_hash,
          g_str_equal, g_free, (GDestroyNotify) tp_g_value_slice_free);
    }

  self->priv->protocol_struct.params = tp_protocol_params_from_param_specs (
        tp_asv_get_boxed (self->priv->protocol_properties,
          TP_PROP_PROTOCOL_PARAMETERS,
          TP_ARRAY_TYPE_PARAM_SPEC_LIST),
        tp_proxy_get_bus_name (self), self->priv->protocol_struct.name);

  /* force vCard field to lower case, even if the CM is spec-incompliant */
  s = tp_asv_get_string (self->priv->protocol_properties,
      TP_PROP_PROTOCOL_VCARD_FIELD);

  if (tp_str_empty (s))
    self->priv->vcard_field = NULL;
  else
    self->priv->vcard_field = g_utf8_strdown (s, -1);

  s = tp_asv_get_string (self->priv->protocol_properties,
      TP_PROP_PROTOCOL_ENGLISH_NAME);

  if (tp_str_empty (s))
    self->priv->english_name = title_case (self->priv->protocol_struct.name);
  else
    self->priv->english_name = g_strdup (s);

  s = tp_asv_get_string (self->priv->protocol_properties,
      TP_PROP_PROTOCOL_ICON);

  if (tp_str_empty (s))
    self->priv->icon_name = g_strdup_printf ("im-%s",
        self->priv->protocol_struct.name);
  else
    self->priv->icon_name = g_strdup (s);

  rccs = tp_asv_get_boxed (self->priv->protocol_properties,
        TP_PROP_PROTOCOL_REQUESTABLE_CHANNEL_CLASSES,
        TP_ARRAY_TYPE_REQUESTABLE_CHANNEL_CLASS_LIST);

  if (rccs != NULL)
    self->priv->capabilities = _tp_capabilities_new (rccs, FALSE);

  auth_types = tp_asv_get_boxed (
      self->priv->protocol_properties,
      TP_PROP_PROTOCOL_AUTHENTICATION_TYPES, G_TYPE_STRV);

  if (auth_types != NULL)
    {
      self->priv->authentication_types = g_strdupv ((GStrv) auth_types);
    }
  else
    {
      gchar *tmp[] = { NULL };
      self->priv->authentication_types = g_strdupv (tmp);
    }

  interfaces = tp_asv_get_strv (self->priv->protocol_properties,
      TP_PROP_PROTOCOL_INTERFACES);

  tp_proxy_add_interfaces (proxy, interfaces);

  if (tp_proxy_has_interface_by_id (self,
        TP_IFACE_QUARK_PROTOCOL_INTERFACE_AVATARS))
    {
      self->priv->avatar_req = tp_avatar_requirements_new (
          (GStrv) tp_asv_get_strv (self->priv->protocol_properties,
            TP_PROP_PROTOCOL_INTERFACE_AVATARS_SUPPORTED_AVATAR_MIME_TYPES),
          tp_asv_get_uint32 (self->priv->protocol_properties,
            TP_PROP_PROTOCOL_INTERFACE_AVATARS_MINIMUM_AVATAR_WIDTH, NULL),
          tp_asv_get_uint32 (self->priv->protocol_properties,
            TP_PROP_PROTOCOL_INTERFACE_AVATARS_MINIMUM_AVATAR_HEIGHT, NULL),
          tp_asv_get_uint32 (self->priv->protocol_properties,
            TP_PROP_PROTOCOL_INTERFACE_AVATARS_RECOMMENDED_AVATAR_WIDTH, NULL),
          tp_asv_get_uint32 (self->priv->protocol_properties,
            TP_PROP_PROTOCOL_INTERFACE_AVATARS_RECOMMENDED_AVATAR_HEIGHT, NULL),
          tp_asv_get_uint32 (self->priv->protocol_properties,
            TP_PROP_PROTOCOL_INTERFACE_AVATARS_MAXIMUM_AVATAR_WIDTH, NULL),
          tp_asv_get_uint32 (self->priv->protocol_properties,
            TP_PROP_PROTOCOL_INTERFACE_AVATARS_MAXIMUM_AVATAR_HEIGHT, NULL),
          tp_asv_get_uint32 (self->priv->protocol_properties,
            TP_PROP_PROTOCOL_INTERFACE_AVATARS_MAXIMUM_AVATAR_BYTES, NULL));
    }

  /* become ready immediately */
  _tp_proxy_set_feature_prepared (proxy, TP_PROTOCOL_FEATURE_PARAMETERS,
      had_immutables);
  _tp_proxy_set_feature_prepared (proxy, TP_PROTOCOL_FEATURE_CORE,
      had_immutables && tp_protocol_check_for_core (self));
}

enum {
    FEAT_PARAMETERS,
    FEAT_CORE,
    N_FEAT
};

static const TpProxyFeature *
tp_protocol_list_features (TpProxyClass *cls G_GNUC_UNUSED)
{
  static TpProxyFeature features[N_FEAT + 1] = { { 0 } };

  if (G_LIKELY (features[0].name != 0))
    return features;

  /* we always try to prepare both of these features, and nothing else is
   * allowed to complete until they have succeeded or failed */
  features[FEAT_PARAMETERS].name = TP_PROTOCOL_FEATURE_PARAMETERS;
  features[FEAT_PARAMETERS].core = TRUE;
  features[FEAT_CORE].name = TP_PROTOCOL_FEATURE_CORE;
  features[FEAT_CORE].core = TRUE;

  /* assert that the terminator at the end is there */
  g_assert (features[N_FEAT].name == 0);

  return features;
}

static void
tp_protocol_class_init (TpProtocolClass *klass)
{
  TpProxyClass *proxy_class = (TpProxyClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (TpProtocolPrivate));

  object_class->constructed = tp_protocol_constructed;
  object_class->get_property = tp_protocol_get_property;
  object_class->set_property = tp_protocol_set_property;
  object_class->dispose = tp_protocol_dispose;
  object_class->finalize = tp_protocol_finalize;

  /**
   * TpProtocol:protocol-name:
   *
   * The machine-readable name of the protocol, taken from the Telepathy
   * D-Bus Interface Specification, such as "jabber" or "local-xmpp".
   *
   * Since: 0.11.11
   */
  g_object_class_install_property (object_class, PROP_PROTOCOL_NAME,
      g_param_spec_string ("protocol-name",
        "Name of this protocol",
        "The Protocol from telepathy-spec, such as 'jabber' or 'local-xmpp'",
        NULL,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * TpProtocol:protocol-properties:
   *
   * The immutable properties of this Protocol, as provided at construction
   * time. This is a map from string to #GValue, which must not be modified.
   *
   * If the immutable properties were not provided at construction time,
   * the %TP_PROTOCOL_FEATURE_PARAMETERS and %TP_PROTOCOL_FEATURE_CORE features
   * will both be unavailable, and this #TpProtocol object will only be useful
   * as a way to access lower-level D-Bus calls.
   *
   * Since: 0.11.11
   */
  g_object_class_install_property (object_class, PROP_PROTOCOL_PROPERTIES,
      g_param_spec_boxed ("protocol-properties",
        "Protocol properties",
        "The immutable properties of this Protocol",
        TP_HASH_TYPE_QUALIFIED_PROPERTY_VALUE_MAP,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * TpProtocol:english-name:
   *
   * The name of the protocol in a form suitable for display to users,
   * such as "AIM" or "Yahoo!", or a string based on #TpProtocol:protocol-name
   * (currently constructed by putting the first character in title case,
   * but this is not guaranteed) if no better name is available or the
   * %TP_PROTOCOL_FEATURE_CORE feature has not been prepared.
   *
   * This is effectively in the C locale (international English); user
   * interfaces requiring a localized protocol name should look one up in their
   * own message catalog based on either #TpProtocol:protocol-name or
   * #TpProtocol:english-name, but should use this English version as a
   * fallback if no translated version can be found.
   *
   * Since: 0.11.11
   */
  g_object_class_install_property (object_class, PROP_ENGLISH_NAME,
      g_param_spec_string ("english-name",
        "English name",
        "A non-NULL English name for this Protocol",
        NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * TpProtocol:vcard-field:
   *
   * The most common vCard field used for this protocol's contact
   * identifiers, normalized to lower case, or %NULL if there is no such field
   * or the %TP_PROTOCOL_FEATURE_CORE feature has not been prepared.
   *
   * Since: 0.11.11
   */
  g_object_class_install_property (object_class, PROP_VCARD_FIELD,
      g_param_spec_string ("vcard-field",
        "vCard field",
        "A lower-case vCard name for this Protocol, or NULL",
        NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * TpProtocol:icon-name:
   *
   * The name of an icon in the system's icon theme. If none was supplied
   * by the Protocol, or the %TP_PROTOCOL_FEATURE_CORE feature has not been
   * prepared, a default is used; currently, this is "im-" plus
   * #TpProtocol:protocol-name.
   *
   * Since: 0.11.11
   */
  g_object_class_install_property (object_class, PROP_ICON_NAME,
      g_param_spec_string ("icon-name",
        "Icon name",
        "A non-NULL Icon name for this Protocol",
        NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * TpProtocol:capabilities:
   *
   * The classes of channel that can be requested from connections to this
   * protocol, or %NULL if this is unknown or the %TP_PROTOCOL_FEATURE_CORE
   * feature has not been prepared.
   *
   * Since: 0.11.11
   */
  g_object_class_install_property (object_class, PROP_CAPABILITIES,
      g_param_spec_object ("capabilities",
        "Capabilities",
        "Requestable channel classes for this Protocol",
        TP_TYPE_CAPABILITIES, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * TpProtocol:param-names:
   *
   * A list of parameter names supported by this connection manager
   * for this protocol, or %NULL if %TP_PROTOCOL_FEATURE_PARAMETERS has not
   * been prepared.
   *
   * Since: 0.11.11
   */
  g_object_class_install_property (object_class, PROP_PARAM_NAMES,
      g_param_spec_boxed ("param-names",
        "Parameter names",
        "A list of parameter names",
        G_TYPE_STRV, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * TpProtocol:authentication-types:
   *
   * A non-%NULL #GStrv of interfaces which provide information as to
   * what kind of authentication channels can possibly appear before
   * the connection reaches the CONNECTED state, or %NULL if
   * %TP_PROTOCOL_FEATURE_CORE has not been prepared.
   *
   * Since: 0.13.9
   */
  g_object_class_install_property (object_class, PROP_AUTHENTICATION_TYPES,
      g_param_spec_boxed ("authentication-types",
        "AuthenticationTypes",
        "A list of authentication types",
        G_TYPE_STRV, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * TpProtocol:avatar-requirements:
   *
   * A #TpAvatarRequirements representing the avatar requirements on this
   * protocol, or %NULL if %TP_PROTOCOL_FEATURE_CORE has not been prepared or
   * if the protocol doesn't support avatars.
   *
   * Since: 0.15.6
   */
  g_object_class_install_property (object_class, PROP_AVATAR_REQUIREMENTS,
      g_param_spec_pointer ("avatar-requirements",
        "Avatars requirements",
        "Avatars requirements",
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * TpProtocol:cm-name:
   *
   * The name of the connection manager this protocol is on.
   *
   * Since: 0.19.1
   */
  g_object_class_install_property (object_class, PROP_CM_NAME,
      g_param_spec_string ("cm-name",
        "Connection manager name",
        "Name of the CM this protocol is on",
        NULL,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  proxy_class->list_features = tp_protocol_list_features;
  proxy_class->must_have_unique_name = FALSE;
  proxy_class->interface = TP_IFACE_QUARK_PROTOCOL;
  tp_protocol_init_known_interfaces ();
}

static void
tp_protocol_init (TpProtocol *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, TP_TYPE_PROTOCOL,
      TpProtocolPrivate);
}

/**
 * tp_protocol_new:
 * @dbus: proxy for the D-Bus daemon; may not be %NULL
 * @cm_name: the connection manager name (such as "gabble")
 * @protocol_name: the protocol name (such as "jabber")
 * @immutable_properties: the immutable D-Bus properties for this protocol
 * @error: used to indicate the error if %NULL is returned
 *
 * <!-- -->
 *
 * Returns: a new protocol proxy, or %NULL on invalid arguments
 *
 * Since: 0.11.11
 */
TpProtocol *
tp_protocol_new (TpDBusDaemon *dbus,
    const gchar *cm_name,
    const gchar *protocol_name,
    const GHashTable *immutable_properties,
    GError **error)
{
  TpProtocol *ret = NULL;
  gchar *bus_name = NULL;
  gchar *object_path = NULL;

  g_return_val_if_fail (TP_IS_DBUS_DAEMON (dbus), NULL);

  if (!tp_connection_manager_check_valid_protocol_name (protocol_name, error))
    goto finally;

  if (!tp_connection_manager_check_valid_name (cm_name, error))
    goto finally;

  bus_name = g_strdup_printf ("%s%s", TP_CM_BUS_NAME_BASE, cm_name);
  object_path = g_strdup_printf ("%s%s/%s", TP_CM_OBJECT_PATH_BASE, cm_name,
      protocol_name);
  /* e.g. local-xmpp -> local_xmpp */
  g_strdelimit (object_path, "-", '_');

  ret = TP_PROTOCOL (g_object_new (TP_TYPE_PROTOCOL,
        "dbus-daemon", dbus,
        "bus-name", bus_name,
        "object-path", object_path,
        "protocol-name", protocol_name,
        "protocol-properties", immutable_properties,
        "cm-name", cm_name,
        NULL));

finally:
  g_free (bus_name);
  g_free (object_path);
  return ret;
}

/**
 * tp_protocol_init_known_interfaces:
 *
 * Ensure that the known interfaces for TpProtocol have been set up.
 * This is done automatically when necessary, but for correct
 * overriding of library interfaces by local extensions, you should
 * call this function before calling
 * tp_proxy_or_subclass_hook_on_interface_add() with first argument
 * %TP_TYPE_PROTOCOL.
 *
 * Since: 0.11.11
 */
void
tp_protocol_init_known_interfaces (void)
{
  static gsize once = 0;

  if (g_once_init_enter (&once))
    {
      GType type = TP_TYPE_PROTOCOL;

      tp_proxy_init_known_interfaces ();

      tp_proxy_or_subclass_hook_on_interface_add (type,
          tp_cli_protocol_add_signals);
      tp_proxy_subclass_add_error_mapping (type,
          TP_ERROR_PREFIX, TP_ERROR, TP_TYPE_ERROR);

      g_once_init_leave (&once, 1);
    }
}

TpConnectionManagerProtocol *
_tp_protocol_get_struct (TpProtocol *self)
{
  return &self->priv->protocol_struct;
}

/**
 * tp_protocol_get_name:
 * @self: a protocol object
 *
 * Return the same thing as the protocol-name property, for convenient use
 * in C code. The returned string is valid for as long as @self exists.
 *
 * Returns: the value of the #TpProtocol:protocol-name property
 *
 * Since: 0.11.11
 */
const gchar *
tp_protocol_get_name (TpProtocol *self)
{
  g_return_val_if_fail (TP_IS_PROTOCOL (self), NULL);
  return self->priv->protocol_struct.name;
}

/**
 * tp_protocol_has_param:
 * @self: a protocol
 * @param: a parameter name
 *
 * <!-- no more to say -->
 *
 * Returns: %TRUE if @self supports the parameter @param.
 *
 * Since: 0.11.11
 */
gboolean
tp_protocol_has_param (TpProtocol *self,
    const gchar *param)
{
  return (tp_protocol_get_param (self, param) != NULL);
}

/**
 * tp_protocol_get_param:
 * @self: a protocol
 * @param: a parameter name
 *
 * <!-- no more to say -->
 *
 * Returns: a structure representing the parameter @param, or %NULL if not
 *          supported
 *
 * Since: 0.11.11
 */
const TpConnectionManagerParam *
tp_protocol_get_param (TpProtocol *self,
    const gchar *param)
{
  g_return_val_if_fail (TP_IS_PROTOCOL (self), FALSE);
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  return tp_connection_manager_protocol_get_param (
      &self->priv->protocol_struct, param);
G_GNUC_END_IGNORE_DEPRECATIONS
}

/**
 * tp_protocol_dup_param:
 * @self: a protocol
 * @param: a parameter name
 *
 * <!-- no more to say -->
 *
 * Returns: (transfer full): a structure representing the parameter @param,
 *  or %NULL if not supported. Free with tp_connection_manager_param_free()
 *
 * Since: 0.17.6
 */
TpConnectionManagerParam *
tp_protocol_dup_param (TpProtocol *self,
    const gchar *param)
{
  g_return_val_if_fail (TP_IS_PROTOCOL (self), NULL);

  return tp_connection_manager_param_copy (tp_protocol_get_param (self, param));
}

/**
 * tp_protocol_can_register:
 * @self: a protocol
 *
 * Return whether a new account can be registered on this protocol, by setting
 * the special "register" parameter to %TRUE.
 *
 * Returns: %TRUE if @protocol supports the parameter "register"
 *
 * Since: 0.11.11
 */
gboolean
tp_protocol_can_register (TpProtocol *self)
{
  return tp_protocol_has_param (self, "register");
}

/**
 * tp_protocol_dup_param_names:
 * @self: a protocol
 *
 * Returns a list of parameter names supported by this connection manager
 * for this protocol.
 *
 * The result is copied and must be freed by the caller with g_strfreev().
 *
 * Returns: (array zero-terminated=1) (transfer full): a copy of
 *  #TpProtocol:param-names
 *
 * Since: 0.11.11
 */
GStrv
tp_protocol_dup_param_names (TpProtocol *self)
{
  g_return_val_if_fail (TP_IS_PROTOCOL (self), NULL);
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  return tp_connection_manager_protocol_dup_param_names (
      &self->priv->protocol_struct);
G_GNUC_END_IGNORE_DEPRECATIONS
}

/**
 * tp_protocol_borrow_params: (skip)
 * @self: a protocol
 *
 * Returns an array of parameters supported by this connection manager,
 * without additional memory allocations. The returned array is owned by
 * @self, and must not be used after @self has been freed.
 *
 * Returns: (transfer none): an array of #TpConnectionManagerParam structures,
 *  terminated by one whose @name is %NULL
 *
 * Since: 0.17.6
 * Deprecated: Since 0.UNRELEASED. New code should use tp_protocol_dup_params()
 *  instead.
 */
const TpConnectionManagerParam *
tp_protocol_borrow_params (TpProtocol *self)
{
  g_return_val_if_fail (TP_IS_PROTOCOL (self), NULL);

  return self->priv->protocol_struct.params;
}

/**
 * tp_protocol_dup_params:
 * @self: a protocol
 *
 * Returns a list of parameters supported by this connection manager.
 *
 * The returned list must be freed by the caller, for instance with
 * <literal>g_list_free_full (l,
 * (GDestroyNotify) tp_connection_manager_param_free)</literal>.
 *
 * Returns: (transfer full) (element-type TelepathyGLib.ConnectionManagerParam):
 *  a list of #TpConnectionManagerParam structures, owned by the caller
 *
 * Since: 0.17.6
 */
GList *
tp_protocol_dup_params (TpProtocol *self)
{
  guint i;
  GList *ret = NULL;

  g_return_val_if_fail (TP_IS_PROTOCOL (self), NULL);

  for (i = 0; self->priv->protocol_struct.params[i].name != NULL; i++)
    {
      ret = g_list_prepend (ret,
          tp_connection_manager_param_copy (
            &(self->priv->protocol_struct.params[i])));
    }

  return g_list_reverse (ret);
}

/**
 * tp_protocol_get_vcard_field:
 * @self: a protocol object
 *
 * <!-- -->
 *
 * Returns: the value of #TpProtocol:vcard-field
 *
 * Since: 0.11.11
 */
const gchar *
tp_protocol_get_vcard_field (TpProtocol *self)
{
  g_return_val_if_fail (TP_IS_PROTOCOL (self), NULL);
  return self->priv->vcard_field;
}

/**
 * tp_protocol_get_english_name:
 * @self: a protocol object
 *
 * <!-- -->
 *
 * Returns: the non-%NULL, non-empty value of #TpProtocol:english-name
 *
 * Since: 0.11.11
 */
const gchar *
tp_protocol_get_english_name (TpProtocol *self)
{
  g_return_val_if_fail (TP_IS_PROTOCOL (self), "");
  return self->priv->english_name;
}

/**
 * tp_protocol_get_icon_name:
 * @self: a protocol object
 *
 * <!-- -->
 *
 * Returns: the non-%NULL, non-empty value of #TpProtocol:icon-name
 *
 * Since: 0.11.11
 */
const gchar *
tp_protocol_get_icon_name (TpProtocol *self)
{
  g_return_val_if_fail (TP_IS_PROTOCOL (self), "dialog-error");
  return self->priv->icon_name;
}

/**
 * tp_protocol_get_authentication_types:
 * @self: a protocol object
 *
 *
 <!-- -->
 *
 * Returns: (transfer none): the value of #TpProtocol:authentication-types
 *
 * Since: 0.13.9
 */
const gchar * const *
tp_protocol_get_authentication_types (TpProtocol *self)
{
  g_return_val_if_fail (TP_IS_PROTOCOL (self), NULL);
  return (const gchar * const *) self->priv->authentication_types;
}

/**
 * tp_protocol_get_capabilities:
 * @self: a protocol object
 *
 * <!-- -->
 *
 * Returns: (transfer none): #TpProtocol:capabilities, which must be referenced
 *  (if non-%NULL) if it will be kept
 *
 * Since: 0.11.11
 */
TpCapabilities *
tp_protocol_get_capabilities (TpProtocol *self)
{
  g_return_val_if_fail (TP_IS_PROTOCOL (self), NULL);
  return self->priv->capabilities;
}

static gboolean
init_gvalue_from_dbus_sig (const gchar *sig,
                           GValue *value)
{
  g_assert (!G_IS_VALUE (value));

  switch (sig[0])
    {
    case 'b':
      g_value_init (value, G_TYPE_BOOLEAN);
      return TRUE;

    case 's':
      g_value_init (value, G_TYPE_STRING);
      return TRUE;

    case 'q':
    case 'u':
      g_value_init (value, G_TYPE_UINT);
      return TRUE;

    case 'y':
      g_value_init (value, G_TYPE_UCHAR);
      return TRUE;

    case 'n':
    case 'i':
      g_value_init (value, G_TYPE_INT);
      return TRUE;

    case 'x':
      g_value_init (value, G_TYPE_INT64);
      return TRUE;

    case 't':
      g_value_init (value, G_TYPE_UINT64);
      return TRUE;

    case 'o':
      g_value_init (value, DBUS_TYPE_G_OBJECT_PATH);
      g_value_set_static_boxed (value, "/");
      return TRUE;

    case 'd':
      g_value_init (value, G_TYPE_DOUBLE);
      return TRUE;

    case 'v':
      g_value_init (value, G_TYPE_VALUE);
      return TRUE;

    case 'a':
      switch (sig[1])
        {
        case 's':
          g_value_init (value, G_TYPE_STRV);
          return TRUE;

        case 'o':
          g_value_init (value, TP_ARRAY_TYPE_OBJECT_PATH_LIST);
          return TRUE;

        case 'y':
          g_value_init (value, DBUS_TYPE_G_UCHAR_ARRAY);
          return TRUE;
        }
    }

  return FALSE;
}

static gboolean
parse_default_value (GValue *value,
                     const gchar *sig,
                     gchar *string,
                     GKeyFile *file,
                     const gchar *group,
                     const gchar *key)
{
  GError *error = NULL;
  gchar *s, *p;

  switch (sig[0])
    {
    case 'b':
      g_value_set_boolean (value, g_key_file_get_boolean (file, group, key,
            &error));

      if (error == NULL)
        return TRUE;

      /* In telepathy-glib < 0.7.26 we accepted true and false in
       * any case combination, 0, and 1. The desktop file spec specifies
       * "true" and "false" only, while GKeyFile currently accepts 0 and 1 too.
       * So, on error, let's fall back to more lenient parsing that explicitly
       * allows everything we historically allowed. */
      g_error_free (error);
      s = g_key_file_get_value (file, group, key, NULL);

      if (s == NULL)
        return FALSE;

      for (p = s; *p != '\0'; p++)
        {
          *p = g_ascii_tolower (*p);
        }

      if (!tp_strdiff (s, "1") || !tp_strdiff (s, "true"))
        {
          g_value_set_boolean (value, TRUE);
        }
      else if (!tp_strdiff (s, "0") || !tp_strdiff (s, "false"))
        {
          g_value_set_boolean (value, TRUE);
        }
      else
        {
          g_free (s);
          return FALSE;
        }

      g_free (s);
      return TRUE;

    case 's':
      s = g_key_file_get_string (file, group, key, NULL);

      g_value_take_string (value, s);
      return (s != NULL);

    case 'y':
    case 'q':
    case 'u':
    case 't':
        {
          guint64 v = tp_g_key_file_get_uint64 (file, group, key, &error);

          if (error != NULL)
            {
              g_error_free (error);
              return FALSE;
            }

          if (sig[0] == 't')
            {
              g_value_set_uint64 (value, v);
              return TRUE;
            }

          if (sig[0] == 'y')
            {
              if (v > G_MAXUINT8)
                {
                  return FALSE;
                }

              g_value_set_uchar (value, v);
              return TRUE;
            }

          if (v > G_MAXUINT32 || (sig[0] == 'q' && v > G_MAXUINT16))
            return FALSE;

          g_value_set_uint (value, v);
          return TRUE;
        }

    case 'n':
    case 'i':
    case 'x':
      if (string[0] == '\0')
        {
          return FALSE;
        }
      else
        {
          gint64 v = tp_g_key_file_get_int64 (file, group, key, &error);

          if (error != NULL)
            {
              g_error_free (error);
              return FALSE;
            }

          if (sig[0] == 'x')
            {
              g_value_set_int64 (value, v);
              return TRUE;
            }

          if (v > G_MAXINT32 || (sig[0] == 'q' && v > G_MAXINT16))
            return FALSE;

          if (v < G_MININT32 || (sig[0] == 'n' && v < G_MININT16))
            return FALSE;

          g_value_set_int (value, v);
          return TRUE;
        }

    case 'o':
      s = g_key_file_get_string (file, group, key, NULL);

      if (s == NULL || !tp_dbus_check_valid_object_path (s, NULL))
        {
          g_free (s);
          return FALSE;
        }

      g_value_take_boxed (value, s);

      return TRUE;

    case 'd':
      g_value_set_double (value, g_key_file_get_double (file, group, key,
            &error));

      if (error != NULL)
        {
          g_error_free (error);
          return FALSE;
        }

      return TRUE;

    case 'a':
      switch (sig[1])
        {
        case 's':
            {
              g_value_take_boxed (value,
                  g_key_file_get_string_list (file, group, key, NULL, &error));

              if (error != NULL)
                {
                  g_error_free (error);
                  return FALSE;
                }

              return TRUE;
            }

        case 'o':
            {
              gsize len = 0;
              GStrv strv = g_key_file_get_string_list (file, group, key, &len,
                  &error);
              gchar **iter;
              GPtrArray *arr;

              if (error != NULL)
                {
                  g_error_free (error);
                  return FALSE;
                }

              for (iter = strv; iter != NULL && *iter != NULL; iter++)
                {
                  if (!g_variant_is_object_path (*iter))
                    {
                      g_strfreev (strv);
                      return FALSE;
                    }
                }

              arr = g_ptr_array_sized_new (len);

              for (iter = strv; iter != NULL && *iter != NULL; iter++)
                {
                  /* transfer ownership */
                  g_ptr_array_add (arr, *iter);
                }

              g_free (strv);
              g_value_take_boxed (value, arr);

              return TRUE;
            }
        }
    }

  if (G_IS_VALUE (value))
    g_value_unset (value);

  return FALSE;
}

#define PROTOCOL_PREFIX "Protocol "
#define PROTOCOL_PREFIX_LEN 9
tp_verify (sizeof (PROTOCOL_PREFIX) == PROTOCOL_PREFIX_LEN + 1);

static gchar *
replace_null_with_empty (gchar *in)
{
  return (in == NULL ? g_strdup ("") : in);
}

static GHashTable *
_tp_protocol_parse_channel_class (GKeyFile *file,
    const gchar *group)
{
  GHashTable *ret;
  gchar **keys, **key;

  ret = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);

  keys = g_key_file_get_keys (file, group, NULL, NULL);

  for (key = keys; key != NULL && *key != NULL; key++)
    {
      gchar *space = strchr (*key, ' ');
      gchar *value = NULL;
      gchar *property = NULL;
      const gchar *dbus_type;
      GValue *v = g_slice_new0 (GValue);

      value = g_key_file_get_string (file, group, *key, NULL);

      /* keys without a space are reserved */
      if (space == NULL)
        goto cleanup;

      property = g_strndup (*key, space - *key);
      dbus_type = space + 1;

      if (!init_gvalue_from_dbus_sig (dbus_type, v))
        goto cleanup;

      if (!parse_default_value (v, dbus_type, value, file, group, *key))
        goto cleanup;

      /* transfer ownership to @ret */
      g_hash_table_insert (ret, property, v);
      property = NULL;
      v = NULL;

cleanup:
      if (v != NULL)
        {
          if (G_IS_VALUE (v))
            tp_g_value_slice_free (v);
          else
            g_slice_free (GValue, v);
        }

      g_free (property);
      g_free (value);
    }

  g_strfreev (keys);

  return ret;
}

static GValueArray *
_tp_protocol_parse_rcc (GKeyFile *file,
    const gchar *group)
{
  GHashTable *fixed;
  GStrv allowed;
  GValueArray *ret;

  fixed = _tp_protocol_parse_channel_class (file, group);
  allowed = g_key_file_get_string_list (file, group, "allowed", NULL, NULL);

  ret = tp_value_array_build (2,
      TP_HASH_TYPE_CHANNEL_CLASS, fixed,
      G_TYPE_STRV, allowed,
      NULL);

  g_hash_table_unref (fixed);
  g_strfreev (allowed);

  return ret;
}

GHashTable *
_tp_protocol_parse_manager_file (GKeyFile *file,
    const gchar *cm_debug_name,
    const gchar *group,
    gchar **protocol_name)
{
  GHashTable *immutables;
  GPtrArray *param_specs, *rccs;
  const gchar *name;
  gchar **rcc_groups, **rcc_group;
  gchar **keys, **key;
  guint i;

  if (!g_str_has_prefix (group, PROTOCOL_PREFIX))
    return NULL;

  name = group + PROTOCOL_PREFIX_LEN;

  if (!tp_connection_manager_check_valid_protocol_name (name, NULL))
    {
      DEBUG ("Protocol '%s' has an invalid name", name);
      return NULL;
    }

  keys = g_key_file_get_keys (file, group, NULL, NULL);

  i = 0;

  for (key = keys; key != NULL && *key != NULL; key++)
    {
      if (g_str_has_prefix (*key, "param-"))
        i++;
    }

  param_specs = g_ptr_array_sized_new (i);

  for (key = keys; key != NULL && *key != NULL; key++)
    {
      if (g_str_has_prefix (*key, "param-"))
        {
          gchar **strv, **iter;
          gchar *value, *def;
          TpConnectionManagerParam param = { NULL };

          value = g_key_file_get_string (file, group, *key, NULL);

          if (value == NULL)
            continue;

          /* strlen ("param-") == 6 */
          param.name = *key + 6;

          strv = g_strsplit (value, " ", 0);
          g_free (value);

          param.dbus_signature = strv[0];

          param.flags = 0;

          for (iter = strv + 1; *iter != NULL; iter++)
            {
              if (!tp_strdiff (*iter, "required"))
                param.flags |= TP_CONN_MGR_PARAM_FLAG_REQUIRED;
              if (!tp_strdiff (*iter, "register"))
                param.flags |= TP_CONN_MGR_PARAM_FLAG_REGISTER;
              if (!tp_strdiff (*iter, "secret"))
                param.flags |= TP_CONN_MGR_PARAM_FLAG_SECRET;
              if (!tp_strdiff (*iter, "dbus-property"))
                param.flags |= TP_CONN_MGR_PARAM_FLAG_DBUS_PROPERTY;
            }

          if ((!tp_strdiff (param.name, "password") ||
              g_str_has_suffix (param.name, "-password")) &&
              (param.flags & TP_CONN_MGR_PARAM_FLAG_SECRET) == 0)
            {
              DEBUG ("\tTreating %s as secret due to its name (please "
                  "fix %s.manager)", param.name, cm_debug_name);
              param.flags |= TP_CONN_MGR_PARAM_FLAG_SECRET;
            }

          def = g_strdup_printf ("default-%s", param.name);
          value = g_key_file_get_string (file, group, def, NULL);

          init_gvalue_from_dbus_sig (param.dbus_signature,
              &param.default_value);

          if (value != NULL && parse_default_value (&param.default_value,
                param.dbus_signature, value, file, group, def))
            param.flags |= TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT;

          DEBUG ("\tParam name: %s", param.name);
          DEBUG ("\tParam flags: 0x%x", param.flags);
          DEBUG ("\tParam sig: %s", param.dbus_signature);

#ifdef ENABLE_DEBUG
          if (G_IS_VALUE (&param.default_value))
            {
              gchar *repr = g_strdup_value_contents (&(param.default_value));

              DEBUG ("\tParam default value: %s of type %s", repr,
                  G_VALUE_TYPE_NAME (&(param.default_value)));
              g_free (repr);
            }
          else
            {
              DEBUG ("\tParam default value: not set");
            }
#endif

          g_ptr_array_add (param_specs, tp_value_array_build (4,
                G_TYPE_STRING, param.name,
                G_TYPE_UINT, param.flags,
                G_TYPE_STRING, param.dbus_signature,
                G_TYPE_VALUE, &param.default_value,
                G_TYPE_INVALID));

          if (G_IS_VALUE (&param.default_value))
            g_value_unset (&param.default_value);

          g_free (value);
          g_free (def);
          g_strfreev (strv);
        }
    }

  g_strfreev (keys);

  immutables = tp_asv_new (
      TP_PROP_PROTOCOL_PARAMETERS, TP_ARRAY_TYPE_PARAM_SPEC_LIST, param_specs,
      NULL);

  tp_asv_take_boxed (immutables, TP_PROP_PROTOCOL_INTERFACES, G_TYPE_STRV,
      g_key_file_get_string_list (file, group, "Interfaces", NULL, NULL));
  tp_asv_take_boxed (immutables, TP_PROP_PROTOCOL_CONNECTION_INTERFACES,
      G_TYPE_STRV,
      g_key_file_get_string_list (file, group, "ConnectionInterfaces",
        NULL, NULL));
  tp_asv_take_string (immutables, TP_PROP_PROTOCOL_VCARD_FIELD,
      replace_null_with_empty (
        g_key_file_get_string (file, group, "VCardField", NULL)));
  tp_asv_take_string (immutables, TP_PROP_PROTOCOL_ENGLISH_NAME,
      replace_null_with_empty (
        g_key_file_get_string (file, group, "EnglishName", NULL)));
  tp_asv_take_string (immutables, TP_PROP_PROTOCOL_ICON,
      replace_null_with_empty (
        g_key_file_get_string (file, group, "Icon", NULL)));
  tp_asv_take_boxed (immutables, TP_PROP_PROTOCOL_AUTHENTICATION_TYPES,
      G_TYPE_STRV, g_key_file_get_string_list (file, group,
          "AuthenticationTypes", NULL, NULL));

  /* Avatars */
  tp_asv_take_boxed (immutables,
      TP_PROP_PROTOCOL_INTERFACE_AVATARS_SUPPORTED_AVATAR_MIME_TYPES,
      G_TYPE_STRV,
      g_key_file_get_string_list (file, group, "SupportedAvatarMIMETypes",
        NULL, NULL));
  tp_asv_set_uint32 (immutables,
      TP_PROP_PROTOCOL_INTERFACE_AVATARS_MINIMUM_AVATAR_HEIGHT,
      g_key_file_get_uint64 (file, group, "MinimumAvatarHeight", NULL));
  tp_asv_set_uint32 (immutables,
      TP_PROP_PROTOCOL_INTERFACE_AVATARS_MINIMUM_AVATAR_WIDTH,
      g_key_file_get_uint64 (file, group, "MinimumAvatarWidth", NULL));
  tp_asv_set_uint32 (immutables,
      TP_PROP_PROTOCOL_INTERFACE_AVATARS_RECOMMENDED_AVATAR_HEIGHT,
      g_key_file_get_uint64 (file, group, "RecommendedAvatarHeight", NULL));
  tp_asv_set_uint32 (immutables,
      TP_PROP_PROTOCOL_INTERFACE_AVATARS_RECOMMENDED_AVATAR_WIDTH,
      g_key_file_get_uint64 (file, group, "RecommendedAvatarWidth", NULL));
  tp_asv_set_uint32 (immutables,
      TP_PROP_PROTOCOL_INTERFACE_AVATARS_MAXIMUM_AVATAR_HEIGHT,
      g_key_file_get_uint64 (file, group, "MaximumAvatarHeight", NULL));
  tp_asv_set_uint32 (immutables,
      TP_PROP_PROTOCOL_INTERFACE_AVATARS_MAXIMUM_AVATAR_WIDTH,
      g_key_file_get_uint64 (file, group, "MaximumAvatarWidth", NULL));
  tp_asv_set_uint32 (immutables,
      TP_PROP_PROTOCOL_INTERFACE_AVATARS_MAXIMUM_AVATAR_BYTES,
      g_key_file_get_uint64 (file, group, "MaximumAvatarBytes", NULL));

  rccs = g_ptr_array_new ();

  rcc_groups = g_key_file_get_string_list (file, group,
      "RequestableChannelClasses", NULL, NULL);

  if (rcc_groups != NULL)
    {
      for (rcc_group = rcc_groups; *rcc_group != NULL; rcc_group++)
        g_ptr_array_add (rccs, _tp_protocol_parse_rcc (file, *rcc_group));
    }

  g_strfreev (rcc_groups);

  tp_asv_take_boxed (immutables, TP_PROP_PROTOCOL_REQUESTABLE_CHANNEL_CLASSES,
      TP_ARRAY_TYPE_REQUESTABLE_CHANNEL_CLASS_LIST, rccs);

  if (protocol_name != NULL)
    *protocol_name = g_strdup (name);

  return immutables;
}

/**
 * tp_protocol_get_avatar_requirements:
 * @self: a #TpProtocol
 *
 * Return the #TpProtocol:avatar-requirements property
 *
 * Returns: (transfer none): the value of #TpProtocol:avatar-requirements
 *
 * Since: 0.15.6
 */
TpAvatarRequirements *
tp_protocol_get_avatar_requirements (TpProtocol *self)
{
  g_return_val_if_fail (TP_IS_PROTOCOL (self), NULL);

  return self->priv->avatar_req;
}

/**
 * tp_protocol_get_cm_name:
 * @self: a #TpProtocol
 *
 * Return the #TpProtocol:cm-name property.
 *
 * Returns: the value of #TpProtocol:cm-name
 *
 * Since: 0.19.1
 */
const gchar *
tp_protocol_get_cm_name (TpProtocol *self)
{
  g_return_val_if_fail (TP_IS_PROTOCOL (self), NULL);

  return self->priv->cm_name;
}
