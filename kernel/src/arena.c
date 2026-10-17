/* backing store for the freestanding RTL heap shim */
unsigned char XpHeapArena[1024 * 1024];
unsigned long XpHeapUsed = 0;
unsigned long XpHeapSize = sizeof(XpHeapArena);
