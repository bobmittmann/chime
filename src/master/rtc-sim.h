/*****************************************************************************
 * RTC chip simulator
 *****************************************************************************/

#ifndef __RTC_SIM_H__
#define __RTC_SIM_H__

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

struct rtc_tm {
	uint8_t seconds;
	uint8_t minutes;
	uint8_t hours;
	uint8_t day;
	uint8_t date;
	uint8_t month;
	uint8_t year;
};

#ifdef __cplusplus
extern "C" {
#endif

int rtc_sim_init(void);

int rtc_connect(int comm);

int rtc_read(struct rtc_tm * rtm);

uint64_t rtc_timestamp(struct rtc_tm * rtm);

#ifdef __cplusplus
}
#endif	

#endif /* __RTC_SIM_H__ */

