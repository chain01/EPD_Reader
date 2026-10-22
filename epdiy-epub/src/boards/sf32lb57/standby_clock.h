#pragma once
class Renderer;
void reader57_standby_boot();
bool reader57_standby_restore();
bool reader57_standby_render_saved(Renderer *renderer);
int reader57_standby_ram(Renderer *renderer,bool test);
bool reader57_standby_ram_take_restored();
