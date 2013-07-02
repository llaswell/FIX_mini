#ifndef SIMUTIL_H
#define SIMUTIL_H

#include <string>
#include <algorithm>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>
#include <ctype.h>

#include <iostream>

using namespace std;

#define  stdc_min(x,y)  ((x<y)?x:y)

extern uint32_t midnightSeconds;

 int sign(long);

 double cappedVal(double , double );
 void stringUpper(string&);
 string popStrTok ( string &, const string );
 double myround(double inD, int precision);
 int fsign(double);
 int calcSecondsSinceMidnight( string _timeString );
 long getSystemSeconds(long& microseconds);
 void getSystemTime(string& timeString, long& microseconds,long &seconds);
 uint32_t calcSecondsSinceMidnight(timespec& _ts);
 uint32_t getMidnightSeconds();
timespec diff(timespec start, timespec end);

#endif
