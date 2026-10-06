/* The kernel's side of the Doom port (kernel.cpp), for
   doomgeneric_baremetal.c and i_sound_baremetal.c. */
#pragma once
#include <stdint.h>

#define BM_W 320              /* Doom's screen: DOOMGENERIC_RESX x RESY, 8-bit */
#define BM_H 200

#ifdef __cplusplus
extern "C" {
#endif
void bm_set_palette(const uint32_t* rgb);   /* 256 entries, 0x00RRGGBB */
void bm_draw(const uint8_t* pixels);        /* BM_W x BM_H palette indexes */
void bm_poll(void);
int bm_key(int* pressed, unsigned char* key);   /* doomkeys.h codes; 0: none */
int bm_mouse(int* buttons, int* dx, int* dy);   /* dy: down; 0: no change */
uint32_t bm_ms(void);
void bm_sleep_ms(uint32_t ms);
#ifdef __cplusplus
}
#endif
