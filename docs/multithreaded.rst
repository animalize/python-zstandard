==========================
Multi-Threaded Compression
==========================

``ZstdCompressor`` accepts a ``threads`` argument that controls the number
of threads to use for compression. The way this works is that input is split
into segments and each segment is fed into a worker pool for compression. Once
a segment is compressed, it is flushed/appended to the output.

.. note::

   These threads are created at the C layer and are not Python threads. So they
   work outside the GIL. It is therefore possible to CPU saturate multiple cores
   from Python.

The segment size for multi-threaded compression is chosen from the window size
of the compressor. This is derived from the ``window_log`` attribute of a
``ZstdCompressionParameters`` instance. By default, segment sizes are in the 1+MB
range.

If multi-threaded compression is requested and the input is smaller than the
configured segment size, only a single compression thread will be used. If the
input is smaller than the segment size multiplied by the thread pool size or
if data cannot be delivered to the compressor fast enough, not all requested
compressor threads may be active simultaneously.

Compared to non-multi-threaded compression, multi-threaded compression has
higher per-operation overhead. This includes extra memory operations,
thread creation, lock acquisition, etc.

Due to the nature of multi-threaded compression using *N* compression
*states*, the output from multi-threaded compression will likely be larger
than non-multi-threaded compression. The difference is usually small. But
there is a CPU/wall time versus size trade off that may warrant investigation.

Output from multi-threaded compression does not require any special handling
on the decompression side. To the decompressor, data generated with single
threaded compressor looks the same as data generated by a multi-threaded
compressor and does not require any special handling or additional resource
requirements.
