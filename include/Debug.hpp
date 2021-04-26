#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>

#include <iostream>

#define MAX_FORMAT_LEN 255 // max format string length

#define DEBUG false // if debug
#define TITLE false // if debugtitle
#define TIMER false // if debugtimer
#define CUR   false

class Debug
{
private:
    static long startTime; /* Last start time in milliseconds. */

public:
    static void debugTitle(const char* str);          // print debug title string
    static void debugItem(const char* format, ...);   // print debug item string
    static void debugCur(const char* format, ...);    // print debug cur string
    static void notifyInfo(const char* format, ...);  // printt debug normal info
    static void notifyError(const char* format, ...); // print debug errror info
    static void startTimer(const char*);              // start timer and print info
    static void endTimer();                           // end timer and print info
};

/** Redundance check. **/
#endif
