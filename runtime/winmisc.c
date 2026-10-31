/* The rest of Hard Truck 2's Windows imports: string resources, a few GDI, thread and OLE
   functions, and the parts the port does without: network play (WSOCK32: Windows Sockets is
   reported as not ready, as on a PC with no network), printing (WINSPOOL), drag and drop
   (SHELL32) and OLE structured storage. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"

/* ------------------------------------------------------------------ USER32 */
/* LoadStringA: string tables are RT_STRING (6) blocks of 16, block id / 16 + 1, each string a
   UTF-16 count and its characters. English strings: Latin-1 bytes, others '?' */
API(USER32, LoadStringA, 4)
{
    uint32_t id = ARG(1), buf = ARG(2), cap = ARG(3);
    uint32_t data, size;
    if (!cap) return 0;
    W8(buf, 0);
    if (!resource_find(6, id / 16 + 1, &data, &size)) {
        LOG(LOG_WIN, "LoadStringA(%u): no such string", id);
        return 0;
    }
    uint32_t p = data;
    for (uint32_t k = 0; k < (id & 15); k++)
        p += 2 + 2 * R16(p);
    uint32_t n = R16(p), i;
    for (i = 0; i < n && i + 1 < cap; i++) {
        uint32_t ch = R16(p + 2 + 2 * i);
        W8(buf + i, ch < 0x100 ? ch : '?');
    }
    W8(buf + i, 0);
    LOG(LOG_WIN, "LoadStringA(%u) -> \"%s\"", id, gstr(buf));
    return i;
}

int wsprintf_impl(char *out, size_t cap, const char *fmt, uint32_t args);
API(USER32, wvsprintfA, 3)
{
    char buf[1025];
    int n = wsprintf_impl(buf, sizeof buf, gstr(ARG(1)), ARG(2));
    memcpy(gptr(ARG(0)), buf, n + 1);
    return n;
}

/* ------------------------------------------------------------------ GDI32 */
API(GDI32, CreatePalette, 1) { return 0x00016000u; }
API(GDI32, RealizePalette, 1) { return 0; }
API(GDI32, SetDIBitsToDevice, 12) { return ARG(8); }    /* the lines asked for */

/* ------------------------------------------------------------------ KERNEL32 */
API(KERNEL32, GetCurrentThread, 0) { return 0xfffffffeu; }   /* the pseudo handle */
API(KERNEL32, GetThreadPriority, 1) { return 0; }
API(KERNEL32, GetProcessVersion, 1) { return 0x00040000u; }  /* a 4.0 program */
API(KERNEL32, TlsFree, 1) { return 1; }
/* a crash handler's tools: not available */
API(KERNEL32, GetThreadContext, 2) { set_last_error(5); return 0; }
API(KERNEL32, SuspendThread, 1) { set_last_error(5); return 0xffffffffu; }
API(KERNEL32, ResumeThread, 1) { set_last_error(5); return 0xffffffffu; }
API(KERNEL32, ReadProcessMemory, 5)
{
    uint32_t n = ARG(3);
    memmove(gptr(ARG(2)), gptr(ARG(1)), n);
    if (ARG(4)) W32(ARG(4), n);
    return 1;
}

/* ------------------------------------------------------------------ OLE */
API(OLE32, CoTaskMemFree, 1) { if (ARG(0)) gfree(ARG(0)); return 0; }
/* #4 SysAllocStringLen(str, n): the length before the characters, the pointer after it */
API(OLEAUT32, 4, 2)
{
    uint32_t s = ARG(0), n = ARG(1);
    uint32_t b = galloc(4 + 2 * n + 2);
    W32(b, 2 * n);
    if (s) memmove(gptr(b + 4), gptr(s), 2 * n);
    else memset(gptr(b + 4), 0, 2 * n);
    W16(b + 4 + 2 * n, 0);
    return b + 4;
}
API(OLEAUT32, 6, 1) { if (ARG(0)) gfree(ARG(0) - 4); return 0; }   /* #6 SysFreeString */

/* ------------------------------------------------------------------ SHELL32, WINSPOOL */
API(SHELL32, DragQueryFileA, 4) { return 0; }
API(SHELL32, DragFinish, 1) { return 0; }
API(WINSPOOL, OpenPrinterA, 3) { if (ARG(1)) W32(ARG(1), 0); return 0; }
API(WINSPOOL, ClosePrinter, 1) { return 0; }
API(WINSPOOL, DocumentPropertiesA, 6) { return 0xffffffffu; }

/* ------------------------------------------------------------------ WSOCK32 (by ordinal) */
#define WSASYSNOTREADY 10091
#define WSAENETDOWN 10050
API(WSOCK32, 115, 2)                    /* WSAStartup */
{
    LOG(LOG_ALWAYS, "WSAStartup(%x): no network play in this port", ARG(0));
    return WSASYSNOTREADY;
}
API(WSOCK32, 111, 0) { return WSAENETDOWN; }                 /* WSAGetLastError */
API(WSOCK32, 23, 3) { return 0xffffffffu; }                  /* socket: INVALID_SOCKET */
API(WSOCK32, 3, 1) { return 0xffffffffu; }                   /* closesocket */
API(WSOCK32, 4, 3) { return 0xffffffffu; }                   /* connect */
API(WSOCK32, 19, 4) { return 0xffffffffu; }                  /* send */
API(WSOCK32, 9, 1) { uint32_t v = ARG(0) & 0xffff; return ((v & 0xff) << 8) | (v >> 8); }   /* htons */
API(WSOCK32, 10, 1) { return 0xffffffffu; }                  /* inet_addr: INADDR_NONE */
