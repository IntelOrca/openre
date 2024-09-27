#pragma once

//All locations are in Bio2.exe version 1.1

void main(void) {} //0x004c3c70

void Init_system(void) {} //0x004c3f10

void InitGeom_func(void) {} //0x004c4460 Need to Verify

void Init_main(void) {} //0x004c4000

void Swap_Cbuff(void) {} //0x004c4690

void Bg_set_mode(void) {} //0x004c4700

void Bg_draw(void) {} //0x004c4890

void Fade_set(void) {} //0x004c49c0

void Fade_start(void) {} //???

void Fade_adjust(void) {} //0x004c4a50

void Fade_off(void) {} //0x004c4ab0

unsigned long Fade_status(void) //0x004c4ad0
{
    return 0;
}

void System_trans(void) {} //0x004c4af0

void Init_global(void) {} //0x004c4d70

unsigned long Cut_check(void) //0x004c4de0
{
    return 0;
}

void Cut_change(void) {} //0x004c4e60

struct VCUT Ccut_serach; //0x004c4e90

unsigned long Hit_ck_point4(void) //0x004c4ec0
{
    return 0;
}

unsigned long Hit_ck_box(void) //0x004c4fb0
{
    return 0;
}

void Card_event_set(void) {} //0x004c4ff0

void Logo(void) {} //???