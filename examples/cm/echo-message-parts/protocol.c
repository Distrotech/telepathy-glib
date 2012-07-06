/*
 * protocol.c - an example Protocol
 *
 * Copyright © 2007-2010 Collabora Ltd.
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */

#include "protocol.h"

#include <telepathy-glib/telepathy-glib.h>

#include "conn.h"
#include "im-manager.h"

static void addressing_iface_init (TpProtocolAddressingInterface *iface);

G_DEFINE_TYPE_WITH_CODE (ExampleEcho2Protocol, example_echo_2_protocol,
    TP_TYPE_BASE_PROTOCOL,
    G_IMPLEMENT_INTERFACE (TP_TYPE_PROTOCOL_ADDRESSING, addressing_iface_init))

const gchar * const supported_avatar_mime_types[] = {
  "image/png",
  "image/jpeg",
  "image/gif",
  NULL };

const gchar * const addressing_vcard_fields[] = {
  "x-jabber",
  "tel",
  NULL };

const gchar * const addressing_uri_schemes[] = {
  "xmpp",
  "tel",
  NULL };

static void
example_echo_2_protocol_init (
    ExampleEcho2Protocol *self)
{
}

static const TpCMParamSpec example_echo_2_example_params[] = {
  { "account", "s", G_TYPE_STRING,
    TP_CONN_MGR_PARAM_FLAG_REQUIRED | TP_CONN_MGR_PARAM_FLAG_REGISTER,
    NULL, /* no default */
    0, /* formerly struct offset, now unused */
    tp_cm_param_filter_string_nonempty, /* filter - empty strings disallowed */
    NULL, /* filter data, unused for our filter */
    NULL /* setter data, now unused */ },
  { NULL }
};

static const TpCMParamSpec *
get_parameters (TpBaseProtocol *self)
{
  return example_echo_2_example_params;
}

static TpBaseConnection *
new_connection (TpBaseProtocol *protocol,
    GHashTable *asv,
    GError **error)
{
  ExampleEcho2Connection *conn;
  const gchar *account;

  account = tp_asv_get_string (asv, "account");

  if (account == NULL || account[0] == '\0')
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "The 'account' parameter is required");
      return NULL;
    }

  conn = EXAMPLE_ECHO_2_CONNECTION (
      g_object_new (EXAMPLE_TYPE_ECHO_2_CONNECTION,
        "account", account,
        "protocol", tp_base_protocol_get_name (protocol),
        NULL));

  return (TpBaseConnection *) conn;
}

gchar *
example_echo_2_protocol_normalize_contact (const gchar *id, GError **error)
{
  if (id[0] == '\0')
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_HANDLE,
          "ID must not be empty");
      return NULL;
    }

  return g_utf8_strdown (id, -1);
}

static gchar *
normalize_contact (TpBaseProtocol *self G_GNUC_UNUSED,
    const gchar *contact,
    GError **error)
{
  return example_echo_2_protocol_normalize_contact (contact, error);
}

static gchar *
identify_account (TpBaseProtocol *self G_GNUC_UNUSED,
    GHashTable *asv,
    GError **error)
{
  const gchar *account = tp_asv_get_string (asv, "account");

  if (account != NULL)
    return g_strdup (account);

  g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
      "'account' parameter not given");
  return NULL;
}

static GPtrArray *
get_interfaces_array (TpBaseProtocol *self)
{
  GPtrArray *interfaces;

  interfaces = TP_BASE_PROTOCOL_CLASS (
      example_echo_2_protocol_parent_class)->get_interfaces_array (self);

  g_ptr_array_add (interfaces, TP_IFACE_PROTOCOL_INTERFACE_AVATARS);
  g_ptr_array_add (interfaces, TP_IFACE_PROTOCOL_INTERFACE_ADDRESSING);

  return interfaces;
}

static void
get_connection_details (TpBaseProtocol *self G_GNUC_UNUSED,
    GStrv *connection_interfaces,
    GType **channel_managers,
    gchar **icon_name,
    gchar **english_name,
    gchar **vcard_field)
{
  if (connection_interfaces != NULL)
    {
      *connection_interfaces = g_strdupv (
          (GStrv) example_echo_2_connection_get_possible_interfaces ());
    }

  if (channel_managers != NULL)
    {
      GType types[] = { EXAMPLE_TYPE_ECHO_2_IM_MANAGER, G_TYPE_INVALID };

      *channel_managers = g_memdup (types, sizeof (types));
    }

  if (icon_name != NULL)
    {
      /* a real protocol would use its own icon name - for this example we
       * borrow the one from ICQ */
      *icon_name = g_strdup ("im-icq");
    }

  if (english_name != NULL)
    {
      /* in a real protocol this would be "ICQ" or
       * "Windows Live Messenger (MSN)" or something */
      *english_name = g_strdup ("Echo II example");
    }

  if (vcard_field != NULL)
    {
      /* in a real protocol this would be "tel" or "x-jabber" or something */
      *vcard_field = g_strdup ("x-telepathy-example");
    }
}

static void
get_avatar_details (TpBaseProtocol *self,
    GStrv *supported_mime_types,
    guint *min_height,
    guint *min_width,
    guint *recommended_height,
    guint *recommended_width,
    guint *max_height,
    guint *max_width,
    guint *max_bytes)
{
  if (supported_mime_types != NULL)
    *supported_mime_types = g_strdupv ((GStrv) supported_avatar_mime_types);

  if (min_height != NULL)
    *min_height = 32;

  if (min_width != NULL)
    *min_width = 32;

  if (recommended_height != NULL)
    *recommended_height = 64;

  if (recommended_width != NULL)
    *recommended_width = 64;

  if (max_height != NULL)
    *max_height = 96;

  if (max_width != NULL)
    *max_width = 96;

  if (max_bytes != NULL)
    *max_bytes = 37748736;
}

static GStrv
dup_supported_uri_schemes (TpBaseProtocol *self)
{
  return g_strdupv ((GStrv) addressing_uri_schemes);
}

static GStrv
dup_supported_vcard_fields (TpBaseProtocol *self)
{
  return g_strdupv ((GStrv) addressing_vcard_fields);
}

static void
example_echo_2_protocol_class_init (
    ExampleEcho2ProtocolClass *klass)
{
  TpBaseProtocolClass *base_class =
      (TpBaseProtocolClass *) klass;

  base_class->get_parameters = get_parameters;
  base_class->new_connection = new_connection;

  base_class->normalize_contact = normalize_contact;
  base_class->identify_account = identify_account;
  base_class->get_interfaces_array = get_interfaces_array;
  base_class->get_connection_details = get_connection_details;
  base_class->get_avatar_details = get_avatar_details;
}

static void
addressing_iface_init (TpProtocolAddressingInterface *iface)
{
  iface->dup_supported_vcard_fields = dup_supported_vcard_fields;
  iface->dup_supported_uri_schemes = dup_supported_uri_schemes;
}
