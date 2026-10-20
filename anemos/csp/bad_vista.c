/* Negative control for the XP import gate: a Vista-only import.
 * The checker must reject this binary; if it passes, the gate is broken. */
#include <windows.h>

int main(void)
{
    SRWLOCK lock;

    InitializeSRWLock(&lock);
    return 0;
}
