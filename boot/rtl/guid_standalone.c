
#ifndef RTL_PAGED_CODE
#define RTL_PAGED_CODE()
#endif
/* hooks guid.c expects; the parse-only test never calls StringFromGUID */

static void *RtlAllocateStringRoutine(unsigned short n);
static void *RtlAllocateStringRoutine(unsigned short n) { (void)n; return 0; }
typedef unsigned char UCHAR; typedef unsigned short USHORT, *PUSHORT;
typedef unsigned long ULONG, *PULONG; typedef long LONG, *PLONG; typedef long NTSTATUS;
typedef unsigned long long ULONGLONG, *PULONGLONG; typedef unsigned int ULONG32;
typedef void *PVOID, *PULONG_PTR; typedef int BOOLEAN;
#define FASTCALL __attribute__((fastcall))
#define NTKERNELAPI
#define NTAPI
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define VOID void
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
typedef unsigned short WCHAR, *PWCHAR; typedef char CHAR, *PCHAR; typedef unsigned short *PWSTR; typedef char *PSTR;
#define NTSYSAPI
#define NTSTATUS_DECL
typedef unsigned long SIZE_T, *PSIZE_T; typedef char *PSZ;
typedef unsigned long *PULONG_PTR_T;
typedef const struct _GUID *REFGUID; typedef struct _GUID { unsigned long Data1; unsigned short Data2, Data3; unsigned char Data4[8]; } GUID;
#define STATUS_INVALID_PARAMETER 0xC000000DL
#define STATUS_BUFFER_TOO_SMALL 0xC0000023L
#define STATUS_BUFFER_OVERFLOW 0x80000005L
#define STATUS_SUCCESS 0
typedef struct _UNICODE_STRING_S { unsigned short Length, MaximumLength; unsigned short *Buffer; } UNICODE_STRING, *PUNICODE_STRING;
#define UNICODE_NULL ((unsigned short)0)
#define STATUS_NO_MEMORY 0xC0000017L
#define STATUS_NAME_TOO_LONG 0xC0000106L
#define UNICODE_STRING_MAX_CHARS 32767
typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v,t) __builtin_va_arg(v,t)
#define va_copy(d,s) __builtin_va_copy(d,s)

static int swprintf(WCHAR *buf, const WCHAR *fmt, ...) { (void)buf; (void)fmt; return 0; }
static const WCHAR GuidFormat[] = {123,37,48,56,108,88,45,37,48,52,88,45,37,48,52,88,45,37,48,50,88,37,48,50,88,45,37,48,50,88,37,48,50,88,37,48,50,88,37,48,50,88,37,48,50,88,37,48,50,88,125,0};
/*++

Copyright (c) 1996  Microsoft Corporation

Module Name:

    guid.c

Abstract:

    This Module implements the guid manipulation functions.

Author:

    George Shaw (GShaw) 9-Oct-1996

Environment:

    Pure Runtime Library Routine

Revision History:

--*/




#if defined(ALLOC_PRAGMA) && defined(NTOS_KERNEL_RUNTIME)
static
int
__cdecl
ScanHexFormat(
    IN const WCHAR* Buffer,
    IN ULONG MaximumLength,
    IN const WCHAR* Format,
    ...);




#endif // ALLOC_PRAGMA && NTOS_KERNEL_RUNTIME

extern const WCHAR GuidFormat[];

#define GUID_STRING_SIZE 38


NTSYSAPI
NTSTATUS
NTAPI
RtlStringFromGUID(
    IN REFGUID Guid,
    OUT PUNICODE_STRING GuidString
    )
/*++

Routine Description:

    Constructs the standard string version of a GUID, in the form:
    "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}".

Arguments:

    Guid -
        Contains the GUID to translate.

    GuidString -
        Returns a string that represents the textual format of the GUID.
        Caller must call RtlFreeUnicodeString to free the buffer when done with
        it.

Return Value:

    NTSTATUS - Returns STATUS_SUCCESS if the user string was succesfully
    initialized.

--*/
{
    RTL_PAGED_CODE();
    GuidString->Length = GUID_STRING_SIZE * sizeof(WCHAR);
    GuidString->MaximumLength = GuidString->Length + sizeof(UNICODE_NULL);
    if (!(GuidString->Buffer = RtlAllocateStringRoutine(GuidString->MaximumLength))) {
        return STATUS_NO_MEMORY;
    }
    swprintf(GuidString->Buffer, GuidFormat, Guid->Data1, Guid->Data2, Guid->Data3, Guid->Data4[0], Guid->Data4[1], Guid->Data4[2], Guid->Data4[3], Guid->Data4[4], Guid->Data4[5], Guid->Data4[6], Guid->Data4[7]);
    return STATUS_SUCCESS;
}


static
int
__cdecl
ScanHexFormat(
    IN const WCHAR* Buffer,
    IN ULONG MaximumLength,
    IN const WCHAR* Format,
    ...)
/*++

Routine Description:

    Scans a source Buffer and places values from that buffer into the parameters
    as specified by Format.

Arguments:

    Buffer -
        Contains the source buffer which is to be scanned.

    MaximumLength -
        Contains the maximum length in characters for which Buffer is searched.
        This implies that Buffer need not be UNICODE_NULL terminated.

    Format -
        Contains the format string which defines both the acceptable string format
        contained in Buffer, and the variable parameters which follow.

Return Value:

    Returns the number of parameters filled if the end of the Buffer is reached,
    else -1 on an error.

--*/
{
    va_list ArgList;
    int     FormatItems;

    va_start(ArgList, Format);
    for (FormatItems = 0;;) {
        switch (*Format) {
        case 0:
            return (MaximumLength && *Buffer) ? -1 : FormatItems;
        case '%':
            Format++;
            if (*Format != '%') {
                ULONG   Number;
                int     Width;
                int     Long;
                PVOID   Pointer;

                for (Long = 0, Width = 0;; Format++) {
                    if ((*Format >= '0') && (*Format <= '9')) {
                        Width = Width * 10 + *Format - '0';
                    } else if (*Format == 'l') {
                        Long++;
                    } else if ((*Format == 'X') || (*Format == 'x')) {
                        break;
                    }
                }
                Format++;
                for (Number = 0; Width--; Buffer++, MaximumLength--) {
                    if (!MaximumLength)
                        return -1;
                    Number *= 16;
                    if ((*Buffer >= '0') && (*Buffer <= '9')) {
                        Number += (*Buffer - '0');
                    } else if ((*Buffer >= 'a') && (*Buffer <= 'f')) {
                        Number += (*Buffer - 'a' + 10);
                    } else if ((*Buffer >= 'A') && (*Buffer <= 'F')) {
                        Number += (*Buffer - 'A' + 10);
                    } else {
                        return -1;
                    }
                }
                Pointer = va_arg(ArgList, PVOID);
                if (Long) {
                    *(PULONG)Pointer = Number;
                } else {
                    *(PUSHORT)Pointer = (USHORT)Number;
                }
                FormatItems++;
                break;
            }
            /* no break */
        default:
            if (!MaximumLength || (*Buffer != *Format)) {
                return -1;
            }
            Buffer++;
            MaximumLength--;
            Format++;
            break;
        }
    }
}


NTSYSAPI
NTSTATUS
NTAPI
RtlGUIDFromString(
    IN PUNICODE_STRING GuidString,
    OUT GUID* Guid
    )
/*++

Routine Description:

    Retrieves a the binary format of a textual GUID presented in the standard
    string version of a GUID: "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}".

Arguments:

    GuidString -
        Place from which to retrieve the textual form of the GUID.

    Guid -
        Place in which to put the binary form of the GUID.

Return Value:

    Returns STATUS_SUCCESS if the buffer contained a valid GUID, else
    STATUS_INVALID_PARAMETER if the string was invalid.

--*/
{
    USHORT    Data4[8];
    int       Count;

    RTL_PAGED_CODE();
    if (ScanHexFormat(GuidString->Buffer, GuidString->Length / sizeof(WCHAR), GuidFormat, &Guid->Data1, &Guid->Data2, &Guid->Data3, &Data4[0], &Data4[1], &Data4[2], &Data4[3], &Data4[4], &Data4[5], &Data4[6], &Data4[7]) == -1) {
        return STATUS_INVALID_PARAMETER;
    }
    for (Count = 0; Count < sizeof(Data4)/sizeof(Data4[0]); Count++) {
        Guid->Data4[Count] = (UCHAR)Data4[Count];
    }
    return STATUS_SUCCESS;
}
