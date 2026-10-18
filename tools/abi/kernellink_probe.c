/* kernellink_probe.c — G1 ABI proof, check D: a clang object against a real
 * kernel library.
 *
 * The point is link-time name agreement, not execution: a replacement in the
 * tree must be able to reference XP's own RTL and be found there. If clang
 * decorated the reference differently from the way the tree's MSVC decorated
 * the definition (ntosrtl.lib defines `_RtlInitString@8`), the link fails with
 * an unresolved external — which is exactly the signal we want. The link uses
 * /nodefaultlib, so nothing but that library can satisfy the reference.
 *
 * Declared here rather than from the leak's headers: the decoration is the
 * test subject, so both sides of the link must state it independently. The
 * real signatures take ANSI_STRING pointers; two pointers are two pointers,
 * which is all this check needs.
 */
void __stdcall RtlInitString(void *destination, const char *source);
void __stdcall RtlInitAnsiString(void *destination, const char *source);

static const char abi_leaf_source[] = "anemos";

__declspec(dllexport) unsigned long __cdecl AbiKernelProbe(void)
{
    volatile unsigned long touched;

    RtlInitString((void *)&touched, abi_leaf_source);
    RtlInitAnsiString((void *)&touched, abi_leaf_source);
    return touched;
}
