/* Status codes shared by every Anemos library unit.
 *
 * One vocabulary across the library, so a caller mixing units never has to
 * reconcile two enums that happen to look alike. md5.h and sha1.h used to
 * each define this; the third unit was the point to stop duplicating it.
 */
#ifndef ANEMOS_ANEM_STATUS_H
#define ANEMOS_ANEM_STATUS_H

typedef enum AnemStatus {
    ANEM_OK = 0,
    ANEM_BADARG,     /* NULL pointer, or an argument that breaks an invariant */
    ANEM_OVERFLOW,   /* a size computation exceeds what size_t can hold */
    ANEM_NOSPACE,    /* output buffer too small; nothing was written */
    ANEM_FORMAT,     /* input is not valid for this encoding (malformed) */
} AnemStatus;

#endif /* ANEMOS_ANEM_STATUS_H */
