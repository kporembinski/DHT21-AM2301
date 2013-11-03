/**************************************************************
 Name        : am2301_poll.c
 Version     : 0.1

 Copyright (C) 2013 Constantin Petra

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.
***************************************************************************/

#include <wiringPi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <syslog.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <mysql/mysql.h>

static const int _pin_am2301 = 5;

static void do_init(void);

typedef struct __sensor_data {
    float rh;
    float t;
} sensor_data;

static MYSQL *con;

static char user[32];
static char passwd[128];

static void quit_handler(int sig)
{
    signal(sig, SIG_IGN);
    mysql_close(con);
    exit(0);
}

static int mysql_stuff_init(int drop)
{
    con = mysql_init(NULL);

    if (con == NULL) {
	fprintf(stderr, "%s\n", mysql_error(con));
	return -1;
    }

    if(mysql_real_connect(con, "localhost", user, passwd,
			  "am2301db", 0, NULL, 0) == NULL)
    {
	fprintf(stderr, "%s\n", mysql_error(con));
	mysql_close(con);
	return -1;
    }
    if (drop != 0) {
	if (mysql_query(con, "CREATE DATABASE am2301db") != 0) {
	    fprintf(stderr, "%s\n", mysql_error(con));
	}
	if (mysql_query(con, "DROP TABLE IF EXISTS am2301db") != 0) {
	    fprintf(stderr, "%s\n", mysql_error(con));
	    mysql_close(con);
	    return -1;
	}
	if (mysql_query(con, "CREATE TABLE am2301db(ts TIMESTAMP, RH INT, Temp INT)") != 0)     {
	    fprintf(stderr, "%s\n", mysql_error(con));
	    mysql_close(con);
	    return -1;
	}
    }
    return 0;
}

static int mysql_add(sensor_data *s)
{
    char query[256];
    char st[128];

    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    strftime(st, 128, "%F %T", &tm);
    sprintf(query, "INSERT INTO am2301db VALUES(\"%s\", %d, %d)",
	    st, (int)(s->t * 10.0), (int)(s->rh * 10.0));
    if (mysql_query(con, query) != 0) {
	fprintf(stderr, "%s\n", mysql_error(con));
	return -1;
    }
    return 0;
}

static void do_init(void)
{
    if (wiringPiSetup() == -1) {
        printf("wiringPi-Error\n");
        exit(1);
    }

    signal (SIGTERM, quit_handler);
    signal (SIGHUP, quit_handler);

    if (mysql_stuff_init(0) != 0) {
	exit(1);
    }

    piHiPri(20);
}

static int wait_change(int mode, unsigned int tmo)
{
    int v1, v2, v3;
    unsigned int now = micros();

    do {
	/* Primitive low-pass filter */ 
	v1 = digitalRead(_pin_am2301);
	v2 = digitalRead(_pin_am2301);
	v3 = digitalRead(_pin_am2301);
	if (v1 == v2 && v2 == v3 && v3 == mode) {
	    return (micros() - now);
	}
    } while ((micros() - now) < tmo);
    return -1;
}

static int read_am2301(sensor_data *s, int mode)
{
    int i, j, k;
    int val;
    int v;
    unsigned char x;
    unsigned char vals[5];

    /* Leave it high for a while */
    pinMode(_pin_am2301, OUTPUT);
    digitalWrite(_pin_am2301, HIGH);
    delayMicroseconds(100);

    /* Set it low to give the start signal */
    digitalWrite(_pin_am2301, LOW);
    delayMicroseconds(1000);

    /* Now set the pin high to let the sensor start communicating */ 
    digitalWrite(_pin_am2301, HIGH);
    pinMode(_pin_am2301, INPUT);
    if (wait_change(HIGH, 100) == -1) {
	return -1;
    }

    /* Wait for ACK */
    if (wait_change(LOW, 100) == -1) {
	return -2;
    }

    if (wait_change(HIGH, 100) == -1) {
	return -3;
    }

    /* When restarting, it looks like this lookfor start bit is not needed */
    if (mode != 0) {
	/* Wait for the start bit */
	if (wait_change(LOW, 200) == -1) {
	    return -4;
	}
	
	if (wait_change(HIGH, 200) == -1) {
	    return -5;
	}
    }
    x = 0;
    k = 0;
    j = 7;

    for (i = 0; i < 40; i++) {
	val = wait_change(LOW, 500);
	if (val == -1) {
	    return -6;
	}

	v = (val >= 50) ? 1 : 0;
	x = x | (v << j);

	if (--j == -1) {
	    vals[k] = x;
	    k++;
	    j = 7;
	    x = 0;
	}
	val = wait_change(HIGH, 500);
	if (val == -1) {
	    return -7;
	}
    }
    pinMode(_pin_am2301, OUTPUT);
    digitalWrite(_pin_am2301, HIGH);

    /* Verify checksum */
    x = vals[0] + vals[1] + vals[2] + vals[3];
    if (x != vals[4]) {
	return -8;
    }

    s->rh = (float) (((uint16_t) vals[0] << 8) | (uint16_t) vals [1]);
    s->rh /= 10.0f;
    s->t = (float) (((uint16_t) vals[2] << 8) | (uint16_t) vals [3]);
    s->t /= 10.0f;

    if (s->rh > 100.0 || s->rh < 0.0 ||
	s->t > 80.0 || s->t < -40.0 )
    {
	return -9;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int i = 0;
    int ret;
    int add_db = 1;
    sensor_data s;
    
    if ((argc < 4) || (strcmp(argv[1], "-u") != 0)) {
	printf("Usage: %s -u user passwd [reset | nodb]\n", argv[0]);
	return -1;
    }

    strncpy(user, argv[2], sizeof(user));
    strncpy(passwd, argv[3], sizeof(passwd));

    if (argc == 5) {
	if (strcmp(argv[4], "reset") == 0) {
	    mysql_stuff_init(1);
	    if (con != 0) {
		mysql_close(con);
	    }
	    return 0;
	}
	else if (strcmp(argv[4], "nodb") == 0) {
	    add_db = 0;
	}
    }

    do_init();

    /* Try to read one value, if that doesn't work, try 10 more times,
     * then bail out.
     */
    ret = read_am2301(&s, 0);
    if (ret == 0) {
	printf("t = %.1f, rh = %.1f\n", s.t, s.rh);
	/* Drop the first measurement */
    }
    delay(2000);

    while (i < 10) {
	ret = read_am2301(&s, 1);
	if (ret == 0) {
	    printf("t = %.1f, rh = %.1f\n", s.t, s.rh);
	    if (add_db != 0) {
		mysql_add(&s);
	    }
	    break;
	}
	delay(2000);
	i++;
    }

    if (i > 10) {
	return -1;
    }
    return 0;
}
