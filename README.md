# Garrus - a library for reading performance counters on Linux
## Developed by Ravi Shreyas Anupindi

Garrus provides programmers with an API to manually instrument their source code to obtain performance counter readings for specific regions in their code. This library was conceived when I was working with Memcached and wanted to measure TLB miss information specifically during SET and GET operations. Right now, the functionality I've provided is very specific to the goals I needed to accomplish, but I plan on revisiting this soon to make it more generic. Works specifically on Linux since it requires Linux perf_events (tested on Ubuntu 16.04).

Some features:
- Supports performance counter readings for multi-threaded applications
- Allows users to measure a single event or a group of three events (this is determined for now)
- Actively avoids high runtime overhead by pre-allocating event counters at library initialization and re-using them during run-time. Helps avoid the high overhead of **open()** and **close()** system calls (especially with KPTI) each time the code region we wish to monitor is encountered. This was especially useful in monitoring GET and SET functions in Memcached that are repeatedly called from worker threads.
- Thread safety and scalability are ensured by allocating each thread its own pool of pre-allocated event counters or counter groups.
- Avoids high write overhead from frequent writes by dumping readings of an event into a buffer and writing the buffer to file when it fills up.

I apologize for the poor documentation (or lack thereof), but you can contact me at ravianupindi@iisc.ac.in for any questions.

**Trivia:** Yes, the name Garrus was inspired by Mass Effect. Just don't ask why.
