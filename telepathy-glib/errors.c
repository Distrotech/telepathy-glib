/*
 * telepathy-errors.c - Source for D-Bus error types used in telepathy
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

#include <telepathy-glib/errors.h>

#include <glib.h>
#include <dbus/dbus-glib.h>

/**
 * TP_ERROR_PREFIX:
 *
 * The common prefix of Telepathy errors, as a string constant, without
 * the trailing '.' character.
 *
 * Since: 0.7.1
 */

/**
 * tp_g_set_error_invalid_handle_type:
 * @type: An invalid handle type
 * @error: Either %NULL, or used to return an error (as for g_set_error)
 *
 * Set the error NotImplemented for an invalid handle type,
 * with an appropriate message.
 *
 * Changed in version 0.7.UNRELEASED: previously, the error was
 * InvalidArgument.
 */
void
tp_g_set_error_invalid_handle_type (guint type, GError **error)
{
  g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
      "unsupported handle type %u", type);
}

/**
 * tp_g_set_error_unsupported_handle_type:
 * @type: An unsupported handle type
 * @error: Either %NULL, or used to return an error (as for g_set_error)
 *
 * Set the error NotImplemented for a handle type which is valid but is not
 * supported by this connection manager, with an appropriate message.
 *
 * Changed in version 0.7.UNRELEASED: previously, the error was
 * InvalidArgument.
 */
void
tp_g_set_error_unsupported_handle_type (guint type, GError **error)
{
  g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
      "unsupported handle type %u", type);
}

/**
 * tp_errors_quark:
 *
 * Return the Telepathy error domain. Since 0.7.17 this function
 * automatically registers the domain with dbus-glib for server-side use
 * (using dbus_g_error_domain_register()) when called.
 *
 * Returns: the Telepathy error domain.
 */
GQuark
tp_errors_quark (void)
{
  static gsize quark = 0;

  if (g_once_init_enter (&quark))
    {
      GQuark domain = g_quark_from_static_string ("tp_errors");

      g_assert (sizeof (GQuark) <= sizeof (gsize));

      g_type_init ();
      dbus_g_error_domain_register (domain, TP_ERROR_PREFIX,
          TP_TYPE_ERROR);
      g_once_init_leave (&quark, domain);
    }

  return (GQuark) quark;
}
