# Server optimization guide

There are two aspects with regard to optimizing Phusion Passenger performance.

The first aspect is *settings tuning*. Phusion Passenger's default settings are not aimed at optimizing, but at *safety*. The defaults are designed to conserve resources, to prevent server overload and to keep web apps up and running. To optimize for performance, you need to tweak some settings whose values depend on your hardware and your environment.

Besides Phusion Passenger settings, you may also want to tune kernel-level settings.

The second aspect is *using performance-enhancing features*. This requires small application-level changes.

If you are optimizing Phusion Passenger for the purpose of **benchmarking** then you should also follow the benchmarking recommendations.

<a name="minimizing_process_spawning"></a>

## Minimizing process spawning

By default, Phusion Passenger spawns and shuts down application processes according to traffic. This allows it to use more resources during busy times, while conserving resources during idle times. This is especially useful if you host more than 1 app on a single server: if not all apps are used at the same time, then you don't have to keep all apps running at the same time.

However, spawning a process takes a lot of time (in the order of 10-20 seconds for a Rails app), and CPU usage will be near 100% during spawning. Therefore, while spawning, your server will be slower at performing other activities, such as handling requests.

For consistent performance, it is thus recommended that you configure a static process pool: telling Phusion Passenger to use a fixed number of processes, instead of spawning and shutting them down dynamically.

### Standalone

Run `passenger start` with `--max-pool-size=N --min-instances=N`, where `N` is the number of processes you want.

### Nginx

Let `N` be the number of processes you want. Set the following configuration in your `http` block:

    passenger_max_pool_size N;

Set the following configuration in your `server` block:

    passenger_min_instances N;

You should also configure `passenger_pre_start` in the `http` block so that your app is started during web server launch:

    # Refer to the Users Guide for more information about passenger_pre_start.
    passenger_pre_start http://your-website-url.com;

### Apache

Let `N` be the number of processes you want. Set the following configuration in the global context:

    PassengerMaxPoolSize N

Set the following configuration in your virtual host block:

    PassengerMinInstances N

You should also configure `PassengerPreStart` in the global context so that your app is started during web server launch:

    # Refer to the Users Guide for more information about PassengerPreStart.
    PassengerPreStart http://your-website-url.com

## Maximizing throughput

This section provides guidance on maximizing Phusion Passenger's throughput. The amount of throughput that Phusion Passenger handles is proportional to the number of processes or threads that you've configured. More processes/threads generally means more throughput, but there is an upper limit. Past a certain value, further increasing the number of processes/threads won't help. If you increase the number of processes/threads even further, then performance may even go down.

The optimal value depends on the hardware and the environment. This section will provide you with formulas to calculate that optimal value. The following factors are involved in calculation:

 * **Memory**. More processes implies a higher memory usage. If too much memory is used then the machine will hit swap, which slows everything down. You should only have as many processes as memory limits comfortably allow. Threads use less memory, so prefer threads when possible. You can create tens of threads in place of one process.
 * **Number of CPUs**. True (hardware) concurrency cannot be higher than the number of CPUs. In theory, if all processes/threads on your system use the CPUs constantly, then:

    * You can increase throughput up to `NUMBER_OF_CPUS` processes/threads.
    * Increasing the number of processes/threads after that point will increase virtual (software) concurrency, but will not increase true (hardware) concurrency and will not increase maximum throughput.

   Having more processes than CPUs may decrease total throughput a little thanks to context switching overhead, but the difference is not big because OSes are good at context switching these days.

   On the other hand, if your CPUs are not used constantly, e.g. because they’re often blocked on I/O, then the above does not apply and increasing the number of processes/threads does increase concurrency and throughput, at least until the CPUs are saturated.
 * **Blocking I/O**. This covers all blocking I/O, including hard disk access latencies, database call latencies, web API calls, etc. Handling input from the client and output to the client does not count as blocking I/O, because Phusion Passenger has buffering layers that relief the application from worrying about this.

   The more blocking I/O calls your application process/thread makes, the more time it spends on waiting for external components. While it’s waiting it does not use the CPU, so that’s when another process/thread should get the chance to use the CPU. If no other process/thread needs CPU right now (e.g. all processes/threads are waiting for I/O) then CPU time is essentially wasted. Increasing the number processes or threads decreases the chance of CPU time being wasted. It also increases concurrency, so that clients do not have to wait for a previous I/O call to be completed before being served.

The formulas in this section assume that your machine is dedicated to Phusion Passenger. If your machine also hosts other software (e.g. a database) then you'll need to tweak the formulas a little bit.

### Tuning the application process and thread count

#### Step 1: determining the application's memory usage

The amount of memory that your application uses on a per-process basis, is key to our calculation. You should first figure out how much memory your application typically needs. Every application has different memory usage patterns, so the typical memory usage is best determined by observation.

Run your app for a while, then run `passenger-status` at different points in time to examine memory usage. Then calculate the average of your data points. In the rest of this section, we'll refer to the amount of memory (in MB) that an application process needs, as `RAM_PER_PROCESS`.

In our experience, a typical medium-sized single-threaded Rails application process can use 150 MB of RAM on a 64-bit machine, even when the spawning method is set to "smart".

#### Step 2: determine the system's limits

First, let's define the maximum number of (single-threaded) processes, or the number of threads, that you can comfortably have given the amount of RAM you have. This is a reasonable upper limit that you can reach without degrading system performance. This number is not the final optimal number, but is merely used for further caculations in later steps.

There are two formulas that we can use, depending on what kind of concurrency model your application is using in production.

**Purely single-threaded multi-process formula**

If you didn't explicitly configure multithreading, then you are using using this concurrency model. Or, if you are *not* using Ruby (e.g. if you using **Python, Node.js or Meteor**), then you are also using this concurrency model, because Phusion Passenger only supports multithreading for Ruby apps.

The formula is then as follows:

    max_app_processes = (TOTAL_RAM * 0.75) / RAM_PER_PROCESS

It is derived as follows:

 * `(TOTAL_RAM * 0.75)`: We can assume that there must be at least 25% of free RAM that the operating system can use for other things. The result of this calculation is the RAM that is freely available for applications. If your system runs a lot of services and thus has less memory available for Phusion Passenger and its apps, then you should lower `0.75` to some constant that you think is appropriate.
 * `/ RAM_PER_PROCESS`: Each process consumes a roughly constant amount of RAM, so the maximum number of processes is a single devision between the aforementioned calculation and this constant.

**Multithreaded formula**

The formula for multithreaded concurrency is as follows:

    max_app_threads_per_process =
      ((TOTAL_RAM * 0.75) - (CHOSEN_NUMBER_OF_PROCESSES * RAM_PER_PROCESS * 0.9)) /
      (RAM_PER_PROCESS / 10)

Here, `CHOSEN_NUMBER_OF_PROCESSES` is the number of application processes you want to use. In case of Ruby, Python, Node.js and Meteor, this should be equal to `NUMBER_OF_CPUS`. This is because all these languages can only utilize a single CPU core per process. If you're using a language runtime that does not have a Global Interpreter Lock, e.g. JRuby or Rubinius, then `CHOSEN_NUMBER_OF_PROCESSES` can be 1.

The formula is derived as follows:

 * `(TOTAL_RAM * 0.75)`: The same as explained earlier.
 * `(CHOSEN_NUMBER_OF_PROCESSES * RAM_PER_PROCESS)`: In multithreaded scenarios, the application processes consume a constant amount of memory, so we deduct this from the RAM that is available to applications. The result is the amount of RAM available to application threads.
 * `/ (RAM_PER_PROCESS / 10)`: A thread consumes about 10% of the amount of memory a process would, so we divide the amount of RAM available to threads with this number. What we get is the number of threads that the system can handle.

On 32-bit systems, `max_app_threads_per_process` should not be higher than about 200. Assuming an 8 MB stack size per thread, you will run out of virtual address space if you go much further. On 64-bit systems you don’t have to worry about this problem.

#### Step 3: derive the applications' needs

The earlier two formulas were not for calculating the number of processes or threads that application needs, but for calculating how much the system can handle without getting into trouble. Your application may not actually need that many processes or threads! If your application is CPU-bound, then you only need a small multiple of the number of CPUs you have. Only if your application performs a lot of blocking I/O (e.g. database calls that take tens of milliseconds to complete, or you call to Twitter) do you need a large number of processes or threads.

Armed with this knowledge, we derive the formulas for calculating how many processes or threads we actually need.

 * If your application performs a lot of blocking I/O then you should give it as many processes and threads as possible:

       # Use this formula for purely single-threaded multi-process scenarios.
       desired_app_processes = max_app_processes

       # Use this formula for multithreaded scenarios.
       desired_app_threads_per_process = max_app_threads_per_process

 * If your application doesn’t perform a lot of blocking I/O, then you should limit the number of processes or threads to a multiple of the number of CPUs to minimize context switching:

       # Use this formula for purely single-threaded multi-process scenarios.
       desired_app_processes = min(max_app_processes, NUMBER_OF_CPUS)

       # Use this formula for multithreaded scenarios.
       desired_app_threads_per_process = min(max_app_threads_per_process, 2 * NUMBER_OF_CPUS)

#### Step 3: configure Phusion Passenger

**Purely single-threaded multi-process scenarios**

 * When using Phusion Passenger Standalone, run `passenger start` with `--max-pool-size=<desired_app_processes> --min-instances=<desired_app_processes>`.
 * When using Phusion Passenger for Nginx, configure:
   * `passenger_max_pool_size <desired_app_processes>;`
   * `passenger_min_instances <desired_app_processes>;`
   * `passenger_pre_start` to have your app started automatically at web server boot.
 * When using Phusion Passenger for Apache, configure:
   * `PassengerMaxPoolSize <desired_app_processes>`
   * `PassengerMinInstances <desired_app_processes>`
   * `PassengerPreStart` to have your app started automatically at web server boot.

**Multithreaded scenarios**

In order to use multithreading you must use Phusion Passenger Enterprise. The open source version of Phusion Passenger does not support multithreading.

 * When using Phusion Passenger Standalone:
   * Run `passenger start` with `--max-pool-size=<CHOSEN_NUMBER_OF_PROCESSES> --min-instances=<CHOSEN_NUMBER_OF_PROCESSES> --concurrency-model=thread --thread-count=<desired_app_threads_per_process>`
   * If `desired_app_processes` is 1, then you should also add `--spawn-method=direct`. By using direct spawning instead of smart spawning, Phusion Passenger will not keep a Preloader process around, saving you some memory. This is because a Preloader process is useless when there's only 1 application process.
 * When using Phusion Passenger for Nginx, configure:
   * `passenger_max_pool_size <CHOSEN_NUMBER_OF_PROCESSES>;`
   * `passenger_min_instances <CHOSEN_NUMBER_OF_PROCESSES>;`
   * `passenger_concurrency_model thread;`
   * `passenger_thread_count <desired_app_threads_per_process>;`
   * `passenger_pre_start` to have your app started automatically at web server boot.
   * If `desired_app_processes` is 1, then you should set `passenger_spawn_method direct`. By using direct spawning instead of smart spawning, Phusion Passenger will not keep a Preloader process around, saving you some memory. This is because a Preloader process is useless when there's only 1 application process.
 * When using Phusion Passenger for Apache, configure:
   * `PassengerMaxPoolSize <desired_app_processes>`
   * `PassengerMinInstances <desired_app_processes>`
   * `PassengerConcurrencyModel thread`
   * `PassengerThreadCount <desired_app_threads_per_process>`
   * `PassengerPreStart` to have your app started automatically at web server boot.
   * If `desired_app_processes` is 1, then you should set `PassengerSpawnMethod direct`. By using direct spawning instead of smart spawning, Phusion Passenger will not keep a Preloader process around, saving you some memory. This is because a Preloader process is useless when there's only 1 application process.

#### Possible step 4: configure Rails

Only if you're using the multithreaded concurrency model do you need to configure Rails. You need to enable thread-safety by setting `config.thread_safe!` in `config/environments/production.rb`. In Rails 4.0 this is on by default for the production environment, but in earlier versions you had to enable it manually.

You should also increase the ActiveRecord pool size because it limits concurrency. You can configure it in `config/database.yml`. Set the `pool` value to the number of threads per application process. But if you believe your database cannot handle that much concurrency, keep it at a low value.

#### Example 1: purely single-threaded multi-process scenario with lots of blocking I/O, in a low-memory server

Suppose you have:

 * 1 GB of RAM.
 * 150 MB of memory usage per application process.
 * Lots of blocking I/O in the application.
 * A purely single-threaded multi-process scenario.

Then the calculation is as follows:

    # Use this formula for purely single-threaded multi-process deployments.
    max_app_processes = (1024 * 0.75) / 150 = 5.12
    desired_app_processes = max_app_processes = 5.12

Conclusion: you should use 5 or 6 processes. Phusion Passenger should be configured as follows:

    # Standalone
    passenger start --max-pool-size=5 --min-instances=5

    # Nginx
    passenger_max_pool_size 5;
    passenger_min_instances 5;

    # Apache
    PassengerMaxPoolSize 5
    PassengerMinInstances 5

However a concurrency of 5 or 6 is way too low if your application performs a lot of blocking I/O. You should use a multithreaded deployment instead, or you need to get more RAM so you can run more processes.

#### Example 2: purely single-threaded multi-process scenario with lots of blocking I/O, in a high-memory server

Suppose you have:

 * 8 CPUs.
 * 32 GB of RAM.
 * 150 MB of memory usage per application process.
 * Lots of blocking I/O in the application.
 * A purely single-threaded multi-process scenario.

Then the calculation is as follows:

    # Use this formula for purely single-threaded multi-process deployments.
    max_app_processes = (1024 * 32 * 0.75) / 150 = 163.84
    desired_app_processes = max_app_processes = 163.84

Conclusion: you should use 163 or 164 processes. This number seems high, but the value is correct. Because your app performs a lot of blocking I/O, you need a lot of I/O concurrency. The more concurrency the better. The amount of concurrency scales linearly with the number of processes, which is why you end up with such a large number.

Phusion Passenger should be configured as follows:

    # Standalone
    passenger start --max-pool-size=163 --min-instances=163

    # Nginx
    passenger_max_pool_size 163;
    passenger_min_instances 163;

    # Apache
    PassengerMaxPoolSize 163
    PassengerMinInstances 163

Note that in this example, 163-164 processes is merely the maximum number of processes that you can run, without overloading your RAM. It does not mean that you have enough concurrency for your application! If you need more concurrency, you should use a multithreaded deployment instead.

#### Example 3: multithreaded deployment with lots of blocking I/O

Consider the same machine as in example 2:

 * 8 CPUs.
 * 32 GB of RAM.
 * 150 MB of memory usage per application process.
 * Lots of blocking I/O in the application.
 * A purely single-threaded multi-process scenario.

But this time you're using multithreading with 8 application processes (because you have 8 CPUs). How many threads do you need per process?

    # Use this formula for multithreaded deployments.
    max_app_threads_per_process
    = ((1024 * 32 * 0.75) - (8 * 150)) / (150 / 10)
    = 1558.4

Conclusion: you should use 1558 threads per process.

    # Standalone
    passenger start --max-pool-size=8 --min-instances=8 --concurrency-model=thread --thread-count=1558

    # Nginx
    passenger_max_pool_size 8;
    passenger_min_instances 8;
    passenger_concurrency_model thread;
    passenger_thread_count 1558;

    # Apache
    PassengerMaxPoolSize 8
    PassengerMinInstances *
    PassengerConcurrencyModel thread
    PassengerThreadCount 1558

Because of the huge number of threads, this only works on a 64-bit platform. If you're on a 32-bit platform, consider lowering the number of threads while raising the number of processes. For example, you can double the number of processes (to 16) and halve the number of threads (to 779).

### Configuring the web server

If you're using Nginx then it does not need additional configuration. Nginx is evented and already supports a high concurrency out of the box.

If you're using Apache, then prefer the worker MPM (which uses a combination of processes and threads) or the event MPM (which is similar to the worker MPM, but better) over the prefork MPM (which only uses processes) whenever possible. PHP requires prefork, but if you don't use PHP then you can probably use one of the other MPMs. Make sure you set a low number of processes and a moderate to high number of threads.

Because Apache performs a lot of blocking I/O (namely HTTP handling), you should give it a lot of threads so that it has a lot of concurrency. Apache's concurrency *must* be somewhat larger than the total number of application processes or total number of application threads. When considering example 3, the Apache concurrency must be larger than `8 * 1558 = 12464`.

If you cannot use the event MPM, consider putting Apache behind an Nginx reverse proxy, with response buffering turned on on the Nginx side. This reliefs a lot of concurrency problems from Apache. If you can use the event MPM then adding Nginx to the mix does not provide many advantages.

### Summary

 * If your application performs a lot of blocking I/O, use lots of processes/threads. You should move away from single-threaded multiprocessing in this case, and start using multithreading.
 * If your application is CPU-bound, use a small multiple of the number of CPUs.
 * Do not exceed the number of processes/threads your system can handle without swapping.

## Performance-enhancing features

<a name="turbocaching"></a>

### Turbocaching

Phusion Passenger supports turbocaching since version 4. Turbocaching is an HTTP cache built inside Phusion Passenger. When used correctly, the cache can accelerate your app tremendously. To utilize turbocaching, you only need to set HTTP caching headers.

#### Learning about HTTP caching headers

The first thing you should do is to [learn how to use HTTP caching headers](https://developers.google.com/web/fundamentals/performance/optimizing-content-efficiency/http-caching). It's pretty simple and straightforward. Since the turbocache is just a normal HTTP shared cache, it respects all the HTTP caching rules.

#### Set an Expires or Cache-Control header

To activate the turbocache, the response must contain either an "Expires" header or a "Cache-Control" header.

The "Expires" header tells the turbocache how long to cache a response. Its value is an HTTP timestamp, e.g. "Thu, 01 Dec 1994 16:00:00 GMT".

The Cache-Control header is a more advanced header that not only allows you to set the caching time, but also how the cache should behave. The easiest way to use it is to set the **max-age** flag, which has the same effect as setting "Expires". For example, this tells the turbocache that the response is cacheable for at most 60 seconds:

    Cache-Control: max-age=60

As you can see, a "Cache-Control" header is much easier to generate than an "Expires" header. Furthermore, "Expires" doesn't work if the visitor's computer's clock is wrongly configured, while "Cache-Control" does. This is why we recommend using "Cache-Control".

Another flag to be aware of is the **private** flag. This flag tells any shared caches -- caches which are meant to store responses for many users -- not to cache the response. The turbocache is a shared cache. However, the browser's cache is not, so the browser can still cache the response. You should set the "private" flag on responses which are meant for a single user, as you will learn later in this article.

And finally, there is the **no-store** flag, which tells *all* caches -- even the browser's -- not to cache the response.

Here is an example of a response which is cacheable for 60 seconds by the browser's cache, but not by the turbocache:

    Cache-Control: max-age=60,private

The HTTP specification specifies a bunch of other flags, but they're not relevant for the turbocache. 

#### Only GET requests are cacheable

The turbocache currently only caches GET requests. POST, PUT, DELETE and other requests are never cached. If you want your response to be cacheable by the turbocache, be sure to use GET requests, but also be sure that your request is idempotent.

#### Avoid using the "Vary" header

The "Vary" header is used to tell caches that the response depends on one or more request headers. But the turbocache does not implement support for the "Vary" header, so if you output a "Vary" header then the turbocache will not cache your response at all. Avoid using the "Vary" header where possible.

### Out-of-band garbage collection

Phusion Passenger supports out-of-band garbage collection for Ruby apps. With this feature enabled, Phusion Passenger can run the garbage collector in between requests, so that the garbage collector doesn't delay the app as much. Please refer to the Users Guide for more information about this feature.

### Using the builtin HTTP engine

In certain situations, using the builtin HTTP engine in Passenger Standalone may yield some performance benefits because it skips a layer of processing.

Passenger normally works by integrating into Nginx or Apache. As described in the [Design & Architecture](Design%20and%20architecture.html) document, requests are first handled by Nginx or Apache, and then forwarded to the Passenger core process (the HelperAgent) and the application process. This architecture provides various benefits, such as security benefits (Nginx and Apache's HTTP connection handling routines are thoroughly battle-tested and secure) and feature benefits (e.g. Gzip compression, superb static file handling).

This is even true if you use the Standalone mode. Although it acts standalone, it is implemented under the hood by running Passenger in a builtin Nginx engine.

However, the fact that all requests go through Nginx or Apache means that there is a slight overhead, which can be avoided. This overhead is small (much smaller than typical application and network overhead), and using Nginx or Apache is very useful, but in certain special situations it may be beneficial to skip this layer.

 * In **microbenchmarks**, the overhead of Nginx and Apache are very noticeable. Removing Nginx and Apache from the setup, and benchmarking against the Passenger HelperAgent directly, will yield much better results.
 * In some **multi-server setups**, Nginx and Apache may be redundant. Recall that in typical multi-server setups there is a load balancer which forwards requests to one of the many web servers. Each web server in this setup runs Passenger. But the load balancer is sometimes already responsible for many of the tasks that Nginx and Apache perform, e.g. the secure handling of HTTP connections, buffering, slow client protection or even static file serving. In these cases, removing Nginx and Apache from the web servers and load balancing to the Passenger HelperAgent directly may have a minor improvement on performance.

Nginx and Apache can be removed by using Passenger's builtin HTTP engine. By using this engine, Passenger will listen directly on a socket for HTTP requests, without using Nginx or Apache.

This builtin HTTP engine can be accessed by starting Passenger Standalone using the `--engine=builtin` parameter, like this:

    passenger start --engine=builtin

It should be noted that the builtin HTTP engine has fewer features than the Nginx engine, by design. For example the builtin HTTP engine does not support serving static files, nor does it support gzip compression. Thus, we recommend using the Nginx engine in most situations, unless you have special needs such as documented above.

## Benchmarking recommendations

### Tooling recommendations

 * Use [wrk](https://github.com/wg/wrk) as benchmarking tool.
   - We do not recommend `ab` because it's slow and buggy.
   - We do not recommend `siege` and `httperf` because they cannot utilize multiple CPU cores.
 * Enable HTTP keep-alive in both the server and in your benchmarking tool. Otherwise you will end up benchmarking how quickly the kernel can set up TCP connections, which is a non-trivial part of the request time.
 * Warm up the server before benchmarking. That is, run a mini-benchmark before the actual benchmark, and discard the result of the mini-benchmark.

### Operating system recommendations

 * Don't benchmark on OS X. OS X's TCP stack and process scheduler are horrible from a performance point of view. We recommend Linux.
 * When on Linux, be sure to [tune your kernel socket settings](http://www.joedog.org/articles-tuning/) so that they don't stall the benchmark.

### Server and application recommendations

 * If the purpose of your benchmark is to compare against Puma, Unicorn or other app servers, be sure to benchmark against Phusion Passenger Standalone, not Phusion Passenger for Nginx or Phusion Passenger for Apache.
   - This is because Puma, Unicorn and other app servers are standalone servers, while Phusion Passenger for Nginx and Phusion Passenger for Apache introduce additional layers (namely Nginx and Apache), making the comparison unfair.
   - Be sure to start Phusion Passenger Standalone with the `builtin` engine. This is the default.
 * Configure Phusion Passenger to [use a static number of processes](#minimizing_process_spawning). The number of processes should be a multiple of the number of CPU cores.
 * Ensure that your app outputs HTTP caching headers, so that Phusion Passenger's [turbocaching](#turbocaching) can kick in.
