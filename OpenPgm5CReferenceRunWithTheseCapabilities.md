#summary OpenPGM 5 : C Reference : Run With These Capabilities
#labels Phase-Implementation

### Capabilities ###
OpenPGM C programs require system capabilities from this list in order to function.

<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>System Capability</th>
<th>Description</th>
</tr>
<tr>
<td><h3>Communications and Events</h3></td>
</tr><tr>
<td><tt>CAP_NET_RAW</tt></td>
<td>To create raw sockets required for the PGM protocol.</td>
</tr><tr>
<td><tt>CAP_SYS_NICE</tt></td>
<td>To set real time thread scheduling and high thread priority for processing incoming messages and high resolution timing.</td>
</tr><tr>
<td><h3>HPET<code>*</code></h3></td>
</tr><tr>
<td>access to <tt>/dev/hpet</tt></td>
<td>The High Performance Event Timer provides a system wide stable monotonic timer.  Default access on Ubuntu is root only.</td>
</tr><tr>
<td><h3>Real-time Clock<code>*</code></h3></td>
</tr><tr>
<td>access to <tt>/dev/rtc</tt></td>
<td>For TSC calibration with the hardware real-time clock access to the <tt>rtc</tt> device can be enabled by adding the user to the real-time Unix group, on Ubuntu this is called <tt>audio</tt>.</td>
</tr>
</table>

**`*` Clock selection** is specified by one system environment variable <tt>PGM_TIMER</tt> governing how to get the current time.


### Clock Selection ###
Kernel timer resolution is in a period of change, kernels can be found with standard resolutions of 1-4ms, with high resolution timers providing microsecond to nanosecond resolution.  <tt><a href='http://en.wikipedia.org/wiki/Rdtsc'>RDTSC</a></tt> is a very cheap mechanism for applications to get the current time in high resolution, however multiple core systems may suffer from drift between each core, some systems may vary frequency in power saving states.  Particularly prone to error are [Hyperthread](http://en.wikipedia.org/wiki/Hyperthread) based processors from Intel, it is highly recommended to disable hyperthreading on all processors wishing to run high speed PGM based messaging.  The High Precision Event Timer, HPET, was created to provide a system wide stable timer in multi-core systems to resolve TSC irregularities.

An operating system calculates the current time of day based off various sources to provide an accurate, stable, and monotonic source to applications.  On Linux the system time is generated from the APIC PM Timer or HPET with interpolation using TSC which can cause the time to become non-monotonic.  Calculating a stable system time is not cheap and historically many applications have been re-engineered to minimise the calls to functions like <tt>gettimeofday()</tt> due to this overhead, using <tt>RDTSC</tt> has been a popular method to bypass this overhead, with multi-core systems either process affinity has to be used to keep using the same TSC source, or reading from the HPET device instead but at a ~500 nanosecond cost.

The RTC device <tt>/dev/rtc</tt> cannot be shared between multiple applications but offers a stable time source external to the TSC and is available on all systems, compared with limited availability of HPET.

The TSC will be calibrated with a per-platform specific configuration, on Linux this will be from <tt>/proc/cpuinfo</tt> on Windows this will be the registry key <tt>HKLM/HARDWARE/DESCRIPTION/System/CentralProcessor/0/~MHz</tt>, on OS X through the system control <tt>hw.cpufrequency</tt>, on FreeBSD using <tt>hw.clockrate</tt>, and on Solaris using <i>kstat</i> and the <tt>clock_MHz</tt> key.  If calibration is unavailable or disabled then a runtime calibration will run using <tt>nanosleep()</tt> as a timing source.  The frequency can be overriden with the environment variable <tt>RDTSC_FREQUENCY</tt> set in kilohertz.  It is recommended to set this frequency or use an alternative timing mechanism on Solaris or Intel platforms with [SpeedStep](http://en.wikipedia.org/wiki/SpeedStep) or [Turbo Boost](http://en.wikipedia.org/wiki/Intel_Turbo_Boost) technology enabled.  A dynamic clock frequency may be discovered by comparing the system reported clock frequency with the hardware capability.

Example, an [Intel Xeon 5140](http://ark.intel.com/products/27217/Intel-Xeon-Processor-5140-4M-Cache-2_33-GHz-1333-MHz-FSB) host shows the following <tt>/proc/cpuinfo</tt> when the [CPUSpeed](http://www.carlthompson.net/Software/CPUSpeed) service is running on RedHat Enterprise Linux:
<pre>
processor       : 3<br>
vendor_id       : GenuineIntel<br>
cpu family      : 6<br>
model           : 15<br>
model name      : Intel(R) Xeon(R) CPU            5140  @ 2.33GHz<br>
stepping        : 6<br>
cpu MHz         : 249.999<br>
...<br>
</pre>

Run "<tt>dmidecode --type Processor</tt>" to reveal clock capability:
<pre>
Processor Information<br>
Socket Designation: Proc 1<br>
Type: Central Processor<br>
Family: Xeon<br>
Manufacturer: Intel<br>
...<br>
External Clock: 1333 MHz<br>
Max Speed: 4800 MHz<br>
Current Speed: 2333 MHz<br>
...<br>
</pre>

On Windows the default timing mechanism follows the SQL Server development team in using the multi-media timers.  It is possible to use the query performance counter API but as MSDN states that the counter frequency cannot change whilst the system is running this may lead to instabilities with modern processor dynamic frequency technologies.


<table cellpadding='5' border='1' cellspacing='0'>
<tr>
<th>Environment Setting</th>
<th>Description</th>
</tr>
<tr>
<td><h3>PGM_TIMER</h3></td>
</tr><tr>
<td><tt>CLOCK_GETTIME</tt></td>
<td>Use <tt>clock_gettime (CLOCK_MONOTONIC, tp)</tt>, see <tt>CLOCK_GETRES(3)</tt> for further details.</td>
</tr><tr>
<td><tt>FTIME</tt></td>
<td>Use <tt>ftime (tp)</tt>, see <a href='http://www.kernel.org/doc/man-pages/online/pages/man3/ftime.3.html'>ftime(3)</a> for further details.  Windows resolution is limited to 10-16ms.</td>
</tr><tr>
<td><tt>RTC</tt></td>
<td>Read from <tt>/dev/rtc</tt> for current time, a 8192 Hz timer providing 122us resolution.</td>
</tr><tr>
<td><tt>TSC</tt></td>
<td>Call <tt>RDTSC</tt> and normalize with a calibrated count of core ticks.</td>
</tr><tr>
<td><tt>HPET</tt></td>
<td>Read from the <tt>HPET</tt> device which handles femtosecond length pulses.</td>
</tr><tr>
<td><tt>GETTIMEOFDAY</tt><code>*</code></td>
<td>Use <tt>gettimeofday (tv, NULL)</tt>, see <a href='http://www.kernel.org/doc/man-pages/online/pages/man2/gettimeofday.2.html'>gettimeofday(2)</a> for further details.  <a href='https://access.redhat.com/knowledge/docs/en-US/Red_Hat_Enterprise_MRG/1.3/html/Realtime_Tuning_Guide/sect-Realtime_Tuning_Guide-General_System_Tuning-gettimeofday_speedup.html'>gettimeofday function call speedup</a> is enabled by default on MRG kernels but consequences will occur with <a href='https://access.redhat.com/knowledge/docs/en-US/Red_Hat_Enterprise_MRG/1.3/html/Realtime_Tuning_Guide/sect-Realtime_Tuning_Guide-Realtime_Specific_Tuning-RT_Specific_gettimeofday_speedup.html'>millisecond coalescing</a> if PGM protocol values are not appropriately set.</td>
</tr><tr>
<td><tt>MMTIME</tt><code>*</code></td>
<td>Use Windows multi-media timers, lower resolution than other timers but offers greater stability and low cost for frequent usage.</td>
</tr><tr>
<td><tt>QPC</tt></td>
<td>Use Windows Performance Counters, dependent upon accurate drivers and suitable "BIOS support" per the word of MSDN.  Windows selects the "best" underlying hardware timing mechanism to use, can be a combination of APIC, HPET and TSC for interpolation.</td>
</tr>
</table>

`*` Default settings.

### Examples ###
Use the PGM protocol via raw sockets using the <tt>CAP_NET_RAW</tt> capability,
```
$ sudo execcap 'cap_net_raw=ep' pgmsend moo
```

On a single core Intel system, use TSC.
```
$ PGM_TIMER=TSC pgmsend moo
```
or,
```
$ pgmsend moo
```

On a multi-core Intel system with HPET device, use HPET.
```
$ sudo chmod a+r /dev/hpet
$ PGM_TIMER=HPET pgmsend moo
```
or,
```
$ sudo chmod a+r /dev/hpet
$ PGM_TIMER=HPET pgmsend moo
```

On a hyper-threaded Intel system without HPET device, use the RTC device.
```
$ PGM_TIMER=RTC pgmsend moo
```

On a multi-core system without HPET and for multiple applications, use <tt>gettimeofday</tt>.
```
$ PGM_TIMER=GTOD pgmsend moo
```

On a Windows system, use the system performance counter.
```
C:\pgm\bin> set PGM_TIMER=TSC
C:\pgm\bin> pgmsend moo
```

On a Solaris system with a 3.2Ghz core, set the TSC frequency first.
```
$ RDTSC_FREQUENCY=3200 PGM_TIMER=TSC pgmsend moo
```
or,
```
$ RDTSC_FREQUENCY=3200 pgmsend moo
```