/* Verify the composite against actual vendor BIN drive codes. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "epd_waveform.h"
#include "epd_waveform_bin_reader.h"

static uint8_t blob[262144];
static uint32_t blob_size,table_offset,table_frames;
static unsigned reader_calls;
static int oversize_mode=-1;
static uint32_t word(uint32_t offset)
{
    assert(offset+4<=blob_size);
    return (uint32_t)blob[offset]|((uint32_t)blob[offset+1]<<8)|
           ((uint32_t)blob[offset+2]<<16)|((uint32_t)blob[offset+3]<<24);
}
static uint32_t select_table(unsigned mode,uint32_t *offset)
{
    uint32_t count=word(8+mode*8),directory=word(12+mode*8),i;
    for(i=0;i<count;++i){
        uint32_t p=directory+i*16;
        if((int32_t)word(p)<=28&&28<(int32_t)word(p+4)){
            *offset=word(p+12);return word(p+8);
        }
    }
    return 0;
}
static unsigned drive(unsigned mode,unsigned f,unsigned index)
{
    uint32_t offset=0,count=select_table(mode,&offset);
    if(f>=count)return 0;
    assert(offset+f*64+index/4<blob_size);
    return (blob[offset+f*64+index/4]>>(6-2*(index%4)))&3;
}
int waveform_bin_reader_init(uint32_t max_size)
{
    assert(max_size>=blob_size);return 0;
}
uint32_t waveform_bin_reader_get_frames(int temperature,WAVE_TABLE_MODE_T mode)
{
    assert(temperature==28);++reader_calls;
    table_frames=select_table((unsigned)mode,&table_offset);
    return (int)mode==oversize_mode?65:table_frames;
}
void waveform_bin_reader_fill_lut(uint32_t *lut,uint32_t frame)
{
    unsigned i;assert(frame<table_frames);++reader_calls;
    for(i=0;i<256;++i)
        lut[i]=((blob[table_offset+frame*64+i/4]>>(6-2*(i%4)))&3)<<3;
}
int main(int argc,char **argv)
{
    uint32_t lut[256],offset=0,n,gray,f,i;
    unsigned prepared_calls,white_counts[16]={0};
    FILE *file,*dump=NULL;assert(argc==2||argc==3);file=fopen(argv[1],"rb");assert(file);
    if(argc==3){dump=fopen(argv[2],"wb");assert(dump);}
    blob_size=(uint32_t)fread(blob,1,sizeof(blob),file);fclose(file);
    assert(word(0)==0xe9d7ab1e&&word(4)==18);
    gray=select_table(5,&offset);
    n=select_table(1,&offset);
    for(f=0;f<n;++f)for(i=0;i<15;++i)if(drive(1,f,(i<<4)|15))++white_counts[i];
    assert(white_counts[0]==18);
    epd_wave_table();assert(epd_wave_prepare_mode(EPD_WAVE_TEXT)==0);
    n=epd_wave_table_get_frames();
    assert(n==gray);prepared_calls=reader_calls;
    for(f=0;f<n;++f){
        epd_wave_table_fill_lut(lut,f);
        for(i=0;i<256;++i){
            unsigned old=i>>4,next=i&15;
            unsigned erase=(white_counts[old]*30+9)/18;
            unsigned expected=old==next?0:next==0?drive(1,f,i):
                              next==15?(f<erase?drive(1,0,0x0f):0):drive(5,f,i);
            assert(lut[i]==(0xff000000u|expected));
            assert(expected<=2);
            if(dump)assert(fputc((int)(lut[i]&3),dump)!=EOF);
        }
    }
    if(dump)assert(fclose(dump)==0);
    epd_wave_table_fill_lut(lut,n);
    for(i=0;i<256;++i)assert(lut[i]==0xff000000u);
    assert(reader_calls==prepared_calls); /* callback does not touch BIN */
    assert(epd_wave_prepare_mode(EPD_WAVE_TEXT)==0);
    assert(reader_calls==prepared_calls); /* cache reused across updates */
    for(i=0;i<3;++i){
        const int modes[]={0,1,3};int mode=modes[i];
        assert(epd_wave_prepare_mode(mode)==0);
        n=epd_wave_table_get_frames();
        assert(n==select_table((unsigned)mode,&offset));
        for(f=0;f<n;++f){
            unsigned index;epd_wave_table_fill_lut(lut,f);
            for(index=0;index<256;++index){
                unsigned expected=drive((unsigned)mode,f,index);
                if((mode==1||mode==7)&&(index>>4)==(index&15))expected=0;
                assert(lut[index]==(0xff000000u|expected));
            }
        }
    }
    assert(epd_wave_prepare_mode(EPD_WAVE_TEXT)==0);
    assert(epd_wave_table_get_frames()==gray);
    epd_wave_table_fill_lut(lut,29);assert(lut[0x0f]==(0xff000000u|drive(1,0,0x0f)));
    epd_wave_table_fill_lut(lut,30);assert(lut[0x0f]==0xff000000u);
    for(i=0;i<2;++i){
        epd_wave_table();oversize_mode=i?1:5;
        assert(epd_wave_prepare_mode(EPD_WAVE_TEXT)<0);
        assert(epd_wave_table_get_frames()==0);
    }
    oversize_mode=-1;epd_wave_table();
    assert(epd_wave_prepare_mode(EPD_WAVE_TEXT)==0);
    {
        const int invalid[]={-1,2,5,7,8,17,18,19,20,21,256};
        for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){
            assert(epd_wave_prepare_mode(invalid[i])<0);
            assert(epd_wave_table_get_frames()==0);
        }
    }
    printf("TEXT_WAVEFORM_PASS frames=%u transitions=256 tuned_white=30 native_DU_black native_GL16_gray neutral_diagonal bounds callback_cache\n",gray);
    return 0;
}
