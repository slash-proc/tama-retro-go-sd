#pragma once

#ifndef _RG_RTC_H_
#define _RG_RTC_H_

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t GW_GetCurrentHour(void);
uint8_t GW_GetCurrentMinute(void);
uint8_t GW_GetCurrentSecond(void);
uint8_t GW_GetCurrentSubSeconds(void);

uint8_t GW_GetCurrentMonth(void);
uint8_t GW_GetCurrentDay(void);
uint8_t GW_GetCurrentWeekday(void);
uint8_t GW_GetCurrentYear(void);
uint64_t GW_GetCurrentMillis(void);

time_t GW_GetUnixTime(void);
void GW_GetUnixTM(struct tm *tm);
void GW_SetUnixTM(struct tm *tm);

void GW_AddToCurrentHour(const int8_t direction);
void GW_AddToCurrentMinute(const int8_t direction);
void GW_AddToCurrentSecond(const int8_t direction);

void GW_AddToCurrentMonth(const int8_t direction);
void GW_AddToCurrentDay(const int8_t direction);
void GW_AddToCurrentYear(const int8_t direction);

#ifdef __cplusplus
}
#endif

#endif
