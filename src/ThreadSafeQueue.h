#include <queue>
#include "Mutex.h"

template <class T>
ThreadSafeQueue : public queue <T>
{
    public:
        void push(const T& x) { qMutex.Lock(); std::queue::push(x); qMutex.Unlock(); }
        T pop() { qMutex.Lock(); T t = std::queue::pop(x); qMutex.Unlock(); return t; }

    private:
        static Mutex qMutex;
};
