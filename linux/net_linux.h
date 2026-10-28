// The network drivers built into the Linux port. On DOS, IPXSETUP.EXE or
// SERSETUP.EXE finds the other players, then starts DOOM.EXE with
// "-net <address of its doomcom_t>", and Doom calls the driver's interrupt
// to send or fetch a packet. Here the same drivers run inside the game:
// net_setup finds the players (the same packets, byte for byte, so a
// Linux machine can play against DOS machines running IPXSETUP/SERSETUP
// and the original DOOM.EXE), then hands d_net.c the doomcom_t through
// I_VanillaNetInit / I_VanillaNetCmd as the DOS build does.
#pragma once
#include <stddef.h>
extern "C" {
#include "d_doomcom.h"
}

// From main(), before the game starts. True: carry on (a net game was set
// up, or none was asked for); false: it failed or was given up (said why).
bool net_setup(int argc, char** argv);
void net_shutdown();

// The two drivers (net_ipx.cpp, net_serial.cpp). setup fills in the
// doomcom_t (numnodes, consoleplayer, numplayers...), cmd carries out
// dc->command, shutdown lets go of the network or line.
bool ipx_wanted(int argc, char** argv);
bool ipx_setup(int argc, char** argv, doomcom_t* dc);
void ipx_cmd(doomcom_t* dc);
void ipx_shutdown();

bool ser_wanted(int argc, char** argv);
bool ser_setup(int argc, char** argv, doomcom_t* dc);
void ser_cmd(doomcom_t* dc);
void ser_shutdown();

// Helpers shared by the drivers.
int net_arg(int argc, char** argv, const char* name);          // index, or 0
const char* net_arg_value(int argc, char** argv, const char* name);   // the word after, or null
bool net_aborted();                                             // Ctrl+C during setup
// Bytes in doomcom_t from data on: the vanilla packet and the free-look
// extension after it.
#define NET_DATA_MAX ((int)(sizeof(doomcom_t) - offsetof(doomcom_t, data)))
