// Picks the network driver from the command line and connects it to d_net.c.
// See net_linux.h.
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "net_linux.h"

static doomcom_t dc;
static enum { NONE, IPX, SERIAL } driver;
static bool running;

int net_arg(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; i++) if (!strcasecmp(argv[i], name)) return i;
    return 0;
}

const char* net_arg_value(int argc, char** argv, const char* name) {
    int i = net_arg(argc, argv, name);
    return i && i + 1 < argc ? argv[i + 1] : nullptr;
}

// Ctrl+C while looking for the other players: give up cleanly (the
// sockets closed, the serial line's settings put back), as Esc does in
// IPXSETUP / SERSETUP. Once the game runs, Ctrl+C is the game's.
static volatile sig_atomic_t aborted;
static void on_int(int) { aborted = 1; }
bool net_aborted() { return aborted != 0; }

static doomcom_t* net_init() { return driver == NONE ? nullptr : &dc; }

static void net_cmd() {
    if (driver == IPX) ipx_cmd(&dc);
    else if (driver == SERIAL) ser_cmd(&dc);
}

bool net_setup(int argc, char** argv) {
    bool ipx = ipx_wanted(argc, argv), ser = ser_wanted(argc, argv);
    if (!ipx && !ser) return true;
    if (ipx && ser) { fprintf(stderr, "Network: IPX or serial, not both\n"); return false; }

    memset(&dc, 0, sizeof dc);
    dc.id = DOOMCOM_ID;
    dc.intnum = 0x66;                   // no interrupt here; what IPXSETUP would print
    dc.ticdup = 1;
    dc.episode = 1;
    dc.map = 1;
    dc.skill = 2;
    dc.savegame = -1;

    struct sigaction sa, old;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_int;             // no SA_RESTART: a wait comes back at once
    sigaction(SIGINT, &sa, &old);
    bool ok = ipx ? ipx_setup(argc, argv, &dc) : ser_setup(argc, argv, &dc);
    sigaction(SIGINT, &old, nullptr);
    if (!ok) {
        if (aborted) printf("\nNetwork game synchronization aborted.\n");
        if (ipx) ipx_shutdown(); else ser_shutdown();
        return false;
    }

    // As the drivers' own -dup / -extratic would (the Linux i_net.c read them).
    if (const char* v = net_arg_value(argc, argv, "-dup")) {
        int d = v[0] - '0';
        if (d >= 1 && d <= 9) dc.ticdup = (short)d;
    }
    if (net_arg(argc, argv, "-extratic")) dc.extratics = 1;

    driver = ipx ? IPX : SERIAL;
    running = true;
    I_VanillaNetInit = net_init;
    I_VanillaNetCmd = net_cmd;
    return true;
}

void net_shutdown() {
    if (!running) return;
    running = false;
    if (driver == IPX) ipx_shutdown();
    else if (driver == SERIAL) ser_shutdown();
}
