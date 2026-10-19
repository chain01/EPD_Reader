#pragma once
#include "EpdiyFrameBufferRenderer.h"
extern "C" {
#include "board527.h"
}
class SF32PaperRenderer : public EpdiyFrameBufferRenderer {
public:
    SF32PaperRenderer(const EpdFont *regular,const EpdFont *bold,const EpdFont *italic,
                      const EpdFont *bold_italic,const uint8_t *busy,int w,int h)
        : EpdiyFrameBufferRenderer(regular,bold,italic,bold_italic,busy,w,h)
    {
        m_frame_buffer=board527_framebuffer();clear_screen();
    }
    void powerOnLcd() {}
    void powerOffLcd(){board527_display_off();}
    void flush_display() override
    {
        int rc=board527_display_present(m_frame_buffer);
        if(rc)rt_kprintf("READER_DISPLAY_ERROR rc=%d\n",rc);
    }
    void flush_area(int x,int y,int w,int h) override
    {
        (void)x;(void)y;(void)w;(void)h;flush_display();
    }
    bool has_gray(){return needs_gray_flush;}
    void reset() override {}
};
