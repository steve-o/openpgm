_Type_
### Declaration ###
<pre>
#include <pgm/pgm.h><br>
<br>
typedef struct {<br>
int    domain;<br>
int    code;<br>
char*  message;<br>
} *pgm_error_t*;<br>
</pre>

### Purpose ###
A object containing information about an error that has occurred.

### Remarks ###
<tt>pgm_error_t</tt> is used to report recoverable runtime errors.

### Example ###
```
 pgm_error_t* err = NULL;
 ...
 printf ("An error has occured, domain:%d code:%d message:%s\n",
         err->domain, err->code, err->message ? err->message : "(null)");
 pgm_error_free (err);
```

### See Also ###
  * <tt><a href='OpenPgm5CReferencePgmErrorFree.md'>pgm_error_free()</a></tt><br>
<ul><li><a href='OpenPgm5CReferenceErrorHandling.md'>Error Handling</a> in OpenPGM C Reference.