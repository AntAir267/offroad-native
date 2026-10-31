/* DirectPlay: Hard Truck 2 creates an IDirectPlay4A at start-up (CoCreateInstance, D1gate.cpp) and
   stops with a fatal error if it cannot [verified: run log 2026-10-26, warn.log "Can not create
   DirectPlay4 object", then the game's fatal handler 0x527500]. The port has no network play:
   this object exists, offers no connections (EnumConnections calls back for none), finds no
   sessions and refuses to open or create any, as DirectPlay does on a PC with no service
   providers. */
#include <pthread.h>
#include <stdlib.h>
#include "runtime.h"

#define DP_OK 0u
#define E_NOINTERFACE 0x80004002u
#define DPERR_UNSUPPORTED 0x80004001u      /* E_NOTIMPL */
#define DPERR_UNAVAILABLE (0x88770000u + 210u)
#define DPERR_NOMESSAGES (0x88770000u + 190u)
#define DPERR_NOCONNECTION (0x88770000u + 170u)
#define DPERR_INVALIDPLAYER (0x88770000u + 150u)

/* {d1eb6d20-8923-11d0-9d97-00a0c90a43cb} CLSID_DirectPlay, interfaces it hands out:
   IDirectPlay4A {0ab1c531-4745-11d1-a7a1-0000f803abfc}, IDirectPlay4 {0ab1c530-...},
   IDirectPlay3A {2b74f7c0-9154-11cf-a9cd-00aa006886e3}, IDirectPlay2A {9d460b20-...} */
static const uint8_t clsid_dplay[16] = {0x20, 0x6d, 0xeb, 0xd1, 0x23, 0x89, 0xd0, 0x11,
                                        0x9d, 0x97, 0x00, 0xa0, 0xc9, 0x0a, 0x43, 0xcb};
static const uint8_t iid_dp4a[16] = {0x31, 0xc5, 0xb1, 0x0a, 0x45, 0x47, 0xd1, 0x11,
                                     0xa7, 0xa1, 0x00, 0x00, 0xf8, 0x03, 0xab, 0xfc};
static const uint8_t iid_dp4[16] = {0x30, 0xc5, 0xb1, 0x0a, 0x45, 0x47, 0xd1, 0x11,
                                    0xa7, 0xa1, 0x00, 0x00, 0xf8, 0x03, 0xab, 0xfc};
static const uint8_t iid_unknown[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46};

typedef struct { int refs; } DPlay;
static pthread_mutex_t dp_lock = PTHREAD_MUTEX_INITIALIZER;
static ComClass cls_dp;

static int guid_is(uint32_t a, const uint8_t g[16]) { return a && !memcmp(gptr(a), g, 16); }

METHOD(dp_QueryInterface, 3)
{
    uint32_t ppv = ARG(2);
    int ok = guid_is(ARG(1), iid_dp4a) || guid_is(ARG(1), iid_dp4) || guid_is(ARG(1), iid_unknown);
    if (ok) {
        pthread_mutex_lock(&dp_lock);
        ((DPlay *)com_host(ARG(0)))->refs++;
        pthread_mutex_unlock(&dp_lock);
    }
    if (ppv) W32(ppv, ok ? ARG(0) : 0);
    LOG(LOG_API, "IDirectPlay4A::QueryInterface -> %s", ok ? "same object" : "no interface");
    return ok ? DP_OK : E_NOINTERFACE;
}

METHOD(dp_AddRef, 1)
{
    pthread_mutex_lock(&dp_lock);
    int n = ++((DPlay *)com_host(ARG(0)))->refs;
    pthread_mutex_unlock(&dp_lock);
    return n;
}

METHOD(dp_Release, 1)
{
    pthread_mutex_lock(&dp_lock);
    int n = --((DPlay *)com_host(ARG(0)))->refs;
    pthread_mutex_unlock(&dp_lock);
    LOG(LOG_API, "IDirectPlay4A::Release -> %d", n);
    return n < 0 ? 0 : n;
}

/* GetCaps(this, DPCAPS *, flags): a DPCAPS of the size the program says, all zero but the size */
METHOD(dp_GetCaps, 3)
{
    uint32_t caps = ARG(1);
    if (!caps) return 0x80070057u;                       /* DPERR_INVALIDPARAMS */
    uint32_t n = R32(caps);
    if (n >= 4 && n <= 256) { memset(gptr(caps + 4), 0, n - 4); }
    return DP_OK;
}

/* The ones that report the empty state */
#define FIXED(name, nargs, hr, why) \
    METHOD(dp_##name, nargs) { LOG(LOG_API, "IDirectPlay4A::" #name " -> %08x (" why ")", hr); return hr; }
FIXED(Close, 1, DP_OK, "nothing open")
FIXED(EnumConnections, 5, DP_OK, "no service providers")
FIXED(EnumSessions, 6, DP_OK, "no sessions")
FIXED(EnumPlayers, 5, DP_OK, "no players")
FIXED(EnumGroups, 5, DP_OK, "no groups")
FIXED(EnumGroupPlayers, 6, DP_OK, "no players")
FIXED(EnumGroupsInGroup, 6, DP_OK, "no groups")
FIXED(Receive, 6, DPERR_NOMESSAGES, "no messages")
FIXED(Initialize, 2, DP_OK, "")
FIXED(InitializeConnection, 3, DPERR_UNAVAILABLE, "no network play")
FIXED(Open, 3, DPERR_NOCONNECTION, "no network play")
FIXED(SecureOpen, 5, DPERR_NOCONNECTION, "no network play")
FIXED(CreatePlayer, 7, DPERR_NOCONNECTION, "no network play")
FIXED(CreateGroup, 6, DPERR_NOCONNECTION, "no network play")
FIXED(CreateGroupInGroup, 7, DPERR_NOCONNECTION, "no network play")
FIXED(GetMessageCount, 3, DPERR_INVALIDPLAYER, "no players")
FIXED(Send, 6, DPERR_INVALIDPLAYER, "no players")
FIXED(SendEx, 10, DPERR_INVALIDPLAYER, "no players")
FIXED(SendChatMessage, 5, DPERR_INVALIDPLAYER, "no players")
FIXED(GetMessageQueue, 6, DPERR_INVALIDPLAYER, "no players")
FIXED(AddPlayerToGroup, 3, DPERR_UNSUPPORTED, "")
FIXED(DeletePlayerFromGroup, 3, DPERR_UNSUPPORTED, "")
FIXED(DestroyGroup, 2, DPERR_UNSUPPORTED, "")
FIXED(DestroyPlayer, 2, DPERR_UNSUPPORTED, "")
FIXED(GetGroupData, 5, DPERR_UNSUPPORTED, "")
FIXED(GetGroupName, 4, DPERR_UNSUPPORTED, "")
FIXED(GetPlayerAddress, 4, DPERR_UNSUPPORTED, "")
FIXED(GetPlayerCaps, 4, DPERR_UNSUPPORTED, "")
FIXED(GetPlayerData, 5, DPERR_UNSUPPORTED, "")
FIXED(GetPlayerName, 4, DPERR_UNSUPPORTED, "")
FIXED(GetSessionDesc, 3, DPERR_NOCONNECTION, "no session")
FIXED(SetGroupData, 5, DPERR_UNSUPPORTED, "")
FIXED(SetGroupName, 4, DPERR_UNSUPPORTED, "")
FIXED(SetPlayerData, 5, DPERR_UNSUPPORTED, "")
FIXED(SetPlayerName, 4, DPERR_UNSUPPORTED, "")
FIXED(SetSessionDesc, 3, DPERR_NOCONNECTION, "no session")
FIXED(AddGroupToGroup, 3, DPERR_UNSUPPORTED, "")
FIXED(DeleteGroupFromGroup, 3, DPERR_UNSUPPORTED, "")
FIXED(GetGroupConnectionSettings, 5, DPERR_UNSUPPORTED, "")
FIXED(SetGroupConnectionSettings, 4, DPERR_UNSUPPORTED, "")
FIXED(StartSession, 3, DPERR_UNSUPPORTED, "")
FIXED(GetGroupFlags, 3, DPERR_UNSUPPORTED, "")
FIXED(GetGroupParent, 3, DPERR_UNSUPPORTED, "")
FIXED(GetPlayerAccount, 5, DPERR_UNSUPPORTED, "")
FIXED(GetPlayerFlags, 3, DPERR_UNSUPPORTED, "")
FIXED(GetGroupOwner, 3, DPERR_UNSUPPORTED, "")
FIXED(SetGroupOwner, 3, DPERR_UNSUPPORTED, "")
FIXED(CancelMessage, 3, DPERR_UNSUPPORTED, "")
FIXED(CancelPriority, 4, DPERR_UNSUPPORTED, "")

/* IDirectPlay4A's methods in vtable order: IUnknown, IDirectPlay2, IDirectPlay3, IDirectPlay4 */
#define DP_VTBL(X) \
    X(QueryInterface) X(AddRef) X(Release) \
    X(AddPlayerToGroup) X(Close) X(CreateGroup) X(CreatePlayer) X(DeletePlayerFromGroup) \
    X(DestroyGroup) X(DestroyPlayer) X(EnumGroupPlayers) X(EnumGroups) X(EnumPlayers) \
    X(EnumSessions) X(GetCaps) X(GetGroupData) X(GetGroupName) X(GetMessageCount) \
    X(GetPlayerAddress) X(GetPlayerCaps) X(GetPlayerData) X(GetPlayerName) X(GetSessionDesc) \
    X(Initialize) X(Open) X(Receive) X(Send) X(SetGroupData) X(SetGroupName) X(SetPlayerData) \
    X(SetPlayerName) X(SetSessionDesc) \
    X(AddGroupToGroup) X(CreateGroupInGroup) X(DeleteGroupFromGroup) X(EnumConnections) \
    X(EnumGroupsInGroup) X(GetGroupConnectionSettings) X(InitializeConnection) X(SecureOpen) \
    X(SendChatMessage) X(SetGroupConnectionSettings) X(StartSession) X(GetGroupFlags) \
    X(GetGroupParent) X(GetPlayerAccount) X(GetPlayerFlags) \
    X(GetGroupOwner) X(SetGroupOwner) X(SendEx) X(GetMessageQueue) X(CancelMessage) \
    X(CancelPriority)
#define DP_FN(n) dp_##n,
#define DP_NAME(n) #n,

/* CoCreateInstance's part: REGDB_E_CLASSNOTREG unless the class is DirectPlay's */
uint32_t dplay_cocreate(uint32_t clsid, uint32_t riid, uint32_t ppv)
{
    if (!guid_is(clsid, clsid_dplay))
        return 0x80040154u;
    if (!cls_dp.vtbl) {
        static void (*const fns[])(CPU *) = { DP_VTBL(DP_FN) };
        static const char *const names[] = { DP_VTBL(DP_NAME) };
        _Static_assert(sizeof fns / sizeof *fns == 53, "IDirectPlay4A has 53 methods");
        com_class_init(&cls_dp, "IDirectPlay4A", 53, fns, names);
    }
    if (ppv) W32(ppv, 0);
    if (!guid_is(riid, iid_dp4a) && !guid_is(riid, iid_dp4) && !guid_is(riid, iid_unknown)) {
        LOG(LOG_ALWAYS, "CoCreateInstance(CLSID_DirectPlay): only IDirectPlay4(A) is provided");
        return E_NOINTERFACE;
    }
    DPlay *d = calloc(1, sizeof *d);
    d->refs = 1;
    uint32_t o = com_new(&cls_dp, d);
    if (ppv) W32(ppv, o);
    LOG(LOG_ALWAYS, "CoCreateInstance(CLSID_DirectPlay) -> %08x (no network play: no connections)", o);
    return DP_OK;
}
