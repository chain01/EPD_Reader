#pragma once
#include "Renderer/Renderer.h"
#include "controls/Actions.h"
void reader_home(Renderer *renderer,int selected);
void reader_settings(Renderer *renderer);
bool reader_frontlight(Renderer *renderer,UIAction action);
bool reader_dfu_page(Renderer *renderer,UIAction action);
extern volatile int reader_dfu_hit;
extern volatile int reader_light_hit;
