This directory contains the Passenger Apache 2 module. It is used in Passenger's Apache integration mode. Most of the logic is not here, but in the agent. The Apache module's responsibilities are:

 * To provide and to handle Apache configuration options.
 * To start the Passenger agent (by starting the Passenger Watchdog).
 * To proxy requests through the Passenger Core.
