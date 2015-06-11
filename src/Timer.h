#ifndef TIMER_H
#define TIMER_H
#include <time.h>
#include <sys/time.h>
#include <string>

class Timer {
    public:
        Timer();
        void startTimer();
        void stopTimer();
        std::string startTime();
        std::string stopTime();
        int timeDiff();
    private:
        time_t startT_;
        time_t stopT_;
};
#endif
