/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* eel-vfs-extensions.c - gnome-vfs extensions.  Its likely some of these will 
                          be part of gnome-vfs in the future.

   Copyright (C) 1999, 2000 Eazel, Inc.

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Authors: Darin Adler <darin@eazel.com>
	    Pavel Cisler <pavel@eazel.com>
	    Mike Fleming  <mfleming@eazel.com>
            John Sullivan <sullivan@eazel.com>
*/

#include <config.h>
#include "eel-i18n.h"
#include "eel-vfs-extensions.h"
#include "eel-glib-extensions.h"
#include "eel-lib-self-check-functions.h"
#include <glib.h>
#include <gio/gio.h>

#include "eel-string.h"

#include <string.h>
#include <stdlib.h>

gboolean
eel_uri_is_trash (const char *uri)
{
	return eel_istr_has_prefix (uri, "trash:");
}

gboolean
eel_uri_is_search (const char *uri)
{
	return eel_istr_has_prefix (uri, EEL_SEARCH_URI);
}

gboolean
eel_uri_is_desktop (const char *uri)
{
	return eel_istr_has_prefix (uri, EEL_DESKTOP_URI);
}

char *
eel_make_valid_utf8 (const char *name)
{
	GString *string;
	const char *remainder, *invalid;
	int remaining_bytes, valid_bytes;

	string = NULL;
	remainder = name;
	remaining_bytes = strlen (name);

	while (remaining_bytes != 0) {
		if (g_utf8_validate (remainder, remaining_bytes, &invalid)) {
			break;
		}
		valid_bytes = invalid - remainder;

		if (string == NULL) {
			string = g_string_sized_new (remaining_bytes);
		}
		g_string_append_len (string, remainder, valid_bytes);
		g_string_append_c (string, '?');

		remaining_bytes -= valid_bytes + 1;
		remainder = invalid + 1;
	}

	if (string == NULL) {
		return g_strdup (name);
	}

	g_string_append (string, remainder);
	g_string_append (string, _(" (invalid Unicode)"));
	g_assert (g_utf8_validate (string->str, -1, NULL));

	return g_string_free (string, FALSE);
}

/**
 * eel_format_uri_for_display:
 *
 * Filter, modify, unescape and change URIs to make them appropriate
 * to display to users. The conversion is done such that the roundtrip
 * to UTf8 is reversible.
 * 
 * Rules:
 * 	file: URI's without fragments should appear as local paths
 * 	file: URI's with fragments should appear as file: URI's
 * 	All other URI's appear as expected
 *
 * @uri: a URI
 *
 * returns a g_malloc'd UTF8 string
 **/
char *
eel_format_uri_for_display (const char *uri) 
{
	GFile *file;
	char *res;

	file = g_file_new_for_uri (uri);
	res = g_file_get_parse_name (file);
	g_object_unref (file);
	return res;
}

char *
eel_filename_strip_extension (const char * filename_with_extension)
{
	char *filename, *end, *end2;

	if (filename_with_extension == NULL) {
		return NULL;
	}
	
	filename = g_strdup (filename_with_extension);

	end = strrchr (filename, '.');

	if (end && end != filename) {
		if (strcmp (end, ".gz") == 0 ||
		    strcmp (end, ".bz2") == 0 ||
		    strcmp (end, ".sit") == 0 ||
		    strcmp (end, ".Z") == 0) {
			end2 = end - 1;
			while (end2 > filename &&
			       *end2 != '.') {
				end2--;
			}
			if (end2 != filename) {
				end = end2;
			}
		}
		*end = '\0';
	}

	return filename;
}

void
eel_filename_get_rename_region (const char           *filename,
				int                  *start_offset,
				int                  *end_offset)
{
	char *filename_without_extension;

	g_return_if_fail (start_offset != NULL);
	g_return_if_fail (end_offset != NULL);

	*start_offset = 0;
	*end_offset = 0;

	g_return_if_fail (filename != NULL);

	filename_without_extension = eel_filename_strip_extension (filename);
	*end_offset = g_utf8_strlen (filename_without_extension, -1);

	g_free (filename_without_extension);
}