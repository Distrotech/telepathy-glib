/*
 * conn.c - an example connection
 *
 * Copyright © 2007-2010 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright © 2007 Nokia Corporation
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */

#include "config.h"

#include "conn.h"

#include <dbus/dbus-glib.h>

#include <telepathy-glib/telepathy-glib.h>

/* This would conventionally be extensions/extensions.h */
#include "examples/extensions/extensions.h"

#include "protocol.h"

static void _hats_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (ExampleExtendedConnection,
    example_extended_connection,
    TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACTS,
      tp_contacts_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (EXAMPLE_TYPE_SVC_CONNECTION_INTERFACE_HATS,
      _hats_iface_init))

/* type definition stuff */

enum
{
  PROP_ACCOUNT = 1,
  N_PROPS
};

struct _ExampleExtendedConnectionPrivate
{
  gchar *account;

  gchar *hat_color;
  ExampleHatStyle hat_style;
  /* dup'd string => slice-allocated GValue */
  GHashTable *hat_properties;
};

static void
example_extended_connection_init (ExampleExtendedConnection *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, EXAMPLE_TYPE_EXTENDED_CONNECTION,
      ExampleExtendedConnectionPrivate);

  self->priv->hat_color = g_strdup ("");
  self->priv->hat_style = EXAMPLE_HAT_STYLE_NONE;
  self->priv->hat_properties = g_hash_table_new_full (g_str_hash,
      g_str_equal, g_free, (GDestroyNotify) tp_g_value_slice_free);
}

static void
get_property (GObject *object,
              guint property_id,
              GValue *value,
              GParamSpec *spec)
{
  ExampleExtendedConnection *self = EXAMPLE_EXTENDED_CONNECTION (object);

  switch (property_id) {
    case PROP_ACCOUNT:
      g_value_set_string (value, self->priv->account);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, spec);
  }
}

static void
set_property (GObject *object,
              guint property_id,
              const GValue *value,
              GParamSpec *spec)
{
  ExampleExtendedConnection *self = EXAMPLE_EXTENDED_CONNECTION (object);

  switch (property_id) {
    case PROP_ACCOUNT:
      g_free (self->priv->account);
      self->priv->account = g_utf8_strdown (g_value_get_string (value), -1);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, spec);
  }
}

static void
finalize (GObject *object)
{
  ExampleExtendedConnection *self = EXAMPLE_EXTENDED_CONNECTION (object);

  tp_contacts_mixin_finalize (object);
  g_free (self->priv->account);
  g_free (self->priv->hat_color);
  g_hash_table_unref (self->priv->hat_properties);

  G_OBJECT_CLASS (example_extended_connection_parent_class)->finalize (object);
}

static gchar *
get_unique_connection_name (TpBaseConnection *conn)
{
  ExampleExtendedConnection *self = EXAMPLE_EXTENDED_CONNECTION (conn);

  return g_strdup (self->priv->account);
}

static gchar *
example_normalize_contact (TpHandleRepoIface *repo,
                           const gchar *id,
                           gpointer context,
                           GError **error)
{
  return example_extended_protocol_normalize_contact (id, error);
}

static void
create_handle_repos (TpBaseConnection *conn,
                     TpHandleRepoIface *repos[TP_NUM_HANDLE_TYPES])
{
  repos[TP_HANDLE_TYPE_CONTACT] = tp_dynamic_handle_repo_new
      (TP_HANDLE_TYPE_CONTACT, example_normalize_contact, NULL);
}

static GPtrArray *
create_channel_factories (TpBaseConnection *conn)
{
  return g_ptr_array_sized_new (0);
}

static gboolean
start_connecting (TpBaseConnection *conn,
                  GError **error)
{
  ExampleExtendedConnection *self = EXAMPLE_EXTENDED_CONNECTION (conn);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
  TpHandle self_handle;

  /* In a real connection manager we'd ask the underlying implementation to
   * start connecting, then go to state CONNECTED when finished, but here
   * we can do it immediately. */

  self_handle = tp_handle_ensure (contact_repo, self->priv->account,
      NULL, NULL);

  tp_base_connection_set_self_handle (conn, self_handle);

  tp_base_connection_change_status (conn, TP_CONNECTION_STATUS_CONNECTED,
      TP_CONNECTION_STATUS_REASON_REQUESTED);

  return TRUE;
}

static void
shut_down (TpBaseConnection *conn)
{
  /* In a real connection manager we'd ask the underlying implementation to
   * start shutting down, then call this function when finished, but here
   * we can do it immediately. */
  tp_base_connection_finish_shutdown (conn);
}

static void
constructed (GObject *object)
{
  TpBaseConnection *base = TP_BASE_CONNECTION (object);
  void (*chain_up) (GObject *) =
    G_OBJECT_CLASS (example_extended_connection_parent_class)->constructed;

  if (chain_up != NULL)
    chain_up (object);

  tp_contacts_mixin_init (object,
      G_STRUCT_OFFSET (ExampleExtendedConnection, contacts_mixin));
  tp_base_connection_register_with_contacts_mixin (base);
}

static const gchar *interfaces_always_present[] = {
    TP_IFACE_CONNECTION_INTERFACE_REQUESTS,
    TP_IFACE_CONNECTION_INTERFACE_CONTACTS,
    EXAMPLE_IFACE_CONNECTION_INTERFACE_HATS,
    NULL };

const gchar * const *
example_extended_connection_get_possible_interfaces (void)
{
  /* in this example CM we don't have any extra interfaces that are sometimes,
   * but not always, present */
  return interfaces_always_present;
}

static GPtrArray *
get_interfaces_always_present (TpBaseConnection *base)
{
  GPtrArray *interfaces;
  guint i;

  interfaces = TP_BASE_CONNECTION_CLASS (
      example_extended_connection_parent_class)->get_interfaces_always_present (base);

  for (i = 0; interfaces_always_present[i] != NULL; i++)
    g_ptr_array_add (interfaces, (gchar *) interfaces_always_present[i]);

  return interfaces;
}

static void
example_extended_connection_class_init (ExampleExtendedConnectionClass *klass)
{
  TpBaseConnectionClass *base_class =
      (TpBaseConnectionClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *param_spec;

  object_class->constructed = constructed;
  object_class->get_property = get_property;
  object_class->set_property = set_property;
  object_class->finalize = finalize;
  g_type_class_add_private (klass, sizeof (ExampleExtendedConnectionPrivate));

  base_class->create_handle_repos = create_handle_repos;
  base_class->get_unique_connection_name = get_unique_connection_name;
  base_class->create_channel_factories = create_channel_factories;
  base_class->start_connecting = start_connecting;
  base_class->shut_down = shut_down;

  base_class->get_interfaces_always_present = get_interfaces_always_present;

  param_spec = g_param_spec_string ("account", "Account name",
      "The username of this user", NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ACCOUNT, param_spec);

  tp_contacts_mixin_class_init (object_class,
      G_STRUCT_OFFSET (ExampleExtendedConnectionClass, contacts_mixin));
}

static void
my_get_hats (ExampleSvcConnectionInterfaceHats *iface,
             const GArray *contacts,
             DBusGMethodInvocation *context)
{
  ExampleExtendedConnection *self = EXAMPLE_EXTENDED_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  GError *error = NULL;
  guint i;
  GPtrArray *ret;

  if (!tp_handles_are_valid (contact_repo, contacts, FALSE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  ret = g_ptr_array_sized_new (contacts->len);

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, guint, i);
      GValueArray *vals = g_value_array_new (4);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 0), G_TYPE_UINT);
      g_value_set_uint (g_value_array_get_nth (vals, 0), handle);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 1), G_TYPE_STRING);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 2), G_TYPE_UINT);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 3),
          TP_HASH_TYPE_STRING_VARIANT_MAP);

      /* for the sake of a simple example, let's assume nobody except me
       * has any hats */
      if (handle == tp_base_connection_get_self_handle (base))
        {
          g_value_set_string (g_value_array_get_nth (vals, 1),
              self->priv->hat_color);
          g_value_set_uint (g_value_array_get_nth (vals, 2),
              self->priv->hat_style);
          g_value_set_boxed (g_value_array_get_nth (vals, 3),
              self->priv->hat_properties);
        }
      else
        {
          g_value_set_static_string (g_value_array_get_nth (vals, 1), "");
          g_value_set_uint (g_value_array_get_nth (vals, 2),
              EXAMPLE_HAT_STYLE_NONE);
          g_value_take_boxed (g_value_array_get_nth (vals, 3),
              g_hash_table_new (NULL, NULL));
        }

      g_ptr_array_add (ret, vals);
    }

  /* success */
  example_svc_connection_interface_hats_return_from_get_hats (context, ret);

  g_boxed_free (EXAMPLE_ARRAY_TYPE_CONTACT_HAT_LIST, ret);
}

static void
my_set_hat (ExampleSvcConnectionInterfaceHats *iface,
            const gchar *color,
            guint style,
            GHashTable *properties,
            DBusGMethodInvocation *context)
{
  ExampleExtendedConnection *self = EXAMPLE_EXTENDED_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;

  g_free (self->priv->hat_color);
  self->priv->hat_color = g_strdup (color);
  self->priv->hat_style = style;
  g_hash_table_remove_all (self->priv->hat_properties);
  tp_g_hash_table_update (self->priv->hat_properties, properties,
      (GBoxedCopyFunc) g_strdup, (GBoxedCopyFunc) tp_g_value_slice_dup);

  /* success */
  example_svc_connection_interface_hats_emit_hats_changed (self,
      tp_base_connection_get_self_handle (base),
      color, style, properties);
  example_svc_connection_interface_hats_return_from_set_hat (context);
}

static void _hats_iface_init (gpointer g_iface,
                              gpointer iface_data)
{
  ExampleSvcConnectionInterfaceHatsClass *klass = g_iface;

#define IMPLEMENT(x) example_svc_connection_interface_hats_implement_##x \
    (klass, my_##x)
  IMPLEMENT (get_hats);
  IMPLEMENT (set_hat);
#undef IMPLEMENT
}
