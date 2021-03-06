/*
 * Copyright (C) 2015 Sony Mobile Communications Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names
 *    of its contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/time.h>

#define LOG_TAG "TimeKeep"

#include <cutils/properties.h>
#include <cutils/log.h>
#include <errno.h>

#define RTC_SYS_FILE "/sys/class/rtc/rtc0/since_epoch"
#define TIME_ADJUST_PATH "/data/time/timekeep"

int read_epoch(unsigned long* epoch) {
	int res = 0;

	int fd = open(RTC_SYS_FILE, O_RDONLY);
	if (fd == -1) {
		res = -errno;
		ALOGD("Failed to open RTC sys path");
	} else {
		char buffer[16];
		memset(buffer, 0, 16);
		res = read(fd, buffer, 16);
		if (res > 0) {
			char *endp = NULL;
			*epoch = strtoul(buffer, &endp, 10);
			// sysfs read returns newline, ok to end up at '\n'
			if (*endp != '\0' && *endp != '\n') {
				ALOGI("Read from " RTC_SYS_FILE " returned "
				      "invalid string %s (%s)", buffer, endp);
				res = -1;
			}
		}
	}

	return res;
}

int store_time() {
	char prop[PROPERTY_VALUE_MAX];
	unsigned long seconds = 0;
	unsigned long epoch_since = 0;
	int res = -1;
	struct tm tm;
	time_t t;
	int timeadj_fd, prop_len;

	// Get time and adjust for local time
	time(&t);
	localtime_r(&t, &tm);
	seconds = mktime(&tm);

	if (seconds > 0) {
		res = read_epoch(&epoch_since);
		if (res < 0) {
			ALOGI("Failed to read epoch while storing");
		} else {
			seconds -= epoch_since;
			prop_len = snprintf(prop, PROPERTY_VALUE_MAX, "%lu\n", seconds);
			timeadj_fd = open(TIME_ADJUST_PATH, O_RDWR|O_CREAT, 0644);
			if (timeadj_fd >= 0) {
				write(timeadj_fd, prop, prop_len);
				close(timeadj_fd);
				ALOGI("Time adjustment stored to file");
				res = 0;
			}
			else {
				ALOGE("Cannot create file. errno = %d", errno);
			}
		}
	}

	return res;
}


int restore_time() {
	struct timeval tv;
	unsigned long time_adjust = 0;
	unsigned long epoch_since = 0;
	int res = -1;
	char prop[PROPERTY_VALUE_MAX];
	int timeadj_fd, read_len;

	memset(prop, 0x0, PROPERTY_VALUE_MAX);

	timeadj_fd = open(TIME_ADJUST_PATH, O_RDONLY);
	if (timeadj_fd < 0) {
		ALOGE("Cannot open file. errno = %d", errno);
		return res;
	}

	read_len = read(timeadj_fd, prop, PROPERTY_VALUE_MAX);
	if (read_len <= 0) {
		ALOGE("Cannot read file. errno = %d", errno);
		return res;
	}

	if (strcmp(prop, "0") != 0) {
		char *endp = NULL;
		errno = 0;
		time_adjust = strtoul(prop, &endp, 10);
		if (*endp != '\0' && *endp != '\n') {
			ALOGI("Property in " TIME_ADJUST_PATH
			      " is not valid: %s (%d)", prop, errno);
			return res;
		}
	} else {
		ALOGI("No time adjust value found for restore");
		return res;
	}

	res = read_epoch(&epoch_since);
	if (res < 0) {
		ALOGI("Failed to read from " RTC_SYS_FILE
		      " (%d), bailing out", res);
	} else {
		tv.tv_sec = epoch_since + time_adjust;
		tv.tv_usec = 0;
		res = settimeofday(&tv, NULL);
		if (res != 0) {
			ALOGI("Failed to restore time (%d), root?",
			      res);
		} else {
			ALOGI("Time restored!");
		}
	}

	return res;
}

int main(int argc, char* argv[]) {
	int res = -1;
	if (argc != 2) {
		ALOGI("usage: timekeep store|restore");
		return res;
	}

	// Keep CAP_SYS_TIME and drop to system user

	if (strcmp(argv[1], "store") == 0) {
		res = store_time();
	}

	if (strcmp(argv[1], "restore") == 0) {
		res = restore_time();
	}

	return res;
}
