/* SPDX-License-Identifier: Apache-2.0 */
#include "patterns.h"
#define DEMO_WIDTH 1216
#define DEMO_HEIGHT 684
#define DEMO_BUFFER_BYTES (DEMO_WIDTH*DEMO_HEIGHT/2)
#include <string.h>
#include <stdio.h>

/* Five columns, seven rows. Only black/white; no font engine or file IO. */
static const uint8_t digits[10][5] = {
    {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e}
};
static const uint8_t letters[26][5] = {
    {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},
    {0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},
    {0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
};

static void rect(uint8_t *b, int x, int y, int w, int h, int black)
{
    int xx, yy;
    for (yy = y; yy < y + h && yy < DEMO_HEIGHT; ++yy)
        for (xx = x; xx < x + w && xx < DEMO_WIDTH; ++xx)
        {
            unsigned pos;
            uint8_t mask;
            if (xx < 0 || yy < 0) continue;
            pos = (unsigned)(yy * DEMO_WIDTH + xx);
            mask = (pos & 1) ? 0xf0 : 0x0f;
            if (black) b[pos / 2] &= (uint8_t)~mask;
            else b[pos / 2] |= mask;
        }
}

static void text(uint8_t *b, int x, int y, const char *s, int scale)
{
    while (*s)
    {
        const uint8_t *g = NULL;
        uint8_t symbol[5] = {0};
        int col, row;
        if (*s >= '0' && *s <= '9') g = digits[*s - '0'];
        else if (*s >= 'A' && *s <= 'Z') g = letters[*s - 'A'];
        else {
            if (*s == '/') { symbol[0]=0x40; symbol[1]=0x30; symbol[2]=0x08; symbol[3]=0x06; symbol[4]=0x01; }
            if (*s == ':') { symbol[1]=0x36; symbol[2]=0x36; }
            if (*s == '-') memset(symbol, 0x08, sizeof(symbol));
            g = symbol;
        }
        for (col=0; col<5; ++col)
            for (row=0; row<7; ++row)
                if (g[col] & (1u << row))
                    rect(b, x+col*scale, y+row*scale, scale, scale, 1);
        x += 6*scale;
        ++s;
    }
}


int patterns_render(uint8_t *b, const char *name)
{
    unsigned x,y;
    memset(b,0xff,DEMO_BUFFER_BYTES);
    if (!strcmp(name,"white")) return 0;
    if (!strcmp(name,"black")) { memset(b,0,DEMO_BUFFER_BYTES); return 0; }
    if (!strcmp(name,"checker") || !strcmp(name,"lines")) {
        for (y=0;y<DEMO_HEIGHT;++y) for(x=0;x<DEMO_WIDTH;x+=2) {
            int black = !strcmp(name,"checker") ? (((x/76)+(y/76))&1) : ((x/4)&1);
            b[(y*DEMO_WIDTH+x)/2]=black?0:255;
        }
        return 0;
    }
    if (!strcmp(name,"gray")) {
        for(y=0;y<DEMO_HEIGHT;++y) for(x=0;x<DEMO_WIDTH;x+=2) {
            unsigned g=x/76;
            b[(y*DEMO_WIDTH+x)/2]=g*17;
        }
        return 0;
    }
    if(!strcmp(name,"touch")) {
        const int xs[]={80,1135,1135,80,608},ys[]={80,80,603,603,342};
        const char *labels[]={"1 TOP LEFT","2 TOP RIGHT","3 BOTTOM RIGHT","4 BOTTOM LEFT","5 CENTER"};
        for(unsigned i=0;i<5;++i) {
            int x=xs[i],y=ys[i];
            rect(b,x-30,y-2,61,5,1);rect(b,x-2,y-30,5,61,1);
            text(b,i==1||i==2?860: i==4?470:35,y<100?130:y>500?530:400,labels[i],3);
        }
        text(b,300,220,"SF32LB527 TOUCH TEST",4);
        text(b,320,265,"TAP 1 2 3 4 5",4);
        return 0;
    }
    if(strcmp(name,"card")) return -1;
    text(b,42,32,"SF32LB527 / E-PAPER TEST",6);
    text(b,42,102,"E0470A03-AF-S  1216 X 684",5);
    text(b,42,160,"SY7636 / LM3630A / TF / V28",5);
    rect(b,0,0,DEMO_WIDTH,4,1); rect(b,0,DEMO_HEIGHT-4,DEMO_WIDTH,4,1);
    rect(b,0,0,4,DEMO_HEIGHT,1); rect(b,DEMO_WIDTH-4,0,4,DEMO_HEIGHT,1);
    for(unsigned i=0;i<16;++i) {
        unsigned g=i;
        for(y=240;y<370;++y) for(x=32+i*72;x<32+(i+1)*72;x+=2)
            b[(y*DEMO_WIDTH+x)/2]=g*17;
    }
    for(y=420;y<600;y+=30) for(x=40;x<580;x+=30)
        rect(b,x,y,30,30,((x-40)/30+(y-420)/30)&1);
    text(b,650,430,"BLACK / WHITE",4);
    text(b,650,480,"16 GRAY LEVELS",4);
    text(b,650,530,"BORDER + GRID",4);
    text(b,42,630,"CHECK IMAGE VISUALLY",4);
    return 0;
}
