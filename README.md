# TCP Diagnostic Tool Design

## Background

- Linux implements a variety of **C**ongestion **C**ontrol algorithms (Vegas, Cubic, BBR, etc.) for users to choose from. It may be possible to design diagnostic logic for each CC, however, this is extremely cumbersome.
- Some large internet companies will optimize their own server TCP stack so the behavior may not be categorized into any of the known CC. If your diagnostic logic relies on certain CC behavior, you will fail.


- Despite CC's complexity, they still have same behavior in some degree. For example, any CC has slow start (BBR's startup, Cubic's hystart, etc.) phase. We can rely on this, but we can't do more assumptions.

The diagnostic logic I designed was derived from BBR, which is based on a simple network path model. Since the logic has little reliance on the specific CC's behavior, it applies to any CC.

## Network Path Model

The model has two parameters: **bottleneck bandwidth** (BtlBw) and **round-trip propagation delay** (RTprop). From [BBR draft](https://tools.ietf.org/html/draft-cardwell-iccrg-bbr-congestion-control-00#section-3.1):

> BBR is a model-based congestion control algorithm: its behavior is based on an explicit model of the network path over which a transport flow travels. BBR's model includes explicit estimates of two parameters:
>
>       1. BBR.BtlBw: the estimated bottleneck bandwidth available to the transport flow, estimated from the maximum delivery rate sample from a moving window.
>
>       2. BBR.RTprop: the estimated two-way round-trip propagation delay of the path, estimated from the the minimum round-trip delay sample from a moving window.

Obviously, `BtlBw` can be used to diagnose `bandwidth limited`; `RTprop` can be used to diagnose `congestion limited`( high queue delay ). In addition, we can derive an important parameter from this model: **bandwidth-delay product** (BDP), which is then maximum amount of data on the network circuit at any given time. We will see later that it can be used to diagnose `sender limited`.

## Estimate RTprop

`RTprop` estimating is based on `RTT`, which is contiguously measured.  `RTT` samples tend to be above the round-trip propagation delay of the  path, due to "noise" introduced by random variation in physical   transmission processes (e.g. radio link layer noise), queues along the network path, the receiver's delayed ack strategy, ack aggregation, etc.  Thus to filter out these effects we uses a min filter: using the minimum recent RTT sample seen by the connection over that past **several seconds**.

In my implemetation, the time interval is **10 seconds**.

## Estimate BtlBw

`BtlBw` estimating is based on `delivery rate`, which is contiguously measured, like `RTT`. A delivery rate sample records the estimated rate at which the network delivered packets for a single flow, calculated over the time interval between the transmission of a data packet and the acknowledgment of that packet.  Since the rate samples only include packets actually cumulatively and/or selectively acknowledged, the sender knows the exact bytes that were delivered to the receiver. Details of measuring `delivery rate` is [here](https://tools.ietf.org/html/draft-cheng-iccrg-delivery-rate-estimation-00#page-3).

Delivery rate samples tend to be below the bottleneck bandwidth available to the flow, due to "noise" introduced by random variation in physical transmission processes or queues along the network path.  Thus to filter out these effects we uses a max filter: using the windowed maximum recent delivery rate sample seen by the connection over that past **several RTT**. Note that:

- When the TCP flow is sender limited or receiver limited, we can not use the delivery rate to estimate the  `BtlBw`.
- Short-lived (never leaves slow start) or interactive (always send small packets) TCP flow may never fully utilizes the network, leading to underestimation of `BtlBw`. In this case we can't use `BtlBw` to diagnose a TCP flow.

In my implementation, the time window is 10 RTT.

## Identify Slow Start

We all know that BBR’s Startup and CUBIC’s slow start both explore the bottleneck capacity exponentially, doubling their sending rate each round. In order to recognize this pattern, we first **group the packets into flights,** then test whether consecutive flight size fits the exponential relationship. In order to group packets into flights, we need to keep track following variables for each TCP flow:

- **round_trip_count**: round trip counter starts from 1, which corresponds to TCP's three-way handshake


- **last_ack_sequence**: largest ack sequence ever seen.
- **next_send_sequence**: largest date sequence ever seen plus data length of that packet
- **current_round_trip_end**: `next_send_sequence` of previous round trip
- **last_flight_size**:  last flight size in bytes

Upon receiving an ACK, we do the following:

```C++
func calc_flight_size(ack):
    if (ack.sequence > last_ack_sequence)
        last_ack_sequence = ack.sequence
    if (last_ack_sequence > current_round_trip_end):
        round_trip_count++
        int this_flight_size = next_send_sequence - current_round_trip_end
        if (this_flight_size < last_flight_size * 3 / 2):
            is_slow_start = false
        last_flight_size = this_flight_size
        current_round_trip_end = next_send_sequence
```

The above code also describes when to quit slow start phase. In order to improve accuracy, we may need to track more consecutive flight sizes rather than two.

## Identify Receiver Limited

`Receiver limited` means the network bandwidth utilization is limited by the receiver’s advertised window.  To identify this, we need:

- **pipe_size**: currently sent but not acked data length in bytes
- **rwnd**: rwnd of newest ack
- **WSC**: TCP window scale option. *Continuous estimation is needed if we missed receiver's SYN packet*
- **MSS**: TCP max segment size. *Continuous estimation is needed if we missed receiver's SYN packet*


Upon sending a packet, we do the following:

```C++
func mark_receiver_limited(packet):
    real_rwnd = rwnd << wsc
    if (real_rwnd < pipe_size + 5 * mss)
        packet.is_receiver_limited = true
```
Note that receiver may advertises a window size of 0, forcing sender to stop, which gives us no chance to output a diagnosis. However, TCP's **persist timer** saves us. From wikipedia:

> When a receiver advertises a window size of 0, the sender stops sending data and starts the *persist timer*. The persist timer is used to protect TCP from a deadlock situation that could arise if a subsequent window size update from the receiver is lost, and the sender cannot send more data until receiving a new window size update from the receiver. When the persist timer expires, the TCP sender attempts recovery by sending a small packet so that the receiver responds by sending another acknowledgement containing the new window size.

Persist timer force receiver to send acks, which leads to `receiver limited`.

## Identify Sender Limited

`Sender limited` means the network bandwidth utilization is limited either by sender's application (`application limited`) or by sender's OS kernel (`kernel limited`). To indentify this, we need:

- **pipe_size**: currently sent but not acked data length in bytes
- **MSS**: TCP max segment size. *Continuous estimation is needed if we missed receiver's SYN packet*
- **network BDP**: `BtlBw` and `RTprop` product

Upon sending a packet, we do the following:

```c++
func mark_sender_limited(packet):
    if (packet.length < mss)
        packet.is_application_limited = true
    else if (bdp < pipe_size + 5 * mss)
        packet.is_kernel_limited = true
```
TCP Nagle algorithm can affect the accuracy of the diagnosis because it tries to merge small TCP packets into a single `MSS` size packet, leading to `kernel limited` instead of `application limited`. The RFC defines the algorithm as:

> inhibit the sending of new TCP segments when new outgoing data arrives from the user if any previously transmitted data on the connection remains unacknowledged.

However, **round trip level diagnosis** saves us: if at least one small packet is seen in current round trip, we conclude `application limited` rather than `kernel limited`.

## Identify Bandwidth Limited

`Bandwidth limited` means  the sender fully utilizes, and is limited by, the bandwidth on the bottleneck link. To identify this, we need:

- **BtlBw**: bottleneck bandwidth of network path

Upon receiving an ack, we do the following:

```c++
func bandwidth_limited(ack):
    if (ack.delivery_rate >= BtlBw * 4 / 5)
        return true
    return false
```
## Identify Congestion Limited

`Congestion limited` means the network has been congested, **not the sender thinks the network has been congested**. To identify this, we need:

- **RTprop**: round-trip propagation delay

Upon receiving an ack, we do the following:

```c++
func congestion_limited(ack):
    if (ack.rtt > RTprop * 6 / 5)
        return true
    return false
```

Note that we use **queue delay** as a signal of congestion rather than packet loss. We encounter a fundamental measurement ambiguity: whether a measured RTT increase is caused by a path length change, bottleneck bandwidth decrease, or real queuing delay increase from another connection’s traffic. We solve this ambiguity based on following observation: **network path changes happen on time scales >> RTprop**. We simply set a timeout for `RTprop` and restart the measurement once there is no update within this time period.

## Identify Idle TCP Flow

A TCP flow is idle means it has no data to transmit, which gives us no chance to output a diagnosis. However, TCP's/application's **keepalive timer** saves us. From wikipedia:

> A keepalive signal is often sent at predefined intervals, and plays an important role on the Internet. After a signal is sent, if no reply is received the link is assumed to be down and future data will be routed via another path until the link is up again. A keepalive signal can also be used to indicate to Internet infrastructure that the connection should be preserved. Without a keepalive signal, intermediate NAT-enabled routers can drop the connection after timeout.

Keepalive timer force receiver to send acks, which leads to `application limited`.

## Identify Timeout/Unresponsive TCP Flow

A TCP flow is timeout/unresponsive means the network/receiver is down or severely congested, leaving nothing to be acked,  which gives us no chance to output a diagnosis.  However, TCP's **timeout retransmission** saves us. To identify this, we simply output `timeout rexmit` upon a timout retransmission.

## Handle TCP SACK Option

**Ingoring SACK can result in overestimation of `BtlBw`.**

## Diagnostic Logic

Upon receiving an ack, we do a diagnosis:

```c++
// pkt - data packet sent
// ack - the corresponding ack

func diagnose(rexmit):
    put("timeout rexmit")
      
func diagnose(pkt, ack):
    if (pkt.is_slow_start)
        put("slow start")
    else if (pkt.is_receiver_limited)
        put("receiver limited")
    else if (pkt.is_application_limited)
        put("application limited")
    else if (pkt.is_kernel_limited)
        put("kernel limited")
    else if (bandwidth_limited(ack))
        put("bandwidth limited")
    else if (congestion_limited(ack))
        put("congetion limited")
    else
        put("unknown limited")
```
**Diagnostic Granularity**. The above code output a diagnostic result for each \{ packet, ack \} pair, which is too granular, and may have a lot of noise. Optional granularity may be fixed number of packets or fixed time interval , just like `Trat`.  However, I use `round trip` level granularity based on following obeservation: **packets in the same round trip tend to suffer from the same limitation. In order to filter out the "noise" or unknown limited, we choose the most votes in the batch of diagnosis results**. Note that the number of packets and time interval may vary in different `round trip`.

## Evaluation

### slow start

- network: bandwidth: **40Mbps**, delay: **30ms**, mss: **1460**, with or without background traffic 
- server: send **10kiB ~ 1000kiB** files, increase by **10kiB**
- client: no limit

**Result without background traffic**. For `cubic`, we correctly identified all of the connections as `slow start`. However, for files >= 300kiB, we also indentified `bandwith limited`. For `bbr`, we correctly identified all of the connections as `slow start`.  However, for files  >= 630kiB, we also indentified `bandwith limited`. The difference between the two is not due to diagnostic errors, but because bbr takes a more aggressive slow-start strategy than cubic and we have just verified this.

**Result with background traffic**. The result is almost the same as above, except that `cubic` see `bandwidth limited` until file size >= 310kiB.

### receiver limited

- network: bandwidth: **40Mbps**, delay: **30ms**, mss: **1460**, with or without background traffic 
- server: send **10MiB** file
- client: set recv buffer range **0.1*BDP** ~ **1.2*BDP** or set recv app read speed **0.1*BtlBW** ~ **1.0*BtlBw**

**various recv buffer size, no background traffic, cubic**:

| recv buffer size (BDP) | receiver limited | slow start | bandwidth limited | kernel limited | total |
| :--------------------: | :--------------: | :--------: | :---------------: | :------------: | :---: |
|          0.1           |       867        |     1      |         0         |       0        |  868  |
|          0.2           |       401        |     2      |         0         |       0        |  403  |
|          0.3           |       237        |     3      |         1         |       4        |  245  |
|          0.4           |       175        |     3      |         0         |       6        |  184  |
|          0.5           |       142        |     4      |         1         |       0        |  147  |
|          0.6           |       119        |     4      |         0         |       0        |  123  |
|          0.7           |        98        |     4      |         0         |       6        |  108  |
|          0.8           |        75        |     5      |         9         |       8        |  97   |
|          0.9           |        73        |     5      |         6         |       0        |  84   |
|          1.0           |        63        |     5      |         9         |       0        |  77   |
|          1.1           |        58        |     5      |         7         |       0        |  70   |
|          1.2           |        50        |     5      |        12         |       0        |  67   |
|          1.3           |        51        |     5      |         4         |       0        |  60   |
|          1.4           |        39        |     5      |        15         |       0        |  59   |
|          1.5           |        38        |     5      |        15         |       0        |  58   |

**various recv buffer size, background traffic, cubic**: almost same as above

**various recv buffer size, no background traffic, bbr**:

| recv buffer size (BDP) | receiver limited | slow start | bandwidth limited | kernel limited | total |
| :--------------------: | :--------------: | :--------: | :---------------: | :------------: | :---: |
|          0.1           |       959        |     1      |         0         |       6        |  966  |
|          0.2           |       402        |     2      |         0         |       0        |  404  |
|          0.3           |       241        |     3      |         1         |       3        |  248  |
|          0.4           |       180        |     3      |         0         |       0        |  183  |
|          0.5           |       143        |     4      |         1         |       0        |  148  |
|          0.6           |       118        |     4      |         0         |       1        |  123  |
|          0.7           |       101        |     5      |         0         |       0        |  107  |
|          0.8           |        88        |     5      |         1         |       0        |  94   |
|          0.9           |        79        |     5      |         0         |       0        |  84   |
|          1.0           |        71        |     5      |         1         |       0        |  77   |
|          1.1           |        64        |     5      |         1         |       0        |  70   |
|          1.2           |        59        |     5      |         0         |       0        |  64   |
|          1.3           |        54        |     6      |         0         |       0        |  60   |
|          1.4           |        5         |     6      |        50         |       0        |  63   |
|          1.5           |        4         |     6      |        52         |       0        |  62   |

**various recv buffer size, background traffic, bbr**: almost same as above

**various app read speed, no background traffic, cubic**:

| recv app read speed(BtlBw) | receiver limited | slow start | bandwidth limited | kernel limited | app limited | total |
| :------------------------: | :--------------: | :--------: | :---------------: | :------------: | :---------: | :---: |
|            0.1             |       410        |     5      |         1         |       1        |      0      |  417  |
|            0.2             |       271        |     5      |         5         |       1        |      0      |  282  |
|            0.3             |       168        |     5      |         9         |       6        |      0      |  188  |
|            0.4             |       111        |     5      |        10         |       14       |      1      |  141  |
|            0.5             |        98        |     5      |        13         |       1        |      0      |  117  |
|            0.6             |        54        |     5      |        17         |       18       |      3      |  97   |
|            0.7             |        48        |     5      |        29         |       1        |      0      |  83   |
|            0.8             |        0         |     5      |        58         |       0        |      0      |  63   |
|            0.9             |        0         |     5      |        56         |       0        |      0      |  61   |
|            1.0             |        0         |     5      |        55         |       0        |      0      |  60   |

**various app read speed, background traffic, cubic**: almost same as above

**various app read speed, no background traffic, bbr**:

| recv app read speed(BtlBw) | receiver limited | slow start | bandwidth limited | kernel limited | app limited | congestion limited | total |
| :------------------------: | :--------------: | :--------: | :---------------: | :------------: | :---------: | :----------------: | ----- |
|            0.1             |       417        |     5      |         0         |       40       |     31      |         0          | 500   |
|            0.2             |       282        |     6      |         1         |       47       |      9      |         0          | 266   |
|            0.3             |       188        |     9      |        10         |       0        |      0      |         12         | 176   |
|            0.4             |       141        |     6      |         5         |       12       |      1      |         0          | 136   |
|            0.5             |       117        |     6      |        11         |       6        |      0      |         0          | 100   |
|            0.6             |        97        |     6      |        42         |       1        |      3      |         1          | 56    |
|            0.7             |        83        |     6      |        39         |       1        |      0      |         0          | 53    |
|            0.8             |        63        |     6      |        46         |       0        |      1      |         0          | 50    |
|            0.9             |        61        |     6      |        47         |       1        |      0      |         0          | 55    |
|            1.0             |        60        |     6      |        43         |       0        |      0      |         0          | 49    |

**various app read speed, background traffic, bbr**: almost same as above.

### application limited

- network: bandwidth: **40Mbps**, delay: **30ms**, mss: **1460**
- server: interval range **1ms~100ms**(randomly), data write to kernel range **1byte ~ 10mss**(randomly), combain **autocorking** and **Nagle** options
- client: no limit

|     sender options     | receiver limited | slow start | bandwidth limited | kernel limited | app limited | congestion limited | total |
| :--------------------: | :--------------: | :--------: | :---------------: | :------------: | :---------: | :----------------: | :---: |
|   cubic, cork, delay   |        0         |     1      |         0         |       1        |     554     |         0          |  556  |
|  cubic, cork, nodelay  |        0         |     1      |         0         |       1        |     434     |         0          |  435  |
|  cubic, nocork, delay  |        0         |     1      |         0         |       1        |     555     |         0          |  557  |
| cubic, nocork, nodelay |        0         |     1      |         0         |       1        |     436     |         0          |  438  |
|    bbr, cork, delay    |        0         |     3      |         0         |      140       |     435     |         0          |  576  |
|   bbr, cork, nodelay   |        0         |     2      |         0         |       7        |     438     |         0          |  446  |
|   bbr, nocork, delay   |        0         |     2      |         0         |       78       |     477     |         0          |  556  |
|  bbr, nocork, nodelay  |        0         |     1      |         0         |       1        |     436     |         0          |  438  |

### kernel send buffer limited

**cubic**:

| send buffer size (BDP) | receiver limited | slow start | bandwidth limited | kernel limited | app limited | total |
| :--------------------: | :--------------: | :--------: | :---------------: | :------------: | :---------: | :---: |
|          0.1           |        0         |     4      |         0         |       1        |     142     |  147  |
|          0.2           |        0         |     3      |         1         |      136       |      2      |  142  |
|          0.3           |        0         |     3      |         1         |       2        |     141     |  147  |
|          0.4           |        0         |     4      |         1         |      121       |      4      |  131  |
|          0.5           |        0         |     5      |         0         |       7        |     140     |  146  |
|          0.6           |        0         |     5      |        11         |       74       |      4      |  94   |
|          0.7           |        0         |     5      |        13         |       62       |      8      |  88   |
|          0.8           |        0         |     5      |         7         |       66       |      3      |  81   |
|          0.9           |        0         |     5      |        16         |       1        |     54      |  76   |
|          1.0           |        0         |     5      |        60         |       0        |      0      |  65   |
|          1.1           |        0         |     5      |        56         |       0        |      0      |  61   |
|          1.2           |        0         |     5      |        52         |       0        |      0      |  57   |
|          1.3           |        0         |     6      |        55         |       1        |      1      |  62   |
|          1.4           |        0         |     6      |        55         |       1        |      0      |  61   |
|          1.5           |        0         |     6      |        55         |       1        |      0      |  61   |


**bbr:**
| send buffer size (BDP) | receiver limited | slow start | bandwidth limited | kernel limited | app limited | total |
| :--------------------: | :--------------: | :--------: | :---------------: | :------------: | :---------: | :---: |
|          0.1           |        0         |     4      |         0         |      135       |      3      |  142  |
|          0.2           |        0         |     3      |         1         |      123       |      4      |  131  |
|          0.3           |        0         |     4      |         2         |      121       |      4      |  131  |
|          0.4           |        0         |     5      |         0         |      106       |     10      |  121  |
|          0.5           |        0         |     4      |         1         |      111       |      5      |  121  |
|          0.6           |        0         |     5      |         1         |       2        |     57      |  65   |
|          0.7           |        0         |     6      |         0         |       1        |     57      |  64   |
|          0.8           |        0         |     6      |        13         |       23       |     16      |  58   |
|          0.9           |        0         |     6      |         0         |       46       |      3      |  55   |
|          1.0           |        0         |     6      |        44         |       2        |      1      |  53   |
|          1.1           |        0         |     6      |        47         |       5        |      1      |  59   |
|          1.2           |        0         |     6      |        45         |       6        |      2      |  59   |
|          1.3           |        0         |     6      |        48         |       6        |      0      |  60   |
|          1.4           |        0         |     6      |        51         |       1        |      2      |  60   |
|          1.5           |        0         |     6      |        46         |       4        |      1      |  57   |
### kernel CC limited

**cubic**:

| packet loss rate | receiver limited | slow start | bandwidth limited | kernel limited | app limited | congestion limited | total |
| :--------------: | :--------------: | :--------: | :---------------: | :------------: | :---------: | :----------------: | :---: |
|       0.01       |        0         |     3      |         1         |      678       |      0      |         0          |  682  |
|       0.02       |        0         |     2      |         1         |      757       |      0      |        148         |  908  |
|       0.03       |        0         |     2      |         6         |      1171      |      0      |         4          | 1183  |
|       0.04       |        0         |     3      |         1         |      1239      |      0      |         2          | 1299  |
|       0.05       |        0         |     3      |         4         |      1487      |      0      |         2          | 1496  |
|       0.06       |        0         |     2      |         1         |      1650      |      0      |         2          | 1655  |
|       0.07       |        0         |     2      |         4         |      1807      |      0      |         2          | 1815  |
|       0.08       |        0         |     3      |         4         |      1913      |      0      |         1          | 1921  |
|       0.09       |        0         |     3      |         2         |      2013      |      0      |         1          | 2019  |
|       0.1        |        0         |     2      |         3         |      2060      |      0      |         0          | 2065  |

**bbr:**

| packet loss rate | receiver limited | slow start | bandwidth limited | kernel limited | app limited | congestion limited | total |
| :--------------: | :--------------: | :--------: | :---------------: | :------------: | :---------: | :----------------: | :---: |
|       0.01       |        0         |     8      |        32         |       2        |      0      |         0          |  42   |
|       0.02       |        0         |     5      |        37         |       0        |      0      |         1          |  43   |
|       0.03       |        0         |     4      |        33         |       1        |      0      |         0          |  38   |
|       0.04       |        0         |     2      |        31         |       4        |      0      |         0          |  37   |
|       0.05       |        0         |     3      |        30         |       5        |      0      |         1          |  38   |
|       0.06       |        0         |     7      |        26         |       2        |      1      |         1          |  37   |
|       0.07       |        0         |     5      |        29         |       0        |      0      |         0          |  34   |
|       0.08       |        0         |     3      |        29         |       3        |      0      |         0          |  35   |
|       0.09       |        0         |     4      |        23         |       1        |      2      |         0          |  30   |
|       0.1        |        1         |     5      |        23         |       2        |      1      |         0          |  32   |

### Bandwidth limited

**cubic**:

| \#conection | receiver limited | slow start | bandwidth limited | kernel limited | app limited | congestion limited | total |
| :---------: | :--------------: | :--------: | :---------------: | :------------: | :---------: | :----------------: | :---: |
|      1      |        0         |     5      |        51         |       5        |      0      |         0          |  61   |
|      2      |        0         |     9      |        116        |       17       |      0      |         71         |  213  |
|      3      |        0         |     16     |        91         |       97       |      0      |        546         |  750  |
|      4      |        0         |     29     |        44         |      492       |      0      |        2347        | 2912  |
|     16      |        0         |     41     |        71         |      3777      |      0      |        6920        | 10809 |

**bbr**:

| \#conection | receiver limited | slow start | bandwidth limited | kernel limited | app limited | congestion limited | total |
| :---------: | :--------------: | :--------: | :---------------: | :------------: | :---------: | :----------------: | :---: |
|      1      |        0         |     6      |        47         |       0        |      0      |         0          |  53   |
|      2      |        0         |     10     |        90         |       3        |      0      |         7          |  110  |
|      3      |        0         |     16     |        84         |       49       |      0      |        191         |  340  |
|      4      |        0         |     24     |        66         |       97       |      0      |        1054        | 1241  |
|     16      |        0         |     41     |        111        |      444       |      0      |        4079        | 4675  |

### Congestion limited



## Example

output format:

- round trip counter
- BtlBw
- RTprop
- start time -> end time
- limitation
- acks diagnosed as above limitation / acks received

**file upload** (bulk data transfer):

```
[1] 0kB/s 6112us 07:18:00.674043 -> 07:18:00.680528 [slow start] (1/1)
[2] 1906kB/s 6112us 07:18:00.680528 -> 07:18:00.687249 [slow start] (9/9)
[3] 4034kB/s 6112us 07:18:00.687249 -> 07:18:00.693552 [slow start] (20/20)
[4] 6883kB/s 6112us 07:18:00.693552 -> 07:18:00.699960 [slow start] (34/34)
[5] 9884kB/s 6112us 07:18:00.699960 -> 07:18:00.706513 [slow start] (24/24)
[6] 12237kB/s 6112us 07:18:00.706513 -> 07:18:00.712898 [bandwidth limited] (29/31)
[7] 13414kB/s 6112us 07:18:00.712898 -> 07:18:00.720109 [bandwidth limited] (37/45)
[8] 13649kB/s 6112us 07:18:00.720109 -> 07:18:00.727585 [bandwidth limited] (29/30)
[9] 13649kB/s 6112us 07:18:00.727585 -> 07:18:00.734572 [bandwidth limited] (28/28)
[10] 13884kB/s 6112us 07:18:00.734572 -> 07:18:00.741791 [bandwidth limited] (29/29)
```
**ssh** (interactive data transfer):

```
[1] 0kB/s 5012us 09:11:28.457792 -> 09:11:30.249016 [slow start] (1/1)
[2] 1kB/s 4831us 09:11:30.249016 -> 09:11:30.925888 [application limited] (1/1)
[3] 1kB/s 4824us 09:11:30.925888 -> 09:11:31.516343 [application limited] (1/1)
[4] 1kB/s 4580us 09:11:31.516343 -> 09:11:32.528395 [application limited] (1/1)
[5] 1kB/s 4580us 09:11:32.528395 -> 09:11:32.689936 [application limited] (1/1)
[6] 1kB/s 4573us 09:11:32.689936 -> 09:11:32.795682 [application limited] (1/1)
[7] 1kB/s 4573us 09:11:32.795682 -> 09:11:32.920271 [application limited] (1/1)
[8] 1kB/s 4573us 09:11:32.920271 -> 09:11:33.067761 [application limited] (1/1)
[9] 1kB/s 4573us 09:11:33.067761 -> 09:11:33.185697 [application limited] (1/1)
[10] 1kB/s 4573us 09:11:33.185697 -> 09:11:33.308508 [application limited] (1/1)
[11] 1kB/s 4573us 09:11:33.308508 -> 09:11:33.406548 [application limited] (1/1)
[12] 1kB/s 4573us 09:11:33.406548 -> 09:11:33.536439 [application limited] (1/1)
[13] 1kB/s 4573us 09:11:33.536439 -> 09:11:33.646821 [application limited] (1/1)
[14] 1kB/s 4573us 09:11:33.646821 -> 09:11:33.767388 [application limited] (1/1)
[15] 1kB/s 4573us 09:11:33.767388 -> 09:11:33.892053 [application limited] (1/1)
```

## Weakness

- must run at sender side, or close to sender
- must see both data and ack packet

## Contribution

- real-time fine-grained TCP diagnosis with 7 kinds of results/limitations
- applies to any CC
