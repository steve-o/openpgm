### Introduction ###
Warnings and errors in OpenPGM maybe raised via log messages or return status values.

### Logging ###
OpenPGM modules log messages through [GLog](http://library.gnome.org/devel/glib/stable/glib-Message-Logging.html), providing versatile support for logging messages with different levels of importance.   The following log domains have been defined for the components of OpenPGM.

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Log Domain</th>
<th>Description</th>
</tr>
<tr>
<td><tt>Pgm</tt></td>
<td>PGM transport.</td>
</tr><tr>
<td><tt>Pgm-Http</tt></td>
<td>HTTP/HTTPS administration and monitoring interface.</td>
</tr><tr>
<td><tt>Pgm-Snmp</tt></td>
<td>SNMP administration and monitoring interface.</td>
</tr>
</table>

Log messages can be redirected using [g\_log\_set\_handler](http://library.gnome.org/devel/glib/stable/glib-Message-Logging.html#g-log-set-handler), one call is required for each domain

### Example ###
```
 static void
 log_handler (
        const gchar`*`      log_domain,
        GLogLevelFlags    log_level,
        const gchar`*`      message,
        gpointer          unused_data
        )
 {
     if (log_domain)
         printf ("%s: %s\n", log_domain, message);
     else
         puts (message);
 }
 
 ...
 
 g_log_set_handler ("Pgm",      G_LOG_LEVEL_MASK, log_handler, NULL);
 g_log_set_handler ("Pgm-Http", G_LOG_LEVEL_MASK, log_handler, NULL);
 g_log_set_handler ("Pgm-Snmp", G_LOG_LEVEL_MASK, log_handler, NULL);
```