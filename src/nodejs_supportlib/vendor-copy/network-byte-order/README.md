# JavaScript implementation of htonl, htons, ntohl, ntohs #

[![Build Status](https://travis-ci.org/mattcg/network-byte-order.png?branch=master)](https://travis-ci.org/mattcg/network-byte-order)
[![Coverage Status](https://coveralls.io/repos/mattcg/network-byte-order/badge.png)](https://coveralls.io/r/mattcg/network-byte-order)

These routines are used for converting values between host and [network byte order](http://en.wikipedia.org/wiki/Endianness#Endianness_in_networking).

Derived from [an implementation](http://blog.couchbase.com/starting-membase-nodejs) by Membase, Inc.

## Usage ##

### htonl(b, i, v) ###

The `htonl()` function converts the given unsigned 32-bit (long) integer from host byte order to network byte order (Little-Endian to Big-Endian).

`b` is an [`Array`](https://developer.mozilla.org/en-US/docs/JavaScript/Reference/Global_Objects/Array) of octets or a Node.JS [`Buffer`](http://nodejs.org/api/buffer.html).
`i` is the zero-based index at which to write into `b`.
`v` is the value to convert.

### htons(b, i, v) ###

The `htons()` function converts the given unsigned 16-bit (short) integer from host byte order to network byte order (Little-Endian to Big-Endian).

`b` is an [`Array`](https://developer.mozilla.org/en-US/docs/JavaScript/Reference/Global_Objects/Array) of octets or a Node.JS [`Buffer`](http://nodejs.org/api/buffer.html).
`i` is the zero-based index at which to write into `b`.
`v` is the value to convert.

### ntohl(b, i) ###

The `ntohl()` function converts the unsigned 32-bit (long) integer from network byte order to host byte order (Big-Endian to Little-Endian).

`b` is an [`Array`](https://developer.mozilla.org/en-US/docs/JavaScript/Reference/Global_Objects/Array) of octets or a Node.JS [`Buffer`](http://nodejs.org/api/buffer.html) to read the value from.
`i` is the zero-based index at which to read from `b`.

### ntohlStr(s, i) ###

`s` is a `string` to the read value from.
`i` is the zero-based index at which to read from `s`.

### ntohs(b, i) ###

The `ntohs()` function converts the unsigned 16-bit (short) integer from network byte order to host byte order (Big-Endian to Little-Endian).

`b` is an [`Array`](https://developer.mozilla.org/en-US/docs/JavaScript/Reference/Global_Objects/Array) of octets or a Node.JS [`Buffer`](http://nodejs.org/api/buffer.html) to read the value from.
`i` is the zero-based index at which to read from `b`.

### ntohsStr(s, i) ###

`s` is a `string` to the read value from.
`i` is the zero-based index at which to read from `s`.

## License ##

Licensed under the [Apache License, Version 2.0](http://www.apache.org/licenses/LICENSE-2.0).
