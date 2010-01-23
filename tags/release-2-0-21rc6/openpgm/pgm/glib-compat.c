/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * GLIB compatibility functions for older library revisions.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <glib.h>


#if GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 16
static
void
g_error_add_prefix (
	gchar**		string,
	const gchar*	format,
	va_list		ap
	)
{
	gchar* oldstring = *string;
	gchar* prefix = g_strdup_vprintf (format, ap);
	*string = g_strconcat (prefix, oldstring, NULL);
	g_free (oldstring);
	g_free (prefix);
}

void
g_prefix_error (
	GError**	err,
	const gchar*	format,
	...
	)
{
	if (err && *err) {
		va_list ap;
		va_start (ap, format);
		g_error_add_prefix (&(*err)->message, format, ap);
		va_end (ap);
	}
}

void
g_warn_message (
	const char*	domain,
	const char*	file,
	int		line,
	const char*	func,
	const char*	warnexpr
	)
{
	char *s, lstr[32];
	g_snprintf (lstr, 32, "%d", line);
	if (warnexpr)
		s = g_strconcat ("(", file, ":", lstr, "):",
				func, func[0] ? ":" : "",
				" runtime check failed: (", warnexpr, ")", NULL);
	else
		s = g_strconcat ("(", file, ":", lstr, "):",
				func, func[0] ? ":" : "",
				" ", "code should not be reached", NULL);
	g_log (domain, G_LOG_LEVEL_WARNING, "%s", s);
	g_free (s);
}
#endif

/* eof */
