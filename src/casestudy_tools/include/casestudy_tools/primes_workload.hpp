#ifndef RTIS_PRIMES_WORKLOAD
#define RTIS_PRIMES_WORKLOAD
#include <time.h>
#include <sys/time.h>
#include <thread>
#include <cstdio>
typedef double ktimeunit;
ktimeunit nth_prime_silly(double millis = 100);
void dummy_load(int load_ms);
// create a global variable to store the calibration value
class DummyLoadCalibration
{
public:
    static int getCalibration();
    static void setCalibration(int calib);

private:
    static int calibration;
};

ktimeunit get_thread_time(struct timespec *currTime);
#endif