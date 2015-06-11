#include "Timer.h"

Timer::Timer() {
    startT_ = time(NULL);
    stopT_ = 0;
}

void Timer::startTimer() {
    startT_ = time(NULL);
}

void Timer::stopTimer() {
    stopT_ = time(NULL);

}

std::string Timer::startTime() {
    struct tm *tm;
    char timebuf[255];

    tm = localtime(&startT_);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M %Z", tm);
    std::string s = timebuf;
    return s;
}

std::string Timer::stopTime() {
    struct tm *tm;
    char timebuf[255];

    tm = localtime(&stopT_);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M %Z", tm);
    std::string s = timebuf;
    return s;
}

int Timer::timeDiff() {
    return (stopT_ - startT_);
}

