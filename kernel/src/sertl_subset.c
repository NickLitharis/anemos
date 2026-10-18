/* kernel/src/sertl_subset.c
 *
 * Self-contained SID / access-mask routines copied VERBATIM from
 * Source/XPSP1/NT/base/ntos/rtl/sertl.c (XP SP1). Pure data manipulation:
 * no heap, no syscalls.
 */
#include "xpkernel.h"

/* ---- RtlInitializeSid (sertl.c:1191-1239) ---- */
NTSTATUS
RtlInitializeSid(
    IN PSID Sid,
    IN PSID_IDENTIFIER_AUTHORITY IdentifierAuthority,
    IN UCHAR SubAuthorityCount
    )
/*++

Routine Description:

    This function initializes an SID data structure.  It does not, however,
    set the sub-authority values.  This must be done separately.

Arguments:

    Sid - Pointer to the SID data structure to initialize.

    IdentifierAuthority - Pointer to the Identifier Authority value to
        set in the SID.

    SubAuthorityCount - The number of sub-authorities that will be placed in
        the SID (a separate action).

Return Value:


--*/
{
    PISID ISid;

    RTL_PAGED_CODE();

    //
    //  Typecast to the opaque SID
    //

    ISid = (PISID)Sid;

    if ( SubAuthorityCount > SID_MAX_SUB_AUTHORITIES ) {
        return( STATUS_INVALID_PARAMETER );
    }

    ISid->SubAuthorityCount = (UCHAR)SubAuthorityCount;
    ISid->Revision = 1;
    ISid->IdentifierAuthority = *IdentifierAuthority;

    return( STATUS_SUCCESS );

}

/* ---- RtlLengthSid (sertl.c:1375-1402) ---- */
ULONG
RtlLengthSid (
    IN PSID Sid
    )

/*++

Routine Description:

    This routine returns the length, in bytes, of a structurally valid SID.

Arguments:

    Sid - Points to the SID whose length is to be returned.  The
        SID's structure is assumed to be valid.

Return Value:

    ULONG - The length, in bytes, of the SID.


--*/

{
    RTL_PAGED_CODE();

    return SeLengthSid(Sid);
}

/* ---- RtlCopySid (sertl.c:1405-1458) ---- */
NTSTATUS
RtlCopySid (
    IN ULONG DestinationSidLength,
    OUT PSID DestinationSid,
    IN PSID SourceSid
    )

/*++

Routine Description:

    This routine copies the value of the source SID to the destination
    SID.

Arguments:

    DestinationSidLength - Indicates the length, in bytes, of the
        destination SID buffer.

    DestinationSid - Pointer to a buffer to receive a copy of the
        source Sid value.

    SourceSid - Supplies the Sid value to be copied.

Return Value:

    STATUS_SUCCESS - Indicates the SID was successfully copied.

    STATUS_BUFFER_TOO_SMALL - Indicates the target buffer wasn't
        large enough to receive a copy of the SID.


--*/

{
    ULONG SidLength = SeLengthSid(SourceSid);

    RTL_PAGED_CODE();

    if (SidLength > DestinationSidLength) {

        return STATUS_BUFFER_TOO_SMALL;

    }

    //
    // Buffer is large enough
    //

    RtlMoveMemory( DestinationSid, SourceSid, SidLength );

    return STATUS_SUCCESS;

}

/* ---- RtlEqualSid (sertl.c:896-945) ---- */
BOOLEAN
RtlEqualSid (
    IN PSID Sid1,
    IN PSID Sid2
    )

/*++

Routine Description:

    This procedure tests two SID values for equality.

Arguments:

    Sid1, Sid2 - Supply pointers to the two SID values to compare.
        The SID structures are assumed to be valid.

Return Value:

    BOOLEAN - TRUE if the value of Sid1 is equal to Sid2, and FALSE
        otherwise.

--*/

{
   ULONG SidLength;

   RTL_PAGED_CODE();

   //
   // Make sure they are the same revision
   //

   if ( ((SID *)Sid1)->Revision == ((SID *)Sid2)->Revision ) {

       //
       // Check the SubAuthorityCount first, because it's fast and
       // can help us exit faster.
       //

       if ( *RtlSubAuthorityCountSid( Sid1 ) == *RtlSubAuthorityCountSid( Sid2 )) {

           SidLength = SeLengthSid( Sid1 );
           return( (BOOLEAN)RtlEqualMemory( Sid1, Sid2, SidLength) );
       }
   }

   return( FALSE );

}

/* ---- RtlEqualPrefixSid (sertl.c:949-1051) ---- */
BOOLEAN
RtlEqualPrefixSid (
    IN PSID Sid1,
    IN PSID Sid2
    )

/*++

Routine Description:

    This procedure tests two SID prefix values for equality.

    An SID prefix is the entire SID except for the last sub-authority
    value.

Arguments:

    Sid1, Sid2 - Supply pointers to the two SID values to compare.
        The SID structures are assumed to be valid.

Return Value:

    BOOLEAN - TRUE if the prefix value of Sid1 is equal to Sid2, and FALSE
        otherwise.

--*/


{
    LONG Index;

    //
    // Typecast to the opaque SID structures.
    //

    SID *ISid1 = Sid1;
    SID *ISid2 = Sid2;

    RTL_PAGED_CODE();

    //
    // Make sure they are the same revision
    //

    if (ISid1->Revision == ISid2->Revision ) {

        //
        // Compare IdentifierAuthority values
        //

        if ( (ISid1->IdentifierAuthority.Value[0] ==
              ISid2->IdentifierAuthority.Value[0])  &&
             (ISid1->IdentifierAuthority.Value[1]==
              ISid2->IdentifierAuthority.Value[1])  &&
             (ISid1->IdentifierAuthority.Value[2] ==
              ISid2->IdentifierAuthority.Value[2])  &&
             (ISid1->IdentifierAuthority.Value[3] ==
              ISid2->IdentifierAuthority.Value[3])  &&
             (ISid1->IdentifierAuthority.Value[4] ==
              ISid2->IdentifierAuthority.Value[4])  &&
             (ISid1->IdentifierAuthority.Value[5] ==
              ISid2->IdentifierAuthority.Value[5])
            ) {

            //
            // Compare SubAuthorityCount values
            //

            if (ISid1->SubAuthorityCount == ISid2->SubAuthorityCount) {

                if (ISid1->SubAuthorityCount == 0) {
                    return TRUE;
                }

                Index = 0;
                while (Index < (ISid1->SubAuthorityCount - 1)) {
                    if ((ISid1->SubAuthority[Index]) != (ISid2->SubAuthority[Index])) {

                        //
                        // Found some SubAuthority values that weren't equal.
                        //

                        return FALSE;
                    }
                    Index += 1;
                }

                //
                // All SubAuthority values are equal.
                //

                return TRUE;
            }
        }
    }

    //
    // Either the Revision, SubAuthorityCount, or IdentifierAuthority values
    // weren't equal.
    //

    return FALSE;
}

/* ---- RtlSubAuthoritySid (sertl.c:1311-1339) ---- */
PULONG
RtlSubAuthoritySid(
    IN PSID Sid,
    IN ULONG SubAuthority
    )
/*++

Routine Description:

    This function returns the address of a sub-authority array element of
    an SID.

Arguments:

    Sid - Pointer to the SID data structure.

    SubAuthority - An index indicating which sub-authority is being specified.
        This value is not compared against the number of sub-authorities in the
        SID for validity.

Return Value:


--*/
{
    RTL_PAGED_CODE();

    return RtlpSubAuthoritySid( Sid, SubAuthority );
}

/* ---- RtlSubAuthorityCountSid (sertl.c:1341-1373) ---- */
PUCHAR
RtlSubAuthorityCountSid(
    IN PSID Sid
    )
/*++

Routine Description:

    This function returns the address of the sub-authority count field of
    an SID.

Arguments:

    Sid - Pointer to the SID data structure.

Return Value:


--*/
{
    PISID ISid;

    RTL_PAGED_CODE();

    //
    //  Typecast to the opaque SID
    //

    ISid = (PISID)Sid;

    return &(ISid->SubAuthorityCount);

}

/* ---- RtlIdentifierAuthoritySid (sertl.c:1278-1309) ---- */
PSID_IDENTIFIER_AUTHORITY
RtlIdentifierAuthoritySid(
    IN PSID Sid
    )
/*++

Routine Description:

    This function returns the address of an SID's IdentifierAuthority field.

Arguments:

    Sid - Pointer to the SID data structure.

Return Value:


--*/
{
    PISID ISid;

    RTL_PAGED_CODE();

    //
    //  Typecast to the opaque SID
    //

    ISid = (PISID)Sid;

    return &(ISid->IdentifierAuthority);

}

/* ---- RtlValidSid (sertl.c:835-892) ---- */
BOOLEAN
RtlValidSid (
    IN PSID Sid
    )

/*++

Routine Description:

    This procedure validates an SID's structure.

Arguments:

    Sid - Pointer to the SID structure to validate.

Return Value:

    BOOLEAN - TRUE if the structure of Sid is valid.

--*/

{
    PISID Isid = (PISID) Sid;
    RTL_PAGED_CODE();
    //
    // Make sure revision is SID_REVISION and sub authority count is not
    // greater than maximum number of allowed sub-authorities.
    //

    try {

        if ( Isid != NULL && (Isid->Revision & 0x0f) == SID_REVISION) {
            if (Isid->SubAuthorityCount <= SID_MAX_SUB_AUTHORITIES) {

                //
                // Verify the memory actually contains the last subauthority
                //
#ifndef NTOS_KERNEL_RUNTIME
#define ProbeAndReadUlongUM(Address) \
        (*(volatile ULONG *)(Address))

                if (Isid->SubAuthorityCount > 0) {
                    ProbeAndReadUlongUM(
                        &Isid->SubAuthority[Isid->SubAuthorityCount-1]
                        );
                }
#endif // !NTOS_KERNEL_RUNTIME
                return TRUE;
          }
        }

    } except(EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }

    return FALSE;

}

/* ---- RtlLengthRequiredSid (sertl.c:1055-1083) ---- */
ULONG
RtlLengthRequiredSid (
    IN ULONG SubAuthorityCount
    )

/*++

Routine Description:

    This routine returns the length, in bytes, required to store an SID
    with the specified number of Sub-Authorities.

Arguments:

    SubAuthorityCount - The number of sub-authorities to be stored in the SID.

Return Value:

    ULONG - The length, in bytes, required to store the SID.


--*/

{
    RTL_PAGED_CODE();

    return (8L + (4 * SubAuthorityCount));

}

/* ---- RtlAreAllAccessesGranted (sertl.c:3213-3245) ---- */
BOOLEAN
RtlAreAllAccessesGranted(
    IN ACCESS_MASK GrantedAccess,
    IN ACCESS_MASK DesiredAccess
    )

/*++

Routine Description:

    This routine is used to check a desired access mask against a
    granted access mask.  It is used by the Object Management
    component when dereferencing a handle.

Arguments:

        GrantedAccess - Specifies the granted access mask.

        DesiredAccess - Specifies the desired access mask.

Return Value:

    BOOLEAN - TRUE if the GrantedAccess mask has all the bits set
        that the DesiredAccess mask has set.  That is, TRUE is
        returned if all of the desired accesses have been granted.

--*/

{
    RTL_PAGED_CODE();

    return ((BOOLEAN)((~(GrantedAccess) & (DesiredAccess)) == 0));
}

/* ---- RtlAreAnyAccessesGranted (sertl.c:3248-3282) ---- */
BOOLEAN
RtlAreAnyAccessesGranted(
    IN ACCESS_MASK GrantedAccess,
    IN ACCESS_MASK DesiredAccess
    )

/*++

Routine Description:

    This routine is used to test whether any of a set of desired
    accesses are granted by a granted access mask.  It is used by
    components other than the the Object Management component for
    checking access mask subsets.

Arguments:

        GrantedAccess - Specifies the granted access mask.

        DesiredAccess - Specifies the desired access mask.

Return Value:

    BOOLEAN - TRUE if the GrantedAccess mask contains any of the bits
        specified in the DesiredAccess mask.  That is, if any of the
        desired accesses have been granted, TRUE is returned.


--*/

{
    RTL_PAGED_CODE();

    return ((BOOLEAN)(((GrantedAccess) & (DesiredAccess)) != 0));
}
