#ifndef RTC_H
#define RTC_H

#include <stdint.h>

typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t month;
    uint32_t year;
} rtc_time_t;

// Function declarations matching your rtc.c implementation
void rtc_read(rtc_time_t *t);
void rtc_format_time(const rtc_time_t *t, char *buf);
void rtc_format_date(const rtc_time_t *t, char *buf);

#endif