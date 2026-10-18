/* kernel/src/nls_subset.c
 *
 * Self-contained Unicode string routines copied VERBATIM from
 * Source/XPSP1/NT/base/ntos/rtl/nls.c (XP SP1).
 *
 * Only functions that do not require the NLS translation tables
 * (NlsUnicodeToAnsiData etc.) are included: case mapping, comparison,
 * copy and append. The ANSI<->Unicode table conversions are excluded.
 */
#include "xpkernel.h"

/* ---- RtlUpcaseUnicodeChar (nls.c:1178-1212) ---- */
WCHAR
RtlUpcaseUnicodeChar(
    IN WCHAR SourceCharacter
    )

/*++

Routine Description:

    This function translates the specified unicode character to its
    equivalent upcased unicode chararacter.  The purpose for this routine
    is to allow for character by character upcase translation.  The
    translation is done with respect to the current system locale
    information.


Arguments:

    SourceCharacter - Supplies the unicode character to be upcased.

Return Value:

    Returns the upcased unicode equivalent of the specified input character.

--*/

{
    RTL_PAGED_CODE();

    //
    // Note that this needs to reference the translation table !
    //

    return (WCHAR)NLS_UPCASE(SourceCharacter);
}

/* ---- RtlDowncaseUnicodeChar (nls.c:1215-1249) ---- */
WCHAR
RtlDowncaseUnicodeChar(
    IN WCHAR SourceCharacter
    )

/*++

Routine Description:

    This function translates the specified unicode character to its
    equivalent downcased unicode chararacter.  The purpose for this routine
    is to allow for character by character downcase translation.  The
    translation is done with respect to the current system locale
    information.


Arguments:

    SourceCharacter - Supplies the unicode character to be downcased.

Return Value:

    Returns the downcased unicode equivalent of the specified input character.

--*/

{
    RTL_PAGED_CODE();

    //
    // Note that this needs to reference the translation table !
    //

    return (WCHAR)NLS_DOWNCASE(SourceCharacter);
}

/* ---- RtlCompareUnicodeString (nls.c:1557-1644) ---- */
LONG
RtlCompareUnicodeString(
    IN PCUNICODE_STRING String1,
    IN PCUNICODE_STRING String2,
    IN BOOLEAN CaseInSensitive
    )

/*++

Routine Description:

    The RtlCompareUnicodeString function compares two counted strings.  The
    return value indicates if the strings are equal or String1 is less than
    String2 or String1 is greater than String2.

    The CaseInSensitive parameter specifies if case is to be ignored when
    doing the comparison.

Arguments:

    String1 - Pointer to the first string.

    String2 - Pointer to the second string.

    CaseInsensitive - TRUE if case should be ignored when doing the
        comparison.

Return Value:

    Signed value that gives the results of the comparison:

        Zero - String1 equals String2

        < Zero - String1 less than String2

        > Zero - String1 greater than String2


--*/

{

    PCWSTR s1, s2, Limit;
    LONG n1, n2;
    WCHAR c1, c2;

    RTL_PAGED_CODE();

    s1 = String1->Buffer;
    s2 = String2->Buffer;
    n1 = String1->Length;
    n2 = String2->Length;

    ASSERT((n1 & 1) == 0);
    ASSERT((n2 & 1) == 0);
    ASSERT(!(((((ULONG_PTR)s1 & 1) != 0) || (((ULONG_PTR)s2 & 1) != 0)) && (n1 != 0) && (n2 != 0)));

    Limit = (PWCHAR)((PCHAR)s1 + (n1 <= n2 ? n1 : n2));
    if (CaseInSensitive) {
        while (s1 < Limit) {
            c1 = *s1++;
            c2 = *s2++;
            if (c1 != c2) {

                //
                // Note that this needs to reference the translation table!
                //

                c1 = NLS_UPCASE(c1);
                c2 = NLS_UPCASE(c2);
                if (c1 != c2) {
                    return (LONG)(c1) - (LONG)(c2);
                }
            }
        }

    } else {
        while (s1 < Limit) {
            c1 = *s1++;
            c2 = *s2++;
            if (c1 != c2) {
                return (LONG)(c1) - (LONG)(c2);
            }
        }
    }

    return n1 - n2;
}

/* ---- RtlEqualUnicodeString (nls.c:1647-1721) ---- */
BOOLEAN
RtlEqualUnicodeString(
    IN PCUNICODE_STRING String1,
    IN PCUNICODE_STRING String2,
    IN BOOLEAN CaseInSensitive
    )

/*++

Routine Description:

    The RtlEqualUnicodeString function compares two counted unicode strings for
    equality.

    The CaseInSensitive parameter specifies if case is to be ignored when
    doing the comparison.

Arguments:

    String1 - Pointer to the first string.

    String2 - Pointer to the second string.

    CaseInsensitive - TRUE if case should be ignored when doing the
        comparison.

Return Value:

    Boolean value that is TRUE if String1 equals String2 and FALSE otherwise.

--*/

{

    PWCHAR s1, s2, Limit;
    LONG n1, n2;
    WCHAR c1, c2;

    RTL_PAGED_CODE();

    n1 = String1->Length;
    n2 = String2->Length;

    if (n1 == n2) {
        s1 = String1->Buffer;
        s2 = String2->Buffer;

        Limit = (PWCHAR)((PCHAR)s1 + (n1&~(sizeof(WCHAR) - 1)));
        if (CaseInSensitive) {
            while (s1 < Limit) {
                c1 = *s1++;
                c2 = *s2++;
                if ((c1 != c2) && (NLS_UPCASE(c1) != NLS_UPCASE(c2))) {
                    return FALSE;
                }
            }

            return TRUE;

        } else {
            while (s1 < Limit) {
                c1 = *s1++;
                c2 = *s2++;
                if (c1 != c2) {
                    return FALSE;
                }
            }

            return TRUE;
        }

    } else {
        return FALSE;
    }
}

/* ---- RtlHashUnicodeString (nls.c:2708-2763) ---- */
NTSTATUS
RtlHashUnicodeString(
    const UNICODE_STRING *String,
    BOOLEAN CaseInSensitive,
    ULONG HashAlgorithm,
    PULONG HashValue
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG TmpHashValue = 0;
    ULONG Chars;
    PCWSTR Buffer;

    if ((String == NULL) ||
        (HashValue == NULL))
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    Buffer = String->Buffer;

    *HashValue = 0;
    Chars = String->Length / sizeof(WCHAR);

    switch (HashAlgorithm)
    {
    default:
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
        break;

    case HASH_STRING_ALGORITHM_DEFAULT:
    case HASH_STRING_ALGORITHM_X65599:
        if (CaseInSensitive)
        {
            while (Chars-- != 0)
            {
                WCHAR Char = *Buffer++;
                TmpHashValue = (TmpHashValue * 65599) + NLS_UPCASE(Char);
            }
        }
        else
        {
            while (Chars-- != 0)
                TmpHashValue = (TmpHashValue * 65599) + *Buffer++;
        }

        break;
    }

    *HashValue = TmpHashValue;
    Status = STATUS_SUCCESS;
Exit:
    return Status;
}

/* ---- RtlCopyUnicodeString (nls.c:1799-1854) ---- */
VOID
RtlCopyUnicodeString(
    OUT PUNICODE_STRING DestinationString,
    IN PCUNICODE_STRING SourceString OPTIONAL
    )

/*++

Routine Description:

    The RtlCopyString function copies the SourceString to the
    DestinationString.  If SourceString is not specified, then
    the Length field of DestinationString is set to zero.  The
    MaximumLength and Buffer fields of DestinationString are not
    modified by this function.

    The number of bytes copied from the SourceString is either the
    Length of SourceString or the MaximumLength of DestinationString,
    whichever is smaller.

Arguments:

    DestinationString - Pointer to the destination string.

    SourceString - Optional pointer to the source string.

Return Value:

    None.

--*/

{
    UNALIGNED WCHAR *src, *dst;
    ULONG n;

    if (ARGUMENT_PRESENT(SourceString)) {
        dst = DestinationString->Buffer;
        src = SourceString->Buffer;
        n = SourceString->Length;
        if ((USHORT)n > DestinationString->MaximumLength) {
            n = DestinationString->MaximumLength;
        }

        DestinationString->Length = (USHORT)n;
        RtlCopyMemory(dst, src, n);
        if (DestinationString->Length < DestinationString->MaximumLength) {
            dst[n / sizeof(WCHAR)] = UNICODE_NULL;
        }

    } else {
        DestinationString->Length = 0;
    }

    return;
}

/* ---- RtlAppendUnicodeToString (nls.c:1857-1916) ---- */
NTSTATUS
RtlAppendUnicodeToString (
    IN PUNICODE_STRING Destination,
    IN PCWSTR Source OPTIONAL
    )

/*++

Routine Description:

    This routine appends the supplied UNICODE string to an existing
    PUNICODE_STRING.

    It will copy bytes from the Source PSZ to the destination PSTRING up to
    the destinations PUNICODE_STRING->MaximumLength field.

Arguments:

    IN PUNICODE_STRING Destination, - Supplies a pointer to the destination
                            string
    IN PWSTR Source - Supplies the string to append to the destination

Return Value:

    STATUS_SUCCESS - The source string was successfully appended to the
        destination counted string.

    STATUS_BUFFER_TOO_SMALL - The destination string length was not big
        enough to allow the source string to be appended.  The Destination
        string length is not updated.

--*/

{
    USHORT n;
    UNALIGNED WCHAR *dst;

    if (ARGUMENT_PRESENT( Source )) {
        UNICODE_STRING UniSource;

        RtlInitUnicodeString(&UniSource, Source);

        n = UniSource.Length;

        if ((n + Destination->Length) > Destination->MaximumLength) {
            return( STATUS_BUFFER_TOO_SMALL );
            }

        dst = &Destination->Buffer[ (Destination->Length / sizeof( WCHAR )) ];
        RtlMoveMemory( dst, Source, n );

        Destination->Length += n;

        if (Destination->Length < Destination->MaximumLength) {
            dst[ n / sizeof( WCHAR ) ] = UNICODE_NULL;
            }
        }

    return( STATUS_SUCCESS );
}

/* ---- RtlAppendUnicodeStringToString (nls.c:1919-1968) ---- */
NTSTATUS
RtlAppendUnicodeStringToString (
    IN OUT PUNICODE_STRING Destination,
    IN PCUNICODE_STRING Source
    )

/*++

Routine Description:

    This routine will concatinate two PSTRINGs together.  It will copy
    bytes from the source up to the MaximumLength of the destination.

Arguments:

    IN PSTRING Destination, - Supplies the destination string
    IN PSTRING Source - Supplies the source for the string copy

Return Value:

    STATUS_SUCCESS - The source string was successfully appended to the
        destination counted string.

    STATUS_BUFFER_TOO_SMALL - The destination string length was not big
        enough to allow the source string to be appended.  The Destination
        string length is not updated.

--*/

{
    USHORT n = Source->Length;
    UNALIGNED WCHAR *dst;

    if (n) {
        if ((n + Destination->Length) > Destination->MaximumLength) {
            return( STATUS_BUFFER_TOO_SMALL );
            }

        dst = &Destination->Buffer[ (Destination->Length / sizeof( WCHAR )) ];
        RtlMoveMemory( dst, Source->Buffer, n );

        Destination->Length += n;

        if (Destination->Length < Destination->MaximumLength) {
            dst[ n / sizeof( WCHAR ) ] = UNICODE_NULL;
            }
        }

    return( STATUS_SUCCESS );
}
