#ifndef _BATTERY_MONITOR_H
#define _BATTERY_MONITOR_H

extern int battery_monitor_cnt;

void battery_monitor_init();
void tick_battery_monitor();

#endif   //  _BATTERY_MONITOR_H