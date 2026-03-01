// kernel/rtc.c
// CMOS Real-Time Clock reader — ports 0x70 / 0x71

#include "rtc.h"
#include "io.h"

#define CMOS_ADDR  0x70
#define CMOS_DATA  0x71

// CMOS register indices
#define RTC_SECONDS  0x00
#define RTC_MINUTES  0x02
#define RTC_HOURS    0x04
#define RTC_DAY      0x07
#define RTC_MONTH    0x08
#define RTC_YEAR     0x09
#define RTC_STATUS_A 0x0A
#define RTC_STATUS_B 0x0B

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

// Wait until the RTC is not in the middle of an update
static void rtc_wait_ready(void) {
    while (cmos_read(RTC_STATUS_A) & 0x80) {
        // Bit 7 of status A = "update in progress"
        // Spin until it clears
    }
}

// Convert BCD to binary (RTC can return values in BCD format)
static uint8_t bcd_to_bin(uint8_t bcd) {
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

void rtc_read(rtc_time_t *t) {
    // Wait for update to finish
    rtc_wait_ready();

    // Read raw values
    uint8_t seconds = cmos_read(RTC_SECONDS);
    uint8_t minutes = cmos_read(RTC_MINUTES);
    uint8_t hours   = cmos_read(RTC_HOURS);
    uint8_t day     = cmos_read(RTC_DAY);
    uint8_t month   = cmos_read(RTC_MONTH);
    uint8_t year    = cmos_read(RTC_YEAR);

    // Check status register B to see if values are BCD or binary
    uint8_t status_b = cmos_read(RTC_STATUS_B);

    if (!(status_b & 0x04)) {
        // Bit 2 = 0 means BCD mode — convert to binary
        seconds = bcd_to_bin(seconds);
        minutes = bcd_to_bin(minutes);
        hours   = bcd_to_bin(hours & 0x7F); // mask bit 7 (PM flag)
        day     = bcd_to_bin(day);
        month   = bcd_to_bin(month);
        year    = bcd_to_bin(year);
    }

    // Handle 12-hour mode
    if (!(status_b & 0x02)) {
        // Bit 1 = 0 means 12-hour mode
        if (hours & 0x80) {
            // PM flag set
            hours = (hours & 0x7F) + 12;
            if (hours == 24) hours = 12; // 12 PM stays 12
        } else if (hours == 12) {
            hours = 0; // 12 AM = 0
        }
    }

    t->seconds = seconds;
    t->minutes = minutes;
    t->hours   = hours;
    t->day     = day;
    t->month   = month;
    t->year    = 2000 + (uint16_t)year;  // CMOS only stores 2-digit year
}

// "HH:MM:SS"
void rtc_format_time(const rtc_time_t *t, char *buf) {
    buf[0] = '0' + (t->hours / 10);
    buf[1] = '0' + (t->hours % 10);
    buf[2] = ':';
    buf[3] = '0' + (t->minutes / 10);
    buf[4] = '0' + (t->minutes % 10);
    buf[5] = ':';
    buf[6] = '0' + (t->seconds / 10);
    buf[7] = '0' + (t->seconds % 10);
    buf[8] = '\0';
}

// "YYYY-MM-DD"
void rtc_format_date(const rtc_time_t *t, char *buf) {
    uint16_t y = t->year;
    buf[0] = '0' + (y / 1000); y %= 1000;
    buf[1] = '0' + (y / 100);  y %= 100;
    buf[2] = '0' + (y / 10);   y %= 10;
    buf[3] = '0' + y;
    buf[4] = '-';
    buf[5] = '0' + (t->month / 10);
    buf[6] = '0' + (t->month % 10);
    buf[7] = '-';
    buf[8] = '0' + (t->day / 10);
    buf[9] = '0' + (t->day % 10);
    buf[10] = '\0';
}