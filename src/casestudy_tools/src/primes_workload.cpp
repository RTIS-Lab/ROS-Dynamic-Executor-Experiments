#include "casestudy_tools/primes_workload.hpp"
#include <iostream>
ktimeunit nth_prime_silly(double millis)
{
  // struct tms this_thread_times;
  struct timespec currTime;
  int sum = 0;
  int i;
  int j;
  ktimeunit const start_cpu_time = get_thread_time(&currTime);
  ktimeunit last_iter_time = 0;
  ktimeunit last_iter_start_time = start_cpu_time;
  for (i = 2; i < 4294967296 - 1; i++)
  {
    // times(&this_thread_times);
    ktimeunit cum_time = get_thread_time(&currTime);
    last_iter_time = cum_time - last_iter_start_time;
    last_iter_start_time = cum_time;
    if ((cum_time - start_cpu_time + last_iter_time*2) > millis)
    {
      break;
    }
    for (j = 2; j < i; j++)
    {
      sum += j;
    }
    if (cum_time - start_cpu_time > millis)
    {
      std::cout << "Warning: Time limit exceeded" << std::endl;
    }
  }
  // std::cout<< "took " << (get_thread_time(&currTime) - start_cpu_time) << "ms, allowed " << millis << "ms" << std::endl;
  return get_thread_time(&currTime) - start_cpu_time;
}
ktimeunit get_thread_time(struct timespec *currTime)
{
  // clockid_t threadClockId;
  // pthread_getcpuclockid(pthread_self(), &threadClockId);
  // clock_gettime(threadClockId, currTime);
  // return currTime->tv_nsec / 1000000.0 + currTime->tv_sec * 1000.0;

  // use the wall clock instead
  clock_gettime(CLOCK_MONOTONIC_RAW, currTime);
  return currTime->tv_nsec / 1000000.0 + currTime->tv_sec * 1000.0;
}

#define DUMMY_LOAD_ITER	100000

void dummy_load(int load_ms) {
	int i, j;
  int dummy_load_calib = DummyLoadCalibration::getCalibration();
  // dummy_load_calib is for 100ms
  dummy_load_calib = dummy_load_calib * load_ms / 100;
	for (j = 0; j < dummy_load_calib; j++)
		for (i = 0 ; i < DUMMY_LOAD_ITER; i++) 
			__asm__ volatile ("nop");
}

void DummyLoadCalibration::setCalibration(int calib) {
  DummyLoadCalibration::calibration = calib;
}

int DummyLoadCalibration::getCalibration() {
  return DummyLoadCalibration::calibration;
}

int DummyLoadCalibration::calibration = 1;