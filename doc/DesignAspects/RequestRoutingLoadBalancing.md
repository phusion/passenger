# Request routing and load balancing

## Context

Passenger routes requests to application processes. The routing algorithm must address these issues:

- **Avoid head-of-line (HoL) blocking**: Round-robin load balancing can introduce HoL blocking because requests have mixed response times. This is especially problematic with requests having extreme outlier response times.

  We only want to route a request to a process that can handle it at that time.

- **Avoid exceeding application I/O concurrency**: When there are more concurrent requests than the processes can handle, then queue those requests and (if possible) spawn more application processes.

  Note that applications' I/O concurrency can be very varied:
  - Some apps only have a limited, fixed amount of I/O concurrency. Example: Ruby apps' I/O concurrency is determined by their thread count.
  - Some apps have an unlimited amount of I/O concurrency. Example: Ruby ActionCable servers and Node.js apps.

- **Balance I/O concurrency**:

- **Balance CPU load**: Some applications (such as JRuby) can process different requests in different CPU cores. Others (such as MRI Ruby, Node.js) can only use a CPU core even though they have high.

- **Allow shutting down idle processes**
- **During rolling restart**

## Decision