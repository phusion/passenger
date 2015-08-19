This directory contains the Passenger Nginx module. It is used in Passenger's Nginx integration mode. Most of the logic is not here, but in the agent. The Nginx module's responsibilities are:

 * To provide and to handle Nginx configuration options.
 * To start the Passenger agent (by starting the Passenger Watchdog).
 * To proxy requests through the Passenger Core.
