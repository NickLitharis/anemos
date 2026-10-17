
/* compiler runtime division helpers, linked into libxprtl.a */
unsigned long long __udivdi3(unsigned long long a, unsigned long long b) {
    unsigned long long q = 0, r = 0; int i;
    if (!b) return 0;
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1ULL);
        if (r >= b) { r -= b; q |= (1ULL << i); }
    }
    return q;
}
unsigned long long __umoddi3(unsigned long long a, unsigned long long b) {
    unsigned long long r = 0; int i;
    if (!b) return 0;
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1ULL);
        if (r >= b) r -= b;
    }
    return r;
}
long long __divdi3(long long a, long long b) {
    int neg = 0; unsigned long long ua, ub, q;
    if (a < 0) { ua = (unsigned long long)(-a); neg ^= 1; } else ua = (unsigned long long)a;
    if (b < 0) { ub = (unsigned long long)(-b); neg ^= 1; } else ub = (unsigned long long)b;
    q = __udivdi3(ua, ub);
    return neg ? -(long long)q : (long long)q;
}
long long __moddi3(long long a, long long b) {
    int neg = (a < 0); unsigned long long ua, ub, r;
    ua = (a < 0) ? (unsigned long long)(-a) : (unsigned long long)a;
    ub = (b < 0) ? (unsigned long long)(-b) : (unsigned long long)b;
    r = __umoddi3(ua, ub);
    return neg ? -(long long)r : (long long)r;
}
